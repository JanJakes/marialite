# libmylite-open-close

## Problem Statement

MyLite now has an embedded MariaDB bootstrap smoke, but applications still have
no MyLite-owned public API. The next slice should introduce the first small
`libmylite` C surface for opening and closing a database path with explicit
handle ownership and handle-owned diagnostics.

This is not the final storage engine or file format. The API must be honest
that it currently wraps MariaDB's embedded runtime and still observes temporary
compatibility side effects while later storage slices replace the datadir-shaped
parts of startup.

## Scope

- Add a public C header for opaque `mylite_db` handles.
- Implement:
  - `mylite_open()`
  - `mylite_open_v2()`
  - `mylite_close()`
  - `mylite_errcode()`
  - `mylite_extended_errcode()`
  - `mylite_mariadb_errno()`
  - `mylite_sqlstate()`
  - `mylite_errmsg()`
- Add the initial result-code and open-flag definitions needed by those
  functions.
- Add a static `libmylite` build target in the MyLite-owned CMake module.
- Add an open/close smoke executable and wrapper script that verifies lifecycle
  behavior in the existing Docker minimal embedded build.
- Record observed primary-file and temporary-runtime side effects in the smoke
  report.

## Non-Goals

- Do not implement SQL execution, prepared statements, column access, memory
  allocation APIs, or warning enumeration.
- Do not expose `MYSQL *` as the public API.
- Do not claim the `.mylite` file has a durable format.
- Do not implement the MyLite storage engine, DDL metadata routing, catalog,
  crash recovery, or cross-process locking.
- Do not support multiple different database paths at once. MariaDB's embedded
  server bootstrap is still process-global in this baseline.
- Do not remove the temporary Aria or `mysql.servers` startup side effects in
  this slice.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/include/mysql.h` declares
  `mysql_server_init()` and `mysql_server_end()` and aliases them as
  `mysql_library_init()` and `mysql_library_end()`.
- `vendor/mariadb/server/libmysqld/libmysql.c:mysql_server_init()` initializes
  client-library globals, client plugins, default ports, SIGPIPE handling, and
  calls `init_embedded_server()` when `EMBEDDED_LIBRARY` is defined.
- `vendor/mariadb/server/libmysqld/libmysql.c:mysql_server_end()` deinitializes
  plugins, client errors, SSL state, and the embedded server, then resets the
  global `mysql_client_init` flag.
- `vendor/mariadb/server/libmysqld/lib_sql.cc:init_embedded_server()` performs
  server-shaped startup: option defaults, common variables, datadir and tmpdir,
  SSL, server components, ACL/grants, time zones, UDFs, replication filters,
  init-file execution, and DDL recovery.
- `vendor/mariadb/server/libmysqld/libmysqld.c:mysql_real_connect()` chooses
  embedded methods for local or omitted hosts, creates an embedded `THD`, and
  verifies the embedded connection without using a network handshake.
- The existing MyLite bootstrap smoke proves the minimal argument set required
  for a controlled embedded runtime and records the current side effects:
  `aria_log.00000001`, `aria_log_control`, and the startup
  `mysql.servers` diagnostic.

## Proposed Design

Create a MyLite-owned static library target under
`vendor/mariadb/server/mylite/` with a public header under the same module. The
first ABI is intentionally small and C-compatible:

```c
typedef struct mylite_db mylite_db;

int mylite_open(const char *filename, mylite_db **out_db);
int mylite_open_v2(
    const char *filename,
    mylite_db **out_db,
    unsigned flags,
    const char *profile);
int mylite_close(mylite_db *db);

int mylite_errcode(mylite_db *db);
int mylite_extended_errcode(mylite_db *db);
unsigned mylite_mariadb_errno(mylite_db *db);
const char *mylite_sqlstate(mylite_db *db);
const char *mylite_errmsg(mylite_db *db);
```

`mylite_open()` should call `mylite_open_v2()` with
`MYLITE_OPEN_READWRITE | MYLITE_OPEN_CREATE`. `mylite_open_v2()` should support
only these first-slice profiles and flags:

- profile `NULL` or `"default"`,
- exactly one of `MYLITE_OPEN_READONLY` or `MYLITE_OPEN_READWRITE`,
- optional `MYLITE_OPEN_CREATE` with read-write opens.

The function should allocate a handle early when `out_db` is valid so open
failures can carry diagnostics on the returned handle. Invalid `out_db` remains
a misuse error without handle-owned diagnostics.

For the current compatibility baseline, opening should:

1. validate the path and flags,
2. create or validate the primary path according to the requested flags,
3. derive a temporary runtime directory from the absolute database path,
4. create runtime `datadir` and `tmp` directories,
5. start MariaDB's embedded runtime with the same controlled defaults used by
   the bootstrap smoke,
6. create one embedded `MYSQL` connection for the handle.

Because `mysql_server_init()` is process-global, this slice should allow
multiple open handles only for the same database path. A second open for a
different path after the embedded runtime has been initialized should return
`MYLITE_BUSY` with handle-owned diagnostics.

Implementation found that calling `mysql_server_end()` and then
`mysql_server_init()` again in the same process segfaults in MariaDB's
`init_common_variables()` path. The first API therefore keeps the embedded
runtime process-scoped after the first successful initialization, while
`mylite_close()` releases the handle's `MYSQL` connection. The process-scoped
runtime is ended through an `atexit()` hook.

The temporary runtime directory is a bootstrap compatibility artifact, not the
target single-file storage design. The smoke report must record it explicitly.

## Affected Subsystems

- MyLite-owned C API header and implementation.
- MyLite CMake module.
- Open/close smoke target and wrapper script.
- API documentation and roadmap status.

No MariaDB parser, optimizer, storage engine, SQL command dispatch, or file
format behavior should change in this slice.

## DDL Metadata Routing Impact

None. This slice does not execute user DDL and does not change how MariaDB
stores table definitions. DDL sidecar elimination remains the
`ddl-metadata-routing` slice.

## Single-File And Embedded-Lifecycle Implications

The public API becomes file-oriented, but the implementation is still a
compatibility bootstrap. It may create a zero-byte primary path and a
deterministically named temporary runtime directory next to it. Those side
effects are not final `.mylite` storage and must be reported by the smoke.

The lifecycle invariant for this slice is:

- successful `mylite_open()` owns one `MYSQL` connection and one runtime
  reference,
- `mylite_close()` releases that connection,
- the embedded MariaDB runtime remains process-scoped after the first
  successful initialization and is ended at process exit,
- failed opens return a handle where possible so diagnostics remain
  handle-owned,
- `mylite_close(NULL)` is tolerated.

## Public API Or File-Format Impact

This slice introduces the first public `libmylite` ABI. The ABI is incomplete
and should be treated as early development, but it establishes the naming,
opaque handle, result-code, flag, and diagnostic conventions for later slices.

No durable `.mylite` file format is introduced. The primary file is only a
placeholder path until the storage-engine, catalog, and recovery slices define
real contents.

## Binary-Size Impact

A new static `libmylite.a` wrapper and open/close smoke target will be built.
The MariaDB embedded library should change little or not at all because this
slice adds first-party wrapper code rather than new MariaDB subsystems. Record
the measured artifact sizes after implementation.

## License, Trademark, And Dependency Impact

No new dependency. New MyLite files use GPL-2.0-only licensing to match the
MariaDB-derived repository.

## Test And Verification Plan

- Run `tools/run-libmylite-open-close-smoke.sh`.
- Verify successful default `mylite_open()` and `mylite_close()`.
- Verify repeated open/close in one process.
- Verify two simultaneous handles for the same database path.
- Verify a simultaneous open for a different path returns `MYLITE_BUSY`.
- Verify invalid arguments, unsupported profiles, unsupported flag
  combinations, and read-only missing files return diagnostics.
- Verify `mylite_close(NULL)` is tolerated.
- Verify the report records primary-file and runtime side effects.
- Verify dynamic plugin artifacts remain absent.
- Run `tools/run-embedded-bootstrap-smoke.sh` to ensure the previous smoke still
  passes.
- Run `bash -n` for changed shell scripts.
- Run `git diff --check`.

## Acceptance Criteria

- Public `libmylite` open/close and diagnostic symbols build in the minimal
  embedded Docker profile.
- The public header is C and C++ compatible and does not expose `MYSQL *`.
- Successful open/close, repeated lifecycle, same-path multi-handle lifecycle,
  and different-path busy behavior are tested.
- Open failures return handle-owned diagnostics when `out_db` is valid.
- The implementation documents and reports current temporary runtime side
  effects without claiming final single-file storage.
- The implementation documents the process-scoped runtime constraint observed
  during repeated embedded initialization.
- The existing embedded bootstrap smoke still passes.
- The upstream MariaDB source delta remains absent or narrow; MyLite-owned code
  lives under the MyLite module.

## Implementation Result

The first `libmylite` target builds as a static library and the open/close smoke
passes:

```sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
```

The smoke verifies:

- `mylite_close(NULL)`,
- invalid `out_db`,
- missing filename with handle-owned diagnostics,
- read-only missing file diagnostics,
- unsupported profile diagnostics,
- unsupported flag diagnostics,
- default open/close,
- repeated open/close in one process,
- two simultaneous handles for the same path,
- `MYLITE_BUSY` for a different path after runtime initialization.

Observed artifacts:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,133,780 bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 29,530 bytes.
- `build/mariadb-minsize/mylite/mylite-open-close-smoke`: 22,613,144 bytes.
- Primary placeholder file: `open-close.mylite`, 0 bytes.
- Temporary compatibility files:
  `open-close.mylite.mylite-runtime/datadir/aria_log.00000001` and
  `open-close.mylite.mylite-runtime/datadir/aria_log_control`.
- Dynamic plugin artifacts: none.
- The pre-existing `mysql.servers` startup diagnostic remains.

## Risks And Unresolved Questions

- Repeated `mysql_server_init()` after `mysql_server_end()` in one process
  currently segfaults inside inherited MariaDB startup code. This slice avoids
  restart by keeping the embedded runtime process-scoped; a later bootstrap
  cleanup slice can decide whether a narrower upstream fix is worth carrying.
- The derived runtime directory is a temporary compatibility artifact. Later
  storage work must replace it with real MyLite file ownership.
- Path identity is initially conservative. Symlink and case-folding behavior can
  be tightened when locking and file ownership are designed.
- Diagnostics are first-pass mappings over MariaDB and local misuse errors. A
  fuller stable error taxonomy belongs with execution and storage slices.
