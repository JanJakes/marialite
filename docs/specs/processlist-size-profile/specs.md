# Processlist Size Profile

## Problem

The aggressive MyLite minsize profile still exposes MariaDB's process-list
metadata surfaces:

- `SHOW PROCESSLIST`
- `COM_PROCESS_INFO`
- `INFORMATION_SCHEMA.PROCESSLIST`

These are daemon administration surfaces. They enumerate server threads,
network clients, host/user context, command state, query text, and progress
fields. MyLite's target runtime is an in-process library opened through the
MyLite C API, with no network listener and no server process-management
contract.

Current measurements from `build/mariadb-minsize-no-static-show-info` show the
linked smoke still contains:

| Symbol | Bytes |
| --- | ---: |
| `mysqld_list_processes()` | 2,264 |
| `fill_schema_processlist()` | 1,564 |
| `Show::processlist_fields_info` | 1,512 |
| `Show::proc_fields_info` | 2,304 |

`Show::proc_fields_info` belongs to stored-routine metadata and is not in scope
for this slice; it appears near the processlist symbols in the linked binary
but should remain governed by the routine Information Schema profile.

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/sql_yacc.yy` parses `SHOW PROCESSLIST` into
  `SQLCOM_SHOW_PROCESSLIST`.
- `vendor/mariadb/server/sql/sql_parse.cc` routes `COM_PROCESS_INFO` and
  `SQLCOM_SHOW_PROCESSLIST` to `mysqld_list_processes()`.
- `vendor/mariadb/server/sql/sql_show.cc` defines `mysqld_list_processes()`,
  process-list callback helpers, `fill_schema_processlist()`, and the
  `PROCESSLIST` Information Schema table descriptor.
- `vendor/mariadb/server/sql/sql_show.cc` also defines
  `thd_get_error_context_description()`, which still needs
  `thread_state_info()` for diagnostics. This slice must not remove that helper.

## Scope

This slice may:

- add `MYLITE_DISABLE_PROCESSLIST_METADATA`,
- enable it from `tools/build-mariadb-minsize.sh`,
- reject `SHOW PROCESSLIST` and `COM_PROCESS_INFO` in the embedded minsize
  profile with `ER_NOT_SUPPORTED_YET`,
- make `INFORMATION_SCHEMA.PROCESSLIST` return an empty result in the
  aggressive minsize profile,
- compile out process-list row rendering and process-list Information Schema
  population code, and
- add smoke coverage for the unsupported diagnostics and absent Information
  Schema table.

## Non-Goals

This slice does not:

- remove thread IDs, THD lifecycle, or scheduler state,
- remove `KILL` or lock handling,
- remove status variables,
- remove `thread_state_info()` from error-context diagnostics, or
- alter non-minsize builds.

## Proposed Design

Add a minsize-only CMake option. When enabled, `sql_parse.cc` rejects
`COM_PROCESS_INFO` and `SQLCOM_SHOW_PROCESSLIST` before calling
`mysqld_list_processes()`.

In `sql_show.cc`, guard the process-list display helpers and
`fill_schema_processlist()`. Keep the `PROCESSLIST` Information Schema
registration and field descriptor because MariaDB's schema-table array is
indexed by `enum_schema_tables`; removing the row requires a broader enum
maintenance slice. Route the table to the existing empty-table filler instead.
Keep `thread_state_info()` available because error-context formatting still
uses it outside process-list metadata.

## Affected Subsystems

- SQL command dispatch in `sql_parse.cc`.
- Process-list result production in `sql_show.cc`.
- Information Schema table registration.
- MyLite open/close unsupported-profile smoke coverage.
- Minsize CMake configuration.

## Single-File and Embedded Lifecycle Impact

No file-format or storage lifecycle change. The removed surfaces inspect
server-thread state and do not own MyLite files.

## Public API and File-Format Impact

No public `libmylite` API change and no `.mylite` file-format change.

SQL compatibility impact: the aggressive minsize profile no longer supports
`SHOW PROCESSLIST`; `INFORMATION_SCHEMA.PROCESSLIST` remains present but empty.

## Binary-Size Impact

Measured savings are small. On top of `show-static-info-size-profile`, this
reduced the stripped `libmariadbd.a` archive by 41,234 bytes and the stripped
linked open/close smoke by 3,048 bytes.

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmariadbd.a` | 27,609,300 | 27,568,066 | -41,234 |
| `sql_show.cc.o` | 520,288 | 480,464 | -39,824 |
| `sql_parse.cc.o` | 284,056 | 284,016 | -40 |
| unstripped `mylite-open-close-smoke` | 7,349,888 | 7,346,376 | -3,512 |
| stripped `mylite-open-close-smoke` | 5,272,912 | 5,269,864 | -3,048 |
| unstripped `mylite-compatibility-smoke` | 7,209,472 | 7,204,048 | -5,424 |
| stripped `mylite-compatibility-smoke` | 5,155,080 | 5,150,448 | -4,632 |

The linked open/close smoke no longer defines `mysqld_list_processes()` or
`fill_schema_processlist()`. `Show::processlist_fields_info` remains because
`INFORMATION_SCHEMA.PROCESSLIST` is still registered as an empty table.

## License, Trademark, and Dependency Impact

This is a GPL-2.0-only MariaDB-derived build-profile change. It adds no new
dependency and changes no trademark-facing packaging.

## Test and Verification Plan

Run:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-processlist MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-processlist tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-processlist tools/run-storage-engine-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-processlist tools/run-embedded-bootstrap-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-processlist tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh`
- `git diff --check`

All commands above passed.

## Acceptance Criteria

- `SHOW PROCESSLIST` reports `ER_NOT_SUPPORTED_YET` in the minsize profile.
- `INFORMATION_SCHEMA.PROCESSLIST` returns an empty result in the minsize
  profile.
- Ordinary metadata commands covered by the current smokes and compatibility
  harness still pass.
- `thd_get_error_context_description()` remains linked and usable.
- Measured size changes are recorded in
  `docs/research/production-size-analysis.md`.

## Risks and Unresolved Questions

- This is a SQL-visible compatibility reduction. It should stay confined to the
  aggressive minsize profile.
- Returning an empty Information Schema table keeps the MariaDB schema-table
  enum contract intact but leaves the process-list field descriptor linked.
