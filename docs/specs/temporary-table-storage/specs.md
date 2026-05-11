# temporary-table-storage

## Problem Statement

MyLite currently rejects `CREATE TEMPORARY TABLE ... ENGINE=MYLITE` by
advertising `HTON_TEMPORARY_NOT_SUPPORTED`. That kept the durable catalog safe
while the first row and index storage paths were still incomplete, but it now
blocks common application SQL. WordPress and plugins frequently use temporary
tables for intermediate result sets, and those tables must work without
becoming durable MyLite catalog objects.

This slice replaces the old rejection boundary with session-scoped temporary
MyLite table storage for the same row, key, generated-column, CHECK, BLOB/TEXT,
FULLTEXT metadata, and spatial metadata subset already supported for ordinary
MyLite tables.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:mylite_init_func()`
  currently sets `HTON_TEMPORARY_NOT_SUPPORTED` in the MyLite handlerton.
- `vendor/mariadb/server/sql/handler.h:506`,
  `vendor/mariadb/server/sql/handler.h:516`, and
  `vendor/mariadb/server/sql/handler.h:2316` define the temporary create
  flags and `HA_CREATE_INFO::tmp_table()`.
- `vendor/mariadb/server/sql/sql_table.cc:create_table_impl()` disables DDL
  logging for temporary creates, checks the session temporary-table list for
  existing names, and routes successful user temporary tables through
  `THD::create_and_open_tmp_table()`.
- `vendor/mariadb/server/sql/sql_table.cc:13414` rejects engines marked with
  `HTON_TEMPORARY_NOT_SUPPORTED`.
- `vendor/mariadb/server/sql/temporary_tables.cc:60` opens successful
  temporary table definitions through `THD::create_and_open_tmp_table()`.
- `vendor/mariadb/server/sql/temporary_tables.cc:444` and nearby helpers find
  session temporary tables before ordinary durable tables.
- `vendor/mariadb/server/sql/temporary_tables.cc:629` drops all open handler
  instances for a temporary table, then calls `free_tmp_table_share()`.
- `vendor/mariadb/server/sql/temporary_tables.cc:709` removes temporary table
  engine state through the handlerton `drop_table` callback.
- `vendor/mariadb/server/sql/table.cc:444` initializes temporary
  `TABLE_SHARE` objects with `tmp_table=INTERNAL_TMP_TABLE` before the handler
  `open()` call; `temporary_tables.cc:1171` later classifies user temporary
  tables as transactional or non-transactional based on the handler.
- `vendor/mariadb/server/sql/handler.cc:567` creates a handler without a
  table share for handlerton temporary cleanup and calls `delete_table(path)`.

## Scope

This slice will:

- allow user-created `CREATE TEMPORARY TABLE ... ENGINE=MYLITE`,
- keep temporary table definitions, rows, indexes, and autoincrement state
  in memory for the owning session,
- route temporary handler reads, writes, deletes, truncates, index reads,
  fulltext scans, and autoincrement through the temporary definition,
- allow temporary tables to shadow durable tables with the same schema/name
  without mutating the durable table,
- remove temporary definitions on `DROP TEMPORARY TABLE`, connection cleanup,
  or temporary create failure cleanup,
- keep temporary definitions out of MyLite durable table discovery,
  information-schema/schema listing hooks, catalog serialization, row page
  writes, index page writes, free-page accounting, and fresh-process reopen.

## Non-Goals

- Do not add a temporary spill-file format.
- Do not make internal optimizer temporary tables use MyLite by default.
- Do not add cross-session temporary table visibility.
- Do not persist temporary tables in the primary `.mylite` file or any MyLite
  companion file.
- Do not add foreign-key constraints to temporary MyLite tables in this slice.
  MariaDB's ordinary application temporary table usage rarely depends on FK
  metadata, and mixing session-scoped tables with durable referenced tables
  needs a separate compatibility decision.

## Proposed Design

Remove `HTON_TEMPORARY_NOT_SUPPORTED` from the MyLite handlerton so MariaDB's
normal temporary table pipeline can reach the engine.

Extend `Mylite_table_definition` with temporary metadata:

- `temporary`: this definition is session-scoped,
- `temporary_owner`: owning `THD` for ordinary user temporary tables,
- `temporary_path`: handler path used by MariaDB cleanup.

Store temporary definitions in the same in-memory `mylite_catalog` vector as
durable definitions so existing row, index, generated-column, fulltext, and
spatial metadata code can be reused. Add lookup helpers that select a
temporary definition only when the handler `TABLE` is temporary; all durable
discovery and catalog namespace helpers continue to use durable definitions
only.

`ha_mylite::create()` will detect `create_info->tmp_table()`, build a
temporary definition from the MariaDB frm image, reject temporary foreign-key
metadata for now, and avoid `mylite_flush_catalog_locked()`. Temporary DML will
still use the existing statement and transaction snapshot machinery, but the
durable flush helper will skip writing temporary definitions and their row or
index payloads.

`ha_mylite::delete_table()` will remove an owning-session temporary definition
first. That matches MariaDB's temporary cleanup path, where handlerton cleanup
creates a handler with no `TABLE_SHARE` and calls `delete_table(path)`.
Ordinary durable drop continues to remove durable definitions when no matching
temporary definition exists.

Temporary definitions must be skipped by:

- table discovery and `SHOW TABLES` MyLite discovery callbacks,
- schema table enumeration callbacks,
- `DROP DATABASE` durable catalog removal,
- catalog payload serialization,
- row and index payload page writers,
- free-page range collection and validation.

Foreign-key parent/child checks will ignore temporary definitions. The create
path rejects temporary FK metadata, and durable FK checks should not treat a
temporary table with a shadowing name as a durable parent.

## Affected Subsystems

- MyLite storage-engine handlerton flags.
- MyLite handler create/delete/read/write/index/autoincrement lookup routing.
- Catalog serialization, page writes, and free-page accounting.
- Temporary table lifecycle tests in the storage-engine smoke.
- Roadmap and single-file storage documentation.

## DDL Metadata Routing Impact

Temporary table frm images are provided by MariaDB's normal temporary-table
create path and must stay in memory only. They must not appear in MyLite table
discovery, the durable `.mylite` catalog payload, row/index page roots, or
fresh-process recovery state.

## Single-File And Embedded-Lifecycle Implications

This slice adds no durable file-format records and no companion files. The
primary `.mylite` file remains unchanged by temporary table DDL and DML except
for unrelated durable statements in the same session. A fresh embedded process
must not discover temporary definitions created by a prior process.

## Public API Or File-Format Impact

No public `libmylite` API change and no file-format version bump. Existing SQL
entry points gain support for the standard temporary-table surface when the
table uses `ENGINE=MYLITE`.

## Binary-Size Impact

Expected code-size impact is small and limited to MyLite storage-engine
helpers and smoke coverage. Post-implementation `MinSizeRel` artifact sizes
will be recorded if the build report changes meaningfully.

## License, Trademark, And Dependency Impact

No new dependency, license, or trademark impact.

## Test And Verification Plan

Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc` to cover:

- creating, inserting, selecting, updating, deleting, and dropping a temporary
  MyLite table,
- primary and secondary index reads on temporary rows,
- autoincrement state for a temporary table,
- `TRUNCATE` on temporary rows,
- temporary table shadowing of a durable table with the same schema/name,
- durable table rows remaining unchanged after temporary DDL/DML/drop,
- temporary tables absent from MyLite durable discovery after drop and across
  fresh-process reopen.

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

- `CREATE TEMPORARY TABLE ... ENGINE=MYLITE` succeeds for the supported row and
  key subset.
- Temporary MyLite rows and indexes work through common `SELECT`, `INSERT`,
  `UPDATE`, `DELETE`, `TRUNCATE`, and `DROP TEMPORARY TABLE` SQL.
- A temporary MyLite table can shadow a durable MyLite table without mutating
  the durable definition or rows.
- Temporary definitions never persist to the primary `.mylite` file and are
  absent after fresh-process reopen.
- Existing durable DDL, DML, indexing, constraints, generated-column, fulltext,
  spatial, transaction, public API, and sidecar coverage keeps passing.

## Risks And Unresolved Questions

- Temporary table DML transaction behavior should be compared against MariaDB
  reference engines in a later compatibility-matrix slice. This slice reuses
  MyLite's current statement and transaction snapshots, which gives rollback
  behavior for supported temporary DML without adding a new recovery format.
- Temporary tables under public read-only opens can be allowed later because
  they do not mutate the primary file, but the current read-only guard is
  conservative and applies before storage mutation routing.
- Temporary foreign-key definitions need a separate design for session-scoped
  referenced tables, durable referenced tables, metadata exposure, and cleanup.

## Implementation Result

Implemented in:

- `vendor/mariadb/server/storage/mylite/ha_mylite.cc`
- `vendor/mariadb/server/mylite/storage_engine_smoke.cc`

MyLite no longer advertises `HTON_TEMPORARY_NOT_SUPPORTED`. User temporary
table definitions are stored in memory with an owning `THD`, reuse the existing
row/index/autoincrement structures, and are skipped by durable discovery,
catalog serialization, row and index payload writers, and free-page range
collection. Handler instances opened against those definitions are tracked
explicitly so MariaDB internal copy-ALTER temporary handlers continue to use
the durable replacement-table path.

The storage smoke now verifies temporary table create, insert, indexed lookup,
ordered index scan, update, delete, autoincrement continuation, truncate, drop,
durable table-name shadowing, and fresh-process absence.

Verification on 2026-05-13:

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

Observed report fields:

- `build/mariadb-minsize/mylite-storage-engine-report.txt`:
  `status=0`, `temporary_rows=1:beta,2:alpha,3:gamma`,
  `temporary_key_lookup_id=2`, `temporary_key_order_ids=2,1,3`,
  `temporary_autoincrement_ids=1,2,7,8`,
  `temporary_truncate_count=0`, and
  `temporary_durable_rows=1:durable`.
- `build/mariadb-minsize/mylite-compatibility-harness-report.txt`:
  all groups reported `status=0`.

Post-implementation `MinSizeRel` artifact observations:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 43,505,684 bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,394,096
  bytes.
