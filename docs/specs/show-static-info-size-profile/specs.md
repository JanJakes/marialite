# SHOW Static Info Size Profile

## Problem

The aggressive MyLite minsize profile still links MariaDB's static
`SHOW AUTHORS`, `SHOW CONTRIBUTORS`, and `SHOW PRIVILEGES` result producers.
These commands expose server attribution and generic privilege help text. They
do not query MyLite storage, do not help embedded file ownership, and retain
static data tables in the linked runtime.

Current measurements from `build/mariadb-minsize-no-routine-is` show the
linked smoke still contains:

| Symbol | Bytes |
| --- | ---: |
| `show_table_authors` | 2,544 |
| `show_table_contributors` | 1,032 |
| `mysqld_show_authors()` | 480 |
| `mysqld_show_contributors()` | 480 |
| `mysqld_show_privileges()` | 480 |

This slice removes only those static `SHOW` result surfaces. It does not remove
ordinary `SHOW TABLES`, `SHOW COLUMNS`, `SHOW VARIABLES`, `SHOW STATUS`, or
storage-engine metadata.

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/sql_yacc.yy` parses `SHOW AUTHORS`,
  `SHOW CONTRIBUTORS`, and `SHOW PRIVILEGES` into `SQLCOM_SHOW_AUTHORS`,
  `SQLCOM_SHOW_CONTRIBUTORS`, and `SQLCOM_SHOW_PRIVILEGES`.
- `vendor/mariadb/server/sql/sql_parse.cc` executes those commands by calling
  `mysqld_show_authors()`, `mysqld_show_contributors()`, and
  `mysqld_show_privileges()`.
- `vendor/mariadb/server/sql/sql_show.cc` includes `authors.h` and
  `contributors.h`, defines `sys_privileges[]`, and formats all three result
  sets.
- `vendor/mariadb/server/sql/authors.h` and
  `vendor/mariadb/server/sql/contributors.h` hold static display rows.

## Scope

This slice may:

- add `MYLITE_DISABLE_SHOW_STATIC_INFO`,
- enable it from `tools/build-mariadb-minsize.sh`,
- reject `SHOW AUTHORS`, `SHOW CONTRIBUTORS`, and `SHOW PRIVILEGES` in the
  embedded minsize profile with `ER_NOT_SUPPORTED_YET`,
- compile out the static row data and formatter functions, and
- add smoke coverage for the unsupported diagnostics.

## Non-Goals

This slice does not:

- remove parser syntax for the commands,
- alter non-minsize builds,
- remove privilege enforcement,
- remove `INFORMATION_SCHEMA.*_PRIVILEGES` tables,
- remove `SHOW GRANTS`, or
- remove ordinary schema, table, variable, status, engine, or warning `SHOW`
  commands.

## Proposed Design

Add a minsize-only CMake option. When enabled, `sql_parse.cc` routes the three
static-info `SHOW` commands to a stable unsupported-feature diagnostic before
they reach the result producers.

Guard the `authors.h`, `contributors.h`, `sys_privileges[]`, and corresponding
`mysqld_show_*()` implementations in `sql_show.cc` with the same macro. This
keeps the upstream command shape intact for non-minsize builds while allowing
section GC and preprocessing to drop the static data from the aggressive
embedded library.

## Affected Subsystems

- SQL command dispatch in `sql_parse.cc`.
- Static `SHOW` result production in `sql_show.cc`.
- MyLite open/close unsupported-profile smoke coverage.
- Minsize CMake configuration.

## Single-File and Embedded Lifecycle Impact

No file-format or storage lifecycle change. The removed commands do not own
storage files, but they are server metadata/help surfaces that are not required
for an embedded SQLite-like library.

## Public API and File-Format Impact

No public `libmylite` API change and no `.mylite` file-format change.

SQL compatibility impact: the aggressive minsize profile no longer supports
`SHOW AUTHORS`, `SHOW CONTRIBUTORS`, or `SHOW PRIVILEGES`.

## Binary-Size Impact

Measured savings are small but real. On top of
`routine-information-schema-size-profile`, this reduced the stripped
`libmariadbd.a` archive by 35,166 bytes and the stripped linked open/close
smoke by 15,368 bytes.

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmariadbd.a` | 27,644,466 | 27,609,300 | -35,166 |
| `sql_show.cc.o` | 555,136 | 520,288 | -34,848 |
| `sql_parse.cc.o` | 284,216 | 284,056 | -160 |
| unstripped `mylite-open-close-smoke` | 7,365,424 | 7,349,888 | -15,536 |
| stripped `mylite-open-close-smoke` | 5,288,280 | 5,272,912 | -15,368 |
| unstripped `mylite-compatibility-smoke` | 7,226,464 | 7,209,472 | -16,992 |
| stripped `mylite-compatibility-smoke` | 5,171,728 | 5,155,080 | -16,648 |

The linked open/close smoke no longer defines `show_table_authors`,
`show_table_contributors`, `mysqld_show_authors()`,
`mysqld_show_contributors()`, or `mysqld_show_privileges()`.

## License, Trademark, and Dependency Impact

This is a GPL-2.0-only MariaDB-derived build-profile change. It adds no new
dependency and changes no trademark-facing packaging.

## Test and Verification Plan

Run:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-static-show-info MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-static-show-info tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-static-show-info tools/run-storage-engine-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-static-show-info tools/run-embedded-bootstrap-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-static-show-info tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh`
- `git diff --check`

All commands above passed.

## Acceptance Criteria

- `SHOW AUTHORS` reports `ER_NOT_SUPPORTED_YET` in the minsize profile.
- `SHOW CONTRIBUTORS` reports `ER_NOT_SUPPORTED_YET` in the minsize profile.
- `SHOW PRIVILEGES` reports `ER_NOT_SUPPORTED_YET` in the minsize profile.
- Ordinary metadata commands covered by the current smokes and compatibility
  harness still pass.
- Measured size changes are recorded in
  `docs/research/production-size-analysis.md`.

## Risks and Unresolved Questions

- This is a SQL-visible compatibility reduction, but the affected commands are
  static server information/help surfaces rather than application query
  features.
- `SHOW PRIVILEGES` is useful for introspection in server tools. MyLite's
  embedded profile already avoids server-user administration, but this should
  stay aggressive-profile-only.
