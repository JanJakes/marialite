# system-schema-namespace-policy

## Problem Statement

MyLite now persists ordinary schema names inside the `.mylite` catalog, but
server-owned schema names still need an explicit policy. Names such as
`mysql`, `performance_schema`, and `sys` should not accidentally become user
catalog schemas just because the embedded profile does not yet provide
replacement system tables.

The current MyLite schema branch rejects `CREATE DATABASE information_schema`
through MariaDB's inherited check, and `SHOW DATABASES` lists
`information_schema` plus MyLite catalog schemas. Other server-owned names can
still enter the catalog through `CREATE DATABASE`, which would create a
misleading namespace without the required grant, plugin, time-zone,
performance, or helper-view semantics.

## Scope

- Define a reserved-schema policy for the embedded MyLite namespace.
- Reject catalog `CREATE DATABASE` for:
  - `mysql`,
  - `performance_schema`,
  - `sys`.
- Reject catalog `DROP DATABASE` for those reserved names, including
  `IF EXISTS`, so unsupported server-owned namespaces are explicit instead of
  silently treated as user schemas.
- Keep inherited `information_schema` behavior: it remains virtual and
  `CREATE DATABASE information_schema` continues to fail before MyLite catalog
  routing.
- Keep the built-in MyLite seed schema protected. Add smoke coverage for
  `DROP DATABASE mylite` and `CREATE OR REPLACE DATABASE mylite`.
- Verify rejected reserved schema operations do not create catalog schema
  records or datadir directories.
- Update roadmap and architecture docs to describe the current policy.

## Non-Goals

- Do not implement `mysql.*` replacement system tables.
- Do not implement performance schema tables.
- Do not implement a MariaDB `sys` helper schema.
- Do not remove the inherited `mysql.servers` startup diagnostic or Aria
  startup logs in this slice.
- Do not change privilege checking, grants, users, roles, named time zones, or
  plugin metadata behavior.
- Do not add public `libmylite` API.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `sql/table.cc` defines `INFORMATION_SCHEMA_NAME`,
  `PERFORMANCE_SCHEMA_DB_NAME`, and `MYSQL_SCHEMA_NAME`.
- `sql/table.h:is_infoschema_db()` and `is_perfschema_db()` provide the
  canonical MariaDB helpers for the virtual information schema and performance
  schema names.
- `sql/table.cc:get_table_category()` treats `information_schema`,
  `performance_schema`, and selected `mysql.*` tables as special table
  categories rather than ordinary user data.
- `sql/sql_db.cc:mysql_create_db_internal()` already rejects
  `information_schema` before schema locking. The MyLite namespace branch then
  creates a catalog schema for every other non-duplicate name.
- `sql/sql_db.cc:mysql_rm_db_internal()` now routes MyLite `DROP DATABASE`
  through `mylite_drop_schema()` for every catalog schema name, and reports
  absent names through the existing `ER_DB_DROP_EXISTS` path.
- `sql/sql_db.cc:mysql_change_db()` switches `information_schema` to the
  virtual schema before directory checks. Other names pass through
  `check_db_dir_existence()`, which now maps to the MyLite catalog when the
  MyLite namespace is active.
- `sql/sql_show.cc:make_db_list()` adds `information_schema` explicitly, then
  enumerates MyLite catalog schemas when the MyLite namespace is active.
  Reserved names that never enter the catalog are therefore not shown.
- `storage/mylite/ha_mylite.cc:mylite_drop_schema()` already rejects the
  built-in `mylite` seed schema with `HA_ERR_UNSUPPORTED`.

## Proposed Design

Add a small SQL-layer reserved-name helper next to the MyLite schema routing in
`sql/sql_db.cc`. It should check the normalized schema name after
`DBNameBuffer` normalization and before MyLite catalog create/drop work:

```text
reserved := mysql | performance_schema | sys
```

`information_schema` stays on the inherited MariaDB path because
`mysql_create_db_internal()` already rejects it and `mysql_change_db()` already
maps it to the virtual schema.

When the MyLite namespace is active:

- `CREATE DATABASE mysql`, `CREATE DATABASE performance_schema`, and
  `CREATE DATABASE sys` fail with an explicit unsupported-statement
  diagnostic.
- `DROP DATABASE mysql`, `DROP DATABASE performance_schema`, and
  `DROP DATABASE sys` fail with the same unsupported diagnostic, including
  `IF EXISTS`.
- Ordinary catalog schemas continue to use `mylite_create_schema()` and
  `mylite_drop_schema()`.
- The built-in `mylite` seed remains protected by the storage-engine helper;
  smoke coverage should record that both direct drop and create-or-replace are
  rejected.

The helper belongs in `sql_db.cc` rather than the storage-engine catalog
parser because the policy is a SQL namespace rule, not an on-disk file-format
change. Existing pre-release catalogs that somehow contain reserved names are
not migrated in this slice; if one is encountered, it will still load under the
current pre-release compatibility rules until a dedicated catalog validation
slice exists.

## Affected Subsystems

- MariaDB SQL schema DDL routing in `sql/sql_db.cc`.
- MyLite storage smoke assertions and report fields.
- Roadmap and single-file storage architecture documentation.

## DDL Metadata Routing Impact

This slice touches schema DDL only. It does not change table-definition images,
copy ALTER, standalone index DDL, or `.frm` bridge behavior.

## Single-File And Embedded-Lifecycle Implications

Rejected reserved schema DDL must not create datadir directories, `.frm`
sidecars, or MyLite catalog records. `information_schema` remains virtual.
`mysql`, `performance_schema`, and `sys` remain absent until replacement
catalog or virtual-table behavior is designed.

## Public API Or File-Format Impact

No public `libmylite` API changes. No intended file-format change.

## Binary-Size Impact

Measured artifact sizes after implementation:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,440,792 bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 87,206 bytes.
- `build/mariadb-minsize/storage/mylite/libmylite_embedded.a`: 305,932 bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,842,288
  bytes.
- `build/mariadb-minsize/mylite/mylite-compatibility-smoke`: 22,776,336 bytes.

The size impact is limited to a small SQL-layer name check and additional
smoke report fields.

## License, Trademark, And Dependency Impact

No new dependency. New code remains GPL-2.0-only. The slice does not change
public MariaDB/MySQL trademark-facing packaging.

## Test And Verification Plan

- Run `git diff --check`.
- Run `bash -n tools/run-storage-engine-smoke.sh
  tools/run-compatibility-test-harness.sh`.
- Run `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`.
- Run `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`.
- In the storage smoke, verify:
  - `CREATE DATABASE mysql` is rejected,
  - `DROP DATABASE IF EXISTS mysql` is rejected,
  - `CREATE DATABASE performance_schema` is rejected,
  - `CREATE DATABASE sys` is rejected,
  - `DROP DATABASE mylite` is rejected,
  - `CREATE OR REPLACE DATABASE mylite` is rejected,
  - `information_schema.SCHEMATA` does not list `mysql`,
    `performance_schema`, or `sys` as MyLite catalog schemas,
  - schema directory and `.frm` artifact scans remain empty.

## Acceptance Criteria

- Reserved server-owned schema names cannot be created as MyLite catalog
  schemas.
- Reserved server-owned schema names cannot be dropped through MyLite catalog
  schema DDL.
- The built-in `mylite` seed schema has explicit smoke coverage for drop and
  create-or-replace rejection.
- Ordinary user schema create/use/table/show/drop behavior from
  `schema-namespace-catalog` still passes.
- Reports and docs distinguish this policy from future replacement
  `mysql.*`, performance schema, or `sys` implementations.

## Implementation Result

`sql/sql_db.cc` now checks normalized schema names before MyLite catalog
create/drop routing. `mysql`, `performance_schema`, and `sys` fail with an
explicit unsupported-statement diagnostic when the MyLite namespace is active,
including `DROP DATABASE IF EXISTS`. Ordinary user schemas continue through
the catalog helpers.

The storage smoke now verifies:

- `reserved_create_mysql=rejected`,
- `reserved_drop_mysql=rejected`,
- `reserved_create_performance_schema=rejected`,
- `reserved_create_sys=rejected`,
- originally `reserved_seed_drop=rejected` and
  `reserved_seed_replace=rejected`; after `seed-probe-removal`, the smoke
  reports `reserved_default_schema_drop=rejected` and
  `reserved_default_schema_replace=rejected`,
- `reserved_schema_count=0`,
- `FRM Artifacts=none`,
- `Schema Directory Artifacts=none`.

Verification completed:

- `git diff --check`.
- `bash -n tools/run-storage-engine-smoke.sh
  tools/run-compatibility-test-harness.sh`.
- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`.
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`.

`mylite-compatibility-harness-report.txt` reports `status=0` for
`embedded_lifecycle`, `libmylite_lifecycle`, `storage_single_file`,
`mariadb_comparison`, and `sidecar_scan`.

## Risks And Unresolved Questions

- A later catalog validation slice may need to reject or migrate pre-release
  catalog files that already contain reserved schema names.
- `sys` is less core than `mysql` and `performance_schema`, but reserving it
  now prevents accidental helper-schema claims before MyLite has a deliberate
  compatibility policy.
- Startup still probes `mysql.servers` and creates inherited Aria logs; this
  slice only prevents user-visible catalog namespace confusion.
