# Persistent trigger storage

## Problem

MyLite now stores tables, indexes, constraints, and views in the primary
`.mylite` catalog, but embedded builds still reject persistent trigger DDL.
Triggers are common application schema objects and MariaDB already executes
them through table-local metadata once `Table_triggers_list` is available.

This slice moves MariaDB trigger definition persistence from durable `.TRG`
and `.TRN` sidecars into the MyLite catalog while preserving MariaDB parsing,
ordering, execution, diagnostics, and metadata display.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/sql_parse.cc` currently rejects
  `SQLCOM_CREATE_TRIGGER` and `SQLCOM_DROP_TRIGGER` under
  `EMBEDDED_LIBRARY` before entering MariaDB's trigger code.
- `vendor/mariadb/server/sql/sql_trigger.cc` stores per-table trigger lists in
  text `.TRG` files. `triggers_file_type` is `TYPE=TRIGGERS`, and
  `triggers_file_parameters` serializes trigger SQL, SQL modes, definers,
  creation character sets, collations, and creation timestamps.
- `Table_triggers_list::check_n_load()` reads the table `.TRG` file with
  `sql_parse_prepare()`, parses each stored `CREATE TRIGGER` statement back
  through `parse_sql()`, attaches the resulting trigger body to the opened
  table, and prepares OLD/NEW row accessors.
- `Table_triggers_list::create_trigger()` uses a `.TRN` file to reserve the
  schema-wide trigger name, then rewrites the table `.TRG` file.
- `Table_triggers_list::drop_trigger()` removes a trigger from the in-memory
  table trigger list, rewrites or removes the `.TRG` file, then removes the
  `.TRN` file.
- `add_table_for_trigger_internal()` and `SHOW CREATE TRIGGER` locate a
  trigger's subject table by reading its `.TRN` file.
- `Table_triggers_list::drop_all_triggers()` removes `.TRN` files and the
  table `.TRG` file when a table is dropped.
- `Table_triggers_list::change_table_name()` updates `.TRG` and `.TRN`
  definitions during table rename and ALTER paths.
- The `persistent-view-storage` slice added a reusable
  `sql_parse_prepare_from_memory()` and `sql_serialize_definition_file()`,
  which can serialize and parse trigger definition text without using the
  datadir.

## Scope

Support sidecar-free persistent triggers for MyLite schemas:

- `CREATE TRIGGER` on MyLite base tables;
- `CREATE OR REPLACE TRIGGER` and `CREATE TRIGGER IF NOT EXISTS`;
- `DROP TRIGGER` and `DROP TRIGGER IF EXISTS`;
- trigger firing for `INSERT`, `UPDATE`, and `DELETE` through inherited
  MariaDB execution paths;
- `SHOW CREATE TRIGGER` and information-schema trigger visibility;
- trigger persistence after fresh-process reopen and recovery read;
- trigger cleanup on table drop;
- trigger metadata update on table rename when MariaDB invokes the existing
  trigger rename hooks.

## Non-Goals

- Do not implement stored procedures, stored functions, packages, or events in
  this slice.
- Do not introduce durable `.TRG` or `.TRN` files as compatibility sidecars.
- Do not change non-embedded MariaDB trigger behavior.
- Do not redesign trigger privilege or binary-log policy beyond preserving the
  existing embedded MyLite boundaries.

## Design

Keep MariaDB's trigger parser, trigger body compiler, row OLD/NEW binding,
execution, metadata display, and ordering logic. Replace only the persistence
bridge for MyLite-owned schemas.

1. Extend `Mylite_table_definition` with catalog-backed trigger metadata:
   the serialized table `.TRG` definition bytes and the schema-wide trigger
   names associated with that table.
2. Serialize trigger metadata in catalog text records keyed by schema and
   table. The `.TRG` payload is stored as opaque MariaDB definition text.
   Trigger-name records store one trigger name per table so `DROP TRIGGER` and
   `SHOW CREATE TRIGGER` can resolve a trigger to its subject table without a
   `.TRN` file.
3. Add MyLite schema bridge helpers to test, read, store, and remove table
   trigger definitions and trigger-name mappings.
4. Patch `Table_triggers_list::check_n_load()` to use the MyLite catalog
   bytes and `sql_parse_prepare_from_memory()` for MyLite schema tables,
   while leaving filesystem parsing unchanged for non-MyLite paths.
5. Patch `create_trigger()`, `drop_trigger()`, `save_trigger_file()`,
   `rm_trigger_file()`, and `rm_trigname_file()` to route MyLite schema
   writes to the catalog. Use one catalog flush per logical trigger metadata
   state so failed writes do not publish partial definitions.
6. Patch trigger-name lookup (`add_table_for_trigger_internal()` and
   `load_table_name_for_trigger()` callers) so MyLite `DROP TRIGGER` and
   `SHOW CREATE TRIGGER` locate subject tables from the catalog.
7. Allow embedded `SQLCOM_CREATE_TRIGGER` and `SQLCOM_DROP_TRIGGER` only when
   the target trigger schema belongs to the MyLite namespace. Non-MyLite
   trigger DDL remains explicitly rejected.

## Affected Subsystems

- `sql/sql_parse.cc` embedded trigger command dispatch.
- `sql/sql_trigger.cc` trigger create, drop, load, rename, and lookup paths.
- `include/mylite_schema.h` and `storage/mylite/ha_mylite.cc` catalog bridge
  helpers and catalog serialization.
- MyLite storage-engine smoke and compatibility harness.
- Single-file storage docs and roadmap.

## DDL Metadata Routing Impact

Trigger DDL becomes MyLite catalog DDL. Table definitions and rows remain in
their current catalog records; trigger definitions become table-local metadata
records that follow table drop and rename operations. The SQL layer must
invalidate table definitions and stored-program cache entries using existing
MariaDB trigger paths after successful create/drop.

## Single-File and Embedded Lifecycle

Accepted trigger definitions must live only in the primary `.mylite` file.
Runtime directories must remain free of durable `.TRG` and `.TRN` files after
create, drop, fresh-process read, recovery read, table drop, and table rename.
If a trigger metadata write fails, the previous trigger definition state must
remain visible or the statement must fail without publishing partial metadata.

## Public API and File Format Impact

No public C API change is expected. The catalog payload gains new trigger
records under the existing catalog format family; old files without trigger
records remain readable.

## Binary Size Impact

Expected binary-size impact is small and limited to catalog bridge helpers and
smoke coverage. The slice does not add dependencies or new execution engines.

## License and Dependency Impact

No new dependency or licensing change.

## Test Plan

- Extend the storage-engine smoke persistence phases with:
  - create a MyLite base table and audit table,
  - create BEFORE/AFTER INSERT, UPDATE, and DELETE triggers,
  - verify trigger effects on rows and audit records,
  - verify `CREATE TRIGGER IF NOT EXISTS` and `CREATE OR REPLACE TRIGGER`,
  - verify `SHOW CREATE TRIGGER` and `information_schema.TRIGGERS`,
  - drop one trigger and verify it stops firing,
  - rename a table with a trigger and verify the trigger follows the new name,
  - reopen and recovery-read the final trigger state,
  - drop a table with triggers and verify trigger metadata is removed.
- Keep embedded bootstrap rejection coverage for non-MyLite trigger DDL.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` on the smoke scripts
  - `git diff --check`

## Acceptance Criteria

- MyLite embedded runtimes can create, replace, drop, reopen, and execute
  persistent triggers in MyLite schemas.
- Trigger metadata is discoverable through `SHOW CREATE TRIGGER` and
  `information_schema.TRIGGERS`.
- Fresh-process read and recovery read fire persisted triggers and omit
  dropped triggers.
- Runtime sidecar scans find no durable `.TRG`, `.TRN`, `.frm`, trigger,
  routine, event, Aria, InnoDB, or MyISAM sidecars.
- Stored routine, package, and event DDL remains explicitly unsupported until
  separate catalog designs exist.

## Implementation Result

The slice is implemented.

- MyLite catalog table definitions now carry serialized `TYPE=TRIGGERS`
  payloads and trigger-name records, both stored in the primary `.mylite`
  catalog.
- Embedded MyLite `CREATE TRIGGER`, `CREATE TRIGGER IF NOT EXISTS`,
  `CREATE OR REPLACE TRIGGER`, and `DROP TRIGGER` are allowed for MyLite
  schemas while non-MyLite trigger DDL remains rejected.
- `Table_triggers_list::check_n_load()` loads MyLite trigger definitions from
  catalog bytes through `sql_parse_prepare_from_memory()`; the inherited
  filesystem path is unchanged for non-MyLite builds.
- Trigger lookup for `DROP TRIGGER` and `SHOW CREATE TRIGGER` uses MyLite's
  catalog trigger-name map instead of durable `.TRN` files.
- Trigger file save/remove helpers route MyLite `.TRG` and `.TRN` operations
  to catalog updates, including table-drop cleanup.
- The storage smoke verifies create, replace, `IF NOT EXISTS`, drop,
  `SHOW CREATE TRIGGER`, `information_schema.TRIGGERS`, INSERT/UPDATE/DELETE
  firing, table rename, rollback of a post-reopen firing probe, fresh-process
  read, recovery read, and absence of durable `.TRG`/`.TRN` sidecars.

## Risks and Unresolved Questions

- MariaDB trigger create/drop currently interleaves `.TRN` reservation,
  `.TRG` rewrite, DDL logging, and binlog setup. The MyLite path must avoid
  leaving a trigger-name mapping without a matching table trigger definition.
- Trigger rename handling is coupled to table rename and ALTER paths. The
  first implementation should route the existing helper functions rather than
  inventing a parallel rename path.
- Stored routines can be referenced from triggers, but routine persistence
  remains blocked until a separate catalog design replaces `mysql.*` system
  table writes.
