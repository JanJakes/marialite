# Window Function Size Profile

## Problem Statement

The aggressive MyLite minsize profile still links MariaDB's dedicated window
function parser, item, optimizer, and execution code. Current linked symbols
include `Item_window_func`, `Window_spec`, `Window_funcs_computation`,
`setup_windows()`, and dedicated functions such as `Item_sum_row_number` and
`Item_sum_rank`.

Window functions are useful analytical SQL, not a daemon-only surface. This
slice is therefore an aggressive size experiment, not an obvious default
compatibility decision. The goal is to measure whether omitting window
functions is worth considering for the smallest embedded profile while keeping
ordinary aggregate functions such as `COUNT()` and `SUM()` intact.

## Source Findings

- Imported MariaDB base: `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- MariaDB documents window functions as `OVER`-clause analytical functions and
  lists both dedicated functions such as `ROW_NUMBER()` and aggregate
  functions used as window functions:
  <https://mariadb.com/docs/server/reference/sql-functions/special-functions/window-functions/window-functions-overview>.
- `vendor/mariadb/server/sql/sql_yacc.yy` parses `window_func_expr`,
  `simple_window_func`, inverse distribution functions, `OVER`, and the
  optional named `WINDOW` clause. These actions construct `Item_window_func`,
  `Item_sum_row_number`, `Item_sum_rank`, `Item_sum_ntile`, percentile
  functions, and related classes.
- `vendor/mariadb/server/sql/item_windowfunc.h` declares dedicated window
  function item classes and `Item_window_func`.
- `vendor/mariadb/server/sql/item_windowfunc.cc` implements
  `Item_window_func` and dedicated window function behavior.
- `vendor/mariadb/server/sql/sql_window.h` and
  `vendor/mariadb/server/sql/sql_window.cc` implement `Window_spec`,
  `Window_frame`, `setup_windows()`, frame cursors, and
  `Window_funcs_computation`.
- `vendor/mariadb/server/sql/sql_select.cc`, `sql_derived.cc`,
  `opt_subselect.cc`, `opt_split.cc`, `sql_prepare.cc`, `sql_union.cc`, and
  `sql_explain.cc` contain retained checks for `have_window_funcs()` and call
  window setup/execution helpers when a parsed query has window functions.
- Current `build/mariadb-minsize-no-udf` archive member sizes:
  `item_windowfunc.cc.o` is 221,072 bytes and `sql_window.cc.o` is
  231,384 bytes.
- A current linked-symbol scan attributes about 47,263 bytes of live symbols to
  direct window-function names in `mylite-open-close-smoke`. This undercounts
  indirect savings from removed vtables, data relocations, and parser action
  references, so the final size must be measured.

## Proposed Design

Add `MYLITE_DISABLE_WINDOW_FUNCTIONS` as an off-by-default MariaDB CMake
option. The aggressive minsize script enables it.

When enabled:

- parser actions for dedicated window functions and `OVER`-clause aggregate
  window functions fail with a stable unsupported-feature diagnostic instead
  of constructing window items;
- the named `WINDOW` clause fails with the same unsupported diagnostic;
- ordinary aggregate functions without `OVER` continue to parse and execute;
- omit `item_windowfunc.cc` and `sql_window.cc` from the embedded source list;
- link a small embedded-only stub for retained planner/executor references
  that should be unreachable when the parser rejects window syntax.

The implementation should keep the grammar tokens intact so that the disabled
profile reports an explicit unsupported feature instead of an accidental parse
error where practical.

## Non-Goals

- Do not remove ordinary aggregate functions.
- Do not remove sort, grouping, temporary-table, or filesort infrastructure
  shared with non-window queries.
- Do not change non-minsize MariaDB behavior.
- Do not claim this is a low-risk default compatibility choice. It is an
  aggressive profile experiment.

## Affected Subsystems

- SQL parser actions for `OVER` and named `WINDOW` clauses.
- Dedicated window item classes.
- Window planner/executor integration in the SELECT path.
- EXPLAIN support for window-function computation nodes.
- Open/close smoke coverage for disabled SQL surfaces.

## Single-File And Embedded-Lifecycle Impact

This slice does not change MyLite file format, catalog metadata, or sidecar
policy. It removes an execution feature that may allocate temporary tables and
filesorts when used, but those shared infrastructures remain available for
ordinary SELECT execution.

## Public API Or File-Format Impact

No public `libmylite` C API or `.mylite` file-format change.

## Binary-Size Impact

The expected upper bound was not the full 452,456 bytes from
`item_windowfunc.cc.o` plus `sql_window.cc.o`, because some window-related
state and aggregate hooks live in shared SQL objects. Direct linked window
symbols accounted for about 47 KiB before indirect relocation and section
effects.

Measured against `build/mariadb-minsize-no-udf`:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 31,748,626 | 31,138,612 | -610,014 |
| unstripped `mylite-open-close-smoke` | 8,272,992 | 8,151,024 | -121,968 |
| stripped `mylite-open-close-smoke` copy | 5,926,048 | 5,849,432 | -76,616 |

The disabled profile removes `item_windowfunc.cc.o` and `sql_window.cc.o` from
`libmariadbd.a`. The linked smoke no longer contains `Item_window_func`,
dedicated `Item_sum_row_number` / rank / `NTILE` window item symbols, or
`Window_func_runner` symbols. Tiny `setup_windows()` and
`Window_funcs_computation` stub symbols remain to satisfy retained SELECT-path
references that are unreachable after parser rejection.

## License, Trademark, And Dependency Impact

No new dependency or license change. This removes a MariaDB SQL compatibility
surface only from the aggressive embedded minsize profile.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-window \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-window \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-window \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Add smoke assertions that:

- `COUNT()` and `SUM()` still work as ordinary aggregates;
- `ROW_NUMBER() OVER ()` fails with the unsupported-feature diagnostic;
- `SUM(1) OVER ()` fails with the unsupported-feature diagnostic;
- a named `WINDOW` clause fails with the unsupported-feature diagnostic.

Compare:

- `libmysqld/libmariadbd.a`
- unstripped and stripped `mylite-open-close-smoke`
- absence of `item_windowfunc.cc.o` and `sql_window.cc.o` from the archive
- absence of `Item_window_func` and dedicated window item implementation
  symbols from the linked smoke, with only minsize stub hook symbols remaining

Verification completed with the commands above. `bash -n
tools/build-mariadb-minsize.sh` and `git diff --check` also pass.

## Acceptance Criteria

- The minsize build completes.
- The open/close smoke and compatibility harness pass.
- Ordinary non-window aggregate SQL still passes smoke coverage.
- Window syntax fails explicitly in the minsize profile.
- Dedicated window implementation objects are absent from the embedded archive.
- Linked smoke no longer contains dedicated window item/runtime symbols beyond
  the retained minsize stub hooks.
- Size measurements are recorded in this spec and in
  `docs/research/production-size-analysis.md`.

## Risks And Unresolved Questions

- This removes a real MariaDB SQL feature, so it is a high compatibility
  tradeoff and may be unsuitable outside the most aggressive profile.
- Parser actions must reject early enough to avoid constructing window item
  classes, or the linked runtime may keep vtables and method bodies alive.
- Shared aggregate hooks for window execution may remain in common aggregate
  classes; removing those would be a deeper, higher-risk follow-up.
