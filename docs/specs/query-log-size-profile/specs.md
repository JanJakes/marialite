# Query Log Size Profile

## Problem

The aggressive MyLite minsize profile still retains MariaDB's server query-log
machinery: general query logs, slow query logs, and CSV log-table handlers.
Those are daemon observability surfaces, not embedded database semantics. They
can also write inherited sidecar files such as `hostname.log` and
`hostname-slow.log`, or route rows to inherited `mysql.general_log` and
`mysql.slow_log` tables.

MyLite should keep error logging for startup diagnostics, but the aggressive
embedded profile does not need general or slow query logging.

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/log.h` defines `MYSQL_QUERY_LOG`,
  `Log_to_csv_event_handler`, `Log_to_file_event_handler`, and `LOGGER`.
  `LOGGER` owns separate handler lists for error, slow, and general logs.
- `vendor/mariadb/server/sql/log.cc` constructs a global `LOGGER logger`.
  `LOGGER::init_base()` creates a file handler for error, slow, and general
  file logging. `LOGGER::init_log_tables()` creates the CSV table handler.
- `LOGGER::error_log_print()` routes error messages through the enabled error
  handlers. The file handler's `log_error()` delegates to `vprint_msg_to_log()`.
- `LOGGER::slow_log_print()`, `LOGGER::general_log_write()`, and
  `LOGGER::general_log_print()` format user/host, command names, timestamps,
  query text, and slow-query timing before writing to file or CSV handlers.
- `MYSQL_QUERY_LOG::write()` implements traditional general and slow query log
  file formatting. These methods are still visible in the current linked
  open/close smoke binary.
- `vendor/mariadb/server/sql/sys_vars.cc` exposes `general_log`,
  `slow_query_log`, `log_slow_query`, `general_log_file`,
  `slow_query_log_file`, `log_slow_query_file`, and `log_output`. The update
  callbacks open or close query log handlers through `logger`.
- `vendor/mariadb/server/sql/mysqld.cc` initializes log tables and log handlers
  during server startup after storage engines are initialized.

Pre-slice linked evidence from
`build/mariadb-minsize-no-option-help-text/mylite/mylite-open-close-smoke`
includes:

- `LOGGER::slow_log_print()`
- `LOGGER::general_log_write()`
- `Log_to_csv_event_handler::log_general()`
- `Log_to_csv_event_handler::log_slow()`
- `Log_to_file_event_handler::log_general()`
- `Log_to_file_event_handler::log_slow()`
- `MYSQL_QUERY_LOG::write()`
- `MYSQL_QUERY_LOG::reopen_file()`

## Scope

This slice may:

- add `MYLITE_DISABLE_QUERY_LOGS`,
- enable it from `tools/build-mariadb-minsize.sh`,
- keep error logging routed through `vprint_msg_to_log()`,
- disable general and slow query logging in the embedded minsize profile,
- keep log-related system-variable names queryable,
- make attempts to enable `general_log`, `slow_query_log`, or
  `log_slow_query` fail with an unsupported-feature diagnostic,
- make `log_output` default to `NONE` and reject non-`NONE` values in the
  aggressive profile, and
- add smoke and symbol checks that the removed query-log runtime is absent.

## Non-Goals

This slice does not:

- remove MariaDB error logging,
- remove all log-related system variables,
- remove binlog, replication, DDL log, or transaction-coordinator stubs,
- change SQL auditing; the embedded build already compiles audit notifications
  to no-ops,
- change public `libmylite` APIs,
- change `.mylite` file format, or
- change non-minsize builds.

## Proposed Design

Add a minsize-only CMake option and compile the query-log parts of `LOGGER` to
no-ops when the option is enabled.

`LOGGER::error_log_print()` should call `vprint_msg_to_log()` directly in this
profile. That keeps existing error diagnostics without constructing the file
log handler whose members also carry general and slow query log state.

`LOGGER::init_log_tables()`, `init_slow_log()`, `init_general_log()`,
`set_handlers()`, `activate_log_handler()`, `deactivate_log_handler()`,
`flush_slow_log()`, `flush_general_log()`, `slow_log_print()`,
`general_log_print()`, and `general_log_write()` should become inert for query
logging. The public wrappers in `log.h` stay link-compatible.

In `sys_vars.cc`, `general_log`, `slow_query_log`, and `log_slow_query`
updates should reject attempts to enable query logging. `log_output` should
default to `NONE` and reject non-`NONE` values. Other log tuning variables can
remain visible for compatibility, but they should not open files or log tables
in this profile.

## Affected Subsystems

- Embedded minsize CMake configuration.
- Server logging in `log.cc` and `log.h`.
- Logging system variables in `sys_vars.cc`.
- Open/close smoke coverage and binary symbol checks.
- Production size analysis.

## Single-File and Embedded Lifecycle Impact

This removes inherited general/slow query log file and table activation from
the aggressive embedded profile. It aligns the runtime with MyLite's current
sidecar policy: query logging must not create daemon-style files or write
server log tables outside the `.mylite` catalog.

Error logging remains available for startup and diagnostics.

## Public API and File-Format Impact

No public `libmylite` API change and no `.mylite` file-format change.

SQL compatibility impact is limited to daemon query-log configuration:
`general_log`, `slow_query_log`, `log_slow_query`, and non-`NONE`
`log_output` settings are unsupported in the aggressive embedded profile.

## Binary-Size Impact

Measured against `build/mariadb-minsize-no-option-help-text`, the implemented
profile reduced:

- `libmariadbd.a` from 27,357,408 bytes to 27,309,098 bytes
  (-48,310 bytes),
- unstripped `mylite-open-close-smoke` from 7,246,384 bytes to 7,225,792 bytes
  (-20,592 bytes),
- stripped `mylite-open-close-smoke` from 5,183,976 bytes to 5,168,408 bytes
  (-15,568 bytes),
- unstripped `mylite-compatibility-smoke` from 7,096,544 bytes to
  7,073,080 bytes (-23,464 bytes), and
- stripped `mylite-compatibility-smoke` from 5,058,136 bytes to 5,039,896 bytes
  (-18,240 bytes).

The main implementation object deltas were `log.cc.o` 242,416 bytes to
195,512 bytes (-46,904 bytes) and `sys_vars.cc.o` 617,112 bytes to
616,536 bytes (-576 bytes). The last handler-body guard saved 38,208 bytes
from the static archive without a meaningful linked-runtime win because the
final executable already discarded those unreachable query-log sections. The
open/close smoke object grew by 7,448 bytes because it now verifies query-log
profile behavior.

## License, Trademark, and Dependency Impact

This is a GPL-2.0-only MariaDB-derived build-profile change. It adds no new
dependency and changes no trademark-facing packaging.

## Test and Verification Plan

Run:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-query-logs MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-query-logs tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-query-logs tools/run-storage-engine-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-query-logs tools/run-embedded-bootstrap-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-query-logs tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh`
- `bash -n tools/run-libmylite-open-close-smoke.sh`
- `git diff --check`

Executed verification:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-query-logs MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-query-logs tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-query-logs tools/run-storage-engine-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-query-logs tools/run-embedded-bootstrap-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-query-logs tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh`
- `bash -n tools/run-libmylite-open-close-smoke.sh`
- `git diff --check`

Measure:

- archive bytes,
- unstripped and stripped open/close smoke bytes,
- unstripped and stripped compatibility smoke bytes,
- `log.cc.o` bytes, and
- absence of representative query-log symbols in the linked open/close smoke.

## Acceptance Criteria

- Current open/close, storage, bootstrap, and compatibility smokes pass.
- `SHOW VARIABLES LIKE 'general_log'` reports `OFF`.
- `SHOW VARIABLES LIKE 'slow_query_log'` reports `OFF`.
- `SHOW VARIABLES LIKE 'log_output'` reports `NONE`.
- `SET GLOBAL general_log=ON`, `SET GLOBAL slow_query_log=ON`,
  `SET GLOBAL log_slow_query=ON`, and `SET GLOBAL log_output='FILE'` fail
  explicitly.
- The linked open/close smoke no longer defines representative query-log
  runtime symbols such as `MYSQL_QUERY_LOG::write()` and
  `Log_to_csv_event_handler::log_slow()`.
- Size changes are recorded in
  `docs/research/production-size-analysis.md`.

## Risks and Unresolved Questions

- Query logs are useful server observability. This is appropriate for the
  aggressive embedded profile only, not for a full MariaDB-compatible server
  build.
- Some retained SQL paths still call `log_slow_statement()` for status
  bookkeeping. This slice should avoid broad SQL parse/dispatch edits unless
  measurement proves that further pruning is worth the compatibility risk.
