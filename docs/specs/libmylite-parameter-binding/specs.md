# libmylite Parameter Binding Slice

## Problem Statement

The first `mylite_stmt` implementation can prepare, step, reset, finalize, and
read result columns, but it rejects SQL containing parameter markers. That
leaves callers without a binary-safe way to pass values into repeated
statements and forces applications back to string interpolation for DML.

This slice adds the first bounded parameter-binding API for prepared
statements. It covers NULL, signed and unsigned 64-bit integers, double,
binary-safe text, and binary-safe BLOB values. Richer MariaDB parameter types
remain future work.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Official MariaDB documentation:
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_prepare>
    documents marker placement restrictions and says markers must be bound
    with `mysql_stmt_bind_param()`.
  - <https://mariadb.com/kb/en/mysql_stmt_bind_param/> documents binding an
    array of `MYSQL_BIND` structures whose size matches
    `mysql_stmt_param_count()`.
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_param_count>
    documents parameter marker counting after prepare.
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_execute>
    documents execution with parameter markers replaced by bound values.
  - <https://mariadb.com/docs/connectors/mariadb-connector-c/api-prepared-statement-functions/mysql_stmt_reset>
    documents resetting a statement while leaving bindings available for
    re-execution.
- `vendor/mariadb/server/include/mysql.h` defines `MYSQL_BIND` input fields:
  `buffer_type`, `buffer`, `buffer_length`, `length`, `is_null`, and
  `is_unsigned`.
- `vendor/mariadb/server/libmysqld/libmysql.c` implements
  `mysql_stmt_prepare()`, allocates `stmt->params` for `param_count`, and
  implements `mysql_stmt_bind_param()` by copying the bind array into the
  statement and setting type-specific parameter serialization callbacks.
- `vendor/mariadb/server/libmysqld/libmysql.c` serializes parameter values in
  `store_param_*()` from caller-provided buffers at execute time, so MyLite
  must keep bound value storage alive until execution completes.
- `vendor/mariadb/server/sql/sql_prepare.cc` implements embedded parameter
  insertion through `emb_insert_params()` and
  `emb_insert_params_with_log()`, using `MYSQL_BIND` buffer types, null flags,
  unsigned flags, and length pointers.
- `vendor/mariadb/server/mylite/mylite.cc` currently owns the public
  `mylite_stmt` wrapper and rejects nonzero `mysql_stmt_param_count()`.

## Scope

This slice will:

- add public bind APIs:
  - `mylite_bind_null()`,
  - `mylite_bind_int64()`,
  - `mylite_bind_uint64()`,
  - `mylite_bind_double()`,
  - `mylite_bind_text()`,
  - `mylite_bind_blob()`,
- add public `MYLITE_STATIC` and `MYLITE_TRANSIENT` destructor sentinels,
- allow `mylite_prepare()` to return prepared statements with parameter
  markers,
- store all bound values inside `mylite_stmt` so MariaDB never reads caller
  buffers after a bind function returns,
- require every parameter slot to be bound before the first execution,
- preserve bindings across `mylite_reset()` so the statement can be re-executed
  with existing or replaced parameter values,
- require `mylite_reset()` before rebinding a statement that has already been
  stepped,
- bind one 1-based public parameter index to one 0-based MariaDB
  `MYSQL_BIND` entry,
- extend the `libmylite` smoke with bound INSERT, SELECT predicates, binary
  BLOB bytes, NULL text/blob values, reset-and-rebind, unbound parameter,
  invalid index, destructor, and close-busy coverage.

## Non-Goals

- Do not add named parameters or parameter-name lookup.
- Do not add temporal, decimal, JSON, geometry, or long-data streaming bind
  APIs.
- Do not expose `MYSQL_BIND`, `MYSQL_STMT`, or MariaDB headers.
- Do not add statement-specific affected-row or insert-id accessors.
- Do not add multi-statement support, multiple result sets, cursors, or
  unbuffered fetch.
- Do not change storage-engine file format, catalog format, DDL routing, or
  transaction semantics.

## Proposed Design

Extend `vendor/mariadb/server/mylite/include/mylite.h`:

```c
#define MYLITE_STATIC ((void (*)(void *))0)
#define MYLITE_TRANSIENT ((void (*)(void *))-1)

MYLITE_API int mylite_bind_null(mylite_stmt *stmt, unsigned index);
MYLITE_API int mylite_bind_int64(
    mylite_stmt *stmt,
    unsigned index,
    long long value);
MYLITE_API int mylite_bind_uint64(
    mylite_stmt *stmt,
    unsigned index,
    unsigned long long value);
MYLITE_API int mylite_bind_double(
    mylite_stmt *stmt,
    unsigned index,
    double value);
MYLITE_API int mylite_bind_text(
    mylite_stmt *stmt,
    unsigned index,
    const char *value,
    size_t value_len,
    void (*destructor)(void *));
MYLITE_API int mylite_bind_blob(
    mylite_stmt *stmt,
    unsigned index,
    const void *value,
    size_t value_len,
    void (*destructor)(void *));
```

`mylite_prepare()` will stop rejecting nonzero `mysql_stmt_param_count()` and
will initialize MyLite-owned parameter state sized to that count.

Each parameter slot stores:

- bound/unbound state,
- `MYSQL_TYPE_*` buffer type,
- null flag,
- signed 64-bit, unsigned 64-bit, or double scalar storage,
- text/blob byte storage,
- unsigned long byte length for MariaDB's `MYSQL_BIND::length` pointer.

`mylite_bind_*()` validates the statement, 1-based index, and execution state.
Binding is allowed before the first `mylite_step()` and after
`mylite_reset()`. Binding after a statement has been stepped returns
`MYLITE_MISUSE` with a stable handle diagnostic until the caller resets the
statement.

Text and BLOB bind functions copy the input bytes immediately into
`mylite_stmt`, including embedded NUL bytes. A NULL input pointer binds SQL
NULL. `value_len > ULONG_MAX` returns `MYLITE_MISUSE`. `MYLITE_STATIC` and
`MYLITE_TRANSIENT` both result in an immediate MyLite-owned copy for this first
implementation. A custom destructor, when non-NULL and not a sentinel, is
called only after a successful copy because MyLite has then accepted ownership
of the caller's input object. If validation or allocation fails, ownership
remains with the caller and the destructor is not called.

Before `mysql_stmt_execute()`, `mylite_step()` checks that all parameter slots
are bound, builds a `std::vector<MYSQL_BIND>` from MyLite-owned parameter
storage, calls `mysql_stmt_bind_param()`, then executes normally. Calling
`mysql_stmt_bind_param()` before each execution keeps MariaDB's copied bind
metadata aligned when a reset-and-rebind changes a parameter type.

`mylite_reset()` keeps parameter values intact and only clears execution and
result state. This matches MariaDB's reset model and lets callers repeat a
statement with the same bindings without rebinding every slot.

## Affected Subsystems

- Public `libmylite` header and static library implementation.
- `libmylite` open/close smoke binary and report schema.
- API docs, roadmap, and this slice spec.

## DDL Metadata Routing Impact

MariaDB permits parameter markers only in selected statement positions, mainly
DML contexts. This slice does not change DDL metadata routing. Smoke coverage
will bind DML and SELECT statements against an existing `ENGINE=MYLITE` table.

## Single-File And Embedded-Lifecycle Implications

Parameter values live only in the prepared statement handle. The slice
introduces no durable files, companion files, catalog entries, or new runtime
sidecars. Existing `mylite_close()` active-statement protection remains the
resource-lifetime boundary.

## Public API And File-Format Impact

Public API additions are limited to bind functions and destructor sentinel
macros. No file-format change.

## Binary-Size Impact

Expected `libmylite.a` growth is modest: parameter storage, bind conversion
helpers, and additional smoke coverage. The MariaDB embedded archive already
contains prepared-statement binding code, so `libmariadbd.a` object count is
not expected to change.

The post-implementation `MinSizeRel` build records:

| Artifact | Size |
| --- | ---: |
| `build/mariadb-minsize/mylite/libmylite.a` | 80,838 bytes |
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

- preparing SQL with markers succeeds,
- stepping a statement with unbound parameters returns `MYLITE_MISUSE`,
- invalid parameter indexes return `MYLITE_MISUSE`,
- bound INSERT writes signed integers, unsigned integers, doubles, text, BLOB,
  and NULL values,
- BLOB input with embedded NUL bytes is preserved through
  `mylite_column_blob()` and `mylite_column_bytes()`,
- bound SELECT predicates return the expected rows,
- `mylite_reset()` preserves existing bindings and supports reset-and-rebind
  after reset,
- rebinding after stepping without reset returns `MYLITE_MISUSE`,
- a custom destructor is called after a successful copied text/blob bind,
- `mylite_close()` still returns `MYLITE_BUSY` while a bound prepared statement
  is active,
- existing open/close, `mylite_exec()`, statement effects, storage, sidecar,
  and compatibility smokes still pass.

## Acceptance Criteria

- Public callers can bind NULL, 64-bit integer, double, text, and BLOB values
  to prepared statements without using MariaDB C API structures.
- Bound values remain valid after bind functions return and through statement
  execution because MyLite owns the storage.
- Every public binding misuse path returns a MyLite result code and a
  handle-local diagnostic when a database handle is available.
- Prepared statements can be reset and re-executed with preserved or replaced
  bindings.
- Existing storage, compatibility, embedded bootstrap, and open/close smokes
  continue to pass.

## Implementation Result

Implemented in `libmylite` with MyLite-owned parameter storage. Prepared SQL
with marker slots now returns a statement handle, all slots must be bound
before execution, and `mylite_reset()` preserves bindings for repeated
execution. Text and BLOB inputs are copied immediately into the statement,
embedded NUL bytes are preserved, NULL input pointers bind SQL NULL, and a
custom destructor is called after a successful copy.

The `libmylite` smoke now verifies unbound parameter diagnostics, invalid
indexes, reset-before-rebind enforcement, bound INSERT rows with signed,
unsigned, double, text, BLOB, and NULL values, binary BLOB result bytes,
binding preservation across reset, reset-and-rebind behavior, custom
destructor invocation, and close-busy protection with active statements.
