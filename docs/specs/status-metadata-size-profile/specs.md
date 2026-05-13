# Status Metadata Size Profile

## Problem

The aggressive MyLite minsize profile still publishes MariaDB daemon status
metadata through `SHOW STATUS`, `INFORMATION_SCHEMA.GLOBAL_STATUS`, and
`INFORMATION_SCHEMA.SESSION_STATUS`.

Those surfaces are server-observability metadata, not application data or
file-format behavior. They retain large static `SHOW_VAR` arrays and dynamic
status-variable registry code even though MyLite has no network daemon,
replication service, external plugin loading, query cache, or performance
schema in the current embedded profile.

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- MariaDB documents `SHOW STATUS` as exposing status values, with `GLOBAL`
  showing values across all connections. Source:
  https://mariadb.com/kb/en/show-status/
- MariaDB documents `INFORMATION_SCHEMA.GLOBAL_STATUS` and
  `INFORMATION_SCHEMA.SESSION_STATUS` as tables for status variable values.
  Source:
  https://mariadb.com/kb/en/information-schema-global_status-and-session_status-tables/
- `vendor/mariadb/server/sql/mysqld.cc` defines the large
  `com_status_vars[]` statement counter table and `status_vars[]` server
  status table.
- `init_common_variables()` calls `add_status_vars(status_vars)`, and embedded
  startup later calls `init_status_vars()`.
- `refresh_global_status()` calls `reset_status_vars()` before clearing the
  retained internal `global_status_var` counters.
- `vendor/mariadb/server/sql/sql_show.cc` owns `all_status_vars`,
  `add_status_vars()`, `remove_status_vars()`, `init_status_vars()`,
  `reset_status_vars()`, `free_status_vars()`, and `fill_status()`.
- `fill_status()` populates `SHOW STATUS` and the two Information Schema status
  tables by walking `all_status_vars`.
- `schema_tables[]` registers `GLOBAL_STATUS` and `SESSION_STATUS` with
  `fill_status` and `make_old_format`.

## Scope

This slice may:

- add `MYLITE_DISABLE_STATUS_METADATA`,
- enable it from `tools/build-mariadb-minsize.sh`,
- omit the static `status_vars[]` and `com_status_vars[]` publication arrays
  from the embedded minsize build,
- replace the dynamic status-variable publication registry with no-op stubs,
- make `SHOW STATUS`, `SHOW GLOBAL STATUS`,
  `INFORMATION_SCHEMA.GLOBAL_STATUS`, and
  `INFORMATION_SCHEMA.SESSION_STATUS` return empty result sets, and
- add open/close smoke coverage and size measurements.

## Non-Goals

This slice does not:

- remove `global_status_var` or per-session `THD::status_var` counters,
- remove statement status counter increments,
- remove `SHOW VARIABLES` or `INFORMATION_SCHEMA.*_VARIABLES`,
- remove `SHOW WARNINGS`, `SHOW ERRORS`, diagnostics, or public MyLite
  diagnostics,
- remove `RESET STATUS` parsing or internal counter reset behavior, or
- change non-minsize builds.

## Proposed Design

Add a minsize-only CMake option. When enabled:

- `mysqld.cc` should not define the publication-only `status_vars[]` and
  `com_status_vars[]` arrays and should skip `add_status_vars(status_vars)`.
- `sql_show.cc` should keep `show_status_array()` and `get_one_variable()`,
  because `SHOW VARIABLES` and Information Schema variables still use them.
- `sql_show.cc` should replace status publication registry functions with
  no-op stubs so retained plugin startup/shutdown calls do not allocate or sort
  status metadata.
- `fill_status()` should return `mylite_fill_empty_schema_table()` so existing
  result metadata stays stable while the profile returns zero status rows.

Returning empty status rows is preferable to removing the schema table entries:
MariaDB's `enum_schema_tables` indexing is sensitive, and previous MyLite
profile slices already use empty Information Schema tables where unsupported
metadata has no MyLite-owned backing store.

## Affected Subsystems

- Minsize CMake configuration.
- MariaDB startup status-variable publication in `mysqld.cc`.
- Status metadata registry and `fill_status()` in `sql_show.cc`.
- MyLite open/close smoke coverage and production size analysis.

## Single-File and Embedded Lifecycle Impact

No file-format or storage lifecycle change. Internal counters remain available
to retained server code; only SQL publication of daemon status metadata is
removed from the aggressive embedded profile.

## Public API and File-Format Impact

No public `libmylite` API change and no `.mylite` file-format change.

SQL compatibility impact: in the aggressive minsize profile, `SHOW STATUS` and
the Information Schema status tables return no rows. This is a deliberate
server-observability compatibility tradeoff.

## Binary-Size Impact

Savings are small but measurable. Against
`build/mariadb-minsize-no-plsql-cursor-attributes`, this slice reduced:

- `libmariadbd.a` by 37,644 bytes,
- unstripped `mylite-open-close-smoke` by 16,184 bytes,
- stripped `mylite-open-close-smoke` by 14,760 bytes,
- unstripped `mylite-compatibility-smoke` by 18,712 bytes, and
- stripped `mylite-compatibility-smoke` by 17,320 bytes.

`sql_show.cc.o` dropped from 480,464 bytes to 474,896 bytes. The exact
`status_vars` and `com_status_vars` publication symbols are absent from the
linked open/close smoke. The internal `global_status_var` and related retained
counter storage remain because SQL execution paths still update those counters.

## License, Trademark, and Dependency Impact

This is a GPL-2.0-only MariaDB-derived build-profile change. It adds no new
dependency and changes no trademark-facing packaging.

## Test and Verification Plan

Run:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-status-metadata MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-status-metadata tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-status-metadata tools/run-storage-engine-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-status-metadata tools/run-embedded-bootstrap-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-status-metadata tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh`
- `bash -n tools/run-libmylite-open-close-smoke.sh`
- `git diff --check`

## Verification Results

All planned checks passed against
`build/mariadb-minsize-no-status-metadata`. The open/close smoke recorded:

`show_status=0,show_global_status=0,global_status=0,session_status=0,variables=version:11.8.6-MariaDB-embedded`

## Acceptance Criteria

- `SHOW STATUS` and `SHOW GLOBAL STATUS` return zero rows in the aggressive
  minsize profile.
- `INFORMATION_SCHEMA.GLOBAL_STATUS` and
  `INFORMATION_SCHEMA.SESSION_STATUS` return zero rows.
- `SHOW VARIABLES` still returns ordinary variable rows.
- Internal diagnostics, warnings, prepared statements, and current storage
  smokes still pass.
- Linked open-close smoke no longer defines exact `status_vars` or
  `com_status_vars` symbols.
- Measured size changes are recorded in
  `docs/research/production-size-analysis.md`.

## Risks and Unresolved Questions

- Some clients use `SHOW STATUS` for health checks even in embedded contexts.
  The aggressive profile can make that tradeoff, but a compatibility profile
  may need status publication back.
- Savings may be capped because internal status counters and the generic
  `SHOW VARIABLES` rendering machinery remain linked.
