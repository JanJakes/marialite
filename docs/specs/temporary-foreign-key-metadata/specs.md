# Temporary foreign-key metadata

## Problem

MyLite now supports durable foreign keys and session temporary MyLite tables,
but those features still do not compose. `CREATE TEMPORARY TABLE ... ENGINE=MYLITE`
with a `FOREIGN KEY` clause is rejected by the MyLite handler even though the
temporary table definition, rows, and indexes already live only in memory and
are excluded from primary-file serialization.

Applications and test suites that use temporary tables as constrained staging
tables should get the same MyLite SQL surface as durable tables where the
storage lifetime allows it.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- MariaDB passes temporary table creation to `handler::ha_create()` with
  `HA_LEX_CREATE_TMP_TABLE`; the storage engine owns whether a temporary
  table definition with FK metadata is valid.
- MyLite currently rejects temporary FK DDL in
  `mylite_store_table_definition()` by returning `HA_ERR_UNSUPPORTED` when
  `mylite_create_info_has_foreign_keys(create_info)` is true for a temporary
  table.
- FK extraction already supports table-local self references and references to
  existing MyLite table definitions, but it only searches durable definitions.
- FK child checks, parent restrict checks, parent action application,
  parent-FK metadata enumeration, and `referenced_by_foreign_key()` currently
  skip temporary definitions.
- Temporary MyLite definitions are already tagged with owner THD state, which
  gives a natural boundary for session-local FK references and parent actions.

## Scope

Support session-local temporary MyLite foreign keys:

- temporary child references a temporary parent in the same session;
- FK metadata appears in `SHOW CREATE TABLE` for the temporary child;
- missing-parent inserts are rejected while `foreign_key_checks` is enabled;
- `ON UPDATE CASCADE` and `ON DELETE CASCADE` from a temporary parent mutate
  the temporary child;
- temporary FK metadata and rows remain absent from durable catalog
  serialization and fresh-process reads.

## Non-Goals

- Do not persist temporary FK metadata.
- Do not make temporary FKs visible to other sessions.
- Do not support durable child tables referencing temporary parent tables.
- Do not implement `SET DEFAULT`, which remains unsupported for both durable
  and temporary FKs.
- Do not add temporary spill files.

## Design

1. Remove the temporary-table FK DDL rejection in
   `mylite_store_table_definition()`.
2. Extract FK definitions for both durable and temporary tables. When the
   child table is temporary, FK resolution should search owner-session
   temporary definitions first and then durable definitions.
3. Child FK checks should run for temporary child definitions using the same
   key-image prefix logic as durable child definitions.
4. Parent restrict and action scans should include owner-session temporary
   children. Temporary parent tables should only consider owner-session
   temporary children, not durable children with the same SQL name.
5. FK action preopen lookup should be able to find an owner-session temporary
   child handler as well as durable child handlers.
6. Parent-FK metadata enumeration and `referenced_by_foreign_key()` should use
   the same owner-session visibility rules so MariaDB preopens child tables
   for cascades and restrict checks.

## Affected Subsystems

- MyLite temporary table DDL.
- MyLite FK definition extraction.
- MyLite FK child and parent enforcement.
- MyLite FK metadata hooks used by MariaDB table-opening paths.
- Storage engine smoke coverage and single-file storage docs.

## Single-File and Embedded Lifecycle

Temporary FK definitions must remain in memory only. The storage engine smoke
already proves temporary tables are absent after drop and fresh process
reopen; this slice extends the temporary table section without adding durable
catalog records or sidecars.

## Public API and File Format Impact

No public API or durable file-format change. The change affects SQL executed
inside one MyLite runtime session.

## Binary Size Impact

Expected impact is small: helper lookup branches and smoke coverage only.

## License and Dependency Impact

No new dependency or licensing change.

## Test Plan

- Extend `run-storage-engine-smoke.sh` to create temporary parent and child
  MyLite tables with a named FK.
- Verify `SHOW CREATE TABLE` exposes the temporary FK.
- Verify a missing-parent child insert is rejected.
- Verify parent primary-key update cascades into the temporary child.
- Verify parent delete cascades the matching temporary child row.
- Verify temporary tables still drop cleanly and do not affect durable
  temporary-table shadowing coverage.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` on the smoke scripts
  - `git diff --check`

## Acceptance Criteria

- Temporary MyLite FK DDL succeeds when referenced temporary tables are visible
  in the same session.
- Temporary FK checks reject invalid child rows.
- Temporary parent cascades mutate only owner-session temporary child rows.
- Temporary FK metadata remains non-durable.
- Existing durable FK, temporary-table, read-only, and sidecar smokes remain
  green.

## Implementation Result

Implemented in `vendor/mariadb/server/storage/mylite/ha_mylite.cc` and
covered by `vendor/mariadb/server/mylite/storage_engine_smoke.cc`.

- Temporary MyLite table definitions now extract FK metadata instead of
  rejecting it.
- Temporary child tables resolve referenced tables against same-session
  temporary definitions first, then durable definitions.
- Transient FK metadata records whether the referenced table was temporary, so
  later temporary-name shadowing cannot retarget an existing FK.
- Child checks, parent restrict/action scans, parent metadata enumeration, and
  `referenced_by_foreign_key()` include owner-session temporary definitions.
- Cascade preopen lookup can find temporary child handlers.
- The storage smoke records
  `temporary_foreign_key_show_create=present`,
  `temporary_foreign_key_missing_parent=rejected`,
  `temporary_foreign_key_update_rows=10:11,20:2`, and
  `temporary_foreign_key_delete_rows=10:11`.

Verification:

- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
- `git diff --check`

## Risks and Unresolved Questions

- Drop ordering for temporary parent tables with referencing temporary child
  tables remains inherited from MariaDB's temporary-table lifecycle. This slice
  focuses on DML enforcement and metadata hooks.
- Cross-session visibility is intentionally absent. A durable parent can only
  see temporary children owned by the current THD.
