# Row Slot Storage Slice

## Problem Statement

`row-page-storage` moved row images out of the logical catalog payload, but each
table still writes a table-sized logical text stream inside row payload pages:

```text
MYLITE ROWS 1
ROW <rowid> <record_hex>
```

That format proves row roots and row-page recovery, but it still pays hex
encoding overhead, has no page-local record directory, and does not establish
the structures needed for later free-space tracking or row-level updates.

This slice should replace new row payload writes with page-local binary row
records and slot directories while preserving the current MariaDB handler
behavior and supported SQL subset.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- MyLite's current row page bridge lives in
  `vendor/mariadb/server/storage/mylite/ha_mylite.cc`:
  `mylite_write_row_payloads_locked()`,
  `mylite_serialize_rows_payload_locked()`,
  `mylite_load_row_payloads_locked()`, and
  `mylite_parse_rows_payload_locked()`.
- The current page-store layer provides outer page identity, type, checksum,
  payload length, and sequential next-page validation through
  `mylite_read_page()`, `mylite_write_page()`,
  `mylite_read_page_chain()`, and `mylite_write_page_chain()`.
- Existing handler methods already depend on fixed MariaDB record images in
  memory: `mylite_store_row()`, `mylite_update_row()`,
  `mylite_read_row()`, `mylite_read_row_by_id()`, and
  `mylite_build_index_entries()`.
- MariaDB handler behavior, diagnostics, table discovery, parser, optimizer,
  and the public `libmylite` C API are not changing in this slice.

## Scope

This slice will:

- introduce a row payload format version 2 inside existing `row_payload` pages,
- write each row payload page as a page-local binary slot directory plus record
  bytes,
- keep append-only table rewrite allocation for now,
- validate row slot pages during catalog generation load,
- keep reading legacy `MYLITE ROWS 1` text row payloads from pre-release files,
- make oversized single-row records explicitly unsupported until overflow pages
  are designed,
- update the storage smoke's physical row-page assertion for row-slot pages,
- record file-size and binary-size impact.

## Non-Goals

- Do not add in-place page updates or page-local reuse yet.
- Do not add a persistent free-list or compaction.
- Do not add overflow pages for records that cannot fit in one row slot page.
- Do not add durable secondary indexes or B-trees.
- Do not add undo, redo, WAL, transaction rollback, or cross-process locking.
- Do not change the public C API.
- Do not stabilize the pre-release file format for external compatibility.

## Proposed Design

### Row Slot Page Payload

The outer page-store page remains unchanged:

- page type: `row_payload` (`2`),
- page id and sequential `next_page_id`,
- page checksum over the full 4096-byte page,
- used payload byte count.

Inside each row payload page, use a compact binary payload:

```text
bytes  0..15   row-slot magic: MYLITEROWSLOT2
bytes 16..19   row_slot_format_version: 2
bytes 20..23   row_count
bytes 24..27   row_data_offset
bytes 28..31   reserved, zero
bytes 32..N    slot directory
bytes N..end   packed MariaDB record images
```

Each slot directory entry is 16 bytes:

```text
bytes  0..7    rowid
bytes  8..11   record_offset, relative to the page payload start
bytes 12..15   record_length
```

Rows are packed contiguously after the slot directory. Each row must fit wholly
inside one row slot page in this slice. A future overflow-page slice can lift
that limit without changing the handler API.

### Row Size Limit

Until overflow pages exist, MyLite should reject row-storage support for tables
whose MariaDB fixed record image cannot fit in a single row slot page. The
initial limit is:

```text
page_payload_capacity - row_slot_header_size - one_slot_entry
```

With the current 4096-byte page and 64-byte outer page header, that is:

```text
4032 - 32 - 16 = 3984 bytes
```

This is an explicit pre-release limitation, not a silent truncation. Existing
BLOB/TEXT rejection stays unchanged.

### Write Protocol

Row-slot writes keep the same catalog publication protocol:

1. for each non-empty table, pack live rows into one or more row-slot pages,
2. append those pages sequentially with page type `row_payload`,
3. compute the row payload root offset, total used payload bytes, and logical
   checksum over the concatenated row-slot payload bytes,
4. publish those values through the table's catalog `ROWPAGE` record,
5. write the catalog payload and alternate header as before.

Deletes are still handled by omitting deleted rows from the next table rewrite.
Updates are still table rewrites. Free-space reuse is intentionally deferred.

### Load And Recovery

Catalog generation load should keep the fallback behavior from
`row-page-storage`:

1. read a candidate catalog payload,
2. parse `ROWPAGE` roots,
3. validate every row payload chain,
4. parse row-slot pages into a temporary catalog,
5. publish only after the full generation succeeds.

For compatibility with current pre-release files, loading a row payload whose
first bytes are `MYLITE ROWS 1\n` should use the legacy text parser. New writes
must use row-slot format version 2.

## Affected Subsystems

- MyLite storage-engine row payload read/write code in `ha_mylite.cc`.
- Table support checks for maximum fixed row image size.
- Storage smoke physical row-page assertion.
- Architecture, roadmap, and slice docs.

No SQL parser, optimizer, public API, or user-visible handler contract should
change except explicit rejection of oversized fixed-row images.

## Single-File And Embedded-Lifecycle Implications

Rows remain inside the one primary `.mylite` file. This slice must not create
new durable companions. Append-only orphan pages after an unpublished header
remain acceptable until free-list and compaction work exists.

## Binary-Size Impact

Expected impact is small first-party code for row-slot packing, row-slot page
validation, and the physical smoke assertion update. No new dependency is
allowed. Record measured artifacts after implementation.

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

- fresh-process row persistence through row-slot pages,
- recovery fallback after corrupting a latest-generation row-slot page,
- physical row payload magic `MYLITEROWSLOT2`,
- `catalog_row_records=0`,
- row payload page type `2`,
- no `.frm` artifacts,
- no catalog temporary sidecars.

The compatibility harness should continue to verify:

- MariaDB reference fingerprints match MyLite fingerprints for the supported
  subset,
- sidecar scan reports no unexpected MyLite sidecars.

## Acceptance Criteria

- New row payload writes use binary row-slot pages, not `MYLITE ROWS 1` text
  streams.
- Existing `MYLITE ROWS 1` development files still load.
- Row-slot pages validate magic, version, row count, slot directory bounds,
  rowid uniqueness, record offsets, and record lengths.
- Oversized fixed-row-image tables are rejected explicitly until overflow pages
  exist.
- Existing storage, recovery, compatibility, embedded lifecycle, and
  `libmylite` lifecycle smokes pass.
- No persistent `.frm`, engine table sidecars, dynamic plugin artifacts, or
  catalog temporary sidecars are introduced.
- Binary and file-size changes are recorded.

## Implementation Result

New row payload writes now use binary row-slot pages inside page-store page type
`2`:

- row slot magic: `MYLITEROWSLOT2`,
- row slot format version: `2`,
- row slot header size: 32 bytes,
- row slot entry size: 16 bytes,
- maximum fixed record image without overflow pages: 3984 bytes,
- each slot stores row id, record offset, and record length,
- record bytes are packed contiguously after the slot directory,
- legacy `MYLITE ROWS 1` text payloads still load.

The row payload loader now detects the first row payload page format and either
uses the legacy text parser or validates the binary row-slot chain. Row-slot
validation checks magic, version, row count, slot directory offset, sequential
record offsets, record bounds, nonzero row ids, duplicate row ids, and the
logical payload checksum from the catalog `ROWPAGE` root.

The storage smoke now verifies both the new physical format and the explicit
large-row rejection. Observed report values:

```text
unsupported_large_row=rejected
catalog_row_records=0
rowpage_records=3
row_payloads=mylite.persisted,mylite.persisted_auto,mylite.persisted_wide
row_payload_page_counts=mylite.persisted:1,mylite.persisted_auto:1,mylite.persisted_wide:6
row_payload_magic=MYLITEROWSLOT2
row_payload_page_type=2
```

Verification passed:

```sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
bash -n tools/run-compatibility-test-harness.sh tools/run-storage-engine-smoke.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

Observed reports after implementation:

- `mylite-storage-engine-report.txt`: `status=0`,
  `unsupported_large_row=rejected`.
- `mylite-catalog-read-report.txt`: `status=0`, `persisted_count=2`,
  `persisted_notes=seven,eight`,
  `persisted_autoincrement_ids=1,5,6,7`, `persisted_wide_count=6`.
- `mylite-catalog-recovery-read-report.txt`: `status=0`,
  `persisted_count=2`, `persisted_notes=seven,eight`,
  `persisted_autoincrement_ids=1,5,6`, `persisted_wide_count=6`,
  `recovery_marker=absent`.
- `mylite-compatibility-harness-report.txt`: all groups `status=0`,
  including `mariadb_comparison` and `sidecar_scan`;
  `unexpected_sidecars=none`.
- `libmylite-open-close-report.txt`: `status=0`.
- `mylite-embedded-bootstrap-report.txt`: `status=0`.

Observed artifacts after this slice:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,337,954 bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 29,698 bytes.
- `build/mariadb-minsize/mylite/mylite-compatibility-smoke`: 22,696,256
  bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,695,648
  bytes.
- `build/mariadb-minsize/mylite/mylite-open-close-smoke`: 22,696,984 bytes.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,695,376
  bytes.
- `build/mariadb-minsize/mylite-compatibility-mylite/catalog.mylite`: 167,936
  bytes.
- `build/mariadb-minsize/mylite-catalog-persistence/catalog.mylite`: 413,696
  bytes.
- `build/mariadb-minsize/mylite-catalog-recovery/catalog.mylite`: 372,736
  bytes.

## Risks And Unresolved Questions

- Tables still rewrite all live rows on every mutation.
- There is still no page reuse, free-list, or compaction.
- Large fixed-row images require a future overflow-page design.
- Row-slot pages still store raw MariaDB record images; durable column encoding
  is not solved here.
- Indexes remain rebuilt from rows in memory.
- FNV-1a remains the current development checksum.
