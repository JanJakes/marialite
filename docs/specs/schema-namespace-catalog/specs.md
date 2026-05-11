# schema-namespace-catalog

## Problem Statement

MyLite table definitions already live in the primary `.mylite` catalog, but
MariaDB schema namespace operations still use datadir directories. The storage
smoke wrapper currently creates a temporary `datadir/mylite` directory before
starting the embedded runtime, and inherited SQL paths still treat `CREATE
DATABASE`, `DROP DATABASE`, `USE`, `SHOW DATABASES`, `SHOW TABLES`, and
`information_schema.SCHEMATA` as directory-backed operations.

That is incompatible with MyLite's target shape. Schemas should be catalog
namespaces inside the `.mylite` file, not persistent directories next to it.

## Scope

- Add a MyLite-owned schema namespace to the storage-engine catalog.
- Persist empty user-created schemas in the primary `.mylite` catalog.
- Keep the existing built-in `mylite` seed schema and `mylite.probe` table for
  current smokes.
- Route MyLite embedded `CREATE DATABASE`, `DROP DATABASE`, and `USE` through
  the catalog namespace instead of datadir directory creation or checks.
- Route `SHOW DATABASES`, `SHOW TABLES`, and
  `information_schema.SCHEMATA`/`TABLES` for MyLite schemas through catalog
  schema and table-name enumeration.
- Remove the storage-engine smoke's pre-created `datadir/mylite` compatibility
  directory and fail if a schema directory is created for MyLite schemas.
- Cover a non-seed schema lifecycle:
  - `CREATE DATABASE mylite_schema`
  - `USE mylite_schema`
  - `CREATE TABLE schema_table (...) ENGINE=MYLITE`
  - `SHOW DATABASES LIKE 'mylite_schema'`
  - `SHOW TABLES FROM mylite_schema LIKE 'schema_table'`
  - `information_schema.SCHEMATA` and `information_schema.TABLES` visibility
  - fresh-process reopen visibility
  - `DROP DATABASE mylite_schema`

## Non-Goals

- Do not normalize table definitions beyond the existing frm-image bridge.
- Do not add catalog records for charset, collation, or schema comments yet;
  schema DDL uses the server default collation for current introspection.
- Do not support dropping the built-in `mylite` seed schema in this slice.
- Do not add system-schema replacement tables.
- Do not implement transactional schema DDL or crash-safe multi-step table DDL
  swaps.
- Do not support non-MyLite durable engines inside MyLite catalog schemas.
- Do not add public `libmylite` API.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `sql/sql_db.cc:mysql_create_db_internal()` locks the schema name, builds a
  datadir path with `build_table_filename()`, checks the directory with
  `mysql_file_stat()`, creates it with `my_mkdir()`, then writes `db.opt`.
- `sql/sql_db.cc:mysql_rm_db_internal()` builds the schema directory path,
  opens it with `my_dir()`, discovers table names via
  `ha_discover_table_names()`, drops tables, removes `db.opt`, and removes the
  directory with `rm_dir_w_symlink()`.
- `sql/sql_db.cc:mysql_change_db()` rejects `USE db` when
  `check_db_dir_existence()` cannot find a schema directory.
- `sql/sql_show.cc:find_files()` lists databases by scanning directories under
  `mysql_data_home`; when listing tables for a specific schema it first opens
  the schema directory with `my_dir()` and only then calls
  `ha_discover_table_names()`.
- `sql/sql_show.cc:make_db_list()` and `fill_schema_schemata()` derive
  `SHOW DATABASES` and `information_schema.SCHEMATA` rows from directory names
  and directory existence checks.
- `sql/handler.h` documents `handlerton::discover_table_names()` for engine
  table enumeration inside a known schema, but MariaDB has no matching engine
  callback for schema-name enumeration.
- `storage/mylite/ha_mylite.cc:mylite_discover_table_names()` already lists
  table definitions by catalog database name, and
  `mylite_discover_table_existence()` checks catalog table existence without a
  `.frm` sidecar.
- `storage/mylite/ha_mylite.cc:mylite_store_table_definition()` stores the
  database name parsed from MariaDB's handler path or `TABLE_SHARE`, so the
  table catalog can already address multiple schema names once schema
  existence is represented.
- `sql/sql_table.cc:mysql_rename_table()` still renames `.frm` files during
  MariaDB copy-ALTER and standalone index DDL swaps, so MyLite catalog schemas
  need a narrow skip for those transient table-definition file renames when no
  schema directory exists.
- `sql/discover.cc:writefile()` writes discovered table definitions to `.frm`
  files for inherited rediscovery paths. MyLite catalog schemas need a narrow
  ENOENT success path so transient writes do not require a durable schema
  directory.
- `tools/run-storage-engine-smoke.sh:run_smoke_phase()` still creates
  `${datadir}/mylite`; this is the temporary compatibility artifact this slice
  should remove.

## Proposed Design

Add a small schema catalog alongside the existing table-definition catalog in
`ha_mylite.cc`:

```text
SCHEMA <schema-name>
TABLE  <schema-name> <table-name> <frm-image>
```

Schema names should use the same hex text encoding pattern as table records.
Existing pre-release catalog generations without `SCHEMA` records remain
loadable: the loader should seed the built-in `mylite` schema and derive schema
names from loaded table definitions. New writes should serialize explicit
`SCHEMA` records so empty schemas persist.

Expose narrow MyLite-owned schema helpers from the storage engine to SQL-layer
fork points:

- check whether the MyLite schema namespace is active,
- check whether a schema exists,
- create a schema,
- drop a non-seed schema and its MyLite table definitions,
- enumerate schema names,
- enumerate table names in a schema.

The helpers should return existing handler-style errors and keep diagnostics in
the SQL layer. This keeps the MyLite catalog ownership in the storage engine
while keeping MariaDB client protocol behavior in `sql_db.cc` and
`sql_show.cc`.

When the MyLite namespace is active, SQL-layer schema paths should bypass
datadir directories:

- `CREATE DATABASE` writes a schema catalog record, with existing MariaDB
  `IF NOT EXISTS` and duplicate-schema diagnostics.
- `DROP DATABASE` drops a non-seed schema and all MyLite table definitions in
  it, reports the number of dropped tables, and clears the current schema when
  needed.
- `USE` accepts catalog schemas and rejects absent catalog schemas.
- `SHOW DATABASES`, `SHOW TABLES`, and information-schema table enumeration
  read MyLite catalog names without calling `my_dir()` for MyLite schemas.

Table creation, rename, and drop should require the owning catalog schema to
exist. Renaming a MyLite table across schemas should require the target schema
to exist.

## Affected Subsystems

- MyLite storage-engine catalog structures, serialization, parsing, loading,
  and transaction snapshots.
- MariaDB SQL database namespace paths in `sql/sql_db.cc`.
- MariaDB schema/table listing paths in `sql/sql_show.cc`.
- Storage-engine smoke executable and wrapper script.
- Roadmap and architecture documentation.

## DDL Metadata Routing Impact

This slice moves schema DDL into MyLite's catalog for the embedded MyLite
runtime. It does not change table-definition images or copy ALTER mechanics.
Schema DROP removes MyLite table definitions directly from the catalog rather
than walking a directory of `.frm` files.

## Single-File And Embedded-Lifecycle Implications

No schema directory should be created under the smoke datadir for `mylite` or
user-created MyLite schemas. Empty schemas and schema table lists should
survive fresh-process reopen because their names live in the primary `.mylite`
catalog payload.

The built-in `mylite` seed schema remains a compatibility bootstrap artifact
for current smokes. At this slice's implementation time, dropping it needed a
later decision because the engine still seeded `mylite.probe` without a
persisted user-created definition. The later `seed-probe-removal` slice removed
that hard-coded probe table while keeping the default schema protected.

## Public API Or File-Format Impact

The internal catalog payload gains pre-release `SCHEMA` records. This is not a
public stable file format yet, but the loader must keep accepting existing
pre-release catalog generations without schema records.

No public `libmylite` API changes.

## Binary-Size Impact

Measured artifact sizes after implementation:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,439,936 bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 87,206 bytes.
- `build/mariadb-minsize/storage/mylite/libmylite_embedded.a`: 305,932 bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,842,408
  bytes.
- `build/mariadb-minsize/mylite/mylite-compatibility-smoke`: 22,776,248 bytes.

The code-size impact is limited to schema-vector bookkeeping, SQL-layer
branching for the active MyLite namespace, and additional smoke assertions. A
deeper production-size investigation belongs in a later dedicated size/profile
analysis, not this implementation slice.

## License, Trademark, And Dependency Impact

No new dependency. New code remains GPL-2.0-only.

## Test And Verification Plan

- Run `git diff --check`.
- Run `bash -n tools/run-storage-engine-smoke.sh
  tools/run-compatibility-test-harness.sh`.
- Run `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`.
- Run `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`.
- Verify the storage report records schema create/use/show/table/drop results.
- Verify the storage report's observed files do not include `datadir/mylite`,
  `datadir/mylite_schema`, or `.frm` artifacts.
- Verify catalog write/read phases see the user schema and table before drop,
  and no longer see them after drop.

## Implementation Result

The implementation adds catalog-backed `SCHEMA` records and MyLite-owned schema
helpers in `storage/mylite/ha_mylite.cc`, with declarations in
`include/mylite_schema.h`. New catalog writes serialize explicit schema names,
while loads still accept older pre-release catalog payloads without `SCHEMA`
records by seeding `mylite` and deriving schema names from table definitions.

`sql/sql_db.cc` routes MyLite `CREATE DATABASE`, `DROP DATABASE`, and `USE`
checks through the catalog namespace when the MyLite storage engine is active.
`sql/sql_show.cc` routes `SHOW DATABASES`, `SHOW TABLES`, and the relevant
information-schema list builders through catalog enumeration for MyLite
schemas. Table creation and rename paths require the owning MyLite catalog
schema to exist.

The inherited table-definition bridge still has narrow `.frm` compatibility
touchpoints. `sql/sql_table.cc` skips transient `.frm` renames for MyLite
catalog schemas during copy ALTER and standalone index DDL swaps, and
`sql/discover.cc` treats `.frm` write ENOENT as success only for MyLite catalog
schemas. Directory-backed engine behavior remains unchanged.

The storage smoke now creates `mylite_schema`, uses it, creates
`schema_table`, verifies `SHOW` and information-schema visibility, reopens the
catalog in fresh processes, and drops the schema for the normal read phase. The
wrapper no longer pre-creates `datadir/mylite` and fails if `datadir/mylite`,
`datadir/mylite_schema`, or `.frm` artifacts appear.

Verification completed:

- `git diff --check`.
- `bash -n tools/run-storage-engine-smoke.sh
  tools/run-compatibility-test-harness.sh`.
- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`.
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`.

Report evidence:

- `mylite-storage-engine-report.txt`: `status=0`,
  `schema_database=mylite_schema`, `schema_table=schema_table`,
  `schema_table_count=2`, `schema_schemata_count=1`,
  `schema_information_table_count=1`, `schema_drop_error=rejected`,
  `FRM Artifacts=none`, and `Schema Directory Artifacts=none`.
- `mylite-catalog-write-report.txt` and `mylite-catalog-read-report.txt`:
  schema namespace fields survive fresh-process persistence checks with no
  schema directory artifacts.
- `mylite-catalog-recovery-read-report.txt`: recovery fallback still exposes
  the persisted schema namespace and reports `Schema Directory Artifacts=none`.
- `mylite-compatibility-harness-report.txt`: all groups report `status=0`.

## Acceptance Criteria

- The storage smoke no longer pre-creates a `mylite` schema directory.
- At this slice's implementation time, `mylite.probe` remained discoverable
  without a schema directory. The later `seed-probe-removal` slice replaced
  that assertion with an empty-default-schema check and user-created table
  discovery coverage.
- `CREATE DATABASE mylite_schema` creates a persistent MyLite schema catalog
  record without a datadir directory.
- `USE mylite_schema` succeeds after schema creation and fails after schema
  drop.
- `CREATE TABLE mylite_schema.schema_table (...) ENGINE=MYLITE` succeeds only
  after the schema exists.
- `SHOW DATABASES`, `SHOW TABLES`, `information_schema.SCHEMATA`, and
  `information_schema.TABLES` see catalog schema/table names.
- A fresh embedded process sees the schema and table before `DROP DATABASE`.
- `DROP DATABASE mylite_schema` removes the schema and table definitions from
  the primary `.mylite` catalog without leaving sidecars.
