# libmylite Statement Effects Slice

## Problem Statement

`mylite_exec()` lets applications execute supported SQL through the public
`libmylite` handle, but callers still cannot observe basic statement side
effects without reaching into the internal MariaDB `MYSQL *`. A usable embedded
API needs affected-row counts, generated insert ids, and warning counts because
these are part of normal MariaDB client-visible behavior.

This slice exposes those values through small public `libmylite` accessors
backed by the opened handle.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Official MariaDB documentation:
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-functions/mysql_affected_rows>
    documents `mysql_affected_rows()` and the `UINT64_MAX` failure sentinel.
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-functions/mysql_insert_id>
    documents `mysql_insert_id()`, including multi-insert behavior and
    `LAST_INSERT_ID(expr)`.
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-functions/mysql_warning_count>
    documents `mysql_warning_count()` and the use of `SHOW WARNINGS` for
    message details.
- `vendor/mariadb/server/include/mysql.h` declares `MYSQL::affected_rows`,
  `MYSQL::insert_id`, `MYSQL::warning_count`, and the public C API functions
  that read them.
- `vendor/mariadb/server/sql-common/client.c` implements
  `mysql_affected_rows()` as a read of `mysql->affected_rows`.
- `vendor/mariadb/server/libmysqld/libmysql.c` implements
  `mysql_insert_id()` and `mysql_warning_count()` as reads from the embedded
  connection handle.
- `vendor/mariadb/server/libmysqld/lib_sql.cc` clears
  `mysql->affected_rows` to `~(my_ulonglong)0` before dispatching a command,
  then copies embedded execution metadata from `embedded_query_result` back
  into the `MYSQL` handle for non-result statements.
- `vendor/mariadb/server/mylite/mylite.cc` owns the embedded `MYSQL *` inside
  `mylite_db` and is the right layer for translating these values into the
  MyLite public API without exposing MariaDB headers.

## Scope

This slice will:

- add `mylite_changes()`, `mylite_last_insert_id()`, and
  `mylite_warning_count()` to the public header,
- implement those accessors against the handle-owned embedded `MYSQL *`,
- return `-1` from `mylite_changes()` when MariaDB reports
  `~(my_ulonglong)0`, preserving the standard "not applicable or failed"
  sentinel in a signed public return type,
- return `0` for null handles or handles without an active embedded
  connection from the unsigned accessors,
- keep the accessors side-effect free and avoid changing handle diagnostics,
- extend the `libmylite` smoke to verify affected rows, last insert id,
  warning count, and null-handle behavior through the public API.

## Non-Goals

- Do not add warning-message enumeration. Callers can use `SHOW WARNINGS`
  through `mylite_exec()` until a richer API is specified.
- Do not add prepared statements, bind APIs, or statement-owned diagnostics.
- Do not add `mylite_total_changes()` or SQLite-specific accumulated-change
  semantics.
- Do not change MariaDB's affected-row semantics, including unchanged
  `UPDATE` rows not being counted.
- Do not change storage-engine row, index, catalog, or file-format behavior.

## Proposed Design

Extend `vendor/mariadb/server/mylite/include/mylite.h`:

```c
MYLITE_API long long mylite_changes(mylite_db *db);
MYLITE_API unsigned long long mylite_last_insert_id(mylite_db *db);
MYLITE_API unsigned mylite_warning_count(mylite_db *db);
```

`mylite_changes()` reads `mysql_affected_rows(db->mysql)`. If the handle is
null, has no active MariaDB connection, or MariaDB returns
`~(my_ulonglong)0`, it returns `-1`. If MariaDB reports a value larger than
`LLONG_MAX`, the function returns `LLONG_MAX`; this avoids signed overflow
while preserving monotonic "many rows changed" behavior for the current public
type.

`mylite_last_insert_id()` reads `mysql_insert_id(db->mysql)`. It returns `0`
for null or inactive handles, matching MariaDB's "no generated id" value.

`mylite_warning_count()` reads `mysql_warning_count(db->mysql)`. It returns
`0` for null or inactive handles. Warning details remain available through
SQL, for example `SHOW WARNINGS`, until a dedicated warning API is designed.

None of the accessors call `clear_error()` or `set_error()`. They are simple
observers of the last executed statement on the handle and must not hide a
previous error diagnostic.

## Affected Subsystems

- Public `libmylite` header and static library implementation.
- `libmylite` open/close/exec smoke binary and report schema.
- `docs/api/libmylite-c-api.md`.
- Roadmap current-state text and slice table.

## DDL Metadata Routing Impact

None. The tests will create and drop a normal supported `ENGINE=MYLITE` table
through the existing DDL routing path, but this slice does not modify metadata
routing.

## Single-File And Embedded-Lifecycle Implications

The accessors observe connection-local state held by the existing
process-scoped embedded runtime. They introduce no durable state and no new
temporary, lock, journal, WAL, or plugin files. Existing sidecar scans must
continue to pass.

## Public API And File-Format Impact

Public API additions:

- `mylite_changes()`,
- `mylite_last_insert_id()`,
- `mylite_warning_count()`.

No file-format change.

## Binary-Size Impact

Expected size impact is negligible: three exported wrappers and smoke coverage
over MariaDB C API functions already linked by `mylite_exec()`. No new
dependency, plugin, storage engine, or MariaDB subsystem is introduced.

The post-implementation `MinSizeRel` build records:

| Artifact | Size |
| --- | ---: |
| `build/mariadb-minsize/mylite/libmylite.a` | 38,512 bytes |
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

- null handle behavior for all three accessors,
- generated autoincrement insert ids through `mylite_last_insert_id()`,
- affected-row counts for multi-row `INSERT`, changed-row `UPDATE`, unchanged
  `UPDATE`, and `DELETE`,
- warning count after a successful statement that emits a warning,
- affected-row sentinel behavior after a duplicate-key failure,
- existing open/close, `mylite_exec()`, DDL/DML, callback, duplicate-key, and
  reopen cases still pass.

## Acceptance Criteria

- Public callers can read last affected rows, last insert id, and warning
  count after statements executed through `mylite_exec()`.
- Accessors do not expose MariaDB handles or require MariaDB headers.
- Accessors tolerate null handles and inactive handles.
- Error diagnostics from the last failed statement remain intact after calling
  the accessors.
- Existing storage, compatibility, embedded bootstrap, and open/close smokes
  continue to pass.

## Implementation Result

Implemented in `vendor/mariadb/server/mylite/include/mylite.h` and
`vendor/mariadb/server/mylite/mylite.cc` with public `mylite_changes()`,
`mylite_last_insert_id()`, and `mylite_warning_count()`. The accessors read the
handle-owned embedded `MYSQL *`, tolerate null handles, and do not rewrite
handle diagnostics.

The `libmylite` smoke now verifies this statement-effects report:

```text
statement_effects=null=-1:0:0,insert=2:1:0,noop_update=0:0:0,update=1:0:0,warning=0:0:1,delete=1:0:0,duplicate=-1:1062:23000
```

That covers null-handle behavior, multi-row generated insert ids, changed and
unchanged updates, `INSERT IGNORE` warning count, delete affected rows, and
duplicate-key failure diagnostics after the accessors are called.

## Risks And Unresolved Questions

- `mylite_changes()` uses a signed return type, so enormous MariaDB
  affected-row counts above `LLONG_MAX` need clamping unless a later API adds
  an unsigned or out-parameter form.
- Warning enumeration is deliberately deferred. `mylite_warning_count()` alone
  is useful but not enough for applications that need structured warning
  details.
- The accessors are handle-local. Statement-local values for future prepared
  statements need their own design once `mylite_stmt` exists.
