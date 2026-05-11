# Routine information schema

## Problem

MyLite now persists standalone stored procedures and functions in the primary
catalog, but MariaDB's `information_schema.ROUTINES` and
`information_schema.PARAMETERS` population still scans the inherited
`mysql.proc` table. Embedded MyLite runtimes must expose routine metadata
through the standard application SQL metadata surface without creating
durable `mysql.proc` sidecars.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/sql_show.cc` registers
  `information_schema.ROUTINES` and `information_schema.PARAMETERS` with
  `fill_schema_proc()`.
- `fill_schema_proc()` opens `mysql.proc` through
  `open_proc_table_for_read()`, optionally seeks by the db/name lookup fields,
  then calls `store_schema_proc()` or `store_schema_params()` for each row.
- `store_schema_proc()` fills the `ROUTINES` row from `mysql.proc` columns and,
  for functions, calls `Sp_handler::sp_load_for_information_schema()` to
  compile an empty-body definition for return type metadata.
- `store_schema_params()` fills the function return row and formal parameter
  rows for `PARAMETERS` by compiling an empty-body definition and walking the
  resulting `sp_pcontext`.
- `vendor/mariadb/server/sql/sp.cc` now routes MyLite schema procedure and
  function create/load/alter/drop through `mylite_read_routine_definition()`,
  but the catalog bridge does not yet enumerate routine definitions.
- `vendor/mariadb/server/include/mylite_schema.h` exposes point routine
  operations only: existence, read, store, update, and remove.

## Scope

Support MyLite catalog-backed standalone procedures and functions in:

- `SELECT ... FROM information_schema.ROUTINES`;
- `SELECT ... FROM information_schema.PARAMETERS`;
- metadata filters on routine schema and routine/specific name;
- fresh-process reopen and recovery-read smoke phases;
- dropped-routine absence.

## Non-Goals

- Do not implement packages, package bodies, events, or event scheduler
  metadata in this slice.
- Do not add durable `mysql.proc`, `mysql.procs_priv`, or other system-table
  sidecars.
- Do not change non-embedded MariaDB behavior.
- Do not redesign routine privileges beyond preserving the existing embedded
  skip-grants behavior and inherited checks where they already apply.

## Design

Add a MyLite-owned routine enumeration API beside the existing point lookup API.
The SQL layer will use it only when the MyLite schema namespace is active.

1. Add `mylite_for_each_routine_definition()` to enumerate all catalog-backed
   standalone routine definitions. The callback receives the existing
   `mylite_routine_definition` view over a copied catalog snapshot so SQL-layer
   metadata compilation does not run under the catalog mutex.
2. In `sql_show.cc`, include the MyLite schema bridge and add MyLite-specific
   row stores that mirror `store_schema_proc()` and `store_schema_params()`
   without fabricating a `mysql.proc` `TABLE`.
3. Extend `Sp_handler` with a MyLite information-schema loader that accepts the
   stored charset/collation names and compiles the same empty-body definition
   used by the mysql.proc path.
4. In `fill_schema_proc()`, enumerate MyLite routine definitions after the
   inherited `mysql.proc` scan. Apply the same lookup filtering and routine
   type filtering before storing rows.
5. Preserve continued package/event rejection and keep all accepted metadata
   sourced from the primary `.mylite` catalog.

## Affected Subsystems

- `include/mylite_schema.h` and `storage/mylite/ha_mylite.cc` catalog bridge
  enumeration.
- `sql/sql_show.cc` information schema population for routine metadata.
- `sql/sp.h` and `sql/sp.cc` stored routine empty-body information-schema
  loader.
- MyLite storage smoke tests and compatibility harness.
- Persistent routine storage docs and roadmap.

## DDL Metadata Routing Impact

No new DDL routing is introduced. This slice reads routine metadata persisted by
the persistent-routine-storage slice and must not create or require
`mysql.proc`.

## Single-File and Embedded Lifecycle

Information schema reads must be pure catalog reads. Querying
`information_schema.ROUTINES` or `information_schema.PARAMETERS` after routine
create, reopen, and recovery must not create durable system-table, Aria,
InnoDB, MyISAM, binlog, or schema-directory sidecars.

## Public API and File Format Impact

No public `libmylite` API or file-format change is expected. The new
enumeration function is an internal MyLite/MariaDB bridge over already-stored
routine records.

## Binary Size Impact

Expected impact is small: one catalog enumeration helper, small
information-schema row shaping helpers, and focused smoke assertions. No new
dependencies are added.

## License and Dependency Impact

No new dependency or licensing change.

## Test Plan

- Extend the storage-engine smoke persistent routine phases to verify:
  - `information_schema.ROUTINES` lists persisted MyLite procedure/function
    rows with expected type, body, comment, definer, and creation context;
  - `information_schema.PARAMETERS` lists procedure parameters plus a function
    return row and function parameter row with expected types/modes;
  - dropped functions are absent from both metadata tables;
  - reopened and recovery-read runtimes see the same routine metadata;
  - no durable sidecars appear after metadata queries.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` on the smoke scripts
  - `git diff --check`

## Acceptance Criteria

- `information_schema.ROUTINES` returns catalog-backed MyLite procedure and
  function rows after create, reopen, and recovery.
- `information_schema.PARAMETERS` returns catalog-backed MyLite parameter and
  function return metadata after create, reopen, and recovery.
- Dropped routines are absent from both metadata tables.
- Metadata rows preserve MariaDB field semantics for supported standalone
  procedures/functions, including return type, parameter type, body text,
  comments, SQL mode, definer, and creation charset/collation names.
- Routine metadata reads do not create durable MariaDB system-table sidecars.

## Risks and Unresolved Questions

- The mysql.proc path uses `Field` conversion helpers for a few enum and time
  fields. The MyLite path must either reuse equivalent string constants or
  store directly in the destination fields with matching MariaDB semantics.
- Full privilege semantics for routine-level grants still depend on
  `mysql.procs_priv` and remain outside this embedded-auth profile.
- MyLite currently supports standalone procedures/functions only; package and
  event metadata remain deliberately unsupported until their storage and
  scheduler semantics are designed.

## Implementation Result

Implemented with a MyLite routine catalog enumeration API and a
`sql_show.cc` bridge that emits `information_schema.ROUTINES` and
`information_schema.PARAMETERS` rows from catalog-backed standalone procedure
and function definitions. MariaDB's empty-body stored routine information
schema compiler is reused through a MyLite creation-context loader, so return
and parameter type metadata stays aligned with inherited routine semantics.

The storage smoke now verifies routine metadata after initial write,
fresh-process reopen, and recovery read:

- `build/mariadb-minsize/mylite-catalog-write-report.txt`:
  - `status=0`
  - `persisted_routine_information_schema_count=1:1:0`
  - `persisted_routine_parameter_information_schema_count=1:1:1:1:0`
- `build/mariadb-minsize/mylite-catalog-read-report.txt`:
  - `status=0`
  - `persisted_routine_information_schema_count=1:1:0`
  - `persisted_routine_parameter_information_schema_count=1:1:1:1:0`
- `build/mariadb-minsize/mylite-catalog-recovery-read-report.txt`:
  - `status=0`
  - `persisted_routine_information_schema_count=1:1:0`
  - `persisted_routine_parameter_information_schema_count=1:1:1:1:0`
