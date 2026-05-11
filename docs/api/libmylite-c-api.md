# libmylite C API

This document sketches the first public API. The goal is SQLite-like ownership
and lifecycle, with MariaDB SQL semantics under the hood.

## Principles

- A database is opened by file path.
- The application owns handles explicitly.
- No daemon, socket, server account, password handshake, or network connection
  is required to open a local file.
- Error reporting is connection-local.
- Prepared statements are reusable and bindable.
- The API is stable and small; MariaDB internals stay private.
- The primary API should not expose `MYSQL *`.

## Handles

```c
typedef struct mylite_db mylite_db;
typedef struct mylite_stmt mylite_stmt;
```

`mylite_db` owns one logical embedded MariaDB connection to one
`.mylite` file. Multiple `mylite_db` handles may share one process-local
runtime for the same file.

`mylite_stmt` owns one prepared statement.

## Result codes

```c
typedef enum mylite_result {
  MYLITE_OK = 0,
  MYLITE_ERROR = 1,
  MYLITE_BUSY = 5,
  MYLITE_NOMEM = 7,
  MYLITE_READONLY = 8,
  MYLITE_IOERR = 10,
  MYLITE_CORRUPT = 11,
  MYLITE_NOTFOUND = 12,
  MYLITE_FULL = 13,
  MYLITE_CANTOPEN = 14,
  MYLITE_CONSTRAINT = 19,
  MYLITE_MISUSE = 21,
  MYLITE_ROW = 100,
  MYLITE_DONE = 101
} mylite_result;
```

The final ABI can keep SQLite-familiar primary result codes where they fit, but
MariaDB diagnostics should live in `mylite_extended_errcode()` and related
diagnostic APIs. Do not overload primary result codes with raw MariaDB server
error numbers.

## Opening and closing

```c
int mylite_open(const char *filename, mylite_db **out_db);
int mylite_open_v2(
    const char *filename,
    mylite_db **out_db,
    unsigned flags,
    const char *profile);
int mylite_close(mylite_db *db);
```

Suggested flags:

```c
#define MYLITE_OPEN_READONLY   0x00000001u
#define MYLITE_OPEN_READWRITE  0x00000002u
#define MYLITE_OPEN_CREATE     0x00000004u
#define MYLITE_OPEN_EXCLUSIVE  0x00000008u
#define MYLITE_OPEN_URI        0x00000010u
```

`profile` can select a build/runtime profile such as `default`, `strict`,
`compat`, or `no-temp-files`.

`mylite_close()` returns `MYLITE_BUSY` if statements or other resources still
depend on the handle. A later `mylite_close_v2()` can offer deferred-close
semantics if there is a real need.

The first implementation is intentionally narrower than the final API shape:
it supports one initialized database path per process because MariaDB's
embedded server bootstrap is process-global and does not safely restart inside
one process after `mysql_server_end()`. `mylite_close()` releases the handle's
embedded connection; the process-scoped runtime is kept until process exit.

## Direct execution

```c
typedef int (*mylite_exec_callback)(
    void *ctx,
    int column_count,
    char **values,
    char **column_names);

int mylite_exec(
    mylite_db *db,
    const char *sql,
    mylite_exec_callback callback,
    void *ctx,
    char **errmsg);
```

This is a convenience API. Production users should prefer prepared statements
for repeated work and binary-safe values.

If `errmsg` is non-NULL and an error string is returned, the caller releases it
with `mylite_free()`.

The first implementation executes one null-terminated SQL string, buffers any
result set, and invokes the callback with text values and column names. It does
not support prepared statements, binary-safe value access, multi-statement
strings, or multiple result sets yet.

## Prepared statements

```c
int mylite_prepare(
    mylite_db *db,
    const char *sql,
    size_t sql_len,
    mylite_stmt **out_stmt,
    const char **tail);

int mylite_step(mylite_stmt *stmt);
int mylite_reset(mylite_stmt *stmt);
int mylite_finalize(mylite_stmt *stmt);
```

Return values:

- `MYLITE_ROW` when a row is available,
- `MYLITE_DONE` when execution is complete,
- another code on error.

The first implementation supports no-parameter statements. `mylite_step()`
executes lazily on the first call, buffers result sets, and keeps column values
valid until the next `mylite_step()`, `mylite_reset()`, or
`mylite_finalize()`. `sql_len == 0` uses `strlen(sql)` for C-string
convenience, and `tail`, when supplied, points to the end of the prepared SQL
on success. Statements containing parameter markers return `MYLITE_MISUSE`
until binding APIs are implemented.

Bind indexes are 1-based, matching SQLite's convention for parameter slots.

## Bindings

```c
int mylite_bind_null(mylite_stmt *stmt, unsigned index);
int mylite_bind_int64(mylite_stmt *stmt, unsigned index, long long value);
int mylite_bind_uint64(mylite_stmt *stmt, unsigned index, unsigned long long value);
int mylite_bind_double(mylite_stmt *stmt, unsigned index, double value);
int mylite_bind_text(
    mylite_stmt *stmt,
    unsigned index,
    const char *value,
    size_t value_len,
    void (*destructor)(void *));
int mylite_bind_blob(
    mylite_stmt *stmt,
    unsigned index,
    const void *value,
    size_t value_len,
    void (*destructor)(void *));
```

MariaDB supports richer types than SQLite. Later APIs should add typed date,
time, decimal, JSON, and geometry bindings instead of forcing everything through
text.

The destructor callback follows SQLite's ownership shape: MyLite either
borrows the input until statement reset/finalize, copies it immediately, or
calls the provided destructor after it no longer needs the value. The final API
should define `MYLITE_STATIC` and `MYLITE_TRANSIENT` constants instead of
requiring users to write sentinel function pointers directly.

## Memory ownership

```c
void mylite_free(void *ptr);
```

Any heap memory returned by MyLite to the caller must be released with
`mylite_free()`. Public APIs should document whether returned text is owned
by the handle, owned by the statement, or caller-owned.

## Columns

```c
typedef enum mylite_column_kind {
  MYLITE_INTEGER = 1,
  MYLITE_FLOAT = 2,
  MYLITE_TEXT = 3,
  MYLITE_BLOB = 4,
  MYLITE_NULL = 5
} mylite_column_kind;

unsigned mylite_column_count(mylite_stmt *stmt);
const char *mylite_column_name(mylite_stmt *stmt, unsigned column);
int mylite_column_type(mylite_stmt *stmt, unsigned column);

long long mylite_column_int64(mylite_stmt *stmt, unsigned column);
unsigned long long mylite_column_uint64(mylite_stmt *stmt, unsigned column);
double mylite_column_double(mylite_stmt *stmt, unsigned column);
const char *mylite_column_text(mylite_stmt *stmt, unsigned column);
const void *mylite_column_blob(mylite_stmt *stmt, unsigned column);
size_t mylite_column_bytes(mylite_stmt *stmt, unsigned column);
```

Column values are valid until the next `mylite_step()`,
`mylite_reset()`, or `mylite_finalize()` on that statement.

## Errors

```c
int mylite_errcode(mylite_db *db);
int mylite_extended_errcode(mylite_db *db);
unsigned mylite_mariadb_errno(mylite_db *db);
const char *mylite_sqlstate(mylite_db *db);
const char *mylite_errmsg(mylite_db *db);
unsigned mylite_warning_count(mylite_db *db);
```

MariaDB warnings matter. The API should expose them, not collapse everything
into a single success/error bit. `mylite_errcode()` is the stable MyLite
classification, while MariaDB errno and SQLSTATE remain available for callers
that need server-compatible diagnostics.

Later:

```c
int mylite_warning(
    mylite_db *db,
    unsigned index,
    unsigned *level,
    unsigned *code,
    const char **message);
```

## Statement effects

```c
long long mylite_changes(mylite_db *db);
unsigned long long mylite_last_insert_id(mylite_db *db);
```

Affected-row counts and generated autoincrement ids are part of MariaDB
observable behavior. They should be exposed as MyLite APIs rather than requiring
callers to reach into an internal `MYSQL *`.

The first implementation exposes these values from the handle's last statement
and also implements `mylite_warning_count()` from the diagnostics section above.
`mylite_changes()` returns `-1` when MariaDB reports its standard not-applicable
or failed-statement sentinel. `mylite_last_insert_id()` and
`mylite_warning_count()` return zero for a null or inactive handle. Warning
details are still retrieved with SQL such as `SHOW WARNINGS`; structured
warning enumeration remains future work.

## Configuration

```c
int mylite_busy_timeout(mylite_db *db, unsigned milliseconds);
int mylite_set_durability(mylite_db *db, int durability);
int mylite_limit(mylite_db *db, int limit_id, long long value);
```

Durability modes:

```c
#define MYLITE_DURABILITY_FULL   2
#define MYLITE_DURABILITY_NORMAL 1
#define MYLITE_DURABILITY_OFF    0
```

The public API should expose MyLite concepts, not raw `my.cnf` option names.

## Compatibility adapter

A later optional adapter can expose the MariaDB C API:

```c
MYSQL *mylite_mysql_handle(mylite_db *db);
```

That adapter is useful for existing code, but it should not define the primary
library semantics. The primary semantics are file ownership and explicit
resource handles.

## Threading

Initial policy:

- A `mylite_db` handle is not used concurrently unless configured for
  serialized mode.
- Different handles may be used on different threads.
- Handles opened on the same file share process-local locking.
- MyLite should preserve useful in-process write concurrency where the storage
  design can do so safely.
- Cross-process write concurrency is not promised in v1.

Future policy can add SQLite-style threading modes once they are backed by real
tests.

## Unsupported server features in v1

The first API should reject or no-op these features clearly:

- network users and authentication,
- replication and binlog,
- Galera/wsrep,
- dynamic plugin installation,
- external storage engines,
- server audit plugins,
- `LOAD DATA LOCAL` over network protocol,
- event scheduler,
- performance schema,
- cross-process write concurrency.

Unsupported features should fail with stable MyLite error codes and MariaDB
diagnostics where possible.
