# generated-column-storage

> Superseded note: this slice kept GEOMETRY generated-column storage out of
> scope while GEOMETRY columns were unsupported.
> `docs/specs/geometry-spatial-storage/specs.md` later added base GEOMETRY row
> storage; virtual generated GEOMETRY index support remains out of scope.
>
> Superseded note: this slice originally rejected indexes on non-stored virtual
> generated columns. `docs/specs/virtual-generated-index-storage/specs.md`
> later added BTREE/HASH index and unique-constraint support for supported
> non-stored virtual generated columns.
>
> Superseded note: this slice originally left virtual generated BLOB/TEXT and
> GEOMETRY row shapes out of scope.
> `docs/specs/virtual-generated-lob-geometry-storage/specs.md` later added
> support for non-indexed virtual generated LOB and GEOMETRY row values while
> keeping virtual LOB/GEOMETRY key parts out of scope.
>
> Superseded note: `docs/specs/virtual-generated-lob-index-storage/specs.md`
> later added ordinary prefix/HASH/unique and FULLTEXT key support for virtual
> generated BLOB/TEXT values. Virtual generated GEOMETRY SPATIAL keys remain
> constrained by MariaDB SQL because virtual generated columns cannot be
> declared `NOT NULL`.

## Problem Statement

MyLite currently rejects all generated-column DDL. That was safer than
accidentally persisting unclear raw-record shapes, but it now blocks common
MariaDB/MySQL application SQL. MariaDB already owns generated-column expression
parsing, validation, write-time computation, and read-time virtual-field
evaluation above the handler layer. MyLite can support the generated-column
subset that fits the current raw-record bridge while continuing to reject
generated index shapes that require storage-engine participation MyLite cannot
yet provide.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/field.h:526` defines generated virtual and stored
  `Virtual_column_info` kinds.
- `vendor/mariadb/server/sql/field.h:640` reports that stored generated values
  are already present in the row buffer after reads.
- `vendor/mariadb/server/sql/field.h:1494` defines `Field::stored_in_db()`;
  non-stored virtual columns are excluded from the stored record image.
- `vendor/mariadb/server/sql/table.h:806` documents that
  `TABLE_SHARE::reclength` excludes generated-only virtual fields.
- `vendor/mariadb/server/sql/table.h:837` and `table.h:838` track stored and
  purely virtual fields on `TABLE_SHARE`.
- `vendor/mariadb/server/sql/handler.cc:3830` and nearby handler read wrappers
  call `TABLE::update_virtual_fields(..., VCOL_UPDATE_FOR_READ)` after engine
  row reads.
- `vendor/mariadb/server/sql/table.cc:9217` implements
  `TABLE::update_virtual_fields()`.
- `vendor/mariadb/server/sql/table.cc:9475` implements
  `TABLE::update_generated_fields()` before writes, including stored generated
  columns and constraints.
- `vendor/mariadb/server/sql/table.cc:4286` copies generated-column metadata
  onto generated key parts through `TABLE::update_keypart_vcol_info()`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.h:40` advertises handler
  table capability flags.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:3250` validates MyLite
  table row-storage support.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:3333` validates MyLite
  key-part storage support.

## Scope

This slice will:

- accept non-indexed virtual generated columns whose values MariaDB computes
  after MyLite row reads,
- accept stored generated columns because their generated values are present in
  MariaDB's stored record image before handler writes,
- accept indexes on stored generated columns through the existing durable
  key-image stream,
- leave indexes on non-stored virtual generated columns to the later
  `virtual-generated-index-storage` slice,
- verify generated-column `CREATE TABLE` and copy `ALTER TABLE` behavior,
- verify generated-column metadata and values survive fresh-process reopen,
- update docs and roadmap from generated-column rejection to supported subset.

## Non-Goals

- Do not implement virtual generated indexes in this slice.
- Do not implement a MyLite-owned generated-expression evaluator.
- Do not support GEOMETRY generated-column storage while GEOMETRY columns
  remain unsupported.
- Do not change MariaDB SQL-mode dependency validation for generated
  expressions.
- Do not implement FULLTEXT or SPATIAL indexes.

## Proposed Design

Replace the blanket generated-column rejection with two narrower checks:

1. Table-shape validation must reject generated columns only when the generated
   field shape cannot be represented by the current record bridge, such as
   GEOMETRY generated fields while GEOMETRY storage is unsupported.
2. Key-shape validation must reject key parts whose field is a non-stored
   virtual generated column in this slice. Stored generated columns are
   physically present in the record image, so existing key-image extraction can
   index them.

MyLite does not need to persist separate expression metadata in this slice. The
existing persisted MariaDB table-definition image contains generated-column
metadata, and MariaDB reconstructs `Field::vcol_info` during discovery. Stored
generated values live in row payloads. Non-stored virtual values are computed by
MariaDB handler wrappers after MyLite returns base record bytes.

## Affected Subsystems

- MyLite table-shape validation.
- MyLite key-shape validation.
- Row encode/decode indirectly, by allowing generated fields through existing
  raw-record storage.
- Durable index roots for stored generated key parts.
- Copy ALTER smoke coverage.
- Architecture and roadmap documentation.

## DDL Metadata Routing Impact

Accepted generated-column DDL must persist only in the primary `.mylite` file
through the existing table-definition image and row/index payloads. At the time
this slice landed, rejected virtual generated indexes had to fail before
replacement table definitions, rows, or index roots were committed; that
rejection was superseded by `virtual-generated-index-storage`.

## Single-File And Embedded-Lifecycle Implications

No companion files are introduced. Generated-expression metadata remains inside
the stored MariaDB table-definition image. Stored generated column values live
in normal row payload pages; stored generated indexes live in normal index
payload pages.

## Public API Or File-Format Impact

No public `libmylite` API change. No file-format version bump is expected
because generated metadata is already part of the stored table-definition image
and row/index payload formats do not change.

## Binary-Size Impact

Expected binary growth is small: narrower validation helpers plus smoke
coverage. No new dependency is added.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc`:
  - create and query a virtual generated column,
  - update the base column and verify the virtual value changes,
  - create and query a stored generated column,
  - create and force-use an index on a stored generated column,
  - reject an index on a virtual generated column and verify table absence,
  - copy-ALTER a generated stored column onto a populated table and verify
    copied rows have generated values,
  - persist generated-column tables and verify values/index lookups after
    fresh-process reopen.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
  - `git diff --check`

## Acceptance Criteria

- Virtual generated column reads are computed correctly before and after base
  row updates.
- Stored generated column values are persisted correctly.
- Stored generated column indexes support forced index lookup/order paths.
- Virtual generated column indexes are outside this slice and were later
  implemented by `virtual-generated-index-storage`.
- Copy ALTER adding a stored generated column preserves rows and computes the
  generated value for existing data.
- Fresh-process reopen preserves generated-column metadata and stored generated
  index roots.
- Existing FK, CHECK, BLOB/TEXT, nullable key, transaction, recovery, lifecycle,
  and sidecar checks keep passing.

## Risks And Unresolved Questions

- Virtual generated BLOB/TEXT and GEOMETRY row values now have separate
  targeted coverage in `virtual-generated-lob-geometry-storage`.
- Virtual generated indexes needed a design for computing generated key images
  from decoded stored rows during index-root rebuilds; that design now lives in
  `virtual-generated-index-storage`.
- Expression SQL-mode dependencies remain MariaDB-owned; MyLite should not
  duplicate that logic unless a later normalized catalog replaces frm images.

## Implementation Result

Implemented in `vendor/mariadb/server/storage/mylite/ha_mylite.h` and
`vendor/mariadb/server/storage/mylite/ha_mylite.cc`.

- MyLite now advertises `HA_CAN_VIRTUAL_COLUMNS` so MariaDB accepts generated
  column definitions for the engine.
- The row-shape gate now accepts stored generated columns and non-indexed
  virtual generated columns. The later
  `virtual-generated-lob-geometry-storage` slice also accepts non-indexed
  virtual generated BLOB/TEXT and GEOMETRY row values.
- The original key-shape gate rejected key parts backed by non-stored virtual
  generated columns and accepted stored generated-column key parts. The later
  `virtual-generated-index-storage` slice accepts supported non-stored virtual
  generated key parts.
- Existing row and index storage formats are unchanged: generated metadata
  stays in the persisted MariaDB table-definition image, stored generated
  values stay in normal row payloads, and stored generated indexes stay in
  normal `INDEXPAGE` payloads.

Extended `vendor/mariadb/server/mylite/storage_engine_smoke.cc` to verify:

- same-process virtual generated-column reads before and after base-column
  updates,
- same-process stored generated-column rows,
- `SHOW CREATE TABLE` metadata for a stored generated column and its index,
- forced lookup and ordered scan through a stored generated-column index,
- historical explicit rejection and table absence for a virtual
  generated-column index,
- copy `ALTER` adding a stored generated column to populated rows,
- fresh-process reopen of persisted virtual generated rows, stored generated
  index lookup, and copy-ALTER generated rows.

Verification passed on May 13, 2026:

- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
- `git diff --check`

Representative storage reports:

- `mylite-storage-engine-report.txt`: `status=0`, `message=ok`,
  `generated_virtual_rows=1:4,2:10`,
  `generated_virtual_updated_rows=1:14,2:10`,
  `generated_stored_rows=1:4,2:10,3:8`,
  `generated_show_create=present`,
  `generated_stored_index_lookup_id=2`,
  `generated_stored_index_order_ids=1,3,2`,
  `generated_alter_rows=1:4`, and
  `unsupported_generated_virtual_index=rejected`. Later storage smokes replaced
  that expectation with virtual generated index lookup/order/unique coverage.
- `mylite-catalog-read-report.txt`: `status=0`, `message=ok`,
  `persisted_generated_virtual_rows=1:6,2:12`,
  `persisted_generated_stored_index_lookup_id=2`,
  `persisted_generated_alter_rows=1:8,2:14`, and row/index payload listings
  include persisted generated-column tables.
- `mylite-compatibility-harness-report.txt`: all groups reported `status=0`.
- `mylite-build-report.txt`: `libmariadbd.a` is 43,468,534 bytes with 500
  archive objects in the MinSizeRel profile.
