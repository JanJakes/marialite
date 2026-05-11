# storage-engine-skeleton

## Problem Statement

MyLite now has a public open/close API, but it still relies entirely on
MariaDB's built-in storage engines. The next implementation step is to add a
static MyLite storage-engine skeleton that MariaDB can register in embedded
builds. This creates the handler boundary where future catalog, table
discovery, row storage, and recovery work will attach.

This slice should prove plugin registration and handler construction only. It
must not imply that MyLite can store user tables yet.

## Scope

- Add a MyLite-owned storage engine under `vendor/mariadb/server/storage/mylite/`.
- Register it as a static MariaDB storage engine named `MYLITE`.
- Implement the minimum `handler` subclass required to compile and construct:
  open, close, create, table-scan stubs, metadata stubs, and table-lock wiring.
- Make table creation return a deliberate unsupported-engine error from the
  handler until DDL metadata routing exists.
- Add a storage-engine smoke target that starts the embedded runtime and
  verifies the `MYLITE` engine is present in `information_schema.ENGINES`.
- Record observed side effects and dynamic plugin artifacts in the smoke
  report.

## Non-Goals

- Do not store rows, indexes, metadata, or catalog state.
- Do not support `CREATE TABLE ... ENGINE=MYLITE` as a successful operation.
- Do not execute DDL in the smoke. MariaDB can write `.frm` before or around
  engine create paths, and DDL routing is a later slice.
- Do not implement table discovery, `discover_table_names()`, or
  `discover_table_existence()`.
- Do not make `MYLITE` the default storage engine.
- Do not remove Aria startup logs or the `mysql.servers` startup diagnostic.
- Do not add dynamic plugin loading.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/cmake/plugin.cmake:CONFIGURE_PLUGINS()` globs
  `storage/*` directories and adds every subdirectory with a `CMakeLists.txt`.
- `MYSQL_ADD_PLUGIN()` supports `STORAGE_ENGINE`, `STATIC_ONLY`, `MANDATORY`,
  `DEFAULT`, and `RECOMPILE_FOR_EMBEDDED`; static storage engines are added to
  `MYSQLD_STATIC_PLUGIN_LIBS` and `EMBEDDED_PLUGIN_LIBS`.
- `vendor/mariadb/server/storage/heap/CMakeLists.txt` registers the MEMORY/HEAP
  engine with `MYSQL_ADD_PLUGIN(heap ... STORAGE_ENGINE MANDATORY
  RECOMPILE_FOR_EMBEDDED)`.
- `vendor/mariadb/server/storage/example/ha_example.cc` shows the basic
  storage-engine registration pattern: a `handlerton` init function sets
  `handlerton::create`, flags, table-file extensions, and plugin metadata
  through `maria_declare_plugin()`.
- `vendor/mariadb/server/sql/handler.h` documents `handlerton::create` and the
  required private virtual methods on `handler`: `open()`, `rnd_init()`,
  `rnd_next()`, `rnd_pos()`, `position()`, `info()`, `external_lock()`,
  `store_lock()`, and `create()` among the minimum methods implemented by the
  example engine.
- The example engine notes that successful `CREATE TABLE ... ENGINE=EXAMPLE`
  still creates a normal MariaDB `.frm` file. MyLite must not use a successful
  create path until DDL metadata routing has been designed and tested.

## Proposed Design

Add `storage/mylite` with:

- `CMakeLists.txt`
- `ha_mylite.h`
- `ha_mylite.cc`

Register the engine with:

```cmake
MYSQL_ADD_PLUGIN(mylite ha_mylite.cc
  STORAGE_ENGINE
  MANDATORY
  RECOMPILE_FOR_EMBEDDED
)
```

The handlerton init function should:

- set `handlerton::create` to allocate `ha_mylite`,
- set `tablefile_extensions` to an empty extension list,
- set conservative flags such as `HTON_NO_PARTITION` and
  `HTON_TEMPORARY_NOT_SUPPORTED`,
- leave discovery hooks unset for this slice.

The handler should:

- initialize a `THR_LOCK` share in `open()`,
- return zero rows from table scans,
- return `HA_ERR_END_OF_FILE` from `rnd_next()`,
- return `HA_ERR_WRONG_COMMAND` for write, update, delete, random-position,
  and create operations,
- report no indexes,
- avoid creating any engine-owned files.

The smoke should reuse the existing controlled embedded startup arguments and
query:

```sql
SELECT ENGINE, SUPPORT
FROM information_schema.ENGINES
WHERE ENGINE = 'MYLITE'
```

The smoke fails if the row is missing. It should not run user DDL.

## Affected Subsystems

- MariaDB plugin build configuration through a new MyLite-owned storage
  subdirectory.
- Static embedded plugin registration.
- MyLite smoke targets and wrapper scripts.

No SQL parser, DDL routing, table discovery, row storage, file format, or public
`libmylite` API behavior should change.

## DDL Metadata Routing Impact

This slice intentionally avoids DDL success. A successful `CREATE TABLE` path
can still create `.frm` sidecars before MyLite has a catalog interception
strategy. The handler `create()` method should return unsupported, and the
smoke should test registration through `information_schema.ENGINES` instead of
DDL.

## Single-File And Embedded-Lifecycle Implications

The engine skeleton must create no durable engine-owned files. Existing embedded
startup side effects from Aria and server-table initialization remain
documented compatibility artifacts. Adding the plugin to the static embedded
profile increases binary size and should be measured.

## Public API Or File-Format Impact

None. No public API entry point or `.mylite` file format changes in this slice.

## Binary-Size Impact

The static engine object code will be linked into `libmariadbd.a` and into the
smoke executable. Record measured size changes after implementation.

## License, Trademark, And Dependency Impact

No new dependency. New MyLite storage-engine files use GPL-2.0-only licensing.

## Test And Verification Plan

- Run `tools/run-storage-engine-smoke.sh`.
- Verify `information_schema.ENGINES` contains a `MYLITE` row.
- Verify the smoke report records the engine support value.
- Verify no dynamic plugin artifacts are created.
- Run `tools/run-libmylite-open-close-smoke.sh` to ensure the public API smoke
  still links and passes with the new static engine.
- Run `tools/run-embedded-bootstrap-smoke.sh` to ensure the prior smoke still
  passes.
- Run `bash -n` for changed shell scripts.
- Run `git diff --check`.

## Acceptance Criteria

- `MYLITE` is built into the embedded minimal profile as a static storage
  engine.
- The storage-engine smoke proves MariaDB can see the engine through
  `information_schema.ENGINES`.
- The skeleton handler creates no engine-owned files and does not successfully
  create user tables.
- The previous embedded bootstrap and `libmylite` open/close smokes still pass.
- The implementation records binary-size impact and current side effects.
- MyLite-owned code stays isolated to the new storage subdirectory and smoke
  support.

## Risks And Unresolved Questions

- Static plugin registration can perturb built-in plugin ordering or binary
  size. The build report and smokes must catch this.
- A handler that is visible but not yet useful can confuse future tests if DDL
  accidentally succeeds. The create path must fail deliberately until
  `ddl-metadata-routing`.
- The exact MariaDB error surfaced by failed MyLite DDL is not part of this
  slice's public contract.
