# virtual-generated-index-storage

> Superseded note: this slice left virtual generated BLOB/TEXT and GEOMETRY
> key parts out of scope.
> `docs/specs/virtual-generated-lob-index-storage/specs.md` later added
> ordinary prefix/HASH/unique and FULLTEXT key support for virtual generated
> BLOB/TEXT values. Virtual generated GEOMETRY SPATIAL keys remain constrained
> by MariaDB SQL because virtual generated columns cannot be declared
> `NOT NULL`.

## Problem Statement

MyLite accepts non-indexed virtual generated columns and indexes on stored
generated columns, but still rejects indexes whose key parts are non-stored
virtual generated columns. That leaves a visible gap in the common
MariaDB/MySQL application SQL surface: `INDEX (generated_virtual_column)`,
unique constraints over virtual generated columns, and copy `ALTER` that adds
such indexes.

The missing part is not expression evaluation. MariaDB already owns generated
column expression parsing, dependency tracking, diagnostics, and value
materialization. MyLite needs to invoke that machinery at the storage-engine
key-maintenance boundaries where it builds durable key-image streams from
stored rows.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/table.h:394` defines virtual-column update modes,
  including `VCOL_UPDATE_INDEXED` and `VCOL_UPDATE_INDEXED_FOR_UPDATE`.
- `vendor/mariadb/server/sql/table.h:1801` exposes
  `TABLE::update_virtual_field()`, which computes one generated field through
  MariaDB's expression tree.
- `vendor/mariadb/server/sql/table.cc:8360` documents marking base fields used
  by virtual indexed fields with `PART_INDIRECT_KEY_FLAG`.
- `vendor/mariadb/server/sql/table.cc:9217` through `table.cc:9367`
  implements `TABLE::update_virtual_fields()` and documents that virtual values
  are stored in the table record buffer.
- `vendor/mariadb/server/sql/table.cc:9377` through `table.cc:9405`
  implements `TABLE::update_virtual_field()`.
- `vendor/mariadb/server/sql/key.cc:100` through `key.cc:165` implements
  `key_copy()`, which builds key bytes from a complete table record buffer and
  `KEY_PART_INFO` metadata.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:4289` currently rejects
  key parts whose fields are non-stored virtual generated columns.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:4311` currently checks
  unique-key NULL semantics from raw record null bits before building key
  images.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:4459` through
  `ha_mylite.cc:4507` builds MyLite durable key images from handler records or
  decoded persisted rows.

## Scope

This slice will:

- accept supported non-stored virtual generated columns as BTREE/HASH key
  parts,
- compute non-stored virtual generated key parts before MyLite calls
  `key_copy()`,
- preserve the existing row payload format and avoid persisting derived virtual
  values separately,
- enforce unique indexes over virtual generated key parts, including
  MariaDB-style multiple-NULL behavior,
- rebuild durable `INDEXPAGE` roots for virtual generated indexes from decoded
  rows,
- support copy `ALTER` adding a virtual generated index to a populated table,
- persist virtual generated index metadata and key roots across fresh-process
  reopen and recovery fallback,
- update smoke coverage and docs.

## Non-Goals

- Do not implement a MyLite-owned generated-expression evaluator.
- Do not persist generated virtual values in the durable row payload as a new
  storage format.
- Do not support non-stored virtual BLOB/TEXT or GEOMETRY generated columns in
  this slice. The later `virtual-generated-lob-geometry-storage` slice supports
  those row shapes for non-indexed virtual generated values.
- Do not add physical inverted FULLTEXT or R-tree SPATIAL indexes.
- Do not change MariaDB's generated-column SQL-mode, determinism, or dependency
  validation rules.

## Proposed Design

Remove the non-stored virtual generated-column rejection from key-part shape
validation for ordinary MyLite key storage. Table-shape validation remains the
guard for virtual generated field types that the current row bridge cannot
materialize safely.

Add a MyLite key-image materialization helper around `key_copy()`:

1. Detect whether a key has non-stored virtual generated key parts.
2. For ordinary stored-only keys, keep the existing direct `key_copy()` path.
3. For keys with virtual generated parts, save `table->record[0]`, copy the
   source record into `table->record[0]`, call
   `TABLE::update_virtual_field()` for each non-stored virtual key-part field,
   build the key bytes with `key_copy()`, then restore the saved record bytes.

This keeps expression evaluation in MariaDB. MyLite only chooses the point at
which indexed virtual values must exist in the record buffer used by key-image
storage.

Unique-key NULL handling should use the generated key image rather than raw
record null bits. This is required because a virtual generated key part may be
NULL only after expression materialization.

## Affected Subsystems

- MyLite key-shape validation.
- MyLite durable key-image generation.
- MyLite unique-key enforcement.
- Durable `INDEXPAGE` rebuilds on DML, copy `ALTER`, and reopen fallback.
- Generated-column smoke coverage and reports.
- Architecture and roadmap documentation.

## DDL Metadata Routing Impact

No new metadata route is needed. Virtual generated index definitions already
live in MariaDB's persisted table-definition image. Supported DDL must still
commit only through the primary `.mylite` catalog and not leave `.frm` sidecars.
Failed generated-index DDL must leave existing table definitions, rows, and
index roots unchanged.

## Single-File And Embedded-Lifecycle Implications

No companion files are introduced. Row payloads remain unchanged. Derived
virtual values are transient record-buffer values used to build deterministic
key images; durable state remains the table-definition image, row payloads, and
normal `INDEXPAGE` key roots in the primary `.mylite` file.

## Public API Or File-Format Impact

No public `libmylite` API change. No file-format version bump is expected
because the durable key-image stream already stores MariaDB key bytes for each
index entry and can represent virtual generated key values once materialized.

## Binary-Size Impact

Expected binary growth is small: helper code and smoke assertions only. No new
dependency is added.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc` to verify:

- `CREATE TABLE` with an indexed virtual generated column succeeds,
- forced lookup through the virtual generated index returns the expected row,
- ordered index scan over the virtual generated key returns the expected order,
- unique virtual generated indexes reject duplicate non-NULL generated values,
- unique virtual generated indexes permit multiple NULL generated values,
- updating a base column updates virtual generated index entries,
- copy `ALTER` can add a virtual generated index to populated rows,
- persisted virtual generated index lookup/order works after fresh-process
  reopen,
- recovery fallback keeps the previously accepted virtual generated index
  roots.

Run:

- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
- `git diff --check`

## Acceptance Criteria

- MyLite accepts BTREE/HASH indexes and unique constraints over supported
  non-stored virtual generated columns.
- MyLite rejects no valid table solely because an ordinary key part is a
  supported non-stored virtual generated field.
- Virtual generated key images are correct for insert, update, delete, index
  rebuild, copy `ALTER`, reopen, and recovery fallback paths.
- Unique virtual generated indexes follow MariaDB NULL semantics.
- Generated-column row payload and file format stay unchanged.
- Existing FK, CHECK, BLOB/TEXT, nullable key, descending/HASH, FULLTEXT,
  SPATIAL, transaction, recovery, lifecycle, and sidecar checks keep passing.

## Risks And Unresolved Questions

- MariaDB supports complex generated expressions whose evaluation can allocate
  memory in the table expression arena. This slice uses MariaDB's existing
  `TABLE::update_virtual_field()` behavior rather than adding separate lifetime
  management.
- Foreign keys over virtual generated columns may need targeted compatibility
  coverage after base virtual generated index storage works. This slice keeps
  FK behavior on the existing key-image path so later FK coverage can build on
  the same materialization helper.
- At this slice's completion, virtual generated BLOB/TEXT and GEOMETRY key
  parts remained out of scope until prefix, FULLTEXT, SPATIAL, and related
  key-image semantics had a dedicated design. The later
  `virtual-generated-lob-index-storage` slice added BLOB/TEXT prefix and
  FULLTEXT support; virtual generated GEOMETRY SPATIAL keys remain constrained
  by MariaDB SQL syntax.

## Implementation Result

Implemented in `vendor/mariadb/server/storage/mylite/ha_mylite.cc`.

- Ordinary BTREE/HASH key parts now accept supported non-stored virtual
  generated fields.
- MyLite materializes virtual generated key-part values by copying the source
  record into `table->record[0]`, calling MariaDB's
  `TABLE::update_virtual_field()` for each virtual key part, building key bytes
  through `key_copy()`, and restoring the saved live record buffer.
- Unique-key NULL checks now inspect generated key-image bytes, so computed
  virtual NULL values follow MariaDB's multiple-NULL unique-key behavior.
- Child FK NULL checks also use generated key-image bytes for the FK prefix,
  keeping later FK coverage on the same materialization path.
- At this slice's completion, FULLTEXT and SPATIAL keys continued to reject
  non-stored virtual generated key parts until their non-ordered storage paths
  had dedicated support. The later `virtual-generated-lob-index-storage` slice
  added FULLTEXT support for virtual generated BLOB/TEXT values; virtual
  generated GEOMETRY SPATIAL keys remain constrained by MariaDB SQL syntax.

Extended `vendor/mariadb/server/mylite/storage_engine_smoke.cc` to verify:

- same-process forced lookup and ordered scan through a virtual generated
  index,
- unique virtual generated indexes rejecting duplicate non-NULL generated
  values while allowing multiple computed NULL values,
- base-column updates refreshing virtual generated index entries,
- copy `ALTER` adding a virtual generated index to populated rows,
- persisted virtual generated index lookup/order across fresh-process reopen,
- recovery fallback preserving the previously accepted virtual generated index
  roots.

Verification passed on May 13, 2026:

- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
- `git diff --check`

Representative storage reports:

- `mylite-storage-engine-report.txt`: `status=0`, `message=ok`,
  `generated_virtual_index_lookup_id=2`,
  `generated_virtual_index_order_ids=4,5,1,3,2`,
  `generated_virtual_index_duplicate=rejected`,
  `generated_virtual_index_null_ids=4,5`,
  `generated_virtual_index_updated_ids=2:10,3:12`, and
  `generated_virtual_index_alter_ids=1,2`.
- `mylite-catalog-read-report.txt`: `status=0`, `message=ok`,
  `persisted_generated_virtual_index_lookup_id=2`,
  `persisted_generated_virtual_index_order_ids=1,3,2`, and
  `persisted_generated_virtual_index_alter_ids=1,2`.
- `mylite-catalog-recovery-read-report.txt`: `status=0`, `message=ok`, with
  the same persisted virtual generated index values after recovery fallback.
- `mylite-compatibility-harness-report.txt`, `libmylite-open-close-report.txt`,
  and `mylite-embedded-bootstrap-report.txt` all reported `status=0`.
