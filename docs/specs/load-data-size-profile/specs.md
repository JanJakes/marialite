# LOAD DATA Size Profile

## Problem Statement

The aggressive embedded minsize profile still compiles MariaDB's
server-oriented `LOAD DATA` / `LOAD XML` implementation. That command reads
from server-local files or client-supplied local files, participates in
replication-specific logging paths, and is not needed for MyLite's core
in-process SQL execution API.

The current `sql_load.cc.o` archive member is 41,720 bytes. The linked
open/close smoke still retains `mysql_load()`, even though MyLite already
omits the related `LOAD_FILE()` SQL function in the server-utility profile.

Current baseline after `locale-minsize-profile`:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 29,210,614 |
| `sql_load.cc.o` object | 41,720 |
| stripped `mylite-open-close-smoke` | 5,582,144 |

## Source Findings

- Imported MariaDB base: `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- MariaDB documents `LOAD DATA INFILE` as importing data from text files and
  distinguishes server-side files from `LOCAL` client-side files:
  <https://mariadb.com/kb/en/load-data-with-load-data-local-infile/>.
- MariaDB documents `SELECT INTO OUTFILE` and `SELECT INTO DUMPFILE` as writing
  result data to files on the server filesystem:
  <https://mariadb.com/kb/en/select-into-outfile/> and
  <https://mariadb.com/kb/en/select-into-dumpfile/>.
- `vendor/mariadb/server/sql/sql_yacc.yy` parses `LOAD DATA` and `LOAD XML`
  into `SQLCOM_LOAD`, creates a `sql_exchange`, and attaches the destination
  table to the statement.
- `vendor/mariadb/server/sql/sql_parse.cc` dispatches `SQLCOM_LOAD` to
  `mysql_load()` after local-infile capability checks and table privilege
  checks.
- `vendor/mariadb/server/sql/sql_load.cc` includes server file IO,
  `LOAD DATA LOCAL`, XML row parsing, binlog load events, replication helpers,
  trigger invocation, and row insertion glue.
- `SELECT INTO OUTFILE` / `DUMPFILE` is parser-entangled through
  `yy_mariadb.cc` actions that instantiate `select_export` / `select_dump`
  classes from `sql_class.cc`. Removing that linked surface requires a
  generated-parser slice, so this attempt focuses only on the clean
  `mysql_load()` object.

## Scope

Add a minsize option that removes `LOAD DATA` / `LOAD XML` execution from the
embedded library.

The option will:

- define `MYLITE_DISABLE_LOAD_DATA` for the aggressive minsize profile;
- remove `../sql/sql_load.cc` from `SQL_EMBEDDED_SOURCES`;
- make `SQLCOM_LOAD` return MariaDB's embedded-disabled diagnostic before file,
  privilege, table, or row-loading work; and
- add open/close smoke coverage for representative `LOAD DATA` and `LOAD XML`
  statements.

## Non-Goals

- Do not remove ordinary `INSERT`, multi-row `INSERT`, or prepared-statement
  parameter binding.
- Do not remove `SELECT INTO OUTFILE` / `DUMPFILE` in this slice; parser action
  references make that a separate generated-parser maintenance decision.
- Do not change non-embedded MariaDB behavior.
- Do not add a MyLite-native bulk import API.

## Proposed Design

Add `MYLITE_DISABLE_LOAD_DATA` as a top-level MariaDB CMake option and enable
it in `tools/build-mariadb-minsize.sh`.

When the option is enabled, `vendor/mariadb/server/libmysqld/CMakeLists.txt`
removes `../sql/sql_load.cc` from the embedded SQL source list.

Patch the `SQLCOM_LOAD` branch in `mysql_execute_command()` so the disabled
profile returns:

```c++
my_error(ER_OPTION_PREVENTS_STATEMENT, MYF(0), "embedded");
```

The original `mysql_load()` dispatch remains compiled only when the option is
off. That keeps the minsize link from rooting `sql_load.cc.o`.

## Affected Subsystems

- Embedded minsize SQL source list.
- `SQLCOM_LOAD` command dispatch.
- `LOAD DATA` / `LOAD XML` SQL compatibility.
- Open/close smoke unsupported-surface coverage.
- Binary-size documentation.

## DDL Metadata Routing Impact

None. `LOAD DATA` is a DML/bulk-import command, not a table-definition path.

## Single-File And Embedded-Lifecycle Impact

This removes server-host file import and client-local infile execution from the
aggressive embedded profile. It avoids a path that reads arbitrary external
files and has replication/logging behavior outside MyLite's file-owned
lifecycle. It does not add sidecars or file-format state.

## Public API Or File-Format Impact

No public `libmylite` API or `.mylite` file-format change.

SQL compatibility impact: `LOAD DATA` and `LOAD XML` return the embedded
disabled-feature diagnostic in the aggressive minsize profile. Applications can
still import data through ordinary `INSERT` statements or prepared statement
binding.

## Binary-Size Impact

Expected archive savings are approximately the current 41,720-byte
`sql_load.cc.o` member. Linked savings should be small because section GC
already drops much of the object, but the live `mysql_load()` command path
should disappear.

## License, Trademark, And Dependency Impact

No new dependency or license change.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-load-data \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-load-data \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-load-data \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-load-data \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Add open/close smoke checks that verify:

- `LOAD DATA INFILE ... INTO TABLE ...` returns
  `ER_OPTION_PREVENTS_STATEMENT`;
- `LOAD XML INFILE ... INTO TABLE ...` returns
  `ER_OPTION_PREVENTS_STATEMENT`; and
- ordinary prepared inserts still work through existing smoke coverage.

Measure:

- archive bytes and object count;
- unstripped and stripped linked smoke bytes;
- absence of `sql_load.cc.o` in `libmariadbd.a`; and
- absence of `mysql_load()` in the linked smoke.

## Acceptance Criteria

- The minsize build completes.
- Embedded bootstrap, open/close smoke, and compatibility harness pass.
- `LOAD DATA` and `LOAD XML` fail explicitly in the aggressive profile.
- Ordinary INSERT and prepared statement coverage still passes.
- The embedded archive no longer contains `sql_load.cc.o`.
- Size results are recorded here and in
  `docs/research/production-size-analysis.md`.

## Implementation Results

Implemented in the aggressive minsize profile with
`MYLITE_DISABLE_LOAD_DATA=ON`.

Final measurement from
`MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-load-data`:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 29,169,370 |
| `mylite/libmylite.a` | 122,792 |
| `storage/mylite/libmylite_embedded.a` | 388,440 |
| `mylite/mylite-open-close-smoke` | 7,755,752 |
| stripped `mylite-open-close-smoke` | 5,570,344 |
| `mylite/mylite-compatibility-smoke` | 7,627,040 |
| stripped `mylite-compatibility-smoke` | 5,462,704 |
| `mylite_load_data_stub.cc.o` | 2,008 |

Delta from the `locale-minsize-profile` baseline:

- `libmariadbd.a`: -41,244 bytes.
- stripped `mylite-open-close-smoke`: -11,800 bytes.
- stripped `mylite-compatibility-smoke`: -13,064 bytes.

The archive no longer contains `sql_load.cc.o`, and the linked open/close
smoke no longer contains `mysql_load()`. The parser-rooted
`select_export` / `select_dump` symbols remain, as expected, because
`SELECT INTO OUTFILE` / `DUMPFILE` parser actions are outside this slice.

Open/close smoke coverage records:

```text
exec_load_data_messages=The MariaDB server is running with the embedded option so it cannot execute this statement | The MariaDB server is running with the embedded option so it cannot execute this statement
```

Verification passed:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-load-data \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-load-data \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-load-data \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-load-data \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh \
  tools/run-embedded-bootstrap-smoke.sh \
  tools/run-libmylite-open-close-smoke.sh \
  tools/run-compatibility-test-harness.sh
git diff --check
```

## Risks And Unresolved Questions

- This removes a real MariaDB bulk-import feature. The tradeoff is acceptable
  only for the most aggressive embedded profile.
- `SELECT INTO OUTFILE` and `SELECT INTO DUMPFILE` remain for now because
  removing their parser roots requires a generated-parser maintenance slice.
- A future MyLite-native import API may be a better fit than re-enabling
  server-host file reads.
