# libmylite Warning Enumeration Slice

## Problem Statement

`mylite_warning_count()` tells callers that MariaDB produced diagnostics, but
there is still no public MyLite API for retrieving the warning, note, or error
records themselves. Applications can run `SHOW WARNINGS`, but that exposes SQL
plumbing to API users and risks changing handle state through an extra query.

This slice adds a small structured warning enumeration API over MariaDB's
documented `SHOW WARNINGS` surface while preserving the public MyLite
last-statement side-effect accessors.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Official MariaDB documentation:
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-functions/mysql_warning_count>
    documents `mysql_warning_count()` and says `SHOW WARNINGS` retrieves
    warning messages.
  - <https://mariadb.com/kb/en/show-warnings/> documents `SHOW WARNINGS` as
    the SQL surface for current-session error, warning, and note messages,
    including `LIMIT` offsets and the effect of `@@max_error_count`.
- `vendor/mariadb/server/include/mysql.h` exposes `MYSQL::affected_rows`,
  `MYSQL::insert_id`, `MYSQL::field_count`, `MYSQL::server_status`, and
  `MYSQL::warning_count`, which are the observable fields that should not be
  left describing the internal warning lookup.
- `vendor/mariadb/server/libmysqld/libmysql.c` implements
  `mysql_warning_count()` as a read of `mysql->warning_count`.
- `vendor/mariadb/server/libmysqld/lib_sql.cc` copies warning counts from
  `THD::get_stmt_da()->current_statement_warn_count()` and embedded result
  metadata into the public `MYSQL` handle.
- `vendor/mariadb/server/sql/sql_error.h` defines the diagnostics storage that
  backs `SHOW WARNINGS`, including condition count caveats when
  `@@max_error_count` limits stored entries.
- `vendor/mariadb/server/sql/sql_admin.cc` uses the diagnostics iterator to
  emit condition level and message rows for administrative statement results,
  confirming the server-side source behind warning-style output.

## Scope

This slice will:

- add public warning level constants for note, warning, and error conditions,
- add `mylite_warning()` to read one stored condition by index,
- use zero-based indexes because the implementation reads the same offset shape
  as `SHOW WARNINGS LIMIT offset, 1`,
- return `MYLITE_NOTFOUND` when the requested condition is not stored,
- return `MYLITE_MISUSE` for null handles, inactive handles, or missing output
  pointers,
- keep returned message text owned by the database handle and valid until the
  next `mylite_warning()` call or handle close,
- run `SHOW WARNINGS LIMIT <index>, 1` internally and restore the public
  `MYSQL` side-effect fields that currently back `mylite_changes()`,
  `mylite_last_insert_id()`, and `mylite_warning_count()`,
- extend the `libmylite` smoke with warning, note/error where practical,
  out-of-range, null/misuse, and last-statement-effect preservation coverage.

## Non-Goals

- Do not add caller-owned allocation for warning text.
- Do not expose `THD`, `Diagnostics_area`, `Sql_condition`, or other MariaDB
  internals.
- Do not enumerate conditions that MariaDB counted but did not store because
  `@@max_error_count` limited the diagnostics list.
- Do not add a statement-specific diagnostics object or history.
- Do not change MariaDB warning generation, SQL mode, or `SHOW WARNINGS`
  behavior.
- Do not change storage-engine file format, catalog format, or transaction
  semantics.

## Proposed Design

Extend `vendor/mariadb/server/mylite/include/mylite.h`:

```c
typedef enum mylite_warning_level {
  MYLITE_WARNING_NOTE = 1,
  MYLITE_WARNING_WARNING = 2,
  MYLITE_WARNING_ERROR = 3
} mylite_warning_level;

MYLITE_API int mylite_warning(
    mylite_db *db,
    unsigned index,
    unsigned *level,
    unsigned *code,
    const char **message);
```

`mylite_warning()` validates all output pointers and clears them before
returning. It snapshots `MYSQL::affected_rows`, `MYSQL::insert_id`,
`MYSQL::field_count`, `MYSQL::server_status`, and `MYSQL::warning_count`, runs
`SHOW WARNINGS LIMIT <index>, 1`, reads the single result row, copies the
message into handle-owned scratch storage, maps the level string to MyLite
level constants, writes the MariaDB error code, frees the result, restores the
snapshotted fields, and returns `MYLITE_OK`.

If `SHOW WARNINGS` returns no row, the function returns `MYLITE_NOTFOUND` with
a handle diagnostic. This is intentionally based on stored condition rows, not
only `mysql_warning_count()`, because MariaDB can count more warnings than it
stores when `@@max_error_count` is low.

The implementation deliberately avoids including `sql_class.h` in
`libmylite`: that header expects MariaDB's `MYSQL_SERVER` compilation context,
while the MyLite C API wrapper is built as a smaller embedded-library target.
The public header remains independent of MariaDB headers.

## Affected Subsystems

- Public `libmylite` header and static library implementation.
- `libmylite` open/close smoke binary and report schema.
- API docs, roadmap, and this slice spec.

## DDL Metadata Routing Impact

None. This API only reads session diagnostics after statements that already ran.

## Single-File And Embedded-Lifecycle Implications

The API reads connection-local memory owned by MariaDB's embedded THD. It
introduces no durable files, no companion files, no new catalog state, and no
new sidecar lifecycle.

## Public API And File-Format Impact

Public API additions are limited to warning level constants and
`mylite_warning()`. No file-format change.

## Binary-Size Impact

Expected `libmylite.a` growth is small: a level mapper, handle-owned warning
message storage, and smoke coverage. `libmariadbd.a` already contains the
diagnostics machinery.

The post-implementation `MinSizeRel` build records:

| Artifact | Size |
| --- | ---: |
| `build/mariadb-minsize/mylite/libmylite.a` | 86,094 bytes |
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

- null handle and missing output pointer misuse,
- warning count and first warning details after a statement that emits a
  warning,
- error-level condition details after a failed statement when stored by the
  diagnostics area,
- out-of-range lookup returns `MYLITE_NOTFOUND`,
- warning message pointer remains handle-owned and stable until the next
  retrieval,
- `mylite_warning()` does not change last affected rows, insert id, or warning
  count for the statement being inspected,
- existing open/close, `mylite_exec()`, statement effects, prepared
  statements, parameter binding, storage, sidecar, and compatibility smokes
  still pass.

## Acceptance Criteria

- Public callers can retrieve stored diagnostic level, code, and message
  entries without writing their own `SHOW WARNINGS` SQL.
- The public API hides MariaDB internals and keeps message lifetime tied to the
  database handle.
- Out-of-range and misuse behavior is stable and documented.
- Warning enumeration does not alter last-statement side-effect accessors.
- Existing storage, compatibility, embedded bootstrap, and open/close smokes
  continue to pass.

## Implementation Result

Implemented in `libmylite` as a handle-local warning lookup API. The public
function validates outputs, runs `SHOW WARNINGS LIMIT <index>, 1`, copies the
returned message into `mylite_db`, maps the level string to
`mylite_warning_level`, restores the snapshotted public `MYSQL` statement
fields, and returns `MYLITE_NOTFOUND` for missing stored conditions.

The `libmylite` smoke now verifies null and missing-output misuse, warning
details for `INSERT IGNORE`, error-condition details for a duplicate-key
failure, out-of-range lookup diagnostics, and preservation of the statement
effect summary for the warning lookup path.
