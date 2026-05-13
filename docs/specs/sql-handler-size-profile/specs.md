# SQL Handler Size Profile

## Problem Statement

MariaDB exposes `HANDLER ... OPEN`, `HANDLER ... READ`, and
`HANDLER ... CLOSE` as SQL statements that give direct cursor-style access to
storage-engine handlers. This is an inherited server/ISAM surface, not the
public MyLite API, and it bypasses the normal SQL result path MyLite is
building around.

The aggressive embedded size profile should reject SQL `HANDLER` statements and
drop the full `sql_handler.cc` implementation while keeping no-op cleanup hooks
for generic table-close and DDL paths.

## Source Findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

Relevant local source paths:

- `vendor/mariadb/server/sql/sql_handler.cc:18` describes
  `HANDLER ... commands - direct access to ISAM`.
- `vendor/mariadb/server/sql/sql_yacc.yy:17794` parses top-level SQL
  `HANDLER` statements and sets `SQLCOM_HA_OPEN`, `SQLCOM_HA_CLOSE`, or
  `SQLCOM_HA_READ`.
- `vendor/mariadb/server/sql/sql_parse.cc:5577` dispatches these commands to
  `mysql_ha_open()`, `mysql_ha_close()`, and `mysql_ha_read()`.
- `vendor/mariadb/server/sql/sql_base.cc:384`, `sql_base.cc:436`, and
  `sql_base.cc:4669` call SQL-handler cleanup helpers when tables are flushed
  or reopened.
- `vendor/mariadb/server/sql/sql_admin.cc:595` calls `mysql_ha_rm_tables()`
  before inherited table maintenance commands.
- `vendor/mariadb/server/sql/sql_handler.h` declares the SQL-handler helper
  API shared by those callers.

Current linked symbol evidence from
`build/mariadb-minsize-no-fulltext-match`:

| Symbol group | Visible linked bytes |
| --- | ---: |
| `mysql_ha_*`, `SQL_HANDLER`, and direct `HANDLER` helper symbols | about 4 KiB |

The current `sql_handler.cc.o` object is 26,672 bytes in the minsize build.

## Design

Add a minsize option named `MYLITE_DISABLE_SQL_HANDLER_COMMAND`.

When enabled:

- parser actions for SQL `HANDLER ...` statements fail with
  `ER_NOT_SUPPORTED_YET`;
- `libmysqld` removes `../sql/sql_handler.cc` from the embedded source list;
- a small `mylite_sql_handler_stub.cc` keeps the `mysql_ha_*` cleanup API
  available for generic table and DDL callers;
- the cleanup helpers become no-ops because no SQL handler can be opened;
- `mysql_ha_open()`, `mysql_ha_close()`, `mysql_ha_read()`, and
  `mysql_ha_read_prepare()` return an explicit unsupported diagnostic if an
  internal path reaches them unexpectedly.

Do not remove MariaDB's generic `handler` storage-engine abstraction. This
slice removes only the SQL `HANDLER` command surface.

## Non-Goals

- Do not change ordinary SQL `SELECT`, cursor, or prepared-statement behavior.
- Do not remove storage-engine `handler` methods used by the SQL executor.
- Do not change stored-program `DECLARE ... HANDLER` syntax; it is a separate
  grammar surface.
- Do not enable `MYLITE_DISABLE_MYISAM_TEMP_SPILL`.

## Affected Subsystems

- Minsize CMake options and build script.
- MariaDB parser actions for top-level SQL `HANDLER` statements.
- Embedded SQL source list and a MyLite minsize stub.
- MyLite open/close unsupported-profile smoke coverage.

## DDL Metadata Routing Impact

No table-definition routing change. SQL `HANDLER` opens live table handles and
keeps statement-independent cursor state; it does not define durable metadata.

## Single-File And Embedded-Lifecycle Impact

The change removes inherited session cursor state that can hold table handles
open across statements. That is aligned with MyLite's explicit handle-owned API
and makes table-close/DDL cleanup paths simpler in the aggressive embedded
profile. It does not change file format, locking, recovery, or sidecar rules.

## Public API Or File-Format Impact

No public `libmylite` C API change and no `.mylite` file-format change.

SQL compatibility impact: the aggressive minsize profile no longer supports
MariaDB SQL `HANDLER` statements. This is intentionally narrower than removing
the storage-engine handler interface.

## Binary-Size Impact

Savings are small. On top of `fulltext-match-size-profile`, this slice
reduced:

- `libmysqld/libmariadbd.a` from 26,454,822 bytes to 26,434,272 bytes,
  saving 20,550 bytes;
- unstripped linked `mylite-open-close-smoke` from 6,860,560 bytes to
  6,853,264 bytes, saving 7,296 bytes;
- stripped linked `mylite-open-close-smoke` from 4,836,264 bytes to
  4,829,616 bytes, saving 6,648 bytes.

The remaining linked `mysql_ha_*` symbols are tiny stubs. The embedded archive
no longer contains the full `sql_handler.cc.o` object.

## License, Trademark, And Dependency Impact

No new dependency or license change.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-handler \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-handler \
  tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-handler \
  tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-handler \
  tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-handler \
  tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh
git diff --check
```

Measure:

```sh
llvm-nm --demangle --size-sort --print-size --radix=d \
  build/mariadb-minsize-no-sql-handler/mylite/mylite-open-close-smoke |
  rg 'mysql_ha_|SQL_HANDLER'
```

Verified:

- `tools/build-mariadb-minsize.sh`
- `tools/run-libmylite-open-close-smoke.sh`
- `tools/run-storage-engine-smoke.sh`
- `tools/run-embedded-bootstrap-smoke.sh`
- `tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh`
- `git diff --check`

## Acceptance Criteria

- SQL `HANDLER ...` reports `ER_NOT_SUPPORTED_YET` in the minsize profile.
- Open/close, storage-engine, embedded-bootstrap, and compatibility smokes
  pass.
- Generic table cleanup and DDL paths still link with no-op SQL-handler helper
  stubs.
- The embedded archive no longer contains `sql_handler.cc.o`.
- Size deltas are recorded in this spec and production size analysis.

## Risks And Unresolved Questions

- Some MariaDB applications use SQL `HANDLER` for low-level engine reads. This
  is a high SQL-compatibility tradeoff even though it has low embedded value.
- If future MyLite APIs expose explicit cursor handles, they should be
  first-party `mylite_*` handles rather than reviving SQL `HANDLER`.
