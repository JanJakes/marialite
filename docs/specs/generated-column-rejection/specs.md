# generated-column-rejection

## Superseded

This slice is retained as historical documentation for the earlier
conservative rejection gate. It was superseded by
[`generated-column-storage`](../generated-column-storage/specs.md), which
supports non-indexed virtual generated columns, stored generated columns,
stored generated-column indexes, and generated-column copy ALTER for supported
MyLite row shapes while continuing to reject virtual generated-column indexes.

## Problem Statement

Generated columns in MariaDB involve expression metadata, virtual/stored
materialization rules, SQL-mode dependencies, generated-column indexes, and
copy/alter interactions. MyLite's current raw-record bridge does not yet own
those semantics. Non-indexed generated columns may appear to work accidentally
if MariaDB computes a value before handler writes, but that would leave unclear
reopen, ALTER, and expression-reevaluation behavior.

This slice rejects generated columns explicitly until MyLite designs the full
generated-column lifecycle.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/field.h:526` defines generated virtual and stored
  `Virtual_column_info` types.
- `vendor/mariadb/server/sql/field.h:899` stores a field's
  `Virtual_column_info *vcol_info`.
- `vendor/mariadb/server/sql/field.h:1494` treats fields with `vcol_info` as
  generated or otherwise computed when deciding whether a field is physically
  stored.
- `vendor/mariadb/server/sql/table.h:838` and
  `vendor/mariadb/server/sql/table.h:1369` expose virtual/generated field
  metadata on `TABLE_SHARE` and `TABLE`.
- `vendor/mariadb/server/sql/table.cc:1155` parses generated-column
  definitions from frm metadata into field `vcol_info`.
- `vendor/mariadb/server/sql/table.cc:1821` updates virtual fields for
  insert/update paths.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1995` already rejects
  generated keys with `HA_GENERATED_KEY`, but non-key generated columns are not
  rejected explicitly yet.

## Scope

This slice will:

- reject virtual generated columns in MyLite table definitions,
- reject stored generated columns in MyLite table definitions,
- reject `ALTER TABLE ... ADD COLUMN ... GENERATED ALWAYS AS ...`,
- keep ordinary defaults, check constraints, supported nullable keys, and
  BLOB/TEXT prefix keys unchanged,
- add smoke evidence that rejected generated-column DDL leaves no visible table
  or broken catalog entry.

## Non-Goals

- Do not implement generated-column expression storage.
- Do not implement virtual-column read materialization.
- Do not implement stored generated-column write/update recomputation.
- Do not support indexes on generated columns.
- Do not change MariaDB expression parsing or SQL-layer generated-column code.

## Proposed Design

Add a MyLite table-shape helper:

```c++
static bool mylite_table_has_generated_columns(const TABLE *table);
```

The helper should inspect `table->field` and return true when any `Field` has
`vcol_info`. `ha_mylite::create()` already calls
`mylite_table_supports_row_storage()` before storing a table definition, so the
row-shape validation can reject generated columns before any catalog mutation.

This is intentionally conservative: MariaDB also uses `Virtual_column_info` for
some default and check metadata, but those are not table fields with
`field->vcol_info`. Existing default-value and check-constraint behavior should
remain unchanged.

## Affected Subsystems

- MyLite handler table-shape validation.
- Storage smoke unsupported-surface coverage.
- Single-file storage architecture docs and roadmap.

## DDL Metadata Routing Impact

Rejected generated-column DDL must not store a table definition or leave a
visible intermediate catalog entry. A failed generated-column ALTER must leave
the original MyLite table usable.

## Single-File And Embedded-Lifecycle Implications

No file-format change. No companion file is introduced.

## Public API Or File-Format Impact

No public `libmylite` API change. No file-format version bump.

## Binary-Size Impact

Expected growth is tiny: one static helper plus smoke strings. Post-implementation
`MinSizeRel` artifact sizes will be recorded.

## License, Trademark, And Dependency Impact

No new dependency. All changes remain in existing GPL-2.0-only MyLite and
MariaDB-derived source files.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc`:
  - reject a virtual generated-column table,
  - reject a stored generated-column table,
  - create a normal base table and reject `ALTER TABLE ... ADD COLUMN ...
    GENERATED ALWAYS AS ...`,
  - verify the base table remains readable and the failed generated tables are
    absent,
  - drop the base table.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` for changed shell scripts
  - `git diff --check`

## Acceptance Criteria

- Virtual and stored generated-column CREATE statements fail explicitly for
  MyLite tables.
- Generated-column ALTER statements fail explicitly and leave the base table
  usable.
- Failed generated-column DDL does not leave visible catalog entries.
- Existing supported storage, copy ALTER, FK rejection, transaction, recovery,
  and public API coverage keeps passing.
- Docs and roadmap identify generated-column semantics as deliberately
  deferred.

## Risks And Unresolved Questions

- MariaDB may represent some computed metadata in `Virtual_column_info` without
  setting `Field::vcol_info`; this slice only rejects table fields with
  generated-column metadata.
- Future support needs to distinguish virtual generated columns, stored
  generated columns, generated-column indexes, expression SQL-mode
  dependencies, and ALTER recomputation.

## Implementation Result

Implemented.

- MyLite row-shape validation now rejects any field whose `Field::vcol_info`
  is set before storing a table definition.
- The existing `HA_GENERATED_KEY` key-shape rejection remains in place for
  generated-column indexes.
- Storage smoke now rejects virtual generated-column CREATE, stored
  generated-column CREATE, and generated-column ALTER, and verifies the base
  table remains readable after the failed ALTER.

Verification on 2026-05-11:

- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `bash -n tools/run-compatibility-test-harness.sh tools/run-storage-engine-smoke.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
- `git diff --check`

Observed report evidence:

- `unsupported_generated_virtual=rejected`
- `unsupported_generated_stored=rejected`
- `unsupported_generated_alter=rejected`
- `generated_base_count=1`

Measured `MinSizeRel` artifacts after the storage-smoke build:

- `build/mariadb-minsize/mylite/libmylite.a`: 87,206 bytes.
- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,417,786 bytes.
- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 571 archive objects.
- Dynamic plugin artifacts: none.
