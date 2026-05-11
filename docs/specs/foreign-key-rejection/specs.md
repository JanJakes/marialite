# foreign-key-rejection

## Problem Statement

MyLite does not yet implement foreign-key metadata, referential checks, cascade
actions, parent-table prelocking, or crash-safe FK updates. Allowing
`FOREIGN KEY` clauses to pass accidentally would be worse than a clear
unsupported error because applications could believe referential integrity is
enforced when it is not.

This slice makes foreign-key DDL fail explicitly for MyLite tables.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/handler.h:2368` defines `HA_CREATE_INFO` with an
  `Alter_info *alter_info` pointer still present for create/alter processing.
- `vendor/mariadb/server/sql/handler.h:714` defines `ALTER_ADD_FOREIGN_KEY`.
- `vendor/mariadb/server/sql/sql_lex.cc:12519` and
  `vendor/mariadb/server/sql/sql_lex.cc:12553` set
  `ALTER_ADD_FOREIGN_KEY` when parsing foreign-key clauses.
- `vendor/mariadb/server/sql/sql_parse.cc:7277` checks parent-table access for
  parsed FK clauses but does not by itself mean the selected storage engine
  enforces the FK.
- `vendor/mariadb/server/sql/sql_table.cc:11096` repeats parent-table access
  checks for `ALTER TABLE ... ADD FOREIGN KEY`.
- `vendor/mariadb/server/sql/handler.h:1797` defines
  `HTON_SUPPORTS_FOREIGN_KEYS`; MyLite does not set this engine flag.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:868` is MyLite's
  `handler::create()` gate for table-shape support.

## Scope

This slice will:

- reject `CREATE TABLE ... FOREIGN KEY ... ENGINE=MYLITE`,
- reject `ALTER TABLE ... ADD FOREIGN KEY` on a MyLite table,
- keep ordinary supported keys, nullable keys, and BLOB/TEXT prefix keys
  supported,
- add smoke evidence that FK DDL fails while the referenced/parent MyLite table
  remains usable,
- document that FK enforcement and cascade semantics are deferred.

## Non-Goals

- Do not implement FK catalog storage.
- Do not implement parent/child constraint checks.
- Do not implement cascade, restrict, set-null, or no-action behavior.
- Do not implement FK prelocking or FK-aware `DROP`, `TRUNCATE`, `ALTER`, or
  `RENAME` behavior.
- Do not add a SQL-layer fork unless handler-level rejection proves
  insufficient.

## Proposed Design

Use the `HA_CREATE_INFO::alter_info` pointer available in MyLite's
`handler::create()` call. Add a narrow helper:

```c++
static bool mylite_create_info_has_foreign_key(
    const HA_CREATE_INFO *create_info);
```

The helper should return true when `create_info`, `create_info->alter_info`,
and `ALTER_ADD_FOREIGN_KEY` indicate the pending CREATE/ALTER contains an FK
clause. `ha_mylite::create()` should reject before storing the table
definition.

This keeps the rejection at MyLite's storage-engine support boundary, avoids
parsing binary frm images or SQL strings, and leaves MariaDB's parser and
diagnostic path in control of the final SQL error text.

## Affected Subsystems

- MyLite handler create validation.
- Storage smoke unsupported-surface coverage.
- Single-file storage architecture docs and roadmap.

## DDL Metadata Routing Impact

Rejected FK DDL must not store a child table definition or leave a visible
intermediate MyLite catalog entry. Parent tables created before the failed FK
statement must remain usable.

## Single-File And Embedded-Lifecycle Implications

No file-format change. No companion file is introduced. The smoke should prove
the failed FK statement does not add persistent catalog entries.

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
  - create a supported parent MyLite table,
  - reject a child table with `FOREIGN KEY(parent_id) REFERENCES parent(id)`,
  - reject `ALTER TABLE parent ADD CONSTRAINT ... FOREIGN KEY ...`,
  - verify the failed child table is absent,
  - verify the parent table remains readable,
  - drop the parent table.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` for changed shell scripts
  - `git diff --check`

## Acceptance Criteria

- FK CREATE and ALTER statements fail explicitly for MyLite tables.
- Failed FK DDL does not leave a child table or broken catalog entry.
- The referenced MyLite parent table remains usable after failed FK DDL.
- Existing supported key, nullable key, BLOB/TEXT key, copy ALTER, transaction,
  recovery, and public API coverage keeps passing.
- Docs and roadmap identify FK semantics as deliberately deferred.

## Risks And Unresolved Questions

- If MariaDB clears `ALTER_ADD_FOREIGN_KEY` before calling MyLite's
  `create()` path for some FK forms, this slice may need to inspect
  `alter_info->key_list` directly for `Key::FOREIGN_KEY`.
- MariaDB may use the same `create()` path for `ALTER TABLE ... ADD FOREIGN KEY`
  copy operations; the helper must reject both CREATE and ALTER forms.
- This does not cover future FK metadata imported from an existing MariaDB
  datadir or SQL dump conversion tool.

## Implementation Result

Implemented.

- `ha_mylite::create()` now rejects any pending CREATE/ALTER whose
  `HA_CREATE_INFO::alter_info` indicates `ALTER_ADD_FOREIGN_KEY`.
- The helper also inspects `alter_info->key_list` for `Key::FOREIGN_KEY` as a
  defensive fallback if a future MariaDB path does not leave the flag set.
- Storage smoke now rejects both `CREATE TABLE ... FOREIGN KEY ... ENGINE=MYLITE`
  and `ALTER TABLE ... ADD FOREIGN KEY` while proving the referenced MyLite
  parent table remains readable and the failed child table is absent.

Verification on 2026-05-11:

- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `bash -n tools/run-compatibility-test-harness.sh tools/run-storage-engine-smoke.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
- `git diff --check`

Observed report evidence:

- `unsupported_foreign_key_create=rejected`
- `unsupported_foreign_key_alter=rejected`
- `foreign_key_parent_count=1`

Measured `MinSizeRel` artifacts after the storage-smoke build:

- `build/mariadb-minsize/mylite/libmylite.a`: 87,206 bytes.
- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,417,754 bytes.
- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 571 archive objects.
- Dynamic plugin artifacts: none.
