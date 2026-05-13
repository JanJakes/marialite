# JSON_TABLE Size Profile

## Problem Statement

The aggressive embedded minsize profile still links MariaDB's `JSON_TABLE`
table-function implementation. `JSON_TABLE` converts JSON documents into a
derived relational table and carries its own handler, temporary-table creation,
JSON path scanning, column extraction, dependency tracking, and print support.
That is useful SQL compatibility, but it is not required for MyLite's embedded
storage lifecycle and is a bounded candidate for the most aggressive size
profile.

Current baseline after `backup-stage-size-profile`:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 30,218,906 |
| `json_table.cc.o` object | 117,696 |
| stripped `mylite-open-close-smoke` | 5,740,424 |

## Source Findings

- Imported MariaDB base: `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- MariaDB documentation describes `JSON_TABLE` as a table function available
  from MariaDB 10.6 that can appear where a table reference can appear,
  including `SELECT`, multi-table `UPDATE`, and multi-table `DELETE` `FROM`
  lists: <https://mariadb.com/docs/server/reference/sql-functions/special-functions/json-functions/json_table>
- `vendor/mariadb/server/sql/json_table.cc` implements the JSON_TABLE
  handler, temporary-table construction, JSON path scanning, result-column
  extraction, optimizer estimates, name-resolution context setup, join-order
  dependency propagation, and SQL printing.
- `vendor/mariadb/server/sql/json_table.h` exposes parser-facing classes:
  `Table_function_json_table`, `Json_table_column`, and
  `Json_table_nested_path`.
- `vendor/mariadb/server/sql/sql_yacc.yy` constructs those classes while
  parsing `JSON_TABLE(...)` and stores the table function on
  `TABLE_LIST::table_function`.
- `vendor/mariadb/server/sql/sql_base.cc` calls
  `create_table_for_function()` when a table reference has a table function.
- `item_subselect.cc`, `opt_subselect.cc`, `sql_explain.cc`, and generic
  `TABLE_LIST` code observe the `table_function` pointer.
- Current linked smoke symbols include `ha_json_table`,
  `Create_json_table`, `Table_function_json_table`, `Json_table_column`, and
  `Json_table_nested_path` methods, so the object is live in the linked
  runtime.

## Scope

Add a minsize option that removes full `JSON_TABLE` execution from the embedded
library. The option will:

- remove `../sql/json_table.cc` from `SQL_EMBEDDED_SOURCES`;
- add a MyLite-owned `JSON_TABLE` stub;
- keep the parser-facing classes and methods needed to link generated parser
  and optimizer code;
- make parsed `JSON_TABLE(...)` fail with a stable unsupported-feature
  diagnostic before creating a derived table; and
- keep ordinary JSON scalar functions intact.

## Non-Goals

- Do not remove the `JSON` type alias.
- Do not remove ordinary JSON scalar functions such as `JSON_VALID()`,
  `JSON_EXTRACT()`, `JSON_VALUE()`, `JSON_OBJECT()`, or `JSON_ARRAY()`.
- Do not change generated parser syntax in this slice.
- Do not change the full MariaDB server target behavior.
- Do not add a public `libmylite` API or `.mylite` file-format change.

## Proposed Design

Add `MYLITE_DISABLE_JSON_TABLE` to
`vendor/mariadb/server/libmysqld/CMakeLists.txt` and enable it in
`tools/build-mariadb-minsize.sh`.

Create `vendor/mariadb/server/libmysqld/mylite_json_table_stub.cc`. The stub
will define the non-inline methods declared in `json_table.h`, but it will not
define `ha_json_table` or create a temporary handler. Parser-time path setup and
execution-time table-function setup will report
`ER_NOT_SUPPORTED_YET, "JSON_TABLE in the MyLite minsize profile"`.

`push_table_function_arg_context()` still needs to preserve the parser context
stack contract because `sql_yacc.yy` unconditionally pops that context after
parsing the first JSON argument.

## Affected Subsystems

- Embedded minsize SQL source list.
- Generated parser link surface.
- JSON table-function execution.
- Open/close smoke unsupported SQL coverage.
- Binary-size documentation.

## DDL Metadata Routing Impact

No direct table-definition routing change is expected. `JSON_TABLE` is a table
reference, not persisted DDL metadata. Views are already an unsupported
MyLite schema-object surface, so omitting SQL printing for persisted
`JSON_TABLE` view definitions is acceptable in the aggressive profile.

## Single-File And Embedded-Lifecycle Impact

No file-format, catalog, recovery, lock, or sidecar change. The slice removes a
derived-table execution path that does not own persistent files.

## Public API Or File-Format Impact

No public `libmylite` API or `.mylite` file-format change.

## Binary-Size Impact

Expected archive savings are bounded by the 117,696-byte `json_table.cc.o`
member minus the replacement stub. Linked-runtime savings should be measurable
because `JSON_TABLE` handler and parser-support symbols are present in the
current smoke binary.

Measured after implementation:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 30,218,906 | 30,103,286 | -115,620 |
| unstripped `mylite-open-close-smoke` | 7,979,416 | 7,964,712 | -14,704 |
| stripped `mylite-open-close-smoke` | 5,740,424 | 5,730,200 | -10,224 |

The archive no longer contains `json_table.cc.o`; it contains
`mylite_json_table_stub.cc.o`, measured at 11,256 bytes. The archive object
count remains 422.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-json-table \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-json-table \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-json-table \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Add open/close smoke assertions that:

- `JSON_VALID()` still succeeds; and
- a simple `JSON_TABLE(...)` query fails with
  `ER_NOT_SUPPORTED_YET` and mentions `JSON_TABLE`.

Measure:

- archive bytes and object count;
- unstripped and stripped linked smoke bytes;
- absence of `json_table.cc.o` in `libmariadbd.a`;
- presence and size of the replacement stub; and
- absence of `ha_json_table` and `Create_json_table` symbols from the linked
  smoke.

## Acceptance Criteria

- The minsize build completes. Passed with
  `build/mariadb-minsize-no-json-table`.
- The open/close smoke and compatibility harness pass. Passed.
- Ordinary JSON scalar function smoke coverage still passes. Passed with
  `exec_json_valid_rows=1`.
- The embedded archive no longer contains `json_table.cc.o`. Passed.
- The linked smoke no longer contains `ha_json_table` or `Create_json_table`
  implementation symbols. Passed.
- Size results are recorded here and in
  `docs/research/production-size-analysis.md`. Passed.

## Risks And Unresolved Questions

- This removes real SQL compatibility from the aggressive minsize profile.
- Parser syntax remains, so unsupported `JSON_TABLE` statements fail after
  parse-time class construction rather than as syntax errors.
- A later product profile may want `JSON_TABLE` back if JSON transformation SQL
  becomes a compatibility target.
