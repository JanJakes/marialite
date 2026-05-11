# embedded-bootstrap

## Problem Statement

MyLite now has a reproducible `libmariadbd.a` build, but it does not yet prove
that the embedded MariaDB runtime can be started, used, and stopped under
MyLite-owned defaults. The next slice needs a narrow bootstrap harness that
starts the embedded runtime in-process, runs a minimal SQL smoke query, shuts it
down cleanly, and records the remaining server-shaped assumptions.

This is not the public `libmylite` API. It is the private bootstrap evidence
needed before the open/close API can wrap the runtime honestly.

## Scope

- Add a MyLite-owned embedded bootstrap smoke target to the MariaDB build.
- Build that target inside the existing Linux-container minimal profile.
- Start MariaDB's embedded runtime with explicit MyLite-controlled arguments.
- Use a temporary datadir and temporary directory under the ignored build tree.
- Run a minimal in-process query through the embedded C API.
- Close the embedded connection and call the embedded shutdown path.
- Record startup arguments, observed files, and diagnostics in a build report.
- Fix only the build/link/runtime issues required for the smoke target.

## Non-Goals

- Do not introduce public `mylite_open()` or any stable ABI.
- Do not create or validate a `.mylite` file format.
- Do not implement a MyLite storage engine, catalog, DDL routing, table
  discovery, or row storage.
- Do not claim single-file behavior. This slice may use an upstream temporary
  datadir because it is measuring bootstrap, not final storage.
- Do not initialize normal MariaDB system tables as a product decision.
- Do not expose MariaDB's `MYSQL *` handle as the MyLite API.
- Do not enable networking, replication, dynamic plugins, grants, or daemon
  administration as supported MyLite surfaces.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/include/mysql.h` declares `mysql_server_init()` and
  `mysql_server_end()`, then aliases `mysql_library_init` and
  `mysql_library_end` to those functions.
- `vendor/mariadb/server/libmysqld/libmysql.c:mysql_server_init()` initializes
  the client library, client plugins, default port/socket environment, SIGPIPE
  handling, and then calls `init_embedded_server()` for embedded builds when
  `argc > -1`.
- `vendor/mariadb/server/libmysqld/lib_sql.cc:init_embedded_server()` includes
  `../sql/mysqld.cc` directly and runs a server-shaped bootstrap:
  `init_early_variables()`, `load_defaults("my", groups, ...)`,
  `sys_var_init()`, `handle_early_options()`, `init_common_variables()`,
  datadir/tmpdir setup, SSL setup, `init_server_components()`, ACL/grant
  initialization unless disabled, time-zone initialization, UDF initialization,
  replication filter allocation, optional init-file execution, and DDL recovery.
- `init_embedded_server()` defaults option groups to `server` and `embedded`.
  A MyLite bootstrap must pass `--no-defaults` explicitly so host `my.cnf`
  files do not leak into tests.
- `vendor/mariadb/server/sql/mysqld.cc:init_common_variables()` resets global
  server state for embedded restarts, initializes replication/binlog filters,
  sets `opt_log_basename`, derives the default datadir, parses command-line
  options, initializes handlers/plugins, and chooses the default storage engine.
- `vendor/mariadb/server/libmysqld/libmysqld.c:mysql_real_connect()` uses the
  embedded method table when the host is local or omitted. It creates an
  embedded `THD`, initializes character-set state, runs embedded connection
  checks, and never opens a TCP connection in that path.
- `vendor/mariadb/server/libmysqld/lib_sql.cc:create_embedded_thd()` creates
  the per-connection `THD`, stores/restores thread globals, initializes query
  state, and inserts the thread into `server_threads`.
- The current minimal static archive is not a complete executable link by
  itself. Building upstream embedded examples in the Docker profile failed with
  unresolved `tpool_wait_begin` and `tpool_wait_end`; those symbols live in
  `build/mariadb-minsize/tpool/libtpool.a`. The smoke target must either link
  `tpool` explicitly or the static archive merge must be adjusted.
- `vendor/mariadb/server/CMakeLists.txt` always adds `libmysqld/examples` and
  `unittest/embedded` when `WITH_EMBEDDED_SERVER` is enabled, but those
  upstream targets are not MyLite bootstrap tests and currently do not link in
  the minimal static profile without the `tpool` fix.

## Proposed Design

Add a MyLite-owned source directory inside the MariaDB build tree:

```text
vendor/mariadb/server/mylite/
```

Add an executable target:

```text
mylite-embedded-bootstrap-smoke
```

The top-level MariaDB CMake integration should be a narrow MyLite fork delta:
add `ADD_SUBDIRECTORY(mylite)` only when `WITH_EMBEDDED_SERVER` is enabled.
The MyLite CMake file should link the smoke target against `mysqlserver` and
the additional static dependencies needed by the minimal profile, starting with
`tpool`.

The smoke program should:

1. Accept `--datadir`, `--tmpdir`, `--lc-messages-dir`, and
   `--report=<path>` arguments from the wrapper script.
2. Create no durable files outside those directories.
3. Call `mysql_server_init()` with a controlled argument vector:
   - `--no-defaults`
   - `--datadir=<smoke datadir>`
   - `--tmpdir=<smoke tmpdir>`
   - `--lc-messages-dir=<build sql/share>`
   - `--skip-grant-tables`
   - `--skip-networking`
   - `--log-output=NONE`
   - `--pid-file=<smoke runtime>/mariadb.pid`
   - `--socket=<smoke runtime>/mariadb.sock`
4. Open an embedded connection with `mysql_init()` and local
   `mysql_real_connect()`.
5. Execute `SELECT 1`.
6. Verify one row and one column with value `1`.
7. Call `mysql_close()` and `mysql_server_end()`.
8. Write a concise report with the exact server arguments, query result, error
   code/message if any, and observed files under the smoke runtime directory.

Add a wrapper script:

```text
tools/run-embedded-bootstrap-smoke.sh
```

The wrapper should reuse the existing Docker image and build directory from
`tools/build-mariadb-minsize.sh`, build `mylite-embedded-bootstrap-smoke`, run
it inside the container, and write its report under:

```text
build/mariadb-minsize/mylite-embedded-bootstrap-report.txt
```

The wrapper should leave the smoke datadir under `build/mariadb-minsize/` for
inspection, because this slice is meant to record remaining upstream side
effects. Later slices can tighten cleanup and unexpected-sidecar assertions
once MyLite owns storage and DDL routing.

## Affected Subsystems

- MariaDB CMake integration for embedded builds.
- A new MyLite-owned source directory under the MariaDB source tree.
- Embedded client/server initialization path in tests, but not in runtime code
  yet.
- Build tooling under `tools/`.

No SQL parser, optimizer, execution, handler, DDL, storage, or public API
semantics should change in this slice.

## DDL Metadata Routing Impact

None. The smoke query must not run DDL. Any datadir files created by startup are
evidence for later bootstrap cleanup and storage slices, not a DDL routing
solution.

## Single-File And Embedded-Lifecycle Implications

This slice deliberately uses a temporary MariaDB datadir to prove controlled
process-local bootstrap. That datadir is not a MyLite database and is not a
product-compatible storage model.

The useful lifecycle evidence is:

- startup does not require a daemon or network connection,
- all temporary startup state is directed into a known runtime directory,
- shutdown returns control to the process without leaving an active embedded
  connection,
- observed files are recorded so later slices can eliminate or replace them.

The slice must not weaken the single-file target by presenting a virtual
datadir or directory bundle as the final design.

## Public API Or File-Format Impact

None. The smoke target may call MariaDB's C API internally, but no public
MyLite ABI or `.mylite` file format is introduced.

## Binary-Size Impact

The smoke executable is a development/test artifact, not part of the final
library surface. If the slice adjusts static linking by adding `tpool` to the
embedded static archive merge, record the new `libmariadbd.a` size and explain
the delta. Otherwise record only the smoke executable size.

## License, Trademark, And Dependency Impact

No new third-party dependency should be added. New MyLite-owned files in the
MariaDB-derived tree should use GPL-2.0-only project licensing. The smoke
target and scripts must avoid implying MariaDB or MySQL affiliation.

## Test And Verification Plan

- Run `tools/build-mariadb-minsize.sh` or verify the existing profile is
  current.
- Build `mylite-embedded-bootstrap-smoke` inside the Docker profile.
- Run `tools/run-embedded-bootstrap-smoke.sh`.
- Verify the report records:
  - controlled server arguments,
  - successful `mysql_server_init()`,
  - successful local embedded `mysql_real_connect()`,
  - successful `SELECT 1` result,
  - successful `mysql_server_end()`,
  - observed runtime files.
- Verify no `.so`, `.dylib`, or `.dll` plugin artifacts are created.
- Verify no files are created outside the smoke runtime directory except the
  build outputs.
- Run `bash -n` for shell scripts.
- Run `git diff --check`.

## Implementation Result

The slice added a MyLite-owned embedded smoke module under
`vendor/mariadb/server/mylite/` and wires it into embedded builds with a narrow
`ADD_SUBDIRECTORY(mylite)` fork delta in `vendor/mariadb/server/CMakeLists.txt`.
The smoke target links `mysqlserver` plus `tpool`, which is required by this
static minimal profile because `libmariadbd.a` does not merge `libtpool.a`.

The repeatable wrapper is:

```sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
```

The successful run produced:

- report: `build/mariadb-minsize/mylite-embedded-bootstrap-report.txt`
- smoke executable: `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`
- smoke executable size: 22,609,880 bytes
- `libmariadbd.a` size unchanged: 44,134,820 bytes
- SQL result: `SELECT 1` returned one row with value `1`
- dynamic plugin artifacts: none

The controlled startup arguments used absolute paths under `/work` inside the
container:

- `--no-defaults`
- `--datadir=/work/build/mariadb-minsize/mylite-embedded-bootstrap/datadir`
- `--tmpdir=/work/build/mariadb-minsize/mylite-embedded-bootstrap/tmp`
- `--lc-messages-dir=/work/build/mariadb-minsize/sql/share`
- `--skip-grant-tables`
- `--skip-networking`
- `--skip-name-resolve`
- `--skip-external-locking`
- `--skip-slave-start`
- `--log-output=NONE`
- `--pid-file=/work/build/mariadb-minsize/mylite-embedded-bootstrap/mariadb.pid`
- `--socket=/work/build/mariadb-minsize/mylite-embedded-bootstrap/mariadb.sock`

Observed runtime files under the smoke runtime directory:

- `datadir/aria_log.00000001` (16,384 bytes)
- `datadir/aria_log_control` (52 bytes)

MariaDB also emitted this startup diagnostic while still completing the smoke
successfully:

```text
Got ERROR: "Can't open and lock privilege tables: Table 'mysql.servers' doesn't exist" errno: 2000
```

That diagnostic comes from upstream server-table initialization and is evidence
for the later unsupported-server-surface and bootstrap-cleanup work. This slice
records it rather than creating normal MariaDB system tables as a product
decision.

## Acceptance Criteria

- A MyLite-owned embedded bootstrap smoke target exists and builds in the
  minimal Docker profile.
- The smoke target starts and stops the embedded runtime in-process with
  explicit MyLite-controlled arguments.
- The smoke target executes `SELECT 1` through the embedded path and validates
  the result.
- The wrapper script is repeatable from the repository root.
- The smoke report captures exact arguments, result, artifact paths, and
  observed runtime files.
- The implementation does not expose a public MyLite API, does not claim
  single-file storage, and does not enable daemon/network behavior.
- Any MariaDB CMake or source delta is narrow, documented, and isolated in the
  MyLite-owned module or in the minimum target wiring needed to build it.

## Risks And Unresolved Questions

- MariaDB embedded startup may require additional datadir scaffolding even with
  `--skip-grant-tables`. If so, document the smallest required files and keep
  them inside the smoke runtime directory.
- Some server options may still create error, DDL recovery, Aria, MyISAM, or
  pid/socket files. This slice records those effects; later slices remove or
  redirect them.
- Disabling grants with `--skip-grant-tables` is acceptable for a private smoke
  test, but the product needs an explicit unsupported-server-surface decision
  for users, authentication, and grants.
- If static linking requires adding `tpool` to `libmariadbd.a`, the size
  baseline from `build-profile-minsize` must be updated in the implementation
  result.
- The smoke target should avoid broad patches to `init_embedded_server()` until
  the observed startup side effects show a specific narrow change is needed.
