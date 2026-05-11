# duplicate-key-dml-storage

## Problem Statement

MyLite enforces primary and unique keys and returns MariaDB duplicate-key
diagnostics. That is enough for plain failing `INSERT`, but common application
SQL also depends on duplicate-key recovery paths:

- `INSERT IGNORE`,
- `REPLACE`,
- `INSERT ... ON DUPLICATE KEY UPDATE`.

`INSERT IGNORE` already has public API warning coverage. `REPLACE` and
`ON DUPLICATE KEY UPDATE` need the storage engine to identify the existing row
that caused the duplicate so MariaDB can delete or update it. MyLite currently
sets the duplicate key number but does not copy the offending row position into
`handler::dup_ref`, leaving this common SQL surface under-specified.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/sql_class.h:115` defines
  `DUP_ERROR`, `DUP_REPLACE`, and `DUP_UPDATE`.
- `vendor/mariadb/server/sql/sql_insert.cc:2402` routes inserts through
  `Write_record::write_record()`, dispatching duplicate behavior to
  `single_insert()`, `insert_on_duplicate_update()`, or `replace_row()`.
- `vendor/mariadb/server/sql/sql_insert.cc:2471` calls
  `handler::get_dup_key()` after duplicate errors.
- `vendor/mariadb/server/sql/sql_insert.cc:2008` implements
  `Write_record::locate_dup_record()`. If the handler reports
  `lookup_errkey`, MariaDB uses `handler::dup_ref` with `ha_rnd_pos()` to read
  the offending row.
- `vendor/mariadb/server/sql/sql_insert.cc:2119` documents `REPLACE` as
  either `DELETE + INSERT` or an optimized `UPDATE`.
- `vendor/mariadb/server/sql/sql_insert.cc:2220` implements the
  `ON DUPLICATE KEY UPDATE` update branch after locating the duplicate row.
- `vendor/mariadb/server/sql/handler.cc:8236` implements
  `handler::ha_write_row()`, which calls the engine's `write_row()` and then
  routes duplicate errors back to SQL-layer duplicate handling.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1480` sets
  `errkey` and `lookup_errkey` for MyLite duplicate insert errors, but it does
  not set `dup_ref`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:4552` finds duplicate
  key entries in `mylite_check_unique_constraints_locked()` and can identify
  the conflicting `Mylite_row::rowid`.

## Scope

This slice will:

- propagate the duplicate row id from MyLite unique-key checks,
- copy the duplicate row position into `handler::dup_ref` for duplicate insert
  and update errors,
- preserve existing duplicate-key diagnostics and ordinary failing `INSERT`
  behavior,
- add same-process storage smoke coverage for `INSERT IGNORE`, `REPLACE`, and
  `INSERT ... ON DUPLICATE KEY UPDATE`,
- add fresh-process persistence coverage for the same clauses.

## Non-Goals

- Do not implement trigger execution. `REPLACE` and ODKU trigger semantics
  remain blocked until trigger metadata is supported.
- Do not add a new file format record. Duplicate positions are current MyLite
  row ids already stored in row payloads.
- Do not change MariaDB SQL-layer conflict semantics.
- Do not claim full replication/binlog behavior; MyLite's embedded profile
  still excludes daemon replication surfaces.

## Proposed Design

Extend `mylite_check_unique_constraints_locked()` with an optional
`duplicate_rowid` output. When a key image matches an existing non-deleted row,
set both the duplicate key ordinal and the duplicate row id before returning
`HA_ERR_FOUND_DUPP_KEY`.

Thread that output through `mylite_store_row()` and `mylite_update_row()`.
When `ha_mylite::write_row()` or `ha_mylite::update_row()` receives
`HA_ERR_FOUND_DUPP_KEY`, keep setting `errkey` and `lookup_errkey`, and also
write the duplicate row id into `dup_ref` with the existing little-endian row
position encoding used by `position()` and `rnd_pos()`.

This keeps the SQL layer on its existing MariaDB path:

- failing plain `INSERT` still surfaces the duplicate diagnostic,
- `INSERT IGNORE` can turn the duplicate into a warning,
- `REPLACE` can locate the existing row, delete or update it, then retry,
- ODKU can locate the existing row and call `ha_update_row()`.

## Affected Subsystems

- MyLite duplicate-key detection and handler position reporting.
- Storage smoke same-process and persistence coverage.
- Roadmap and single-file storage documentation.

## DDL Metadata Routing Impact

No table-definition metadata change. The tests use existing primary and unique
key metadata persisted in the MariaDB table-definition image and MyLite
`INDEXPAGE` roots.

## Single-File And Embedded-Lifecycle Implications

No durable format change. `REPLACE` and ODKU mutate rows through existing
delete/update/insert paths, which already publish row and index roots through
the current transaction and catalog generation machinery. Reopen coverage must
prove the final row/index state persists in the primary `.mylite` file without
sidecars.

## Public API Or File-Format Impact

No public API change and no file-format version bump.

## Binary-Size Impact

Expected size impact is negligible: one extra row-id output through existing
duplicate-key helpers and smoke coverage.

## License, Trademark, And Dependency Impact

No new dependency, license, or trademark impact.

## Test And Verification Plan

Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc` to cover:

- `INSERT IGNORE` skipping duplicate primary-key rows while inserting valid
  rows,
- `REPLACE` by primary key,
- `REPLACE` by secondary unique key on a table with `AUTO_INCREMENT`,
- `INSERT ... ON DUPLICATE KEY UPDATE` by primary key,
- ODKU by secondary unique key,
- final index-backed lookup and ordered rows,
- persistence write/read and fresh-process append after reopen.

Run:

- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `bash -n tools/run-storage-engine-smoke.sh
  tools/run-compatibility-test-harness.sh
  tools/run-libmylite-open-close-smoke.sh
  tools/run-embedded-bootstrap-smoke.sh
  tools/build-mariadb-minsize.sh`
- `git diff --check`

## Acceptance Criteria

- `REPLACE` and ODKU can locate duplicate rows through `dup_ref`.
- Duplicate-key diagnostics for plain failing inserts remain unchanged.
- `INSERT IGNORE`, `REPLACE`, and ODKU preserve primary and secondary unique
  indexes after update/delete/insert paths.
- Final conflict-DML state survives fresh-process reopen.
- Existing table DDL, row DML, indexes, constraints, transactions, recovery,
  temporary tables, and public API coverage keeps passing.

## Risks And Unresolved Questions

- MariaDB may consume generated autoincrement values differently for failed
  duplicate attempts than MyLite's current table-local counter path. The slice
  verifies final row semantics and avoids over-specifying gaps.
- REPLACE with future trigger support will need additional coverage because
  MariaDB runs delete/insert triggers around the duplicate row.
- ODKU updates that cause a second unique-key conflict should continue to fail
  through existing `ha_update_row()` duplicate diagnostics; this slice does not
  add conflict recovery for failed update branches.

## Implementation Result

MyLite now propagates the conflicting row id from
`mylite_check_unique_constraints_locked()` through insert/update helpers and
stores it in `handler::dup_ref` whenever `HA_ERR_FOUND_DUPP_KEY` is returned.
MariaDB can then locate the duplicate row through MyLite's existing `rnd_pos()`
path for `REPLACE` and `INSERT ... ON DUPLICATE KEY UPDATE`.

The storage smoke now verifies:

- `INSERT IGNORE` skips duplicate rows and keeps valid rows,
- `REPLACE` by primary key,
- `REPLACE` by secondary unique key on an autoincrement table,
- ODKU by primary key,
- ODKU by secondary unique key,
- secondary-index lookup after conflict DML,
- persistence write/read plus fresh-process append after reopen.

Verification passed:

```sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

Observed reports after implementation:

- `mylite-storage-engine-report.txt`:
  `conflict_dml_rows=2:beta:beta-updated,3:gamma:gamma-updated,4:alpha:alpha-replaced`
  and `conflict_dml_lookup_id=4`.
- `mylite-catalog-write-report.txt`:
  `persisted_conflict_dml_rows=2:beta:beta-updated,3:gamma:gamma-updated,4:alpha:alpha-replaced`
  and `persisted_conflict_dml_lookup_id=4`.
- `mylite-catalog-read-report.txt`:
  `persisted_conflict_dml_rows=2:beta:beta-updated,3:gamma:gamma-read-replaced,4:alpha:alpha-read,5:epsilon:five`
  and `persisted_conflict_dml_lookup_id=4`.

Observed artifacts after this slice:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 43,505,900 bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,393,976
  bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 93,752 bytes.
- `build/mariadb-minsize/mylite/mylite-open-close-smoke`: 22,404,904 bytes.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,262,024
  bytes.
