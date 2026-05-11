# libmylite Exec Slice

## Problem Statement

`libmylite` can currently open and close a `.mylite` path, initialize the
embedded MariaDB runtime, and expose handle-owned diagnostics. Applications
still cannot execute SQL through the public MyLite API, so the product path
"open one primary file, execute MariaDB SQL in-process, close it" is only
covered through internal smoke binaries that call the MariaDB C API directly.

This slice adds the first bounded SQL execution API: `mylite_exec()`, a
convenience wrapper for one SQL string with optional row callbacks and optional
caller-owned error text.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Official MariaDB documentation:
  - <https://mariadb.com/kb/en/embedded-mariadb-interface/> documents that the
    embedded server uses the normal C API shape while linking against
    `libmysqld`.
  - <https://mariadb.com/kb/en/mysql_real_query/> documents binary-safe query
    execution and using result metadata to determine whether a result set is
    available.
  - <https://mariadb.com/kb/en/mysql_store_result/> documents buffered result
    retrieval, the `NULL` return ambiguity, and `mysql_field_count()`.
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-functions/mysql_fetch_row>
    documents row retrieval and NULL column value handling.
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-functions/mysql_free_result>
    documents result ownership.
- `vendor/mariadb/server/mylite/mylite.cc`:
  - `mylite_db` already owns a connected embedded `MYSQL *`,
  - `mylite_open_v2()` passes the opened primary path through
    `--mylite-catalog-file`,
  - diagnostics are already stored on `mylite_db`.
- `vendor/mariadb/server/mylite/include/mylite.h` currently exposes open,
  close, and diagnostics, but no SQL execution or MyLite-owned free function.
- `vendor/mariadb/server/include/mysql.h` declares `mysql_real_query()`,
  `mysql_store_result()`, `mysql_field_count()`, `mysql_fetch_fields()`,
  `mysql_fetch_row()`, `mysql_fetch_lengths()`, `mysql_free_result()`,
  `mysql_affected_rows()`, `mysql_insert_id()`, `mysql_errno()`,
  `mysql_error()`, `mysql_sqlstate()`, and `mysql_warning_count()`.
- `vendor/mariadb/server/libmysqld/libmysql.c` implements `mysql_query()` as a
  `mysql_real_query()` wrapper and exposes field count, insert id, SQLSTATE,
  and warning count from the embedded connection state.
- `vendor/mariadb/server/libmysqld/lib_sql.cc` routes embedded result storage
  through `mysql_store_result()`.

## Scope

This slice will:

- add public `mylite_exec_callback`, `mylite_exec()`, and `mylite_free()`
  declarations,
- execute one null-terminated SQL string through the handle's embedded
  `MYSQL *` with `mysql_real_query(mysql, sql, strlen(sql))`,
- pass result rows to the optional callback as text values plus column names,
- pass SQL NULL values as `NULL` callback value pointers,
- buffer result sets with `mysql_store_result()` and always release them with
  `mysql_free_result()`,
- classify SQLSTATE class `23` failures as `MYLITE_CONSTRAINT` while preserving
  MariaDB errno, SQLSTATE, and message on the handle,
- return `MYLITE_MISUSE` for null handles or null SQL,
- allocate any returned `errmsg` with MyLite-owned memory released by
  `mylite_free()`,
- extend the open/close smoke into an execution smoke that verifies scalar
  SELECT, DDL, DML, row callback values, duplicate-key diagnostics, and reopen
  persistence through the public API.

## Non-Goals

- Do not add prepared statements, parameter binding, column accessor APIs, or
  binary-safe value APIs.
- Do not support multi-statement strings, multiple result sets, stored
  procedure result streams, or streaming/unbuffered results.
- Do not expose `MYSQL *` or require callers to include MariaDB headers.
- Do not add new result codes beyond the current public enum.
- Do not change storage engine file format, catalog format, or DDL metadata
  routing.
- Do not claim handle-level thread safety beyond the existing open/close
  lifecycle.

## Proposed Design

Extend `vendor/mariadb/server/mylite/include/mylite.h`:

```c
typedef int (*mylite_exec_callback)(
    void *ctx,
    int column_count,
    char **values,
    char **column_names);

MYLITE_API int mylite_exec(
    mylite_db *db,
    const char *sql,
    mylite_exec_callback callback,
    void *ctx,
    char **errmsg);
MYLITE_API void mylite_free(void *ptr);
```

`mylite_exec()` will clear the handle diagnostics, call `mysql_real_query()`,
and on success inspect `mysql_field_count()`.

If no result set is available, execution is complete after preserving the
connection success diagnostics. If a result set is available, `mylite_exec()`
will call `mysql_store_result()`, retrieve column names with
`mysql_fetch_fields()`, iterate rows with `mysql_fetch_row()`, and pass row
values to the callback. It will release the result before returning. If
`mysql_store_result()` returns `NULL` while `mysql_field_count()` is nonzero,
the wrapper reports the MariaDB error from the handle.

If the callback returns nonzero, `mylite_exec()` stops iteration, releases the
result, stores a MyLite error message such as `callback requested abort`, and
returns `MYLITE_ERROR`. This keeps the first callback contract simple until a
dedicated `MYLITE_ABORT` result code is justified.

On MariaDB execution errors, `mylite_exec()` maps SQLSTATE class `23` to
`MYLITE_CONSTRAINT`; all other SQL execution failures return `MYLITE_ERROR`
unless a future slice adds more precise public classifications. In every case,
`mylite_mariadb_errno()`, `mylite_sqlstate()`, and `mylite_errmsg()` expose the
underlying MariaDB diagnostic.

`errmsg`, when non-NULL, is set to `NULL` on entry. On error, it receives a
heap copy of the current MyLite message allocated with `malloc()` and freed
with `mylite_free()`. If that allocation fails, `mylite_exec()` returns
`MYLITE_NOMEM` and leaves `*errmsg == NULL`.

## Affected Subsystems

- Public `libmylite` header and static library implementation.
- Open/close smoke executable and report schema.
- `docs/api/libmylite-c-api.md` direct execution section.
- Roadmap current-state text and slice table.

## DDL Metadata Routing Impact

The API will be able to execute DDL that current storage-engine smokes already
exercise, including `CREATE TABLE`, `INSERT`, and `SELECT` against `ENGINE=MYLITE`.
This slice does not change DDL routing; it routes through the existing embedded
SQL layer and storage engine.

## Single-File And Embedded-Lifecycle Implications

`mylite_exec()` uses the `MYSQL *` connection owned by the open `mylite_db`
handle. It keeps the existing process-global embedded runtime constraint: one
initialized primary path per process, shared by same-path handles, and no
restart after `mysql_server_end()` until inherited startup behavior is solved.

Execution must keep all durable user state in the opened `.mylite` primary
file and the already documented temporary runtime directory. The smoke must
continue to reject unexpected durable `.frm` sidecars.

## Public API And File-Format Impact

Public API changes:

- add `mylite_exec_callback`,
- add `mylite_exec()`,
- add `mylite_free()`.

No file-format change.

## Binary-Size Impact

Measured size impact is modest: one public wrapper, result iteration code, a
small MyLite-owned allocation helper, and smoke coverage. No new MariaDB
subsystem or dependency is linked.

The post-implementation `MinSizeRel` build records:

| Artifact | Size |
| --- | ---: |
| `build/mariadb-minsize/mylite/libmylite.a` | 37,858 bytes |
| `build/mariadb-minsize/libmysqld/libmariadbd.a` | 44,415,256 bytes |

The build report still records 571 `libmariadbd.a` archive objects and no
dynamic plugin artifacts.

## License, Trademark, And Dependency Impact

No new dependency, license, or trademark impact. The public API remains
GPL-2.0-only because it links MariaDB-derived server code.

## Test And Verification Plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
bash -n tools/run-compatibility-test-harness.sh tools/run-storage-engine-smoke.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

The `libmylite` smoke should verify:

- `mylite_exec(NULL, ...)` and `mylite_exec(db, NULL, ...)` return
  `MYLITE_MISUSE`,
- scalar `SELECT` works without exposing `MYSQL *`,
- callback receives column names, text values, and SQL NULL values,
- callback abort returns `MYLITE_ERROR` and releases the result,
- DDL and DML through `mylite_exec()` persist to the opened `.mylite` file,
- duplicate-key errors return `MYLITE_CONSTRAINT` with MariaDB errno `1062`
  and SQLSTATE `23000`,
- optional `errmsg` text is allocated and released with `mylite_free()`,
- reopening the same path sees committed rows inserted through `mylite_exec()`,
- existing open/close lifecycle cases still pass.

## Acceptance Criteria

- Public callers can execute supported MariaDB SQL against the opened `.mylite`
  file through `mylite_exec()` without touching MariaDB C API handles.
- Result callbacks receive stable row values for the callback duration and
  correct column names.
- MariaDB diagnostics remain available through existing diagnostic APIs after
  success and failure.
- Caller-owned `errmsg` memory is released through `mylite_free()`.
- Existing storage, compatibility, embedded bootstrap, and open/close smokes
  continue to pass.

## Implementation Result

Implemented in `vendor/mariadb/server/mylite/include/mylite.h` and
`vendor/mariadb/server/mylite/mylite.cc` with public `mylite_exec_callback`,
`mylite_exec()`, and `mylite_free()`. `mylite_exec()` executes a
null-terminated SQL string with `mysql_real_query()`, buffers result sets with
`mysql_store_result()`, invokes callbacks with row values and column names, and
releases result ownership with `mysql_free_result()`. SQLSTATE class `23`
errors map to `MYLITE_CONSTRAINT`; the original MariaDB errno, SQLSTATE, and
message remain available through existing diagnostic APIs.

The `libmylite` smoke now verifies:

- `exec_scalar_columns=total,empty`,
- `exec_scalar_rows=3:NULL`,
- `exec_callback_abort_message=callback requested abort`,
- `exec_dml_rows=1:one,2:NULL`,
- `exec_duplicate_key_message=Duplicate entry '1' for key 'PRIMARY'`,
- duplicate-key diagnostics with `MYLITE_CONSTRAINT`, MariaDB errno `1062`,
  and SQLSTATE `23000`,
- `exec_reopen_rows=1:one,2:NULL`.

## Risks And Unresolved Questions

- This is a convenience API. It is text-oriented and not a substitute for
  prepared statements or binary-safe value access.
- Callback abort uses `MYLITE_ERROR` because the public result enum has no
  abort code yet.
- Multi-statement and multi-result support need a separate design because they
  require explicit result-draining rules and client capability choices.
- Handle-level thread-safety is still undefined beyond the existing lifecycle
  smoke coverage.
