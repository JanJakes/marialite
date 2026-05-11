# Persistent view storage

## Problem

MyLite currently rejects persistent view DDL in embedded builds. That was the
right boundary while the table catalog was incomplete, but views are common
application SQL and are closer to the existing MyLite table-definition bridge
than triggers, routines, packages, or events. MariaDB stores view definitions
as text `.frm` definition files, and the existing MyLite catalog already stores
opaque table-definition images inside the primary `.mylite` file.

This slice adds MyLite-owned storage for persistent views without re-enabling
durable `.frm` sidecars.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/sql_parse.cc` dispatches
  `SQLCOM_CREATE_VIEW` and `SQLCOM_DROP_VIEW` to `mysql_create_view()` and
  `mysql_drop_view()`. The current MyLite patch rejects both under
  `EMBEDDED_LIBRARY`.
- `vendor/mariadb/server/sql/sql_view.cc:956` documents
  `mysql_register_view()` as the function that writes view `.frm` definition
  files and manages view-definition backups.
- `vendor/mariadb/server/sql/sql_view.cc:971` builds the canonical view
  definition text from MariaDB's parsed `SELECT`, populates the `TABLE_LIST`
  view metadata, and calls `sql_create_definition_file()`.
- `vendor/mariadb/server/sql/sql_view.cc:1909` implements `mysql_drop_view()`
  by checking the view `.frm` and deleting it from the datadir.
- `vendor/mariadb/server/sql/table.cc:650` documents `open_table_def()` as the
  table-definition loader. For text view definitions it detects
  `TYPE=VIEW\n`, creates a `File_parser`, marks `TABLE_SHARE::is_view`, and
  asks `mariadb_view_version_get()` to populate the view metadata version.
- `vendor/mariadb/server/sql/table_cache.cc` notes that `tdc_acquire_share()`
  always requests discovery, but `open_table_def()` currently only calls
  `ha_discover_table()` after a missing file when `GTS_TABLE` is set. A
  view-only open through `tdc_open_view()` uses `GTS_VIEW`, so MyLite view
  discovery needs an explicit source patch.
- `vendor/mariadb/server/sql/parse_file.cc` only exposes
  `sql_parse_prepare()` for parsing definition files from a filesystem path.
  MyLite needs an equivalent parser entry point for definition bytes loaded
  from the `.mylite` catalog.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc` already serializes
  opaque table-definition images in `TABLE` catalog records and discovers them
  through `handlerton::discover_table`. The same catalog record can carry text
  view definition bytes if discovery branches on the `TYPE=VIEW\n` header.

## Scope

Support sidecar-free persistent views for MyLite schemas:

- `CREATE VIEW schema.view AS SELECT ...` over MyLite tables;
- `CREATE OR REPLACE VIEW`;
- `ALTER VIEW`;
- `DROP VIEW` and `DROP VIEW IF EXISTS`;
- `SHOW TABLES`, `SHOW FULL TABLES`, `SHOW CREATE VIEW`, and
  `information_schema.VIEWS` visibility;
- selecting from persisted views after fresh-process reopen and recovery read.

## Non-Goals

- Do not implement triggers, stored routines, packages, or events in this
  slice.
- Do not implement view-update DML semantics beyond what MariaDB already allows
  when the view metadata is available.
- Do not implement dependency-aware cascade behavior beyond MariaDB's existing
  view checks.
- Do not write durable view `.frm` files as an intermediate compatibility
  layer.
- Do not change non-embedded MariaDB behavior.

## Design

Keep MariaDB's parser, analyzer, view expansion, `SHOW CREATE VIEW`, and
information-schema code paths. Replace only the filesystem persistence bridge
for MyLite-owned embedded schemas.

1. Add a parse-file helper that can parse `TYPE=VIEW\n...` definition bytes
   from memory into a `File_parser` allocated on the caller's `MEM_ROOT`.
2. Add a serialization helper for MariaDB definition-file text so
   `mysql_register_view()` can produce the same view definition bytes without
   creating a filesystem `.frm` file.
3. Extend the MyLite schema/catalog bridge with functions to store, replace,
   test, and remove view definition bytes. Reuse the current catalog `TABLE`
   record payload because the persisted object is still an opaque MariaDB
   definition image keyed by schema and object name.
4. Extend `mylite_discover_table()` so a catalog definition beginning with
   `TYPE=VIEW\n` initializes `TABLE_SHARE` as a view with a memory-backed
   `File_parser`; binary table definitions continue to use
   `init_from_binary_frm_image()`.
5. Patch `open_table_def()` so missing-file discovery is attempted for
   `GTS_VIEW` as well as `GTS_TABLE`. This lets `tdc_open_view()` and
   `SHOW CREATE VIEW` discover MyLite views without a datadir `.frm`.
6. Patch embedded `SQLCOM_CREATE_VIEW`/`SQLCOM_DROP_VIEW` dispatch to allow
   MyLite schema targets while keeping non-MyLite persistent view DDL rejected.
7. Patch `mysql_register_view()` and `mysql_drop_view()` to use the MyLite
   catalog path when the target schema belongs to the MyLite namespace.

## Affected Subsystems

- `sql/sql_parse.cc` embedded command dispatch.
- `sql/sql_view.cc` view registration and drop paths.
- `sql/table.cc` table-definition discovery for view-only opens.
- `sql/parse_file.*` memory-backed definition parsing and serialization.
- `include/mylite_schema.h` and `storage/mylite/ha_mylite.cc` catalog helpers.
- MyLite storage-engine smoke, compatibility harness, and docs.

## DDL Metadata Routing Impact

This slice routes view definition bytes into the primary `.mylite` catalog
instead of a durable datadir `.frm`. Table DDL remains routed through the
handler create/alter/drop path. View DDL must invalidate the table-definition
cache after create/alter/drop so reopened statements see current metadata.

## Single-File and Embedded Lifecycle

Accepted view definitions must live in the primary `.mylite` file. Runtime
directories must remain free of persistent view `.frm` sidecars after
CREATE/ALTER/DROP, fresh-process read, and recovery read. If a view catalog
write fails, the previous view definition must remain visible or the create
statement must fail without publishing a partial definition.

## Public API and File Format Impact

No public C API change is expected. Reusing the existing `TABLE` catalog record
for text view definition bytes avoids a file-format version bump in this slice.

## Binary Size Impact

Expected binary-size impact is small and limited to SQL-layer bridge helpers
and smoke coverage. The slice does not add dependencies or new execution
engines.

## License and Dependency Impact

No new dependency or licensing change.

## Test Plan

- Extend the storage-engine smoke persistence phases with:
  - create base MyLite tables,
  - create a simple view and select from it,
  - `SHOW CREATE VIEW`,
  - `SHOW FULL TABLES` view type,
  - `information_schema.VIEWS`,
  - `CREATE OR REPLACE VIEW`,
  - `ALTER VIEW`,
  - `DROP VIEW IF EXISTS`,
  - fresh-process read and recovery read of the final view state.
- Keep the embedded bootstrap rejection coverage for non-MyLite view DDL and
  trigger/routine/event DDL; MyLite-schema view acceptance is covered by the
  storage-engine persistence smoke.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` on the smoke scripts
  - `git diff --check`

## Acceptance Criteria

- MyLite embedded runtimes can create, replace, alter, drop, reopen, and query
  persistent views in MyLite schemas.
- View metadata is discoverable through `SHOW CREATE VIEW`, `SHOW FULL TABLES`,
  and `information_schema.VIEWS`.
- Fresh-process read and recovery read expand views and return expected rows.
- Runtime sidecar scans find no durable `.frm`, trigger, routine, event, Aria,
  InnoDB, or MyISAM sidecars.
- Trigger, routine, package, and event DDL remains explicitly unsupported until
  separate catalog designs exist.

## Implementation Result

The slice is implemented.

- `parse_file` now has a memory-backed parser entry point for text definition
  bytes and a serializer that emits the same `TYPE=VIEW` definition format
  MariaDB previously wrote to `.frm` files.
- Embedded MyLite `CREATE VIEW`, `CREATE OR REPLACE VIEW`, `ALTER VIEW`, and
  `DROP VIEW` are allowed for MyLite catalog schemas while non-MyLite
  persistent view DDL remains rejected in embedded builds.
- `mysql_register_view()` stores serialized view definition bytes in the
  MyLite catalog instead of creating a durable view `.frm`; `mysql_drop_view()`
  removes MyLite view definitions from the same catalog path.
- MyLite discovery initializes `TABLE_SHARE` objects for catalog-backed views
  through the memory-backed `File_parser`; binary table-definition discovery is
  unchanged.
- The storage smoke now verifies view create, replace, alter, drop,
  `SHOW CREATE VIEW`, `information_schema.TABLES`, `information_schema.VIEWS`,
  fresh-process read, and recovery read. The final persisted view returns
  `2:beta`, and a dropped view remains absent.

## Risks and Unresolved Questions

- MariaDB view metadata uses file-parser internals that were historically
  path-oriented. The memory parser helper should be narrow and source-local to
  avoid broad parse-file rewrites.
- Updatable views may work through inherited MariaDB semantics once metadata is
  available, but this slice should only rely on read/select behavior unless the
  smoke proves a safe subset.
- Stored routines and triggers can reference views. Those interactions remain
  blocked until routine and trigger catalog support exists.
