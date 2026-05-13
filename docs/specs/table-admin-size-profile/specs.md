# Table Admin Size Profile

## Problem Statement

The aggressive embedded minsize profile still compiles MariaDB's table
maintenance implementation in `sql_admin.cc`. That code implements
server-oriented `CHECK TABLE`, `REPAIR TABLE`, `ANALYZE TABLE`,
`OPTIMIZE TABLE`, `CACHE INDEX`, and `LOAD INDEX INTO CACHE` behavior,
including key-cache assignment, MyISAM repair paths, view check/repair hooks,
and table recreation fallbacks.

MyLite's current embedded profile already removes or hides the durable engines
that make most of those maintenance paths useful. The remaining MyLite storage
engine should eventually expose MyLite-owned integrity and statistics
maintenance, not MariaDB server maintenance commands that can assume `.frm`,
engine sidecars, key caches, and table-repair workflows.

Current baseline after `view-runtime-size-profile`:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 29,810,458 |
| `sql_admin.cc.o` object | 151,472 |
| stripped `mylite-open-close-smoke` | 5,691,984 |

## Source Findings

- Imported MariaDB base: `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/sql_admin.cc` implements the shared
  `mysql_admin_table()` machinery plus `Sql_cmd_analyze_table`,
  `Sql_cmd_check_table`, `Sql_cmd_optimize_table`, and
  `Sql_cmd_repair_table`.
- `vendor/mariadb/server/sql/sql_parse.cc` dispatches
  `SQLCOM_ASSIGN_TO_KEYCACHE` to `mysql_assign_to_keycache()` and
  `SQLCOM_PRELOAD_KEYS` to `mysql_preload_keys()`.
- `vendor/mariadb/server/sql/sql_parse.cc` dispatches `SQLCOM_ANALYZE`,
  `SQLCOM_CHECK`, `SQLCOM_OPTIMIZE`, and `SQLCOM_REPAIR` through the
  parser-created `lex->m_sql_cmd` objects.
- `vendor/mariadb/server/sql/sql_prepare.cc` still needs
  `fill_check_table_metadata_fields()` to describe the four-column result
  shape for prepared admin statements.
- `vendor/mariadb/server/sql/sql_partition_admin.cc` derives partition-admin
  command classes from the base table-admin command classes and calls their
  `execute()` methods. The replacement must therefore keep the base class
  methods linkable.
- `vendor/mariadb/server/sql/sql_yacc.yy` includes `sql_admin.h` and creates
  table-admin command objects during parsing, so the minsize profile should
  replace the implementation object rather than remove parser classes.

## Scope

Add a minsize option that removes MariaDB's full table-admin implementation
from the embedded library. The option will:

- remove `../sql/sql_admin.cc` from `SQL_EMBEDDED_SOURCES`;
- add a MyLite-owned table-admin stub;
- preserve prepared-statement metadata for table-admin result sets;
- return unsupported diagnostics for table maintenance, key-cache assignment,
  and index preload commands; and
- keep non-embedded MariaDB behavior unchanged.

## Non-Goals

- Do not implement MyLite-native integrity checks, repair, statistics refresh,
  or vacuum/compact behavior.
- Do not change parser syntax for table-admin statements.
- Do not change `CHECKSUM TABLE`, ordinary DML, DDL, or SELECT behavior.
- Do not remove key-cache system variables in this slice.
- Do not remove partition-admin parser classes in this slice.

## Proposed Design

Add `MYLITE_DISABLE_TABLE_ADMIN` to
`vendor/mariadb/server/libmysqld/CMakeLists.txt` and enable it in
`tools/build-mariadb-minsize.sh`.

Create `vendor/mariadb/server/libmysqld/mylite_table_admin_stub.cc`. The stub
will:

- keep `fill_check_table_metadata_fields()` equivalent to upstream so prepared
  statement metadata remains stable;
- implement `mysql_assign_to_keycache()` and `mysql_preload_keys()` as
  unsupported embedded commands;
- implement `Sql_cmd_analyze_table::execute()`,
  `Sql_cmd_check_table::execute()`, `Sql_cmd_optimize_table::execute()`, and
  `Sql_cmd_repair_table::execute()` as unsupported embedded commands; and
- leave partition-admin derived classes linked through those base methods.

## Affected Subsystems

- Embedded minsize SQL source list.
- Table maintenance SQL command execution.
- Key cache and index preload SQL command execution.
- Prepared statement metadata for table-admin statements.
- Binary-size documentation and smoke coverage.

## DDL Metadata Routing Impact

No supported DDL metadata routing changes. `REPAIR TABLE ... USE_FRM` and
`OPTIMIZE TABLE` table-recreation fallbacks are removed from the aggressive
embedded profile rather than redirected to MyLite storage.

## Single-File And Embedded-Lifecycle Impact

This removes inherited server maintenance paths that can assume external table
definition files, key-cache state, and engine-specific repair side effects. It
does not add MyLite companion files and does not change `.mylite` file
ownership.

## Public API Or File-Format Impact

No public `libmylite` API or `.mylite` file-format change.

## Binary-Size Impact

Expected archive savings are bounded by the 151,472-byte `sql_admin.cc.o`
member minus the replacement metadata/unsupported-command stub. Linked savings
may be smaller because parser command classes and partition-admin wrappers
remain live.

Measured after implementation:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 29,810,458 | 29,778,266 | -32,192 |
| unstripped `mylite-open-close-smoke` | 7,916,352 | 7,894,368 | -21,984 |
| stripped `mylite-open-close-smoke` | 5,691,984 | 5,674,104 | -17,880 |
| unstripped `mylite-compatibility-smoke` | 7,792,880 | 7,768,712 | -24,168 |
| stripped `mylite-compatibility-smoke` | 5,588,856 | 5,569,024 | -19,832 |
| `size` decimal for `mylite-open-close-smoke` | 5,929,617 | 5,909,185 | -20,432 |

The archive no longer contains `sql_admin.cc.o`. It contains
`mylite_table_admin_stub.cc.o` instead. The replacement object is 119,312 bytes
versus the removed 151,472-byte `sql_admin.cc.o`, so the archive reduction is
modest. Most of the replacement object size comes from retained
`Item_empty_string` metadata construction and C++ COMDAT/vtable sections pulled
in through `item.h`; the linked smoke still saves about 17 KiB after section GC.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-table-admin \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-table-admin \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-table-admin \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-table-admin \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Measure:

- archive bytes and object count;
- unstripped and stripped linked smoke bytes;
- absence of `sql_admin.cc.o` in `libmariadbd.a`;
- presence and size of the replacement stub;
- unsupported diagnostics for `CHECK TABLE`, `REPAIR TABLE`, `ANALYZE TABLE`,
  `OPTIMIZE TABLE`, `CACHE INDEX`, and `LOAD INDEX INTO CACHE`; and
- absence of compatibility-harness regressions.

## Acceptance Criteria

- The minsize build completes.
- Embedded bootstrap, open/close smoke, and compatibility harness pass.
- The open/close smoke verifies table-admin commands report stable unsupported
  diagnostics.
- The embedded archive no longer contains `sql_admin.cc.o`.
- Prepared statement metadata still links through
  `fill_check_table_metadata_fields()`.
- Size results are recorded here and in
  `docs/research/production-size-analysis.md`.

## Verification Results

Passed:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-table-admin \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-table-admin \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-table-admin \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-table-admin \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

The open/close report records explicit unsupported diagnostics for
`ANALYZE TABLE`, `CHECK TABLE`, `OPTIMIZE TABLE`, `REPAIR TABLE`,
`CACHE INDEX`, and `LOAD INDEX INTO CACHE`. The sidecar scan found no `.frm`
or `.FRM` files in the table-admin build directory.

## Risks And Unresolved Questions

- `ANALYZE TABLE` is a real SQL feature and may eventually be useful for
  MyLite-owned optimizer statistics. This slice removes MariaDB's inherited
  table-admin implementation only from the aggressive minsize profile.
- `CHECK TABLE` and `REPAIR TABLE` can be valuable maintenance surfaces, but
  MyLite needs storage-native integrity, recovery, and repair semantics before
  exposing them honestly.
- Key-cache assignment and index preload are MyISAM/key-cache shaped and should
  stay omitted while MyISAM is internal-only.
