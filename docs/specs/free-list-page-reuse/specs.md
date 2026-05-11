# Free List Page Reuse Slice

## Problem Statement

MyLite now stores catalog, row, index, and row-overflow data in typed 4096-byte
page chains, but every mutation still allocates replacement chains at EOF. Old
row, index, and catalog payload chains become unreachable after the alternate
catalog header is published.

That append-only policy was acceptable while proving file-format recovery. It is
now the main storage growth problem: every row update rewrites table row pages,
index payload pages, and the catalog payload even when the logical database size
stays stable. This slice should add the first persistent free-page list and
reuse complete obsolete page-chain ranges without implementing compaction,
transactions, or arbitrary page allocation.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- MyLite's current page-store publication code lives in
  `vendor/mariadb/server/storage/mylite/ha_mylite.cc`:
  `mylite_write_catalog_locked()`, `mylite_write_row_payloads_locked()`,
  `mylite_write_row_slot_pages_locked()`, `mylite_write_index_payloads_locked()`,
  `mylite_write_page_chain()`, `mylite_read_page_chain()`, and
  `mylite_load_catalog_generation_locked()`.
- MyLite currently requires sequential page chains in
  `mylite_read_page_chain()` and in mixed row payload loading through
  `mylite_parse_row_payload_pages_locked()`. This slice should preserve that
  invariant by reusing only consecutive free ranges large enough for a new
  chain.
- Handler mutations enter MyLite through MariaDB handler methods implemented in
  `ha_mylite.cc`: `mylite_store_table_definition()`,
  `mylite_remove_table_definition()`, `mylite_rename_table_definition()`,
  `mylite_store_row()`, `mylite_update_row()`, and `mylite_delete_row()`.
  MariaDB's wrapper calls are in `vendor/mariadb/server/sql/handler.cc`
  (`handler::ha_write_row()`, `handler::ha_update_row()`,
  `handler::ha_delete_row()`, `handler::ha_create()`, and
  `handler::ha_rename_table()`), and virtual handler contracts are declared in
  `vendor/mariadb/server/sql/handler.h`.
- MariaDB's own durable engines track reusable space explicitly rather than
  guessing from file holes. Aria's bitmap interface is declared in
  `vendor/mariadb/server/storage/maria/ma_blockrec.h` and documented in
  `vendor/mariadb/server/storage/maria/ma_bitmap.c`; it records page fullness,
  reserves pages during writes, and flushes bitmap state at checkpoint/close.
  InnoDB's tablespace allocator in
  `vendor/mariadb/server/storage/innobase/fsp/fsp0fsp.cc` manages free extents
  and free fragment pages through explicit page metadata. MyLite should not copy
  those formats, but the source confirms that reusable page state must be
  validated and persisted as part of engine state.
- `docs/architecture/single-file-storage.md` names free-space tracking as a
  required primary-file region, and prior specs deliberately deferred free-list,
  compaction, and transaction recovery.

## Scope

This slice will:

- add a catalog-level `FREEPAGE` record for persistent free page ranges,
- add an in-memory free range list loaded with each accepted catalog generation,
- validate free ranges against active catalog, row, and index page-chain roots,
- allocate new catalog, row, and index page chains from complete consecutive
  free ranges when possible,
- fall back to EOF allocation when no free range is large enough,
- record obsolete page chains from the previous accepted generation into the
  newly published generation's free list,
- include the previous catalog payload chain in the delayed free list,
- include row and index chains from dropped tables through pending obsolete
  ranges,
- keep the one-file storage contract and two-header publication protocol,
- update storage smoke physical inspection to prove `FREEPAGE` records exist
  and reused page ids are used for later writes,
- record file-size and binary-size impact.

## Non-Goals

- Do not implement arbitrary non-consecutive page chains.
- Do not implement page-local row free space reuse, partial row updates, slot
  reuse, vacuum, or compaction.
- Do not implement B-trees, page splits, or incremental index updates.
- Do not add rollback journal, WAL, undo, redo, transaction rollback, or
  cross-process writer locking.
- Do not truncate the primary file after tail pages become free.
- Do not reuse pages made obsolete by the current write before that write's
  header is published.
- Do not change the public `libmylite` C API.
- Do not stabilize the pre-release file format.

## Proposed Design

### Free Range Records

The logical catalog gains global records:

```text
FREEPAGE <page_id> <page_count>
```

Fields are tab-separated decimal integers. `page_id` must be at least `2`, and
`page_count` must be nonzero. Records are sorted and merged when serialized.
Adjacent ranges are serialized as one range; overlapping ranges reject the
catalog generation.

`FREEPAGE` records describe complete page ids, not byte offsets. They may point
to pages that still contain old typed page headers. A page becomes free because
the accepted catalog generation no longer references it, not because its old
bytes were zeroed.

### One-Generation-Delayed Safety Rule

The allocator may consume only free ranges that were part of the catalog
generation loaded before the current write began. Page chains made obsolete by
the current write are appended to the new generation's free list but are not
eligible for allocation until a later write loads or continues from that newly
accepted generation.

This preserves the existing two-header recovery guarantee:

1. before the new header is published, the previous header may still be the
   newest valid generation and must be able to read its old catalog, row, and
   index chains;
2. therefore the current write must not overwrite those old chains;
3. after the new header is published and flushed, the new catalog generation
   may advertise the old chains as free for future writes.

If a crash happens after replacement pages are written but before header
publication, those replacement pages are orphans, not free-list members. A later
compaction or recovery slice can reclaim orphans by scanning generations; this
slice only reuses pages advertised by an accepted catalog.

### Allocation Policy

Page-chain allocation remains sequential:

1. compute the page count needed for the logical payload,
2. find the first free range whose `page_count` is at least that size,
3. allocate from the start of that range and shrink or remove the range,
4. if no range fits, append at the page-aligned EOF as today.

This preserves the current `next_page_id == page_id + 1` validation and avoids
relaxing row/index/catalog readers before a fuller pager design exists.

### Write Protocol

`mylite_write_catalog_locked()` should manage a local allocation state:

1. copy the current accepted free range list into an allocator list,
2. collect old active row and index payload chains from the in-memory catalog,
3. collect pending obsolete chains from table drops,
4. remember the previous catalog payload chain from the latest valid header,
5. write replacement row payload chains using the allocator,
6. write replacement index payload chains using the allocator,
7. build the next free list from the allocator leftovers plus the old active
   row, index, dropped-table, and catalog payload ranges,
8. serialize `FREEPAGE` records with the catalog payload,
9. write the catalog payload using the allocator,
10. fsync page data, publish the alternate header, and fsync the header.

The previous catalog payload range must be added to the free list after the new
catalog payload has been allocated. That prevents the new catalog payload from
overwriting the old payload needed by the previous header.

If the flush fails, the caller's existing catalog rollback restores table state,
and `mylite_write_catalog_locked()` must restore the previous free range list.

### Load And Validation

Catalog generation loading should parse `FREEPAGE` records into a temporary
free range list. A generation is accepted only after:

- catalog, row, and index payloads validate as they do today,
- free ranges are sorted, non-overlapping, and inside the file,
- no free range overlaps the candidate header's catalog payload chain,
- no free range overlaps any loaded table `ROWPAGE` chain,
- no free range overlaps any loaded `INDEXPAGE` chain.

Only then should MyLite swap both the loaded table catalog and loaded free range
list into process state.

### Dropped Tables

Dropping a table removes its definition before the next catalog serialization,
so its row and index roots would otherwise be lost before the writer can mark
them obsolete. `mylite_remove_table_definition()` should collect the table's
current row and index ranges into a pending obsolete list before erasing the
definition. The pending list is folded into the next successful free-list
publication and restored if the flush fails.

### File Format

The page header format remains unchanged. The logical catalog grammar gains a
global `FREEPAGE` record but keeps the existing catalog magic and format version
because current pre-release v2 readers already reject unknown records and there
is no released compatibility contract. A later external compatibility decision
can choose whether to bump the file format for every grammar addition.

## Affected Subsystems

- MyLite storage-engine catalog, page-chain allocation, and generation-loading
  code in `ha_mylite.cc`.
- DDL drop handling in `ha_mylite.cc` for pending obsolete roots.
- Storage smoke physical file-format inspection in
  `tools/run-storage-engine-smoke.sh`.
- Storage smoke persistence workload in
  `vendor/mariadb/server/mylite/storage_engine_smoke.cc` if an additional
  rewrite is needed to force visible reuse.
- Architecture and roadmap documentation.

No SQL parser, optimizer, public C API, build target, or MariaDB table
definition image format should change.

## DDL Metadata Routing Impact

DDL metadata routing remains logically unchanged: `CREATE`, copy `ALTER`,
`DROP`, and `RENAME` still mutate the MyLite catalog and must not leave durable
`.frm` sidecars. The free list changes only how obsolete page chains are
advertised for later reuse.

The drop path gets one storage-specific responsibility: remember row and index
chains owned by a removed table until the next successful catalog publication.

## Single-File And Embedded-Lifecycle Implications

Free-list state lives inside the primary `.mylite` file as catalog records. No
durable companion files are introduced. The allocator continues to run under the
existing process-local catalog mutex, so this slice does not claim
cross-process concurrency.

Fresh embedded process tests remain necessary because a reopened process must
load the accepted free list before consuming it.

## Public API And File-Format Impact

The public C API is unchanged.

The internal pre-release v2 file format gains `FREEPAGE` catalog records. Page
types and page headers stay unchanged. Old pre-release files without `FREEPAGE`
records continue to load with an empty free list.

## Binary-Size Impact

Expected impact is small first-party code for free range parsing, merging,
validation, chain allocation, and smoke assertions. No dependency is allowed.
Record measured artifacts after implementation.

## License, Trademark, And Dependency Impact

No new dependency. New code remains GPL-2.0-only first-party MyLite storage code
inside the MariaDB-derived tree. No trademark or packaging surface changes.

## Test And Verification Plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
bash -n tools/run-compatibility-test-harness.sh tools/run-storage-engine-smoke.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

The storage smoke should verify:

- normal DDL, DML, index, autoincrement, row-overflow, and persistence behavior
  still pass,
- a fresh read phase loads `FREEPAGE` records from the persisted catalog,
- a later write reuses at least one free page range instead of only appending,
- physical inspection proves `FREEPAGE` records do not overlap live catalog,
  row, or index roots,
- recovery fallback still works when the latest generation is corrupted,
- no `.frm` artifacts,
- no catalog temporary sidecars.

The compatibility harness should continue to verify:

- MariaDB reference fingerprints match MyLite fingerprints for the supported
  subset,
- sidecar scan reports no unexpected MyLite sidecars.

## Acceptance Criteria

- Accepted catalog generations can persist and reload `FREEPAGE` records.
- Free ranges are rejected when malformed, overlapping, outside the file, or
  overlapping live catalog, row, or index page chains.
- New page-chain writes consume only complete consecutive free ranges from the
  previously accepted generation.
- Page chains made obsolete by the current generation are not reused until a
  later write.
- Dropped table row and index roots are recorded as pending obsolete ranges and
  published in the next successful free list.
- Storage smoke observes both `FREEPAGE` records and at least one reused page
  range in a fresh-process persistence workflow.
- Existing storage, recovery, compatibility, embedded lifecycle, and
  `libmylite` lifecycle smokes pass.
- No persistent `.frm`, engine table sidecars, dynamic plugin artifacts, or
  catalog temporary sidecars are introduced.

## Risks And Unresolved Questions

- This is not transaction recovery. It preserves the existing header
  publication guarantee but does not roll back multi-statement logical work.
- Orphan pages from failed, unpublished writes remain unreclaimed.
- Tail truncation is deferred; the file may not shrink even when the last pages
  become free.
- The allocator still requires consecutive ranges because readers currently
  require sequential chains.
- Full compaction and page-local row/index reuse remain separate slices.
