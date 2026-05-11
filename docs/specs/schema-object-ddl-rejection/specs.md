# schema-object-ddl-rejection

## Problem Statement

MyLite stores user table definitions and row/index payloads in the primary
`.mylite` catalog, but MariaDB still has persistent schema-object DDL paths
for views, triggers, stored routines, packages, and events. Those paths do not
route through the MyLite storage engine and currently depend on `.frm`,
`.TRG`/`.TRN`, or `mysql.*` metadata storage that MyLite has not designed.

This slice makes those persistent schema-object DDL surfaces fail explicitly
in embedded MyLite builds before they can create sidecar files or mutate
server system tables.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/sql_parse.cc:5180` dispatches
  `SQLCOM_CREATE_EVENT` and `SQLCOM_ALTER_EVENT` to `Events::create_event()`
  and `Events::update_event()`.
- `vendor/mariadb/server/sql/sql_parse.cc:5658` dispatches stored procedure,
  stored function, package, and package-body creation to
  `mysql_create_routine()`.
- `vendor/mariadb/server/sql/sql_parse.cc:5682` dispatches stored routine
  ALTER and DROP commands to `alter_routine()` and `drop_routine()`.
- `vendor/mariadb/server/sql/sql_parse.cc:5720` dispatches CREATE/ALTER VIEW
  through `mysql_create_view()`, and `sql_parse.cc:5729` dispatches DROP VIEW
  through `mysql_drop_view()`.
- `vendor/mariadb/server/sql/sql_parse.cc:5743` dispatches CREATE TRIGGER and
  DROP TRIGGER through `mysql_create_or_drop_trigger()`.
- `vendor/mariadb/server/sql/sql_view.cc:956` documents
  `mysql_register_view()` as writing a view `.frm` file and processing `.frm`
  backups.
- `vendor/mariadb/server/sql/sql_trigger.cc:178` defines trigger `.TRG`
  files, `sql_trigger.cc:257` defines `.TRN` trigger-name files, and
  `sql_trigger.cc:1136` creates the trigger definition file.
- `vendor/mariadb/server/sql/sp.h:129` documents stored routine definitions
  in the `mysql.proc` system table, and `sp.cc:1248` writes stored-routine
  objects into `mysql.proc`.
- `vendor/mariadb/server/sql/events.cc:74` describes event creation as adding
  a row to `mysql.event`; `event_db_repository.cc:651` implements
  `Event_db_repository::create_event()`.

## Scope

This slice will:

- reject CREATE/ALTER/DROP VIEW in embedded MyLite builds,
- reject CREATE/DROP TRIGGER in embedded MyLite builds,
- reject CREATE/ALTER/DROP stored procedure, stored function, package, and
  package body in embedded MyLite builds,
- reject CREATE/ALTER/DROP EVENT in embedded MyLite builds,
- extend the embedded bootstrap smoke to assert explicit embedded diagnostics
  for representative statements,
- document these schema-object surfaces as unsupported until MyLite owns their
  metadata.

## Non-Goals

- Do not implement MyLite catalog storage for views, triggers, routines,
  packages, or events.
- Do not normalize these object definitions into a MyLite-native schema.
- Do not remove MariaDB parser support.
- Do not change non-embedded MariaDB behavior.
- Do not decide final compatibility for executable stored routines, trigger
  execution, event scheduling, or view expansion.
- Do not reject read-only SHOW commands as part of this slice.

## Proposed Design

Use the same embedded diagnostic already used for unsupported plugin, UDF, and
foreign-server statements:

```c++
my_error(ER_OPTION_PREVENTS_STATEMENT, MYF(0), "embedded");
```

Patch `mysql_execute_command()` under `#ifdef EMBEDDED_LIBRARY` so the targeted
schema-object DDL cases return before entering the persistent metadata helpers.
The rejection belongs in SQL command dispatch, not in the MyLite storage
engine, because these object types bypass handler `create()` and do not have a
MyLite table-definition image to store today.

Extend `vendor/mariadb/server/mylite/bootstrap_smoke.cc` with representative
unsupported statements. The existing smoke already records labels, errno,
SQLSTATE, message, and statement text; this slice adds schema-object labels
and expects MariaDB error `ER_OPTION_PREVENTS_STATEMENT`.

## Affected Subsystems

- `sql/sql_parse.cc` command dispatch for embedded builds.
- MyLite embedded bootstrap smoke report.
- Roadmap and single-file storage documentation.

No storage-engine row format, catalog file format, public `libmylite` API, or
non-embedded MariaDB behavior changes.

## DDL Metadata Routing Impact

This slice explicitly separates MyLite table DDL from unsupported persistent
schema-object DDL. User table `CREATE`, `ALTER`, `DROP`, and `RENAME` remain
handled by the MyLite storage-engine/catalog path. Views, triggers, routines,
packages, and events are rejected until their metadata can be stored in the
primary `.mylite` catalog without durable MariaDB sidecars or hidden
`mysql.*` system table writes.

## Single-File And Embedded-Lifecycle Implications

Rejecting these commands prevents accidental `.frm`, `.TRG`, `.TRN`,
`mysql.proc`, and `mysql.event` persistence in the embedded runtime. It also
keeps the lifecycle honest: a MyLite primary file cannot claim to own all
schema metadata while these object types still depend on MariaDB's server
datadir metadata formats.

## Public API Or File-Format Impact

No public API change and no file-format version bump.

## Binary-Size Impact

No meaningful binary-size change is expected. The same MariaDB code remains
compiled; embedded execution returns earlier.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/bootstrap_smoke.cc` with unsupported
  statements for:
  - `CREATE VIEW`
  - `ALTER VIEW`
  - `DROP VIEW`
  - `CREATE TRIGGER`
  - `DROP TRIGGER`
  - `CREATE PROCEDURE`
  - `ALTER PROCEDURE`
  - `DROP PROCEDURE`
  - `CREATE FUNCTION` as a stored function
  - `DROP FUNCTION`
  - `CREATE EVENT`
  - `ALTER EVENT`
  - `DROP EVENT`
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `bash -n tools/run-embedded-bootstrap-smoke.sh
    tools/run-compatibility-test-harness.sh`
  - `git diff --check`

## Acceptance Criteria

- Targeted schema-object DDL returns `ER_OPTION_PREVENTS_STATEMENT` in embedded
  builds.
- The embedded bootstrap smoke fails if any targeted statement succeeds or
  returns a different diagnostic.
- Table DDL and MyLite storage smoke coverage continue to pass.
- Docs and roadmap identify these object types as explicitly unsupported until
  MyLite-owned metadata exists.
- Non-embedded source paths remain unchanged except for preprocessor-guarded
  code.

## Risks And Unresolved Questions

- `DROP FUNCTION` also covers UDF deletion in MariaDB syntax. This slice
  intentionally rejects it in embedded builds because UDFs and stored
  functions are both unsupported MyLite surfaces today.
- SHOW commands for absent views, triggers, routines, and events may still
  report inherited MariaDB diagnostics. This slice only covers persistent DDL.
- Future support needs a schema-object catalog design, dependency tracking,
  crash recovery, invalidation behavior, and sidecar-free introspection.

## Implementation Result

Implemented in `mysql_execute_command()` for embedded builds only. The slice
adds early `ER_OPTION_PREVENTS_STATEMENT` returns before MariaDB can enter the
view, trigger, stored routine, package, or event metadata paths. `DROP EVENT`
is rejected outside the event-scheduler conditional so the current embedded
build cannot fall through to a successful no-op.

Later update: `persistent-view-storage` supersedes this rejection for
user-created views in MyLite schemas, and `persistent-trigger-storage`
supersedes it for user-created triggers on MyLite tables.
`persistent-routine-storage` supersedes it for standalone stored procedures and
stored functions in MyLite schemas. Package, package-body, event, non-MyLite
view, non-MyLite trigger, and non-MyLite routine DDL still follows the embedded
rejection boundary.

The embedded bootstrap smoke now verifies explicit rejections for:

- `CREATE VIEW`
- `ALTER VIEW`
- `DROP VIEW`
- `CREATE TRIGGER`
- `DROP TRIGGER`
- `CREATE PROCEDURE`
- `ALTER PROCEDURE`
- `DROP PROCEDURE`
- `CREATE FUNCTION` as a stored function
- `DROP FUNCTION`
- `CREATE EVENT`
- `ALTER EVENT`
- `DROP EVENT`

Report evidence from
`MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`:

- `build/mariadb-minsize/mylite-embedded-bootstrap-report.txt`:
  - `status=0`
  - `message=ok`
  - each schema-object label returned `errno=1290`
  - each schema-object label returned SQLSTATE `HY000`

Verification run:

- `git diff --check`
- `bash -n tools/run-embedded-bootstrap-smoke.sh
  tools/run-compatibility-test-harness.sh tools/run-storage-engine-smoke.sh
  tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`

Measured `MinSizeRel` artifacts from
`build/mariadb-minsize/mylite-build-report.txt` and `ls -l`:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,413,682 bytes,
  571 objects.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,706,920
  bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 87,206 bytes.
