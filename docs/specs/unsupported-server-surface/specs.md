# unsupported-server-surface

## Problem Statement

The embedded bootstrap smoke now proves in-process startup and a trivial query,
but it also shows inherited server behavior that is not a MyLite product
surface. Some SQL commands already fail explicitly in embedded builds, while
others still enter dynamic plugin, UDF, or `mysql.*` server-table code and fail
accidentally because system tables or dynamic plugin support are absent.

This slice should convert the first set of daemon-oriented surfaces into
deliberate embedded rejections and test those rejections through the existing
bootstrap smoke.

## Scope

- Add explicit embedded-mode rejection for dynamic extension surfaces:
  `CREATE FUNCTION ... SONAME`, `INSTALL PLUGIN`, and `UNINSTALL PLUGIN`.
- Add explicit embedded-mode rejection for foreign-server metadata surfaces:
  `CREATE SERVER`, `ALTER SERVER`, and `DROP SERVER`.
- Extend the MyLite embedded bootstrap smoke to run those unsupported
  statements and verify that each fails through an explicit embedded diagnostic.
- Record the unsupported-surface results in the smoke report.
- Keep the rejection local to embedded builds with `#ifdef EMBEDDED_LIBRARY`.

## Non-Goals

- Do not design the final MyLite diagnostic enum or public error API.
- Do not implement public `libmylite` wrappers for unsupported features.
- Do not remove MariaDB parser support for these statements.
- Do not remove upstream plugin, UDF, or server-table code from non-embedded
  builds.
- Do not change `DROP FUNCTION` handling in this slice. It is shared with
  stored function handling and needs a separate compatibility decision.
- Do not solve grants, users, replication, events, or all administrative SQL in
  this slice. Those need broader compatibility and API policy decisions.
- Do not create MariaDB system tables to satisfy server-oriented commands.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/sql_parse.cc:mysql_execute_command()` already has
  embedded-specific rejection for some surfaces:
  - `SQLCOM_SHUTDOWN` returns `ER_NOT_SUPPORTED_YET` for "embedded server".
  - `SQLCOM_BINLOG_BASE64_EVENT` returns `ER_OPTION_PREVENTS_STATEMENT` for
    "embedded".
  - Some event-scheduler commands return `ER_NOT_SUPPORTED_YET` when the event
    scheduler is unavailable in embedded builds.
- `SQLCOM_CREATE_FUNCTION` is the UDF command path. In builds with `HAVE_DLOPEN`
  it calls `mysql_create_function()` and may attempt dynamic library handling.
- `SQLCOM_DROP_FUNCTION` shares the stored-routine drop block. Unqualified
  `DROP FUNCTION` first probes UDF metadata but can continue into stored
  routine deletion behavior, so a safe UDF-only rejection needs a more precise
  stored routine compatibility decision.
- `SQLCOM_INSTALL_PLUGIN` and `SQLCOM_UNINSTALL_PLUGIN` call
  `mysql_install_plugin()` and `mysql_uninstall_plugin()`, which operate on the
  `mysql.plugin` table and dynamic plugin metadata.
- `SQLCOM_CREATE_SERVER`, `SQLCOM_ALTER_SERVER`, and `SQLCOM_DROP_SERVER` call
  server-table helpers in `vendor/mariadb/server/sql/sql_servers.cc`.
- `sql_servers.cc:servers_init(false)` tries to read `mysql.servers` during
  server component initialization. The embedded bootstrap smoke records the
  resulting startup diagnostic when the table is absent:

  ```text
  Got ERROR: "Can't open and lock privilege tables: Table 'mysql.servers' doesn't exist" errno: 2000
  ```

- `sql_servers.cc:servers_init(true)` can initialize the in-memory server cache
  without reading `mysql.servers`, but changing startup initialization is a
  broader bootstrap-cleanup decision. This slice focuses on SQL command
  execution first.

## Proposed Design

Use MariaDB's existing embedded diagnostic style for this first pass:

```c++
my_error(ER_OPTION_PREVENTS_STATEMENT, MYF(0), "embedded");
```

Patch `mysql_execute_command()` so these command cases fail before they touch
dynamic plugin/UDF loading or `mysql.*` server tables when `EMBEDDED_LIBRARY`
is defined:

- `SQLCOM_CREATE_FUNCTION`
- `SQLCOM_INSTALL_PLUGIN`
- `SQLCOM_UNINSTALL_PLUGIN`
- `SQLCOM_CREATE_SERVER`
- `SQLCOM_ALTER_SERVER`
- `SQLCOM_DROP_SERVER`

Extend `vendor/mariadb/server/mylite/bootstrap_smoke.cc` with an unsupported
statement list. After `SELECT 1` succeeds, run each statement and assert:

- `mysql_query()` fails,
- `mysql_errno()` is non-zero,
- `mysql_error()` is recorded in the report,
- execution continues to the next unsupported statement.

The report should include one line per unsupported statement with the statement
label, MariaDB errno, SQLSTATE when available, and message. The smoke target
should fail if any unsupported statement succeeds.

## Affected Subsystems

- `sql/sql_parse.cc` command dispatch in embedded builds only.
- MyLite embedded bootstrap smoke target and report.

No parser, optimizer, storage-engine, file-format, or public API behavior
should change outside embedded builds.

## DDL Metadata Routing Impact

The slice does not implement MyLite DDL routing. It prevents selected
server-oriented SQL commands from reaching metadata tables that MyLite does not
support. Normal user table DDL remains unchanged and is still handled by later
DDL routing and storage-engine slices.

## Single-File And Embedded-Lifecycle Implications

Rejecting these commands avoids accidental durable writes to `mysql.plugin`,
`mysql.func`, or `mysql.servers` and avoids dynamic library loading in the
embedded profile. The startup `mysql.servers` diagnostic and Aria startup logs
remain documented bootstrap side effects until a later cleanup slice changes
server component initialization.

## Public API Or File-Format Impact

None. The diagnostics are MariaDB diagnostics observed through the internal
embedded C API smoke target. Public MyLite error mapping remains future API
work.

## Binary-Size Impact

No meaningful binary-size change is expected. The code paths remain compiled;
embedded execution returns earlier.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

- Run `tools/run-embedded-bootstrap-smoke.sh`.
- Verify the report records successful `SELECT 1`.
- Verify the report records expected failures for:
  - `CREATE FUNCTION ... SONAME`
  - `INSTALL PLUGIN`
  - `UNINSTALL PLUGIN`
  - `CREATE SERVER`
  - `ALTER SERVER`
  - `DROP SERVER`
- Verify dynamic plugin artifacts remain absent.
- Run `bash -n` for changed shell scripts.
- Run `git diff --check`.

## Acceptance Criteria

- The selected dynamic extension and foreign-server SQL commands fail
  explicitly in embedded builds before reaching dynamic loading or `mysql.*`
  server-table mutation paths.
- The smoke target fails if any selected unsupported statement succeeds.
- The smoke report records the unsupported-surface diagnostics.
- Non-embedded MariaDB code paths are not changed.
- No public MyLite ABI or single-file storage claim is introduced.

## Implementation Result

Implemented in `mysql_execute_command()` for embedded builds only. The smoke
now verifies six explicit rejections:

- `CREATE FUNCTION ... SONAME`
- `INSTALL PLUGIN`
- `UNINSTALL PLUGIN`
- `CREATE SERVER`
- `ALTER SERVER`
- `DROP SERVER`

Each statement fails with MariaDB error `1290`, SQLSTATE `HY000`, and message:

```text
The MariaDB server is running with the embedded option so it cannot execute this statement
```

Verification command:

```sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
```

Observed implementation artifacts after the slice:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,133,780 bytes.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,611,272
  bytes.
- Dynamic plugin artifacts: none.
- Startup still emits the pre-existing `mysql.servers` diagnostic recorded by
  the embedded-bootstrap slice.

## Risks And Unresolved Questions

- `DROP FUNCTION` remains deferred because the MariaDB command path can fall
  through from UDF metadata lookup to stored function deletion. MyLite should
  reject UDF deletion without blocking stored routine compatibility, but that
  needs a separate routine-policy slice.
- MariaDB has many additional server-oriented surfaces: users, grants,
  replication control, events, `SET GLOBAL`, log control, resource groups, and
  administrative `SHOW` statements. This slice documents but does not complete
  the full compatibility matrix.
- Startup still calls `servers_init(false)` and emits a `mysql.servers`
  diagnostic. A later bootstrap-cleanup slice can evaluate whether embedded
  MyLite should call the no-table initialization path or replace server-cache
  initialization entirely.
