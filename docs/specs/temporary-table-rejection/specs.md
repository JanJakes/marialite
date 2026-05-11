# temporary-table-rejection

Historical rejection boundary. This slice was superseded by
`temporary-table-storage`, which supports user-created temporary MyLite tables
through session-scoped in-memory definitions that are excluded from durable
catalog and payload publication.

## Problem Statement

MyLite's durable catalog stores table definitions and rows in the primary
`.mylite` file. MariaDB temporary tables have different lifetime, discovery,
cleanup, and storage semantics. MyLite already marks its handlerton as not
supporting temporary tables, but the smoke does not prove
`CREATE TEMPORARY TABLE ... ENGINE=MYLITE` fails before creating a durable
catalog entry.

This slice adds explicit coverage for temporary-table rejection.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:603` sets
  `HTON_TEMPORARY_NOT_SUPPORTED` on the MyLite handlerton.
- `vendor/mariadb/server/sql/handler.h:1775` defines
  `HTON_TEMPORARY_NOT_SUPPORTED`.
- `vendor/mariadb/server/sql/sql_table.cc:13394` rejects temporary table
  creation when the resolved engine has `HTON_TEMPORARY_NOT_SUPPORTED`.
- `vendor/mariadb/server/sql/handler.cc:5994` tags handler create calls with
  `HA_LEX_CREATE_TMP_TABLE`, and `handler.cc:6504` avoids writing frm files for
  temporary table creation.
- `vendor/mariadb/server/sql/sql_table.cc:4947` routes successful temporary
  table creation through `THD::create_and_open_tmp_table()`.

## Scope

This slice will:

- prove `CREATE TEMPORARY TABLE ... ENGINE=MYLITE` is rejected,
- prove the failed temporary table is absent from MyLite discovery,
- prove a normal durable MyLite table remains usable after the failed
  temporary-table attempt,
- document temporary tables as unsupported until MyLite owns a temporary
  storage lifecycle.

## Non-Goals

- Do not implement MyLite temporary tables.
- Do not design temporary spill, in-memory, or no-companion-file modes.
- Do not change MariaDB internal temporary-table engines.
- Do not change regular durable MyLite table DDL.

## Proposed Design

No storage-engine code change is expected. MyLite already sets
`HTON_TEMPORARY_NOT_SUPPORTED`; add storage smoke coverage that creates a
normal table, attempts a temporary MyLite table, verifies rejection, verifies
absence through `SHOW TABLES`, and verifies the normal table remains readable.

If the test shows MyLite reaches `ha_mylite::create()` and persists a temporary
definition, add an explicit `create_info->tmp_table()` guard in
`ha_mylite::create()`.

## Affected Subsystems

- Storage engine smoke same-process DDL coverage.
- Roadmap and single-file storage documentation.

## DDL Metadata Routing Impact

Rejected temporary MyLite table DDL must not store a table-definition image in
the `.mylite` catalog. Durable table discovery must not expose the rejected
temporary name.

## Single-File And Embedded-Lifecycle Implications

No file-format change. This slice prevents accidental confusion between
durable MyLite catalog tables and session-scoped temporary tables whose storage
and cleanup lifecycle has not been designed.

## Public API Or File-Format Impact

No public `libmylite` API change and no file-format version bump.

## Binary-Size Impact

Expected size impact is zero apart from smoke-test code. Post-implementation
`MinSizeRel` artifact sizes will be recorded.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc`:
  - create a normal MyLite table and insert a row,
  - reject `CREATE TEMPORARY TABLE ... ENGINE=MYLITE`,
  - verify the temporary table name is absent from `SHOW TABLES`,
  - verify the normal table remains readable,
  - drop the normal table.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `bash -n tools/run-storage-engine-smoke.sh
    tools/run-compatibility-test-harness.sh`
  - `git diff --check`

## Acceptance Criteria

- Temporary MyLite table creation fails.
- The rejected temporary table name is not discoverable through MyLite table
  discovery.
- A durable MyLite table remains readable after the failed temporary-table
  attempt.
- Existing DDL, DML, copy ALTER, transaction, unsupported-index, and public API
  coverage keeps passing.

## Risks And Unresolved Questions

- Future support needs a temporary storage policy: in-memory only, primary-file
  scoped, companion spill files, cleanup guarantees, and interaction with
  read-only opens.

## Implementation Result

Implemented in `vendor/mariadb/server/mylite/storage_engine_smoke.cc`.

The storage smoke now creates a durable `mylite.temporary_base` table, inserts
one row, verifies `CREATE TEMPORARY TABLE ... ENGINE=MYLITE` is rejected,
verifies `unsupported_temporary` is absent from `SHOW TABLES`, verifies the
durable base row still reads as `1:durable`, and drops the base table.

Verification on 2026-05-11:

- `git diff --check`
- `bash -n tools/run-storage-engine-smoke.sh
  tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`

Observed report fields:

- `build/mariadb-minsize/mylite-storage-engine-report.txt`:
  `status=0`, `unsupported_temporary_table=rejected`,
  `temporary_base_rows=1:durable`.
- `build/mariadb-minsize/mylite-compatibility-harness-report.txt`:
  all groups reported `status=0`.

Post-implementation `MinSizeRel` artifact observations:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,413,682 bytes,
  571 objects.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,839,352
  bytes.
