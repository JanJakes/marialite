# help-command-size-profile

## Problem

The MyLite minsize profile still builds MariaDB's SQL `HELP` command. `HELP`
is an interactive server/client convenience feature backed by `mysql.help_*`
tables populated by installation scripts. MyLite's embedded API should not
depend on server install-time help tables, and PHP/PDO-style consumers do not
need in-database command help in the runtime library.

This slice removes the `HELP` implementation from the minsize embedded archive
while preserving a clear unsupported-command diagnostic.

## Source Findings

Base source: MariaDB Server `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- MariaDB documentation describes `HELP search_string` as a command that
  displays help information from server help tables and says those tables are
  populated by `mariadb-install-db` or `fill_help_tables.sql`:
  <https://mariadb.com/docs/server/reference/sql-statements/administrative-sql-statements/help-command>.
- `vendor/mariadb/server/sql/sql_yacc.yy` parses `HELP_SYM ident_or_text`,
  sets `SQLCOM_HELP`, and stores the search text in `LEX::help_arg`.
- `vendor/mariadb/server/sql/sql_parse.cc` executes `SQLCOM_HELP` by calling
  `mysqld_help(thd, lex->help_arg)`.
- `vendor/mariadb/server/sql/sql_prepare.cc` prepares `SQLCOM_HELP` metadata by
  calling `mysqld_help_prepare(thd, stmt->lex->help_arg, &fields)`.
- `vendor/mariadb/server/sql/sql_help.h` exposes only these two entry points:
  `mysqld_help()` and `mysqld_help_prepare()`.
- `vendor/mariadb/server/sql/sql_help.cc` opens and scans the `mysql`
  `help_topic`, `help_category`, `help_keyword`, and `help_relation` tables.
  In the current minsize build, `sql_help.cc.o` is 179,488 bytes before archive
  stripping.

## Design

- Add a MyLite-owned `MYLITE_DISABLE_HELP_COMMAND` CMake option in
  `vendor/mariadb/server/libmysqld/CMakeLists.txt`.
- Set `-DMYLITE_DISABLE_HELP_COMMAND=ON` in `tools/build-mariadb-minsize.sh`.
- When the option is enabled, remove `../sql/sql_help.cc` from
  `SQL_EMBEDDED_SOURCES` and append a small
  `mylite_help_command_stub.cc`.
- Keep parser syntax and `SQLCOM_HELP` routing intact. The stub returns
  `ER_NOT_SUPPORTED_YET` for both direct execution and prepared-statement
  metadata preparation.
- Extend the `libmylite` open/close smoke with `HELP 'contents'` coverage so
  the unsupported behavior is pinned.

## Affected Subsystems

- Build profile: `tools/build-mariadb-minsize.sh`.
- Embedded SQL source list: `vendor/mariadb/server/libmysqld/CMakeLists.txt`.
- Unsupported SQL surface: `HELP`.
- Test coverage: `vendor/mariadb/server/mylite/open_close_smoke.cc`.

## DDL Metadata Routing Impact

None. `HELP` reads server help metadata tables but does not create, alter, or
drop user table definitions.

## Single-File and Embedded Lifecycle Impact

Removing `HELP` avoids attempting to open `mysql.help_*` tables from an
embedded MyLite database. The stub must not create files, open tables, or alter
embedded lifecycle state.

## Public API and File Format Impact

No public `libmylite` C API change. No `.mylite` file-format change.

SQL compatibility impact: the minsize profile does not support `HELP`; it
returns a stable unsupported-feature diagnostic.

## Binary-Size Impact

Expected savings are small-to-moderate. The current unstripped
`sql_help.cc.o` build object is 179,488 bytes, but archive stripping and linked
dead-code behavior mean the final `libmariadbd.a` and stripped linked smoke
binary deltas will be lower.

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
- Confirm the build report records `MYLITE_DISABLE_HELP_COMMAND:BOOL=ON`.
- Confirm the linked smoke report records the `HELP` unsupported-feature
  message.
- Confirm `libmariadbd.a` no longer defines `mysqld_help()` from the full
  help-table implementation.

## Acceptance Criteria

- `tools/build-mariadb-minsize.sh` succeeds.
- `tools/run-libmylite-open-close-smoke.sh` succeeds.
- `tools/run-compatibility-test-harness.sh` succeeds.
- `HELP 'contents'` fails with `ER_NOT_SUPPORTED_YET` in the minsize profile.
- Artifact size deltas are recorded in this spec and in
  `docs/research/production-size-analysis.md`.

## Risks and Unresolved Questions

- Parser syntax remains available, so unsupported `HELP` statements fail at
  execution time rather than parse time.
- Prepared `HELP` statements also become unsupported; this matches the
  direct-execution behavior and avoids building result metadata from
  `mysql.help_*` tables.
