# PLSQL Cursor Attribute Size Profile

## Problem

The aggressive MyLite minsize profile rejects stored routine DDL and disables
Oracle mode, but MariaDB's parser/runtime still keeps PL/SQL-style cursor
attribute items for expressions such as `cursor%FOUND`, `cursor%NOTFOUND`,
`cursor%ISOPEN`, and `cursor%ROWCOUNT`.

These items read stored-program cursor state from `THD::spcont`. They have no
use in MyLite's current embedded profile because MyLite cannot create or
execute stored routines and does not support Oracle PL/SQL compatibility.

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/sql_yacc.yy` parses `explicit_cursor_attr` as
  `ident PERCENT_ORACLE_SYM plsql_cursor_attr` and calls
  `LEX::make_item_plsql_cursor_attr()`.
- `vendor/mariadb/server/sql/sql_lex.cc` resolves the cursor in `spcont` and
  constructs `Item_func_cursor_isopen`, `Item_func_cursor_found`,
  `Item_func_cursor_notfound`, or `Item_func_cursor_rowcount`.
- `LEX::sp_for_loop_cursor_condition_test()` also constructs
  `Item_func_cursor_found` for stored-program cursor `FOR` loops.
- `vendor/mariadb/server/sql/item_cmpfunc.h` declares the boolean cursor
  attribute item classes.
- `vendor/mariadb/server/sql/item_func.h` declares
  `Item_func_cursor_rowcount`.
- `vendor/mariadb/server/sql/item_func.cc` implements cursor attribute
  evaluation by reading `sp_cursor` state from the current stored-program
  runtime context.

## Scope

This slice may:

- add `MYLITE_DISABLE_PLSQL_CURSOR_ATTRIBUTES`,
- enable it from `tools/build-mariadb-minsize.sh`,
- make parsed Oracle-mode cursor attribute expressions fail with an
  unsupported-feature diagnostic in the aggressive minsize profile,
- make stored-program cursor `FOR` loop condition generation fail with an
  unsupported-feature diagnostic,
- compile out cursor attribute item method definitions, and
- add smoke and symbol coverage for the removed cursor attribute item runtime.

## Non-Goals

This slice does not:

- remove stored-program grammar,
- remove stored cursor declarations or cursor instructions,
- remove `sp_head`, `sp_instr`, `sp_cursor`, or `sp_rcontext`,
- alter non-minsize builds, or
- decide future stored routine compatibility.

## Proposed Design

Add a minsize-only CMake option. When enabled,
`LEX::make_item_plsql_cursor_attr()` should return `ER_NOT_SUPPORTED_YET`
without constructing cursor attribute item objects. Guard the corresponding
item method definitions so linked minsize artifacts can drop their vtables and
evaluation code when no code constructs them.

The rejection stays in `sql_lex.cc`, not grammar generation, to keep the
upstream parser delta narrow and avoid a broad generated-parser maintenance
slice.

Because this profile also disables Oracle mode, `cursor%FOUND` in ordinary
default SQL mode tokenizes as modulo and fails with MariaDB's unknown-column
diagnostic before the PL/SQL cursor-attribute path is reachable. The direct
regression check for this slice is therefore the linked-symbol absence check in
`tools/run-libmylite-open-close-smoke.sh`; the runtime smoke records that the
default profile still does not expose PL/SQL cursor attribute syntax.

## Affected Subsystems

- PL/SQL cursor attribute expression construction in `sql_lex.cc`.
- Stored-program cursor loop condition construction in `sql_lex.cc`.
- Cursor attribute item execution in `item_func.cc`.
- MyLite open/close unsupported-profile smoke coverage.
- Minsize CMake configuration.

## Single-File and Embedded Lifecycle Impact

No file-format or storage lifecycle change. The removed path reads
stored-program cursor state that cannot exist in MyLite's current embedded
profile.

## Public API and File-Format Impact

No public `libmylite` API change and no `.mylite` file-format change.

SQL compatibility impact: in the aggressive minsize profile, the Oracle-mode
cursor attribute construction path reports unsupported behavior instead of
attempting to resolve stored-program cursor state. Default SQL mode still does
not expose PL/SQL cursor attribute syntax because Oracle mode is disabled.

## Binary-Size Impact

Measured against `build/mariadb-minsize-no-stored-function-lookup`, this
slice reduced:

- `libmariadbd.a` from 27,524,742 to 27,455,348 bytes (-69,394),
- unstripped `mylite-open-close-smoke` from 7,322,008 to 7,305,640 bytes
  (-16,368),
- stripped `mylite-open-close-smoke` from 5,254,144 to 5,242,360 bytes
  (-11,784),
- unstripped `mylite-compatibility-smoke` from 7,177,880 to 7,160,216 bytes
  (-17,664), and
- stripped `mylite-compatibility-smoke` from 5,133,264 to 5,120,568 bytes
  (-12,696).

`item_func.cc.o` dropped from 1,435,608 to 1,373,056 bytes, and
`sql_lex.cc.o` dropped from 663,624 to 660,112 bytes.

## License, Trademark, and Dependency Impact

This is a GPL-2.0-only MariaDB-derived build-profile change. It adds no new
dependency and changes no trademark-facing packaging.

## Test and Verification Plan

Run:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-plsql-cursor-attributes MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-plsql-cursor-attributes tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-plsql-cursor-attributes tools/run-storage-engine-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-plsql-cursor-attributes tools/run-embedded-bootstrap-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-plsql-cursor-attributes tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh`
- `bash -n tools/run-libmylite-open-close-smoke.sh`
- `git diff --check`

## Acceptance Criteria

- PL/SQL cursor attribute expressions fail explicitly in the aggressive
  minsize profile when the Oracle-mode token path is reachable; default SQL
  mode continues not to expose PL/SQL cursor attribute syntax because Oracle
  mode is disabled.
- Native SQL functions covered by the current smokes still work.
- Stored routine DDL rejection and empty routine Information Schema behavior
  still pass.
- No exact `Item_func_cursor_isopen`, `Item_func_cursor_found`,
  `Item_func_cursor_notfound`, `Item_func_cursor_rowcount`, or
  `Item_func_cursor_bool_attr` symbols remain in the linked open-close smoke.
- Measured size changes are recorded in
  `docs/research/production-size-analysis.md`.

## Verification Results

Passed:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-plsql-cursor-attributes MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-plsql-cursor-attributes tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-plsql-cursor-attributes tools/run-storage-engine-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-plsql-cursor-attributes tools/run-embedded-bootstrap-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-plsql-cursor-attributes tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh`
- `bash -n tools/run-libmylite-open-close-smoke.sh`
- `git diff --check`

The open/close smoke script also verifies that the linked
`mylite-open-close-smoke` binary defines no exact
`Item_func_cursor_isopen`, `Item_func_cursor_found`,
`Item_func_cursor_notfound`, `Item_func_cursor_rowcount`, or
`Item_func_cursor_bool_attr` symbols.

## Risks and Unresolved Questions

- This changes diagnostics for a PL/SQL expression surface. It is coherent
  only while stored routines and Oracle mode remain unsupported in the
  aggressive embedded profile.
- Savings may be tiny because the broader stored-program parser/runtime still
  remains linked.
