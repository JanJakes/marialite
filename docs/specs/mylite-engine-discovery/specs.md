# mylite-engine-discovery

## Problem Statement

The static `MYLITE` engine is registered, but MariaDB cannot yet discover a
table definition from MyLite-owned metadata. The next slice should wire the
engine's discovery hooks and prove that MariaDB can open a table definition
supplied by MyLite without a durable `.frm` file.

Because the durable catalog does not exist yet, this slice should use a tiny
in-engine seed catalog for one smoke table. That keeps the discovery bridge
testable without claiming final catalog storage.

## Scope

- Add `MYLITE` handlerton discovery hooks:
  - `discover_table()`
  - `discover_table_names()`
  - `discover_table_existence()`
- Add one seed catalog entry for smoke testing:
  `mylite.probe` with SQL definition
  `CREATE TABLE probe (id INT) ENGINE=MYLITE`.
- Keep the seed catalog private to the engine implementation.
- Extend the storage-engine smoke to create only a temporary schema directory
  under the smoke datadir, query the discovered table, and verify it returns
  zero rows through the skeleton handler.
- Record observed runtime files and dynamic plugin artifacts.

## Non-Goals

- Do not implement the durable MyLite catalog.
- Do not persist table definitions in `.mylite`.
- Do not execute `CREATE TABLE`, `ALTER TABLE`, `DROP TABLE`, or `RENAME TABLE`.
- Do not make discovered table definitions user-configurable.
- Do not add public `libmylite` APIs.
- Do not claim general table discovery beyond the single seed table.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `handler.h` defines `discover_table()`, `discover_table_names()`, and
  `discover_table_existence()` on `handlerton`.
- `handler.cc:ha_initialize_handlerton()` tracks whether engines provide
  discovery hooks and installs default existence behavior when needed.
- `handler.cc:ha_discover_table_names()` calls each engine's
  `discover_table_names()` while listing tables for `SHOW TABLES` and
  information schema.
- `handler.cc:ha_table_exists()` can call `discover_table_existence()` and then
  full discovery when no `.frm` exists.
- `table.cc:TABLE_SHARE::init_from_sql_statement_string()` can initialize a
  table definition from a SQL `CREATE TABLE` string and accepts a `write`
  boolean. Passing `false` avoids writing the generated `.frm` image.
- `storage/test_sql_discovery/test_sql_discovery.cc` is a minimal upstream test
  engine that discovers one table from a SQL string and calls
  `init_from_sql_statement_string()`.
- `storage/sequence/sequence.cc` and `storage/perfschema/ha_perfschema.cc`
  show production uses of SQL-string discovery with `write=false`.

## Proposed Design

Add a private seed catalog entry in `ha_mylite.cc`:

```text
db: mylite
table: probe
sql: CREATE TABLE probe (id INT) ENGINE=MYLITE
```

The handlerton init function should set:

```c++
mylite_hton->discover_table= mylite_discover_table;
mylite_hton->discover_table_names= mylite_discover_table_names;
mylite_hton->discover_table_existence= mylite_discover_table_existence;
```

Discovery behavior:

- `discover_table_names()` adds `probe` only when the requested database is
  `mylite`.
- `discover_table_existence()` returns true only for `mylite.probe`.
- `discover_table()` initializes the `TABLE_SHARE` for `mylite.probe` from the
  seed SQL string with `write=false`; all other names return
  `HA_ERR_NO_SUCH_TABLE`.

The storage-engine smoke should:

1. start the embedded runtime with the existing controlled defaults,
2. create the temporary datadir schema directory `mylite`,
3. verify `MYLITE` exists in `information_schema.ENGINES`,
4. run `SHOW TABLES FROM mylite` and verify `probe` is listed,
5. run `SELECT COUNT(*) FROM mylite.probe` and verify the result is `0`,
6. fail if a `.frm` or dynamic plugin artifact appears in the smoke runtime.

## Affected Subsystems

- MyLite storage-engine handlerton hooks.
- Storage-engine smoke target and wrapper script.
- Slice docs and roadmap.

No public API, durable catalog, DDL write path, or file format behavior changes.

## DDL Metadata Routing Impact

This slice avoids DDL. The discovered table definition is generated from a
private SQL string with `write=false`, so it should not write a `.frm` image.
DDL routing remains a later slice.

## Single-File And Embedded-Lifecycle Implications

The seed catalog is process-local code, not durable metadata. It exists only to
prove that MyLite can feed table definitions to MariaDB's discovery path without
filesystem metadata sidecars. The smoke creates a temporary schema directory
under its runtime datadir because MariaDB still expects a database namespace
during this compatibility phase.

## Public API Or File-Format Impact

None.

## Binary-Size Impact

The discovery hooks add small first-party code to the static `MYLITE` engine.
Record measured artifact sizes after implementation.

## License, Trademark, And Dependency Impact

No new dependency. New code remains GPL-2.0-only.

## Test And Verification Plan

- Run `tools/run-storage-engine-smoke.sh`.
- Verify the report records `engine=MYLITE`, `support=YES`,
  `discovered_table=probe`, and `count=0`.
- Verify no `.frm` files appear in the smoke runtime.
- Verify dynamic plugin artifacts remain absent.
- Run `tools/run-libmylite-open-close-smoke.sh`.
- Run `tools/run-embedded-bootstrap-smoke.sh`.
- Run `bash -n` for changed shell scripts.
- Run `git diff --check`.

## Acceptance Criteria

- MariaDB discovers `mylite.probe` through MyLite engine hooks without a durable
  `.frm` file.
- The skeleton handler can open the discovered table and return an empty scan.
- The smoke records no dynamic plugin artifacts and no `.frm` sidecars.
- The implementation remains a seed-catalog bridge, not a durable catalog.
- Previous smokes still pass.

## Implementation Result

The `MYLITE` handlerton now installs discovery hooks backed by a private seed
catalog entry for `mylite.probe`. The storage-engine smoke passes:

```sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
```

The report records:

- `engine=MYLITE`
- `support=YES`
- `discovered_table=probe`
- `count=0`
- dynamic plugin artifacts: none
- `.frm` artifacts: none

The later `seed-probe-removal` slice removed the hard-coded `mylite.probe`
table after user-created catalog-backed table definitions covered discovery.
Current storage smokes now expect the default schema to start empty and verify
that `mylite.probe` is absent.

Regression smokes also pass:

```sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
```

Observed artifacts after this slice:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,227,954 bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 29,530 bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,613,232
  bytes.
- `build/mariadb-minsize/mylite/mylite-open-close-smoke`: 22,616,304 bytes.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,679,920
  bytes.

## Risks And Unresolved Questions

- Creating a temporary schema directory is still a compatibility artifact. The
  later catalog and DDL routing slices must eliminate schema-directory
  dependence for real `.mylite` files.
- SQL-string discovery exercises MariaDB's parser and table-definition image
  generator. This is useful, but durable catalog storage still needs binary
  image versioning and recovery policy.
- The seed table must not escape into user-facing compatibility claims.
