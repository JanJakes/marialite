# Diagnostics Statement Size Profile

## Problem Statement

The aggressive embedded minsize profile still builds MariaDB's SQL
programmatic diagnostics statement runtime: `GET DIAGNOSTICS`, `SIGNAL`, and
`RESIGNAL`. MyLite already exposes statement diagnostics through the public C
API, while stored routines and compound-program metadata are unsupported in the
current embedded profile.

Current baseline after `json-function-size-profile`:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 28,094,108 |
| `sql_get_diagnostics.cc.o` object | 109,144 |
| `sql_signal.cc.o` object | 28,192 |
| stripped `mylite-open-close-smoke` | 5,352,064 |

## Source Findings

- Imported MariaDB base: `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- MariaDB documents `GET DIAGNOSTICS` under programmatic compound-statement
  diagnostics. It copies statement or condition information from the
  diagnostics area into variables.
  <https://mariadb.com/docs/server/reference/sql-statements/programmatic-compound-statements/programmatic-compound-statements-diagnostics/get-diagnostics>
- MariaDB documents `SIGNAL` and `RESIGNAL` as custom condition-raising
  statements, generally useful inside stored programs; `RESIGNAL` requires an
  active error handler.
  <https://mariadb.com/docs/server/reference/sql-statements/programmatic-compound-statements/signal>
  <https://mariadb.com/docs/server/reference/sql-statements/programmatic-compound-statements/resignal>
- `vendor/mariadb/server/sql/sql_yacc.yy` parses these statements and creates
  `Sql_cmd_get_diagnostics`, `Sql_cmd_signal`, and `Sql_cmd_resignal` command
  objects.
- `vendor/mariadb/server/sql/sql_get_diagnostics.cc` implements
  `Sql_cmd_get_diagnostics::execute()` plus the statement/condition
  information aggregation classes used only by this SQL statement.
- `vendor/mariadb/server/sql/sql_signal.cc` implements
  `Diag_condition_item_names`, `Set_signal_information` helpers, and
  `Sql_cmd_signal` / `Sql_cmd_resignal` runtime behavior.
- MyLite's existing schema-object DDL rejection marks stored routines,
  packages, triggers, events, and views unsupported until their metadata can be
  stored in the primary `.mylite` catalog.
- `docs/specs/libmylite-exec/specs.md` explicitly avoids stored procedure
  result streams in the public `mylite_exec()` API.
- `docs/specs/libmylite-warning-enumeration/specs.md` preserves C API access
  to handle diagnostics and warning rows, so removing SQL `GET DIAGNOSTICS`
  does not remove MyLite diagnostic access.

## Scope

Add a minsize option that removes SQL diagnostics statement execution from the
embedded library. The option will:

- define `MYLITE_DISABLE_SQL_DIAGNOSTICS_STATEMENTS`;
- remove `../sql/sql_get_diagnostics.cc` and `../sql/sql_signal.cc` from
  `SQL_EMBEDDED_SOURCES`;
- add a MyLite-owned stub that keeps parser-created command objects linkable;
- make `GET DIAGNOSTICS`, `SIGNAL`, and `RESIGNAL` fail with explicit
  unsupported diagnostics in the aggressive profile; and
- keep MariaDB's internal diagnostics area, warning storage, SQLSTATE handling,
  and public MyLite C API diagnostics intact.

## Non-Goals

- Do not remove `SHOW WARNINGS`, `SHOW ERRORS`, or MyLite warning enumeration.
- Do not remove MariaDB's internal `Diagnostics_area` or `Sql_condition`
  implementation.
- Do not edit the generated parser or remove syntax tokens.
- Do not remove stored routines in this slice.
- Do not change full MariaDB server target behavior.
- Do not change public `libmylite` diagnostics APIs.

## Proposed Design

Add `MYLITE_DISABLE_SQL_DIAGNOSTICS_STATEMENTS` to
`vendor/mariadb/server/libmysqld/CMakeLists.txt` and enable it in
`tools/build-mariadb-minsize.sh`.

When the option is enabled, remove `sql_get_diagnostics.cc` and
`sql_signal.cc` from the embedded source list and add
`mylite_sql_diagnostics_stub.cc`.

The stub will define:

- `Diag_condition_item_names`, preserving parser duplicate-property error text;
- `Set_signal_information` copy and clear helpers;
- `Sql_cmd_get_diagnostics::execute()`;
- `Diagnostics_information_item::set_value()`;
- `Statement_information_item::get_value()`;
- `Statement_information::aggregate()`;
- `Condition_information_item::make_utf8_string_item()`;
- `Condition_information_item::get_value()`;
- `Condition_information::aggregate()`;
- `Sql_cmd_common_signal::eval_signal_informations()`;
- `Sql_cmd_common_signal::raise_condition()`;
- `Sql_cmd_signal::execute()`;
- `Sql_cmd_resignal::execute()`.

All executable paths should raise `ER_NOT_SUPPORTED_YET` with a
statement-specific message. Helper methods that should be unreachable after
the command-level unsupported path can also raise the same diagnostic and
return failure/null.

Update the open/close smoke to verify representative unsupported diagnostics:

- `GET DIAGNOSTICS @mylite_diag = NUMBER`;
- `SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mylite signal'`;
- `RESIGNAL`.

## Affected Subsystems

- Embedded minsize SQL source list.
- Programmatic diagnostics statement execution.
- Open/close smoke unsupported SQL coverage.
- Binary-size analysis documentation.

## DDL Metadata Routing Impact

None. This slice removes SQL statement execution that reads or writes
diagnostic variables. It does not affect persistent table definitions or MyLite
catalog routing.

## Single-File And Embedded-Lifecycle Impact

No file-format, catalog, recovery, locking, or sidecar-file change. The slice
removes stored-program-adjacent SQL execution code that does not own files.

## Public API Or File-Format Impact

No public `libmylite` API or `.mylite` file-format change. MyLite's C API
diagnostics remain the supported embedded diagnostics surface.

## Binary-Size Impact

Expected archive savings are bounded by `sql_get_diagnostics.cc.o` and
`sql_signal.cc.o`, minus the replacement stub. Linked-runtime savings may be
small because the SQL statement bodies are not hot in current smokes, but the
archive cleanup is useful for the aggressive profile.

Measured after implementation:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 28,094,108 | 27,978,354 | -115,754 |
| unstripped `mylite-open-close-smoke` | 7,448,136 | 7,439,896 | -8,240 |
| stripped `mylite-open-close-smoke` | 5,352,064 | 5,345,504 | -6,560 |
| stripped `mylite-compatibility-smoke` | 5,240,144 | 5,232,424 | -7,720 |

The archive no longer contains `sql_get_diagnostics.cc.o` or
`sql_signal.cc.o`. It keeps the 25,104-byte replacement
`mylite_sql_diagnostics_stub.cc.o`.

## License, Trademark, And Dependency Impact

No new dependency or license impact. The imported MariaDB sources remain in the
tree and are only omitted from the embedded minsize build profile.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-diagnostics \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-diagnostics \
  tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-diagnostics \
  tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-diagnostics \
  tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Measure:

- archive bytes;
- unstripped and stripped linked smoke bytes;
- absence of `sql_get_diagnostics.cc.o` and `sql_signal.cc.o` in
  `libmariadbd.a`; and
- unsupported SQL statement messages in the open/close report.

## Acceptance Criteria

- The minsize build succeeds with
  `MYLITE_DISABLE_SQL_DIAGNOSTICS_STATEMENTS=ON`. Passed with
  `build/mariadb-minsize-no-sql-diagnostics`.
- The open/close smoke and compatibility harness pass. Passed.
- `GET DIAGNOSTICS`, `SIGNAL`, and `RESIGNAL` fail with stable unsupported
  diagnostics. Passed.
- Internal MariaDB diagnostics and MyLite C API diagnostics continue to work
  for ordinary SQL failures. Passed through existing duplicate-key diagnostics
  and warning enumeration smoke coverage.
- The embedded archive no longer contains `sql_get_diagnostics.cc.o` or
  `sql_signal.cc.o`. Passed.
- Size results are recorded here and in
  `docs/research/production-size-analysis.md`. Passed.

## Verification

Validated on 2026-05-12 with:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-diagnostics \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-diagnostics \
  tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-diagnostics \
  tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-diagnostics \
  tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

The open/close report includes:

```text
exec_sql_diagnostics_messages=get_diagnostics:This version of MariaDB doesn't yet support 'GET DIAGNOSTICS in the MyLite minsize profile';signal:This version of MariaDB doesn't yet support 'SIGNAL in the MyLite minsize profile';resignal:This version of MariaDB doesn't yet support 'RESIGNAL in the MyLite minsize profile'
```

## Risks And Unresolved Questions

- `SIGNAL` can be used at top level in MariaDB, not only inside stored
  programs. Omitting it is a SQL compatibility loss and belongs only in the
  most aggressive size profile.
- `GET DIAGNOSTICS` can be useful to SQL scripts, but MyLite's embedded API
  already has a stronger direct diagnostics surface.
- This slice does not remove the larger stored routine runtime. That remains a
  separate candidate because parser and function-resolution coupling are much
  wider.
