# Direct MyLite Dispatch Size Profile

## Problem Statement

The public MyLite C API intentionally hides MariaDB `MYSQL *`, `MYSQL_RES *`,
`MYSQL_STMT *`, and `MYSQL_BIND` handles. The current implementation still uses
those inherited embedded C API types internally because they were the fastest
way to prove open/close, `mylite_exec()`, warnings, prepared statements, and
parameter binding.

That compatibility layer now looks like one of the few remaining size and
architecture candidates with meaningful upside. This slice specifies replacing
the internal MyLite execution path with a first-party dispatch/result adapter
over MariaDB `THD`, `dispatch_command()`, diagnostics, and prepared-statement
internals, then omitting the inherited client C API wrapper roots that are no
longer needed by MyLite.

## Source Findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

MariaDB documentation:

- MariaDB's embedded interface documentation describes `libmysqld` as exposing
  the same C API as the normal client library, so applications can link the
  embedded library and use `mysql.h`.
  Source: <https://mariadb.com/kb/en/embedded-mariadb-interface/>
- MariaDB Connector/C documents `MYSQL`, `MYSQL_RES`, and `MYSQL_STMT` as
  opaque client data structures, with result storage and statement lifecycle
  owned by the client API.
  Source:
  <https://mariadb.com/docs/connectors/mariadb-connector-c/mariadb-connectorc-data-structures>
- `mysql_real_query()` is the binary-safe client C API statement execution
  entry point, and `mysql_store_result()` buffers result sets from the last
  executed query.
  Sources:
  <https://mariadb.com/docs/connectors/mariadb-connector-c/api-functions/mysql_real_query>,
  <https://mariadb.com/docs/connectors/mariadb-connector-c/api-functions/mysql_store_result>
- Connector/C prepared statement docs define the `mysql_stmt_init()` /
  `mysql_stmt_prepare()` / `mysql_stmt_execute()` / `mysql_stmt_store_result()`
  path used by the current MyLite wrapper.
  Sources:
  <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_init>,
  <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_prepare>,
  <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_execute>,
  <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_store_result>

Relevant local source paths:

- `docs/api/libmylite-c-api.md` says the primary API should not expose
  `MYSQL *`; a future compatibility adapter may expose one separately.
- `vendor/mariadb/server/mylite/mylite.cc:32` stores `MYSQL *` in
  `mylite_db`.
- `vendor/mariadb/server/mylite/mylite.cc:78` stores `MYSQL_STMT *`,
  `MYSQL_RES *`, and `MYSQL_BIND` arrays in `mylite_stmt`.
- Before Stage 1, `vendor/mariadb/server/mylite/mylite.cc:283` implemented
  `mylite_exec()` with `mysql_real_query()`, `mysql_field_count()`,
  `mysql_store_result()`, `mysql_fetch_row()`, and `mysql_free_result()`.
- Before Stage 1, `vendor/mariadb/server/mylite/mylite.cc:389` implemented
  `mylite_warning()` by running `SHOW WARNINGS` through the same `MYSQL *`
  connection, then restoring copied `MYSQL` statement-effect fields.
- `vendor/mariadb/server/mylite/mylite.cc:448` implements `mylite_prepare()`
  with `mysql_stmt_init()`, `mysql_stmt_prepare()`,
  `mysql_stmt_result_metadata()`, and `mysql_stmt_param_count()`.
- `vendor/mariadb/server/mylite/mylite.cc:1276` executes prepared statements
  with `MYSQL_BIND`, `mysql_stmt_bind_param()`, `mysql_stmt_execute()`,
  `mysql_stmt_store_result()`, `mysql_stmt_bind_result()`,
  `mysql_stmt_fetch()`, and `mysql_stmt_fetch_column()`.
- `vendor/mariadb/server/libmysqld/lib_sql.cc:107` implements
  `emb_advanced_command()`: it manages current `THD`, clears diagnostics and
  previous result data, calls `dispatch_command()`, and restores globals.
- `vendor/mariadb/server/libmysqld/lib_sql.cc:296` reads embedded query
  results from `THD::first_data` into `MYSQL` fields.
- `vendor/mariadb/server/libmysqld/lib_sql.cc:337` executes embedded prepared
  statements by setting `THD::client_param_count` and `THD::client_params`,
  dispatching `COM_STMT_EXECUTE`, then reading query results.
- `vendor/mariadb/server/libmysqld/lib_sql.cc:484` registers
  `embedded_methods`, the function table used by the inherited `MYSQL` handle.
- `vendor/mariadb/server/libmysqld/lib_sql.cc:532` owns embedded server
  bootstrap through `init_embedded_server()`, and
  `vendor/mariadb/server/libmysqld/lib_sql.cc:705` creates embedded `THD`
  sessions.
- `vendor/mariadb/server/libmysqld/lib_sql.cc:944`,
  `vendor/mariadb/server/libmysqld/lib_sql.cc:1163`,
  `vendor/mariadb/server/libmysqld/lib_sql.cc:1293`, and
  `vendor/mariadb/server/libmysqld/lib_sql.cc:1368` implement the embedded
  result queue by overriding `Protocol` methods to populate `MYSQL_DATA`.
- `vendor/mariadb/server/libmysqld/libmysql.c:126` implements
  `mysql_server_init()`, which delegates to `init_embedded_server()`.
- `vendor/mariadb/server/libmysqld/libmysql.c:719` implements
  `mysql_query()` over `mysql_real_query()`, and the same file implements most
  client-side `mysql_stmt_*` wrapper behavior.
- `vendor/mariadb/server/libmysqld/libmysqld.c:76` implements embedded
  `mysql_real_connect()` over `cli_mysql_real_connect()`.
- `vendor/mariadb/server/sql/sql_parse.cc:1601` defines
  `dispatch_command()`, the retained server command entry point.
- `vendor/mariadb/server/sql/sql_prepare.cc:160` defines the internal
  `Prepared_statement` class, and `vendor/mariadb/server/sql/sql_prepare.cc:4099`
  implements `Prepared_statement::prepare()`.

Current size evidence from
`build/mariadb-minsize-no-eh-frame-header`:

| Object | Bytes | Current role |
| --- | ---: | --- |
| `libmysqld/CMakeFiles/sql_embedded.dir/lib_sql.cc.o` | 468,784 | Embedded bootstrap, THD setup, `MYSQL` method table, result capture |
| `libmysqld/CMakeFiles/sql_embedded.dir/libmysql.c.o` | 76,648 | `mysql_server_init()`, client option handling, result helpers, `mysql_stmt_*` wrappers |
| `libmysqld/CMakeFiles/sql_embedded.dir/__/sql-common/client.c.o` | 67,712 | Shared client connection/options/auth helper code |
| `libmysqld/CMakeFiles/sql_embedded.dir/__/sql/sql_prepare.cc.o` | 145,200 | Server prepared statement implementation |
| `libmysqld/CMakeFiles/sql_embedded.dir/__/sql/protocol.cc.o` | 42,528 | Generic protocol methods still used outside embedded result capture |
| `libmysqld/CMakeFiles/sql_embedded.dir/libmysqld.c.o` | 4,232 | Embedded `mysql_real_connect()` wrapper |
| `libmysqld/CMakeFiles/sql_embedded.dir/__/sql-common/client_plugin.c.o` | 12,680 | Client plugin helper code rooted through client API |

The related archive upper bound is about 0.78 MiB, but not all of that is
removable:

- `lib_sql.cc.o` also owns embedded runtime bootstrap and `THD` creation, so
  replacing `MYSQL *` in `mylite.cc` alone will not remove it.
- `sql_prepare.cc.o` is still needed if MyLite preserves real MariaDB prepared
  statement semantics instead of expanding parameters into SQL text.
- `protocol.cc.o` is shared server protocol infrastructure and should not be
  assumed removable.

The currently linked stripped smoke has about 26 KiB of directly visible
`mysql_*`, `mysql_stmt_*`, `Prepared_statement`, `Protocol`, and
`dispatch_command()` symbols from this surface. That number is a lower bound;
removing object roots can also drop associated data, vtables, option helpers,
and relocation metadata.

## Design

Implement this as a staged direct-dispatch bridge rather than a single broad
rewrite.

### Stage 1: Direct query/session bridge

Add a MyLite-owned internal embedded session bridge in the MariaDB server tree,
for example `libmysqld/mylite_direct_dispatch.cc`, with a small C++ interface
used only by `vendor/mariadb/server/mylite/mylite.cc`.

The bridge should:

- start and stop the process-wide embedded runtime through
  `init_embedded_server()` and `end_embedded_server()` rather than
  `mysql_server_init()` and `mysql_server_end()`;
- create one embedded `THD` per `mylite_db` through `create_embedded_thd()`;
- perform the same global save/restore, statement reset, thread-stack, and
  current-THD handling currently centralized in `emb_advanced_command()`;
- dispatch one-shot SQL through `dispatch_command(COM_QUERY, thd, sql, len + 1,
  true)`;
- expose a first-party result object with column names, column types, nullable
  row values, affected rows, insert id, server status, warning count, MariaDB
  errno, SQLSTATE, and message text;
- make `mylite_exec()`, `mylite_changes()`, `mylite_last_insert_id()`,
  `mylite_warning_count()`, and `mylite_warning()` consume that result and
  diagnostic state without touching `MYSQL`.

Result capture should not fake a `MYSQL` handle. The preferred design is a
small `Protocol` subclass or bridge that records the same information MyLite
needs today, avoiding `MYSQL_DATA`, `MYSQL_RES`, `MYSQL_FIELD`, and
`MYSQL_ROWS` as internal public-API-shaped storage. If the first implementation
must reuse part of `lib_sql.cc` result capture, it should be treated as an
intermediate step and measured separately.

### Stage 2: Direct prepared-statement bridge

Prepared statements are harder because MariaDB's `Prepared_statement` class is
defined inside `sql_prepare.cc`, while the current public MyLite wrapper relies
on the exported `mysql_stmt_*` client facade.

Add a narrow MyLite bridge inside `sql_prepare.cc` or an adjacent
upstream-derived file that can access the internal prepared statement class
without exposing it publicly. The bridge should:

- prepare SQL with MariaDB's internal prepared-statement path so `?` marker
  syntax, metadata, type resolution, and diagnostics remain MariaDB-derived;
- store parameter values in MyLite-owned storage, not `MYSQL_BIND`;
- map MyLite bind kinds to `Item_param` values using MariaDB's native parameter
  machinery rather than SQL string interpolation;
- execute through the same direct result-capture protocol as Stage 1;
- preserve reset/finalize ownership, close-busy semantics, warnings,
  affected rows, insert id, column names, column types, BLOB/TEXT bytes, and
  truncated-value handling.

Do not replace prepared statements with SQL literal expansion. That would be a
semantic workaround: it would change parsing boundaries, binary value handling,
character-set handling, statement reuse, diagnostics, and injection safety.

### Stage 3: Omit obsolete client C API roots

After both one-shot execution and prepared statements no longer reference
`MYSQL`, `MYSQL_RES`, `MYSQL_STMT`, or `MYSQL_BIND`, add a minsize profile
option such as `MYLITE_DISABLE_EMBEDDED_MYSQL_C_API`.

When enabled, the build should omit or stub the obsolete embedded client C API
objects that are no longer needed by MyLite:

- `libmysql.c.o`,
- `libmysqld.c.o`,
- `sql-common/client.c.o` when no other retained path needs it,
- `sql-common/client_plugin.c.o` when no retained path needs client plugins,
- the `MYSQL *` result-capture portions of `lib_sql.cc` after bootstrap and
  direct result capture are split.

This option must not remove `mysql_server_init()` compatibility in a default
MariaDB embedded build. It belongs only to the aggressive MyLite minsize
profile unless a future compatibility adapter reintroduces the MariaDB C API as
an optional target.

## Non-Goals

- Do not change the public `libmylite` C API.
- Do not expose MariaDB internal `THD`, `Protocol`, or `Prepared_statement`
  types through public MyLite headers.
- Do not remove MariaDB parser, analyzer, optimizer, execution, diagnostics,
  type, or collation semantics that ordinary supported SQL depends on.
- Do not use SQL string interpolation as a prepared-statement replacement.
- Do not claim MariaDB C API compatibility in the aggressive direct-dispatch
  profile.
- Do not solve multi-database-process restart semantics in this slice.

## Affected MariaDB Subsystems

- Embedded bootstrap: `init_embedded_server()`, `end_embedded_server()`,
  runtime globals, and MyLite runtime reference counting.
- Session lifecycle: `THD` creation, deletion, current-THD save/restore, and
  per-session diagnostics.
- SQL command dispatch: `dispatch_command()` for `COM_QUERY` and prepared
  statement command equivalents.
- Protocol/result delivery: `Protocol` metadata, row, OK, EOF, and error paths.
- Prepared statements: `sql_prepare.cc`, `Prepared_statement`, parameter
  binding, execution, result metadata, and result rows.
- Build profile: `libmysqld` object lists and MyLite minsize options.

## DDL Metadata Routing Impact

No DDL storage behavior should change. `CREATE`, `ALTER`, `DROP`, `RENAME`,
schema namespace operations, table discovery, and existing MyLite catalog
routing must continue through the same MariaDB SQL execution paths and MyLite
storage engine handlers.

The direct result adapter must preserve DDL statement effects and diagnostics
because existing smokes assert unsupported DDL rejection behavior and no
durable `.frm` sidecars.

## Single-File And Embedded-Lifecycle Impact

The slice should improve lifecycle ownership by removing a compatibility
connection object that was designed to mimic remote client behavior. The
replacement must keep current invariants:

- one initialized database path per process,
- process-local runtime remains alive after handle close,
- read-only and read-write runtime modes remain mutually exclusive,
- primary-file locks remain attached to the MyLite storage-engine lifetime,
- no daemon socket, user account handshake, or network connection is exposed,
- existing unexpected-sidecar checks remain green.

## Public API Or File-Format Impact

No public API or `.mylite` file-format change is intended. This is an internal
execution bridge and size-profile change.

If a future MariaDB C API compatibility adapter is needed, it should be a
separate optional target rather than a dependency of the core `libmylite` API.

## Binary-Size Impact

Expected size impact must be measured per stage.

Stage 1 may only save a small linked amount if it still needs `lib_sql.cc` and
`MYSQL` result capture. Stage 3 is where archive savings become meaningful.
Current object upper bounds from the no-EH-header build are:

- direct client wrapper candidates: about 0.16 MiB archive
  (`libmysql.c.o`, `libmysqld.c.o`, `client.c.o`, `client_plugin.c.o`),
- larger embedded bridge and prepared-statement candidates: up to about
  0.64 MiB archive, but only after bootstrap/result capture are split and
  prepared statements have a direct bridge,
- stripped linked runtime: unknown until measured; current visible symbol-size
  evidence suggests the first safe stage may be tens of KiB, while a complete
  bridge split could plausibly reach low six figures.

The acceptance bar is measured output, not the upper-bound estimate.

## License, Trademark, And Dependency Impact

No new dependency. All changes remain inside the GPL-2.0-only MariaDB-derived
tree.

Removing the MariaDB C API from the aggressive core profile is a compatibility
and packaging decision, not a license change. Public packaging must still avoid
implying affiliation with MariaDB or MySQL.

## Test And Verification Plan

Stage 1:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-dispatch \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-dispatch \
  tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-dispatch \
  tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-dispatch \
  tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-dispatch \
  tools/run-compatibility-test-harness.sh
```

Additional checks:

- compare `mylite_exec()` callbacks for column names, NULLs, and BLOB/TEXT
  bytes against the current `MYSQL_RES` path;
- verify `mylite_changes()`, `mylite_last_insert_id()`,
  `mylite_warning_count()`, and `mylite_warning()` keep current behavior;
- verify errors preserve public MyLite code, MariaDB errno, SQLSTATE, and
  message expectations for supported and unsupported statements;
- verify repeated open/close, close-busy, read-only, exclusive, and URI open
  cases remain green;
- verify sidecar scan stays clean.

Stage 2:

- run all prepared-statement and parameter-binding smoke sections;
- add focused prepared tests for NULL, signed/unsigned 64-bit, double, text,
  embedded-NUL BLOB, BLOB/TEXT result bytes, truncation expansion, reset, and
  finalize;
- compare selected prepared behavior to the MariaDB reference runtime where the
  current harness has coverage.

Stage 3:

- verify `nm` no longer shows retained `mysql_real_query`,
  `mysql_store_result`, `mysql_fetch_row`, `MYSQL_STMT` wrapper symbols, or
  client plugin helper roots in the aggressive linked smoke;
- record static archive, unstripped smoke, stripped smoke, section profile,
  and dynamic dependency deltas;
- run `git diff --check` and shell syntax checks for touched scripts.

## Acceptance Criteria

- Public `libmylite` API behavior remains compatible with current smokes and
  documented API semantics.
- No public MyLite handle contains or exposes `MYSQL *`, `MYSQL_RES *`,
  `MYSQL_STMT *`, or `MYSQL_BIND`.
- Current open/close, storage, embedded bootstrap, compatibility harness, and
  sidecar scans pass.
- Prepared statements keep real MariaDB parameter semantics rather than SQL
  interpolation.
- Obsolete client C API object roots are omitted only after both query and
  prepared paths no longer use them.
- Size deltas are recorded in this spec and in
  `docs/research/production-size-analysis.md`.

## Stage 1 Implementation Results

Stage 1 is implemented as an intermediate direct-dispatch bridge, not as the
final removal of inherited embedded client C API roots.

Implemented changes:

- added `libmysqld/mylite_direct_dispatch.h` and a small bridge in
  `libmysqld/lib_sql.cc`;
- routed `mylite_exec()` and `mylite_warning()` through
  `mylite_embedded_direct_query()`;
- preserved the existing embedded `MYSQL *` session and MariaDB `MYSQL_DATA`
  result capture for now;
- mirrored `emb_read_rows()` row-list termination before exposing direct
  results to MyLite;
- copied column names and row values into callback-local storage before calling
  the public MyLite callback;
- stored statement effects on `mylite_db` so `mylite_changes()`,
  `mylite_last_insert_id()`, and `mylite_warning_count()` no longer depend on
  public client C API accessors for one-shot statements.

Measured against `build/mariadb-minsize-no-binlog-object-init`:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 25,280,988 | 25,283,578 | +2,590 |
| `mylite/libmylite.a` | 76,138 | 76,688 | +550 |
| unstripped `mylite-open-close-smoke` | 6,437,912 | 6,436,248 | -1,664 |
| stripped `mylite-open-close-smoke` | 4,525,904 | 4,524,384 | -1,520 |
| `size` decimal total | 4,739,978 | 4,735,838 | -4,140 |

Verification:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-dispatch \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-dispatch \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-dispatch \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-dispatch \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-dispatch \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh \
  tools/run-libmylite-open-close-smoke.sh \
  tools/run-embedded-bootstrap-smoke.sh \
  tools/run-storage-engine-smoke.sh \
  tools/run-compatibility-test-harness.sh
git diff --check
```

All commands pass after a clean rebuild of
`build/mariadb-minsize-direct-dispatch`. A prior failed parallel verification
run corrupted that generated CMake build directory because multiple scripts
reconfigured and stripped the same archive at the same time; the source changes
were unaffected, and verification was rerun sequentially after deleting the
generated build directory.

## Stage 2A Direct Session Results

Stage 2A replaces `libmylite`'s owned `MYSQL *` connection with an opaque
`MYLITE_EMBEDDED_DIRECT_SESSION` while preserving the existing embedded result
capture and the prepared-statement compatibility route when that API is
enabled. This is not the full Stage 2 prepared-statement bridge: the aggressive
minsize profile currently disables the public prepared-statement API, so the
measured win comes from removing inherited connect/query client C API roots
from linked `libmylite` consumers.

Implemented changes:

- added direct embedded server start/stop wrappers over `init_embedded_server()`
  and `end_embedded_server()`;
- added direct embedded session open/close wrappers that allocate the minimal
  embedded `MYSQL` shell needed by MariaDB `THD` setup;
- routed `mylite_open()`, `mylite_close()`, `mylite_exec()`,
  `mylite_warning()`, statement-effect accessors, and runtime startup through
  the direct session bridge;
- left prepared statements on the session's internal `MYSQL` handle when
  `MYLITE_DISABLE_PREPARED_STATEMENT_API` is off, so non-minsize builds keep
  the current compatibility path;
- added `MYLITE_DISABLE_EMBEDDED_MYSQL_C_API` to the minsize build profile.

Measured against `build/mariadb-minsize-lld-o2`:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 22,437,702 | 22,444,096 | +6,394 |
| `mylite/libmylite.a` | 76,680 | 75,696 | -984 |
| unstripped `mylite-open-close-smoke` | 5,855,640 | 5,844,824 | -10,816 |
| stripped `mylite-open-close-smoke` | 3,990,440 | 3,982,696 | -7,744 |
| `size` decimal total | 4,203,300 | 4,194,990 | -8,310 |
| PHP-shaped shared probe, unstripped | 5,729,736 | 5,409,056 | -320,680 |
| PHP-shaped shared probe, stripped | 3,881,776 | 3,650,336 | -231,440 |
| PHP-shaped shared probe, sectionless | 3,879,416 | 3,647,976 | -231,440 |

The new lowest measured PHP-shaped floor is 3,647,976 bytes
(3.48 MiB / 3.65 MB) for a section-header-stripped shared probe.

Linked symbol checks on `mylite-open-close-smoke` no longer show retained
`mysql_server_init`, `mysql_server_end`, `mysql_init`, `mysql_real_connect`,
`mysql_close`, `mysql_real_query`, `mysql_store_result`, `mysql_fetch_row`,
`mysql_free_result`, `mysql_stmt_*`, `free_old_query`, `net_clear_error`,
`embedded_methods`, or `emb_advanced_command` symbols. Only the tiny inherited
`mysql_client_plugin_init` and `mysql_client_plugin_deinit` stubs remain from
the checked client symbol set.

Verification:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-session \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-session \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-session \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-session \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-direct-session \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh \
  tools/run-libmylite-open-close-smoke.sh \
  tools/run-embedded-bootstrap-smoke.sh \
  tools/run-storage-engine-smoke.sh \
  tools/run-compatibility-test-harness.sh
git diff --check
```

All commands passed.

The prepared-enabled compatibility branch also compiled with:

```sh
docker run --rm -v "$PWD":/work -w /work \
  mylite-mariadb-minsize:ubuntu-24.04 \
  cmake --build build/mariadb-minsize-default-verify \
    --target mylite/libmylite.a --parallel 8
```

## Risks And Unresolved Questions

- `lib_sql.cc` mixes bootstrap, THD creation, `MYSQL` result capture, and C API
  glue. A clean size win likely requires splitting this file or adding a small
  MyLite-only bridge without destabilizing upstream-derived startup.
- `Prepared_statement` is local to `sql_prepare.cc`, so a direct prepared bridge
  must be implemented close to upstream internals. This increases fork
  maintenance cost.
- MariaDB protocol/result methods carry subtle OK/EOF/error, metadata, warning,
  and charset behavior. The first-party adapter must preserve the parts exposed
  through `libmylite`.
- Some inherited SQL paths may assume `thd->mysql` exists in embedded mode.
  Those assumptions need source-level audit and tests before the `MYSQL` handle
  can be removed completely.
- The size upside is less certain than pure removal slices. The first stage may
  be mostly architectural groundwork; the measurable win arrives only when the
  build can omit obsolete client C API objects.
