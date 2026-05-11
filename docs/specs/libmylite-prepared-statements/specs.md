# libmylite Prepared Statements Slice

## Problem Statement

`mylite_exec()` is useful for one-shot SQL, but it is text-oriented and cannot
provide reusable statements or binary-safe result access. The public API sketch
already names `mylite_stmt`, `mylite_prepare()`, `mylite_step()`,
`mylite_reset()`, `mylite_finalize()`, and column accessors. Applications need
that statement lifecycle before parameter binding can be added safely.

This slice adds the first public prepared-statement API over MariaDB's embedded
prepared statement machinery. It deliberately supports no-parameter statements
only; binding is a separate slice.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Official MariaDB documentation:
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_init>
    documents statement allocation and `mysql_stmt_close()` ownership.
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_prepare>
    documents preparing SQL text and parameter-marker restrictions.
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_execute>
    documents execution and using `mysql_stmt_fetch()` for result sets.
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_bind_result>
    documents binding one `MYSQL_BIND` per result column.
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_fetch>
    documents `MYSQL_NO_DATA` and `MYSQL_DATA_TRUNCATED`.
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_result_metadata>
    documents result metadata ownership and `mysql_free_result()`.
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_reset>
    documents resetting a statement to its post-prepare state.
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_close>
    documents statement deallocation and pending-result cancellation.
- `vendor/mariadb/server/include/mysql.h` declares `MYSQL_STMT`,
  `MYSQL_BIND`, prepared statement functions, `MYSQL_NO_DATA`, and
  `MYSQL_DATA_TRUNCATED`.
- `vendor/mariadb/server/libmysqld/libmysql.c` implements
  `mysql_stmt_init()`, `mysql_stmt_prepare()`, `mysql_stmt_execute()`,
  `mysql_stmt_bind_result()`, `mysql_stmt_fetch()`,
  `mysql_stmt_fetch_column()`, `mysql_stmt_store_result()`,
  `mysql_stmt_result_metadata()`, `mysql_stmt_reset()`, and
  `mysql_stmt_close()` for the embedded library.
- `vendor/mariadb/server/sql/sql_prepare.cc` routes embedded prepared
  execution through `Protocol_local`, fills embedded result metadata, and
  copies OK-packet affected-row and insert-id metadata into the statement
  handle.
- `vendor/mariadb/server/mylite/mylite.cc` owns the handle-level embedded
  `MYSQL *`, diagnostics, and public API boundary.

## Scope

This slice will:

- expose opaque `mylite_stmt`,
- add `mylite_prepare()`, `mylite_step()`, `mylite_reset()`, and
  `mylite_finalize()`,
- add first column accessors:
  - `mylite_column_count()`,
  - `mylite_column_name()`,
  - `mylite_column_type()`,
  - `mylite_column_int64()`,
  - `mylite_column_uint64()`,
  - `mylite_column_double()`,
  - `mylite_column_text()`,
  - `mylite_column_blob()`,
  - `mylite_column_bytes()`,
- add public column type constants for integer, floating, text, blob, and NULL
  values,
- execute no-parameter statements lazily on the first `mylite_step()`,
- return `MYLITE_ROW` for fetched rows and `MYLITE_DONE` when execution
  completes without a row,
- buffer prepared result sets with `mysql_stmt_store_result()` so the first
  implementation can size result buffers safely,
- bind every result column through `MYSQL_BIND` and expose binary-safe value
  lengths with `mylite_column_bytes()`,
- reject parameter markers explicitly until bind APIs exist,
- make `mylite_close()` return `MYLITE_BUSY` when statements still depend on
  the handle,
- extend the `libmylite` smoke with prepared SELECT, prepared DML, reset,
  finalize, null/misuse, binary BLOB bytes, and close-busy coverage.

## Non-Goals

- Do not add parameter binding.
- Do not add statement-specific affected-row or insert-id APIs.
- Do not support multi-statements, multiple result sets, cursors, or streaming
  unbuffered fetch.
- Do not expose `MYSQL_STMT *`, `MYSQL_BIND`, or MariaDB headers.
- Do not add warning enumeration or statement-level warning APIs.
- Do not change storage-engine file format, catalog format, or DDL routing.

## Proposed Design

Extend `vendor/mariadb/server/mylite/include/mylite.h`:

```c
typedef struct mylite_stmt mylite_stmt;

typedef enum mylite_column_type {
  MYLITE_INTEGER = 1,
  MYLITE_FLOAT = 2,
  MYLITE_TEXT = 3,
  MYLITE_BLOB = 4,
  MYLITE_NULL = 5
} mylite_column_type;

MYLITE_API int mylite_prepare(
    mylite_db *db,
    const char *sql,
    size_t sql_len,
    mylite_stmt **out_stmt,
    const char **tail);
MYLITE_API int mylite_step(mylite_stmt *stmt);
MYLITE_API int mylite_reset(mylite_stmt *stmt);
MYLITE_API int mylite_finalize(mylite_stmt *stmt);
```

`mylite_prepare()` validates the handle, SQL pointer, and output pointer. If
`sql_len` is zero, it uses `strlen(sql)` for C-string convenience. On success,
`*tail` is set to `sql + effective_sql_len` when `tail` is non-NULL. MariaDB's
prepared API does not expose SQLite-style partial-parse tail discovery, so this
slice does not accept multiple statements hidden behind the tail.

After `mysql_stmt_prepare()`, `mylite_prepare()` checks
`mysql_stmt_param_count()`. If parameter markers are present, it closes the
MariaDB statement, stores a MyLite error such as `parameter binding is not
implemented`, and returns `MYLITE_MISUSE`. Parameter binding will get its own
API and tests.

`mylite_stmt` stores:

- owner `mylite_db *`,
- owned `MYSQL_STMT *`,
- optional `MYSQL_RES *` metadata,
- copied column names and mapped declared column types,
- result buffers, null flags, lengths, and truncation flags,
- execution/bind/done state.

`mylite_step()` executes lazily. On first call it calls `mysql_stmt_execute()`.
If `mysql_stmt_field_count()` is zero, it returns `MYLITE_DONE`. If a result
set exists, it enables `STMT_ATTR_UPDATE_MAX_LENGTH`, calls
`mysql_stmt_store_result()`, allocates one buffer per column using metadata
length and max-length evidence, binds the buffers with `mysql_stmt_bind_result()`,
and fetches the first row. Later calls fetch subsequent rows.

Column buffers are retained on `mylite_stmt` and remain valid until the next
`mylite_step()`, `mylite_reset()`, or `mylite_finalize()`. Text values are
NUL-terminated for convenience, while `mylite_column_bytes()` is authoritative
for the byte length. `mylite_column_blob()` returns the same storage as the
text accessor but does not promise NUL-free content.

`mylite_reset()` calls `mysql_stmt_reset()` and clears local execution, result,
and current-row state while preserving the prepared SQL. `mylite_finalize()`
closes the MariaDB statement, frees metadata, decrements the owner handle's
open-statement count, and deletes the MyLite statement. Finalizing `NULL`
returns `MYLITE_OK`.

`mylite_close()` checks `db->open_statements`. If nonzero, it stores a
handle-owned `MYLITE_BUSY` diagnostic and leaves the handle open.

## Affected Subsystems

- Public `libmylite` header and static library implementation.
- `libmylite` open/close/exec smoke binary and report schema.
- API docs and roadmap.

## DDL Metadata Routing Impact

Prepared statements may execute currently supported DDL and DML, but this slice
does not change metadata routing. Smoke coverage should prepare DML and SELECT
statements against an already created `ENGINE=MYLITE` table.

## Single-File And Embedded-Lifecycle Implications

Prepared statements depend on an open `mylite_db` connection. `mylite_close()`
must not tear down a handle while statements still own embedded statement
resources. The slice introduces no durable files, no journal/WAL companion, and
no new runtime sidecars.

## Public API And File-Format Impact

Public API additions:

- `mylite_stmt`,
- `mylite_column_type`,
- `mylite_prepare()`,
- `mylite_step()`,
- `mylite_reset()`,
- `mylite_finalize()`,
- first column accessors.

No file-format change.

## Binary-Size Impact

Expected size impact is moderate for `libmylite.a`: statement state, result
buffer allocation, and smoke coverage. The MariaDB embedded archive already
exports prepared statement symbols, so `libmariadbd.a` object count is not
expected to increase. The implementation result should record measured sizes.

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

- null handle, null SQL, null output statement, null finalize, and parameter
  marker misuse cases,
- preparing a SELECT exposes column count and names before stepping,
- `mylite_step()` returns `MYLITE_ROW` for each row and `MYLITE_DONE` at EOF,
- integer, text, NULL, and BLOB values are readable through column accessors,
- `mylite_column_bytes()` preserves BLOB values with embedded NUL bytes,
- `mylite_reset()` re-executes the prepared statement from the first row,
- prepared DML returns `MYLITE_DONE` and persists changes visible to
  `mylite_exec()`,
- `mylite_close()` returns `MYLITE_BUSY` while a prepared statement is active,
- finalizing the statement releases the handle so close succeeds,
- existing open/close, `mylite_exec()`, statement effects, storage, sidecar,
  and compatibility smokes still pass.

## Acceptance Criteria

- Public callers can prepare, step, reset, and finalize no-parameter SQL
  statements without touching MariaDB C API handles.
- Result column names, types, byte lengths, and values are available through
  `libmylite` accessors.
- Binary result values with embedded NUL bytes are not truncated by the public
  API.
- `mylite_close()` preserves handle/resource ownership by returning
  `MYLITE_BUSY` while statements are active.
- Existing storage, compatibility, embedded bootstrap, and open/close smokes
  continue to pass.

## Risks And Unresolved Questions

- The first implementation buffers result sets. Streaming fetch and cursor
  modes need a separate design if memory use becomes a problem.
- Column type mapping is intentionally coarse. MariaDB's richer date, time,
  decimal, JSON, geometry, and unsigned numeric distinctions need later typed
  accessors.
- Parameter binding is the next natural API slice after the statement
  lifecycle is stable.
- Close-busy ownership requires callers to finalize statements before closing a
  database handle. Deferred-close semantics remain future work.
