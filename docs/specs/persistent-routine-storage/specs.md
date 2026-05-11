# Persistent routine storage

## Problem

MyLite supports catalog-backed tables, indexes, constraints, views, and
triggers, but embedded builds still reject stored procedure and stored function
DDL. Stored routines are part of common MySQL/MariaDB application SQL and
MariaDB already owns their parser, compiler, cache, execution engine,
diagnostics, and `SHOW CREATE` rendering.

This slice moves standalone stored procedure and stored function persistence
from the `mysql.proc` system table into the MyLite catalog for MyLite schemas,
without introducing durable system-table sidecars.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/sql_parse.cc` dispatches
  `SQLCOM_CREATE_PROCEDURE`, `SQLCOM_CREATE_SPFUNCTION`,
  `SQLCOM_CREATE_PACKAGE`, and `SQLCOM_CREATE_PACKAGE_BODY` to
  `mysql_create_routine()`, but the current embedded MyLite branch rejects
  those commands before entering MariaDB's routine code.
- `sql_parse.cc` dispatches `SQLCOM_ALTER_PROCEDURE`,
  `SQLCOM_ALTER_FUNCTION`, `SQLCOM_DROP_PROCEDURE`, `SQLCOM_DROP_FUNCTION`,
  `SQLCOM_DROP_PACKAGE`, and `SQLCOM_DROP_PACKAGE_BODY` through
  `alter_routine()` and `drop_routine()`, and the current embedded branch
  rejects them.
- `vendor/mariadb/server/sql/sp.cc` defines the `mysql.proc` row shape in
  `proc_table_fields` and opens that system table through
  `open_proc_table_for_read()` and `open_proc_table_for_update()`.
- `Sp_handler::sp_create_routine()` writes procedure/function/package rows to
  `mysql.proc`, including db, name, type, params, returns, body, definer,
  timestamps, SQL mode, comment, creation character sets/collations, UTF-8
  body text, security, deterministic, data-access, and aggregate flags.
- `Sp_handler::db_find_routine()` reads `mysql.proc`, builds a
  `Stored_routine_creation_ctx`, then calls `db_load_routine()` to construct a
  `CREATE PROCEDURE`/`CREATE FUNCTION` statement and parse it through
  `sp_compile()`.
- `Sp_handler::sp_cache_routine()` and `Sp_handler::sp_find_routine()` keep
  MariaDB's per-session stored-program cache, using `db_find_and_cache_routine`
  only when a routine is absent or stale.
- `Sp_handler::sp_drop_routine()` and `Sp_handler::sp_update_routine()` delete
  or update `mysql.proc` rows and invalidate the stored-program cache.
- `Sp_handler::sp_show_create_routine()` reloads a routine through
  `db_find_routine()` and delegates the final result set to
  `sp_head::show_create_routine()`.
- `vendor/mariadb/server/sql/sql_show.cc` originally populated
  `information_schema.ROUTINES` and `information_schema.PARAMETERS` by scanning
  `mysql.proc` through `open_proc_table_for_read()`. The
  `routine-information-schema` slice now adds a MyLite catalog enumeration
  bridge for standalone procedures and functions.

## Scope

Support sidecar-free standalone stored routines for MyLite schemas:

- `CREATE PROCEDURE` and `CREATE FUNCTION`;
- `CREATE OR REPLACE` and `IF NOT EXISTS` variants where MariaDB parses them;
- `ALTER PROCEDURE` and `ALTER FUNCTION` for supported characteristic changes;
- `DROP PROCEDURE`, `DROP FUNCTION`, and `IF EXISTS` variants;
- `CALL` execution for stored procedures after create, reopen, and recovery;
- stored function invocation from ordinary SQL after create, reopen, and
  recovery;
- `SHOW CREATE PROCEDURE` and `SHOW CREATE FUNCTION`;
- explicit continued rejection for packages and package bodies in embedded
  MyLite builds.

## Non-Goals

- Do not implement packages or package bodies in this slice.
- Do not implement events or event scheduling in this slice.
- Do not introduce durable `mysql.proc`, `mysql.procs_priv`, or other
  MariaDB system-table sidecars.
- Do not change non-embedded MariaDB behavior.
- Do not redesign stored-routine privileges. The current embedded profile uses
  skip-grant-table test/runtime behavior, and routine-level grant persistence
  remains a separate server-auth surface.
- Do not normalize routine bodies into a new AST format; store MariaDB routine
  metadata needed to reconstruct and compile the canonical routine definition.

## Design

Keep MariaDB's stored-routine parser, body compiler, execution engine,
stored-program cache, recursion checks, `CALL`, function invocation, and
`SHOW CREATE` behavior. Replace only the persistence bridge for standalone
procedures and functions in MyLite schemas.

1. Extend the MyLite catalog with standalone routine records keyed by schema,
   routine type, and routine name. Store the fields MariaDB currently persists
   in `mysql.proc` for procedures and functions.
2. Add MyLite bridge helpers to check existence, read, store, update, remove,
   and enumerate catalog-backed routine definitions.
3. Patch `Sp_handler::sp_create_routine()` so standalone procedures/functions
   in MyLite schemas store into the MyLite catalog instead of opening
   `mysql.proc`. Preserve MariaDB's existing duplicate, `OR REPLACE`, and
   `IF NOT EXISTS` behavior.
4. Patch `Sp_handler::db_find_routine()` so routine cache misses for MyLite
   schemas load catalog records, construct a `Stored_routine_creation_ctx` from
   stored charset/collation names, and call `db_load_routine()`.
5. Patch `Sp_handler::sp_drop_routine()` and `Sp_handler::sp_update_routine()`
   so MyLite schema procedures/functions mutate catalog records and invalidate
   the stored-program cache.
6. Allow embedded routine DDL only for MyLite schema procedures/functions.
   Packages, package bodies, routines outside MyLite schemas, and events remain
   explicitly rejected.
7. Add information-schema enumeration after the execution slice is stable. The
   follow-up `routine-information-schema` slice now owns that bridge by
   enumerating MyLite routine catalog records without fabricating `mysql.proc`
   rows.

## Affected Subsystems

- `sql/sql_parse.cc` embedded routine command dispatch.
- `sql/sp.cc` stored-routine create, load, cache, update, drop, and creation
  context handling.
- `include/mylite_schema.h` and `storage/mylite/ha_mylite.cc` catalog bridge
  helpers and catalog serialization.
- MyLite storage-engine smoke and compatibility harness.
- Embedded bootstrap rejection smoke for packages/events/non-MyLite routines.
- Single-file storage docs, schema-object rejection docs, and roadmap.

## DDL Metadata Routing Impact

Procedure and function DDL becomes MyLite catalog DDL for MyLite schemas.
Routine definitions are schema-level metadata records, not table-local records.
They must be removed on `DROP DATABASE` with the owning schema and must survive
fresh-process reopen and recovery read without `mysql.proc`.

## Single-File and Embedded Lifecycle

Accepted routine definitions must live only in the primary `.mylite` file.
Runtime directories must remain free of durable `mysql.proc`, `mysql.*` system
table files, Aria logs, InnoDB files, and other inherited sidecars after
create, alter, drop, reopen, and recovery. Failed catalog writes must not
publish partial routine metadata.

## Public API and File Format Impact

No public C API change is expected. The catalog payload gains routine records
under the current catalog format family; old files without routine records
remain readable.

## Binary Size Impact

Expected binary-size impact is modest and limited to catalog bridge helpers,
small SQL-layer routing, and smoke coverage. The slice does not add new
dependencies or execution engines.

## License and Dependency Impact

No new dependency or licensing change.

## Test Plan

- Extend the storage-engine smoke persistence phases with:
  - create a MyLite table used by routines,
  - create a procedure that mutates that table and can be called with `CALL`,
  - create a function used from ordinary `SELECT`,
  - verify `CREATE PROCEDURE/FUNCTION IF NOT EXISTS` and `CREATE OR REPLACE`,
  - verify `ALTER PROCEDURE`/`ALTER FUNCTION` characteristic persistence,
  - verify `SHOW CREATE PROCEDURE` and `SHOW CREATE FUNCTION`,
  - drop one routine and verify it is not callable,
  - reopen and recovery-read the final routine state,
  - verify no durable `mysql.proc` or system-table sidecars appear.
- Keep embedded bootstrap rejection coverage for packages, package bodies,
  events, and non-MyLite routine DDL.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` on the smoke scripts
  - `git diff --check`

## Acceptance Criteria

- MyLite embedded runtimes can create, replace, alter, drop, reopen, and
  execute persistent procedures and functions in MyLite schemas.
- `CALL`, stored function invocation, and `SHOW CREATE` use inherited MariaDB
  semantics after loading definitions from the MyLite catalog.
- Fresh-process read and recovery read execute persisted routines and omit
  dropped routines.
- Runtime sidecar scans find no durable `mysql.proc`, routine, Aria, InnoDB,
  MyISAM, binlog, or schema-directory sidecars.
- Packages, package bodies, events, and non-MyLite routine DDL remain
  explicitly unsupported until separate catalog designs exist.

## Risks and Unresolved Questions

- `information_schema.ROUTINES` and `information_schema.PARAMETERS` are now
  covered by the follow-up `routine-information-schema` bridge for standalone
  MyLite procedures and functions.
- Routine-level grants use `mysql.procs_priv` and are outside the current
  embedded-auth profile. Supporting persistent grants would require a separate
  server-auth design.
- Stored functions can be used inside views, triggers, generated expressions,
  and other routines. The first implementation should test direct SQL and at
  least one persisted trigger/function interaction if time permits.
- Packages share `mysql.proc` storage but have package-specific compile and
  dependency behavior. They should remain rejected until a package-specific
  slice accounts for package spec/body coupling.
- Events also use stored-program creation context but persist through
  `mysql.event` and scheduler code, so they remain a separate slice.

## Implementation Result

Implemented with schema-level MyLite catalog records for standalone procedures
and functions. `Sp_handler` create, load, alter, drop, cache invalidation, and
`SHOW CREATE` paths route MyLite-schema procedures/functions through the
catalog while preserving MariaDB's parser, compiler, execution engine, and
diagnostics. Embedded command dispatch now allows procedure/function DDL only
for MyLite schemas; packages, package bodies, events, and routines outside
MyLite schemas remain rejected.

The storage smoke now verifies:

- `CREATE PROCEDURE`, `CREATE PROCEDURE IF NOT EXISTS`, `CALL`;
- `CREATE FUNCTION`, `CREATE OR REPLACE FUNCTION`, stored function invocation;
- `ALTER PROCEDURE`, `ALTER FUNCTION`, `SHOW CREATE PROCEDURE`, and
  `SHOW CREATE FUNCTION`;
- dropped function rejection;
- fresh-process read and recovery read execution;
- rollback of a post-reopen procedure call.

Report evidence from `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`:

- `build/mariadb-minsize/mylite-catalog-write-report.txt`:
  - `status=0`
  - `persisted_routine_rows=1:alpha:proc,2:beta:proc`
  - `persisted_routine_function_value=theta:fn2`
  - `persisted_routine_show_create=present`
  - `persisted_routine_information_schema_count=1:1:0`
  - `persisted_routine_parameter_information_schema_count=1:1:1:1:0`
  - `persisted_routine_dropped_function=rejected`
- `build/mariadb-minsize/mylite-catalog-read-report.txt`:
  - `status=0`
  - `persisted_routine_information_schema_count=1:1:0`
  - `persisted_routine_parameter_information_schema_count=1:1:1:1:0`
  - `persisted_routine_reopen_call=called`
- `build/mariadb-minsize/mylite-catalog-recovery-read-report.txt`:
  - `status=0`
  - `persisted_routine_information_schema_count=1:1:0`
  - `persisted_routine_parameter_information_schema_count=1:1:1:1:0`
  - `persisted_routine_reopen_call=called`
