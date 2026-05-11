# Row Page Storage Slice

## Problem Statement

MyLite now writes the logical catalog payload through typed 4096-byte page
chains, but the catalog still contains every raw row image as a `ROW` text
record. That keeps the first DML bridge simple, but it makes the catalog a
table heap, causes every row mutation to rewrite row bytes as catalog metadata,
and prevents later row/index/free-space work from using the page-store layer.

This slice should move simple durable row images out of the logical catalog
payload and into typed row page chains. It should preserve the existing MariaDB
handler behavior and supported SQL subset while establishing table-level row
roots in the catalog.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- MyLite's current handler and row bridge live in
  `vendor/mariadb/server/storage/mylite/ha_mylite.cc`:
  `mylite_store_row()`, `mylite_update_row()`, `mylite_delete_row()`,
  `mylite_read_row()`, `mylite_read_row_by_id()`,
  `mylite_build_index_entries()`, and
  `mylite_check_unique_constraints_locked()`.
- The current logical catalog serializer/parser in `ha_mylite.cc` writes table
  definitions, rowid/autoincrement counters, and `ROW` records from
  `mylite_serialize_catalog_locked()` and
  `mylite_parse_catalog_payload_locked()`.
- The page-store substrate from `pager-page-store` lives in
  `mylite_read_catalog_payload()`, `mylite_write_catalog_payload()`,
  `mylite_read_page()`, and `mylite_write_page()`. It already has page ids,
  page checksums, page types, and append-only allocation.
- `docs/architecture/single-file-storage.md` calls for table/index pages and
  free-space tracking as separate file regions under the primary `.mylite`
  file.
- MariaDB storage engines ultimately interact with the server through handler
  row operations. This slice should not change parser, optimizer, diagnostics,
  table discovery, or the public `libmylite` API.

## Scope

This slice will:

- add a `row_payload` page type to the MyLite page-store layer,
- store each table's live row images in a table-owned row payload chain,
- add catalog records that point from a table definition to its row payload
  root offset, logical byte length, and checksum,
- keep row payload allocation append-only,
- keep the existing in-memory `Mylite_row` bridge and raw MariaDB record images,
- keep parsing legacy catalog `ROW` records for pre-release development files,
- validate row page chains when loading a catalog generation,
- keep the existing DML, key, autoincrement, and recovery smokes passing,
- record file-size and binary-size impact.

## Non-Goals

- Do not implement final row slots, tuple headers, NULL maps, overflow pages, or
  record-level free-space reuse.
- Do not move secondary indexes into durable index pages.
- Do not add B-trees, page splits, a free-list, compaction, undo, redo, WAL, or
  transaction rollback.
- Do not change supported SQL types, key restrictions, or BLOB/TEXT rejection.
- Do not change the public `libmylite` C API.
- Do not promise file-format stability beyond current pre-release tests.

## Proposed Design

### Catalog Roots

The logical catalog remains the small metadata root. For each frm-backed table,
write a `ROWPAGE` record after the `AUTOINC` record:

```text
ROWPAGE <db_hex> <table_hex> <root_offset> <payload_length> <payload_checksum>
```

Fields are tab-separated in the existing catalog style. Empty tables use zeros
for root offset, length, and checksum. Non-empty tables must point to a
page-aligned root offset at page id 2 or later.

The existing `ROW` catalog record parser stays as a legacy import path, but new
writes should not emit catalog `ROW` records.

### Row Payload Format

Version one row payloads are still a bridge format over the existing raw record
images. A table row payload is a logical text stream stored inside
`row_payload` pages:

```text
MYLITE ROWS 1
ROW <rowid> <record_hex>
ROW <rowid> <record_hex>
```

Rows are tab-separated on disk. The db/table owner is not repeated in each row;
the owning table comes from the catalog `ROWPAGE` record. Deleted rows are not
written. On load, row ids must be non-zero, unique within the table, and below
the exhausted `uint64_t` sentinel already used by the handler bridge.

This is not the final row format. It only removes row bytes from the catalog
metadata payload while preserving existing handler behavior.

### Page-Chain Helpers

Generalize the catalog page read/write helpers into typed page-chain helpers:

1. `mylite_read_page_chain()` validates a chain of the requested page type,
   sequential page ids, payload lengths, page checksums, and logical checksum.
2. `mylite_write_page_chain()` appends a typed chain at EOF, page-aligns the
   root, writes sequential page ids, and returns root offset plus logical
   checksum.
3. The catalog helpers become thin wrappers over the generic helpers using the
   existing `catalog_payload` page type.

Sequential page links are still required. Append-only allocation is acceptable
for this slice because free-space reuse is a later design problem.

### Load And Recovery

Loading should evaluate catalog generations newest to oldest. For each
candidate:

1. validate and read the catalog payload page chain,
2. parse table metadata and row roots into a temporary catalog,
3. read and validate every non-empty row payload chain,
4. parse row payloads into the temporary catalog,
5. publish the temporary catalog only after the full candidate succeeds.

This keeps recovery behavior coherent now that row data lives outside the
catalog payload. A latest generation with a damaged row payload should be
ignored in favor of the previous valid generation rather than partially loading
metadata and then failing.

### Write Protocol

A write still publishes by header generation:

1. open the primary `.mylite` file,
2. find the latest valid header generation for generation number and slot
   selection,
3. write row payload page chains for every non-empty table and update their
   in-memory catalog root metadata,
4. serialize the logical catalog with `ROWPAGE` records,
5. write the catalog payload page chain,
6. fsync page data,
7. publish the alternate header slot,
8. fsync the header.

If a crash happens before header publication, newly appended row pages are
orphans. That is acceptable until free-list and compaction work exists.

## Affected Subsystems

- MyLite storage-engine file-format helpers in `ha_mylite.cc`.
- Catalog serialization and parsing in `ha_mylite.cc`.
- Catalog recovery loading in `ha_mylite.cc`.
- Storage smoke scripts or smoke binary only if additional row-page assertions
  are needed.
- Architecture and roadmap docs.

No SQL parser, optimizer, public C API, or user-visible handler contract should
change.

## DDL Metadata Routing Impact

DDL metadata routing remains logically unchanged. `CREATE`, copy `ALTER`,
`DROP`, and `RENAME` still mutate the MyLite catalog and must not leave durable
`.frm` sidecars. A table rename moves the in-memory row set with the table; the
next publish writes a new `ROWPAGE` root for the renamed table.

## Single-File And Embedded-Lifecycle Implications

Rows remain inside the one primary `.mylite` file. This slice must not create
new durable companions. Existing inherited Aria process files used by the
embedded runtime remain outside the MyLite table storage contract and continue
to be monitored by the compatibility harness sidecar scan.

## Public API And File-Format Impact

The public C API is unchanged.

The internal v2 page-store-backed format gains a new row page type and `ROWPAGE`
catalog records. Existing pre-release v2 development files that still contain
catalog `ROW` records should continue loading through the legacy parser path,
but new writes should publish row data through row page chains.

## Binary-Size Impact

Expected impact is small first-party code for row payload serialization,
generic page-chain helpers, catalog generation fallback, and parser records. No
new dependency is allowed. Record measured artifacts after implementation.

## License, Trademark, And Dependency Impact

No new dependency. New code remains GPL-2.0-only first-party MyLite storage code
in the MariaDB-derived tree. No trademark or packaging surface changes.

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

The storage smoke should continue to verify:

- normal DDL/DML lifecycle,
- fresh-process row persistence,
- autoincrement persistence after reopen,
- recovery fallback after corrupting the latest published generation,
- no `.frm` artifacts,
- no catalog temporary sidecars.

The compatibility harness should continue to verify:

- MariaDB reference fingerprints match MyLite fingerprints for the supported
  subset,
- sidecar scan reports no unexpected MyLite sidecars.

## Acceptance Criteria

- New `.mylite` writes no longer emit live row images as catalog `ROW` records.
- Each non-empty table has a validated `ROWPAGE` catalog root pointing to typed
  row payload pages.
- Catalog loading reconstructs rows from row page chains and still accepts
  legacy catalog `ROW` records.
- If the latest generation cannot validate its row payloads, loading falls back
  to the previous valid generation.
- Existing storage, recovery, compatibility, embedded lifecycle, and
  `libmylite` lifecycle smokes pass.
- No persistent `.frm`, engine table sidecars, dynamic plugin artifacts, or
  catalog temporary sidecars are introduced.
- Binary and file-size changes are recorded.

## Implementation Result

The `.mylite` v2 page-store format now has a `row_payload` page type:

- catalog payload pages keep page type `1`,
- row payload pages use page type `2`,
- each frm-backed table now serializes a `ROWPAGE` catalog record,
- new catalog writes no longer emit live row images as catalog `ROW` records,
- empty tables publish a zero row root,
- non-empty tables publish a page-aligned row payload root offset, logical
  payload length, and payload checksum,
- row payloads start with `MYLITE ROWS 1` and contain table-local `ROW` records
  with row id and raw MariaDB record image,
- legacy catalog `ROW` records still parse for pre-release development files.

The catalog page helpers are now generic typed page-chain helpers. Catalog load
evaluates header generations newest to oldest and only publishes a temporary
catalog after both its catalog payload and every referenced row payload chain
validate. The recovery smoke corrupts the first page appended by the latest
generation; because row pages are written before the catalog payload, this now
exercises row-payload corruption fallback and still recovers the previous
generation.

The storage smoke also checks the physical row-page shape after fresh-process
reopen. Observed row-page report:

```text
status=0
catalog_row_records=0
rowpage_records=2
row_payloads=mylite.persisted,mylite.persisted_auto
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

- `mylite-catalog-read-report.txt`: `status=0`, `persisted_count=2`,
  `persisted_notes=seven,eight`,
  `persisted_autoincrement_ids=1,5,6,7`, no `.frm` artifacts, no catalog
  sidecars.
- `mylite-catalog-recovery-read-report.txt`: `status=0`,
  `persisted_count=2`, `persisted_notes=seven,eight`,
  `persisted_autoincrement_ids=1,5,6`, `recovery_marker=absent`, no `.frm`
  artifacts, no catalog sidecars.
- `mylite-compatibility-harness-report.txt`: all groups `status=0`,
  including `mariadb_comparison` and `sidecar_scan`;
  `unexpected_sidecars=none`.
- `libmylite-open-close-report.txt`: `status=0`.
- `mylite-embedded-bootstrap-report.txt`: `status=0`.

Observed artifacts after this slice:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,329,272 bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 29,698 bytes.
- `build/mariadb-minsize/mylite/mylite-compatibility-smoke`: 22,695,440
  bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,694,624
  bytes.
- `build/mariadb-minsize/mylite/mylite-open-close-smoke`: 22,695,752 bytes.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,694,144
  bytes.
- `build/mariadb-minsize/mylite-compatibility-mylite/catalog.mylite`: 167,936
  bytes.
- `build/mariadb-minsize/mylite-catalog-persistence/catalog.mylite`: 118,784
  bytes.
- `build/mariadb-minsize/mylite-catalog-recovery/catalog.mylite`: 110,592
  bytes.

## Risks And Unresolved Questions

- Row payloads are still table-sized logical streams, so write amplification is
  reduced in the catalog but not solved for large tables.
- Append-only row page allocation grows files until a later free-list and
  compaction slice.
- The row payload bridge still stores raw MariaDB record images. A future row
  format must decide durable column encoding, NULL handling, overflow values,
  and compatibility across table rebuilds.
- Indexes remain rebuilt from row images in memory. Durable index pages are a
  separate slice.
- FNV-1a remains the current development checksum. A later durability slice may
  replace it with a stronger page checksum.
