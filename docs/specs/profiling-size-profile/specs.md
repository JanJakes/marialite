# profiling-size-profile

## Problem

The current MyLite minsize profile still builds MariaDB's session statement
profiling support. Profiling is a server diagnostic surface used by
`SHOW PROFILE`, `SHOW PROFILES`, the `profiling` session variable, and
`information_schema.PROFILING`. It is not part of MyLite's file-owned embedded
database contract, and every compiled diagnostic surface increases the default
runtime footprint.

This slice tests disabling MariaDB profiling in the minsize profile through the
existing upstream build option instead of adding a MyLite-specific fork delta.

## Source Findings

Base source: MariaDB Server `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/CMakeLists.txt` defines
  `OPTION(ENABLED_PROFILING "Enable profiling" ON)`.
- `vendor/mariadb/server/config.h.cmake` emits `ENABLED_PROFILING` into the
  generated config header when the option is enabled.
- `vendor/mariadb/server/sql/sql_profile.cc` compiles the full profiling
  implementation only under `#if defined(ENABLED_PROFILING)`. The disabled
  path for `fill_query_profile_statistics_info()` reports
  `ER_FEATURE_DISABLED`.
- `vendor/mariadb/server/sql/sql_profile.h` exposes the `PROFILING`,
  `QUERY_PROFILE`, and `PROF_MEASUREMENT` types only when
  `ENABLED_PROFILING` is defined.
- `vendor/mariadb/server/sql/sql_parse.cc` already handles disabled profiling
  for `SHOW PROFILES` by returning `ER_FEATURE_DISABLED` when
  `ENABLED_PROFILING` is not defined.
- `vendor/mariadb/server/sql/sys_vars.cc` exposes `profiling` and
  `profiling_history_size` only under `ENABLED_PROFILING`; `have_profiling`
  remains available and reports support state.
- MariaDB documentation describes `SHOW PROFILE` and `SHOW PROFILES` as
  profiling information for statements executed during the current session,
  controlled by the `profiling` session variable:
  <https://mariadb.com/kb/en/show-profile/> and
  <https://mariadb.com/docs/server/reference/sql-statements/administrative-sql-statements/show/show-profiles>.

## Design

- Add `-DENABLED_PROFILING=OFF` to `tools/build-mariadb-minsize.sh`.
- Include `ENABLED_PROFILING` in the minsize build report cache-entry list so
  the profile decision is visible in generated build evidence.
- Extend the `libmylite` open/close smoke to execute `SHOW PROFILES` and
  verify that the minsize profile reports profiling as disabled.
- Do not modify MariaDB parser syntax, SQL command enums, or information-schema
  table declarations in this slice. The goal is to use MariaDB's existing
  disabled-profiling behavior and measure its size impact before attempting
  deeper SQL-layer removal.

## Affected Subsystems

- Build profile: `tools/build-mariadb-minsize.sh`.
- MariaDB generated config: `ENABLED_PROFILING` is omitted from
  `build/mariadb-minsize/include/config.h`.
- Embedded SQL diagnostics: `SHOW PROFILES` and `SHOW PROFILE` become disabled
  features rather than active profiling reports.
- `libmylite` smoke coverage records the disabled behavior.

## DDL Metadata Routing Impact

None. Profiling does not create or mutate table definitions and does not affect
MyLite catalog routing.

## Single-File and Embedded Lifecycle Impact

Disabling profiling should reduce per-session diagnostic state and should not
create new sidecar files. The existing embedded lifecycle and sidecar-scan
smokes must continue to pass.

## Public API and File Format Impact

No public `libmylite` C API change. No `.mylite` file-format change.

SQL compatibility impact: `SHOW PROFILE`, `SHOW PROFILES`, the `profiling`
system variable, and `information_schema.PROFILING` are not supported in the
minsize profile.

## Binary-Size Impact

Expected savings are modest but low risk because MariaDB already wraps the
implementation with `ENABLED_PROFILING`. Current defined profiling symbols in
`libmariadbd.a` include `PROFILING`, `QUERY_PROFILE`, `PROF_MEASUREMENT`,
`fill_query_profile_statistics_info()`, and `make_profile_table_for_show()`.
The implementation object is much smaller than parser, charset, GIS, or XML
objects, so this is expected to save tens of KiB rather than MiB.

Measure after implementation:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
stat -c "%s" build/mariadb-minsize/libmysqld/libmariadbd.a
ar t build/mariadb-minsize/libmysqld/libmariadbd.a | wc -l
stat -c "%s" build/mariadb-minsize/mylite/mylite-open-close-smoke
cp build/mariadb-minsize/mylite/mylite-open-close-smoke \
  build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
strip --strip-unneeded \
  build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
stat -c "%s" build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
size build/mariadb-minsize/mylite/mylite-open-close-smoke
```

## License, Trademark, and Dependency Impact

No new dependency. No license or trademark impact beyond the existing
MariaDB-derived GPL-2.0-only project status.

## Test Plan

- Build the minsize profile.
- Run the `libmylite` open/close smoke.
- Run the grouped compatibility harness.
- Confirm the build report records `ENABLED_PROFILING:BOOL=OFF`.
- Confirm the linked smoke report records the `SHOW PROFILES` disabled-feature
  message.
- Confirm the linked artifact no longer defines the full profiling classes.

## Acceptance Criteria

- `tools/build-mariadb-minsize.sh` succeeds.
- `tools/run-libmylite-open-close-smoke.sh` succeeds.
- `tools/run-compatibility-test-harness.sh` succeeds.
- `SHOW PROFILES` fails through MariaDB's disabled-feature path in the minsize
  profile.
- Artifact size deltas are recorded in this spec and in
  `docs/research/production-size-analysis.md`.

## Risks and Unresolved Questions

- MariaDB may still compile small profiling table metadata and disabled-feature
  stubs; the size win may be very small.
- Keeping the parser syntax while disabling runtime support means unsupported
  profiling statements produce feature-disabled errors rather than syntax
  errors. That is acceptable for a minsize attempt and is easier to maintain
  than parser surgery.
