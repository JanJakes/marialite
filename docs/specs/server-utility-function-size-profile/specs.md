# server-utility-function-size-profile

## Problem Statement

The MyLite minsize profile still registers several MariaDB SQL functions whose
behavior is primarily useful for daemon administration, replication state,
server-host file access, benchmarking, or deliberate connection delay. These
functions do not fit the default embedded, file-owned MyLite profile and still
leave item classes, builders, vtables, and helper code live in the linked
runtime.

This slice removes those native function builders from the aggressive minsize
profile and records the binary-size effect.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Official MariaDB documentation:
  - `BENCHMARK()` executes an expression repeatedly for timing expression
    evaluation:
    <https://mariadb.com/docs/server/reference/sql-functions/secondary-functions/information-functions/benchmark>.
  - `SLEEP()` pauses the current statement:
    <https://mariadb.com/docs/server/reference/sql-functions/secondary-functions/miscellaneous-functions/sleep>.
  - `GET_LOCK()`, `IS_FREE_LOCK()`, `IS_USED_LOCK()`, `RELEASE_LOCK()`, and
    `RELEASE_ALL_LOCKS()` are server-wide user-level lock helpers:
    <https://mariadb.com/kb/en/get_lock/>,
    <https://mariadb.com/docs/server/reference/sql-functions/secondary-functions/miscellaneous-functions/is_free_lock>,
    <https://mariadb.com/kb/en/release_lock/>, and
    <https://mariadb.com/docs/server/reference/sql-functions/secondary-functions/miscellaneous-functions/release_all_locks>.
  - `LOAD_FILE()` reads a file from the server host and requires FILE
    privilege and `secure_file_priv` checks:
    <https://mariadb.com/docs/server/reference/sql-functions/string-functions/load_file>.
  - `BINLOG_GTID_POS()`, `MASTER_GTID_WAIT()`, and `MASTER_POS_WAIT()` are
    binary-log or replication-position functions:
    <https://mariadb.com/docs/server/reference/sql-functions/secondary-functions/information-functions/binlog_gtid_pos>,
    <https://mariadb.com/docs/server/reference/sql-functions/secondary-functions/miscellaneous-functions/master_gtid_wait>,
    and
    <https://mariadb.com/docs/server/reference/sql-functions/secondary-functions/miscellaneous-functions/master_pos_wait>.
  - `UUID_SHORT()` depends on `server_id`, server startup time, and a process
    counter:
    <https://mariadb.com/docs/server/reference/sql-functions/secondary-functions/miscellaneous-functions/uuid_short>.
- `vendor/mariadb/server/sql/item_create.cc` declares and registers native
  builders for `BENCHMARK`, `BINLOG_GTID_POS`, `GET_LOCK`, `IS_FREE_LOCK`,
  `IS_USED_LOCK`, `LOAD_FILE`, `MASTER_GTID_WAIT`, `MASTER_POS_WAIT`,
  `RELEASE_ALL_LOCKS`, `RELEASE_LOCK`, `SLEEP`, and `UUID_SHORT`.
- `vendor/mariadb/server/sql/item_func.h` and
  `vendor/mariadb/server/sql/item_func.cc` implement the item classes for
  `BENCHMARK`, `SLEEP`, user-level lock functions, replication waits, and
  `UUID_SHORT`.
- `vendor/mariadb/server/sql/item_strfunc.h` and
  `vendor/mariadb/server/sql/item_strfunc.cc` implement `BINLOG_GTID_POS()` and
  `LOAD_FILE()`.
- `vendor/mariadb/server/sql/item.cc` still initializes sleep and UUID_SHORT
  support helpers unconditionally. `pause_execution()` is also used by
  retained JSON code, and `server_uuid_value()` is used by `ddl_log.cc`, so this
  slice must not remove those shared helpers.

Current linked-symbol inspection shows live builders, item vtables, and runtime
methods for the targeted functions in
`build/mariadb-minsize-oracle-functions/mylite/mylite-open-close-smoke`.

## Scope

This slice will add an aggressive minsize option:

```text
MYLITE_DISABLE_SERVER_UTILITY_FUNCTIONS=ON
```

When enabled, the minsize embedded profile will omit native function builders
and item class implementations for:

- `BENCHMARK()`
- `BINLOG_GTID_POS()`
- `GET_LOCK()`
- `IS_FREE_LOCK()`
- `IS_USED_LOCK()`
- `LOAD_FILE()`
- `MASTER_GTID_WAIT()`
- `MASTER_POS_WAIT()`
- `RELEASE_ALL_LOCKS()`
- `RELEASE_LOCK()`
- `SLEEP()`
- `UUID_SHORT()`

The functions should fail through MariaDB's unknown-function path, matching the
current XML, GIS, vector, JSON schema, and Oracle function minsize behavior.

## Non-Goals

- Do not remove `UUID()`, `UUID_v4()`, `UUID_v7()`, `RANDOM_BYTES()`, normal
  diagnostic functions such as `ROW_COUNT()` and `LAST_INSERT_ID()`, or normal
  string/date/numeric functions.
- Do not remove `pause_execution()` or sleep mutex initialization while retained
  JSON code can still call it.
- Do not remove `uuid_short_init()` or `server_uuid_value()` because DDL logging
  still uses `server_uuid_value()`.
- Do not remove user-level lock cleanup hooks in `THD`; they are shared session
  cleanup infrastructure even if the SQL constructors are omitted.
- Do not claim replication/binlog source removal. This slice only removes the
  user-facing SQL functions listed above.

## Proposed Design

1. Add `MYLITE_DISABLE_SERVER_UTILITY_FUNCTIONS` to
   `vendor/mariadb/server/libmysqld/CMakeLists.txt` and define
   `MYLITE_DISABLE_SERVER_UTILITY_FUNCTIONS` when enabled.
2. Enable the option in `tools/build-mariadb-minsize.sh`.
3. Guard the targeted `Create_func_*` builder classes, singleton definitions,
   builder methods, and `func_array` entries in `item_create.cc`.
4. Guard the targeted `Item_*` class declarations in `item_func.h` and
   `item_strfunc.h`.
5. Guard the corresponding method definitions in `item_func.cc` and
   `item_strfunc.cc`.
6. Extend the `libmylite` open/close smoke to verify representative function
   names fail as unknown functions while retained normal functions still work.

The guard should be source-local and easy to rebase. It should not alter parser
grammar, ordinary expression execution, or the MyLite C API.

## Affected MariaDB Subsystems

- Embedded minsize build profile.
- Native SQL function registry in `item_create.cc`.
- SQL item class definitions in `item_func.*` and `item_strfunc.*`.
- `libmylite` smoke coverage for minsize unsupported SQL surfaces.

## DDL Metadata Routing Impact

No DDL metadata behavior changes. These functions are scalar expression
surfaces and must not create or alter MyLite catalog entries.

## Single-File And Embedded-Lifecycle Impact

The slice removes functions that either access server-global state, read files
from the server host, block a connection deliberately, or depend on
replication/binlog state. The MyLite runtime must continue to open, execute
ordinary SQL, close, and produce no unexpected durable sidecars.

## Public API Or File-Format Impact

No public `libmylite` C API change and no `.mylite` file-format change.

SQL compatibility impact: the aggressive minsize profile no longer supports the
listed functions. This is an intentional embedded-profile compatibility tradeoff
and should be recorded in production size analysis.

## Binary-Size Impact

Expected savings are modest. The current linked smoke contains live vtables and
methods for the targeted item classes. Section GC should drop most guarded
classes from the final linked runtime, and guarding method bodies should also
reduce the stripped static archive.

The implementation must record:

- stripped `libmariadbd.a` size,
- archive object count,
- unstripped linked `mylite-open-close-smoke` size,
- stripped linked `mylite-open-close-smoke` size,
- `size` section totals when available,
- symbol evidence that targeted item classes/builders are absent.

## License, Trademark, And Dependency Impact

No new dependencies and no license or trademark changes. The slice removes
MariaDB-derived code from the minsize profile through a MyLite-owned build
option.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-server-utility-functions \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-server-utility-functions \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-server-utility-functions \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
git diff --check
```

Inspect:

```sh
nm -C --size-sort \
  build/mariadb-minsize-server-utility-functions/mylite/mylite-open-close-smoke |
  grep -E 'Item_func_(benchmark|sleep|get_lock|release_all_locks|release_lock|is_free_lock|is_used_lock|uuid_short)|Item_master_(pos|gtid)_wait|Item_load_file|Item_func_binlog_gtid_pos|Create_func_(benchmark|sleep|get_lock|release_all_locks|release_lock|is_free_lock|is_used_lock|uuid_short|load_file|master_pos_wait|master_gtid_wait|binlog_gtid_pos)'
```

The grep should return no targeted item or builder symbols in the minsize
linked smoke.

## Acceptance Criteria

- The minsize profile builds with
  `MYLITE_DISABLE_SERVER_UTILITY_FUNCTIONS=ON`.
- Existing open/close, storage, compatibility, transaction, recovery, and
  sidecar checks pass.
- Representative targeted functions fail as unknown functions through
  `libmylite`.
- Ordinary retained functions still execute.
- Production size analysis records measured archive and stripped linked
  deltas.
- The diff stays focused to build flags, native function registration, item
  class guards, smoke tests, and docs.

## Risks And Unresolved Questions

- Some shared helpers for removed functions remain live through other retained
  code paths, limiting linked savings.
- `UUID_SHORT()` helper state cannot be removed in this slice because
  `ddl_log.cc` uses `server_uuid_value()`.
- Removing `GET_LOCK()` builders does not remove all user-level-lock cleanup
  code because `THD` lifecycle still references it.
- `SLEEP()` removal may not remove `pause_execution()` while JSON code retains
  it for debug or timeout behavior.
