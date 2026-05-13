# SFORMAT Function Size Profile

## Problem Statement

The aggressive MyLite minsize profile still links fmtlib template code through
MariaDB's `SFORMAT()` SQL function. That is a rare convenience string-formatting
surface, not part of the core parser, optimizer, executor, storage engine, or
ordinary numeric/date formatting behavior that MyLite needs to preserve.

This slice tests whether MyLite can omit `SFORMAT()` from the aggressive
embedded profile while retaining ordinary MariaDB formatting functions such as
`FORMAT()`, `DATE_FORMAT()`, `GET_FORMAT()`, `FORMAT_BYTES()`, and
`FORMAT_PICO_TIME()`.

## Source Findings

MariaDB source references are from imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

Official MariaDB documentation:

- `SFORMAT()` is available from MariaDB 10.7 and formats strings using fmtlib
  rules. Source:
  <https://mariadb.com/docs/server/reference/sql-functions/string-functions/sformat>
- `FORMAT()` formats numbers with grouping and decimal places. Source:
  <https://mariadb.com/docs/server/reference/sql-functions/string-functions/format>
- `FORMAT_BYTES()` and `FORMAT_PICO_TIME()` are human-readable utility
  functions available in newer MariaDB releases. Sources:
  <https://mariadb.com/docs/server/reference/sql-functions/secondary-functions/miscellaneous-functions/miscellaneous-functions-format_bytes>,
  <https://mariadb.com/docs/server/reference/sql-functions/date-time-functions/format_pico_time>

Relevant local source paths:

- `vendor/mariadb/server/sql/item_strfunc.cc` includes `fmt/args.h` with
  `FMT_HEADER_ONLY=1` and implements `Item_func_sformat::val_str()` with
  `fmt::dynamic_format_arg_store` and `fmt::vformat()`.
- `vendor/mariadb/server/sql/item_strfunc.h` declares `Item_func_sformat`.
- `vendor/mariadb/server/sql/item_create.cc` declares
  `Create_func_sformat`, creates `Item_func_sformat`, and registers
  `SFORMAT` in the native function registry.
- `vendor/mariadb/server/sql/item_create.cc` separately registers `FORMAT`,
  `FORMAT_BYTES`, and `FORMAT_PICO_TIME`, so `SFORMAT` can be isolated without
  removing those functions.
- `docs/research/production-size-analysis.md` and linked symbol inspection show
  many `fmt::v9::*` symbols in `mylite-open-close-smoke`, rooted through
  `Item_func_sformat`.

## Scope

This slice may:

- add `MYLITE_DISABLE_SFORMAT_FUNCTION`,
- enable it in `tools/build-mariadb-minsize.sh`,
- compile out the `Item_func_sformat` class declaration and implementation in
  the aggressive embedded profile,
- remove `SFORMAT` from the native function registry in that profile, and
- add smoke coverage proving `SFORMAT()` fails explicitly while ordinary
  `FORMAT()` still works.

## Non-Goals

This slice does not:

- remove ordinary numeric `FORMAT()`,
- remove date/time formatting functions,
- remove general string functions such as `CONCAT`, `REPLACE`, or `SUBSTR`,
- remove fmtlib from non-minsize or full MariaDB builds,
- change public `libmylite` C API behavior, or
- change the `.mylite` file format.

## Proposed Design

Add `MYLITE_DISABLE_SFORMAT_FUNCTION` as an off-by-default embedded CMake
option. When enabled, define `MYLITE_DISABLE_SFORMAT_FUNCTION` for embedded SQL
sources.

Guard the fmtlib include and `SFORMAT()` implementation in
`item_strfunc.cc`, the `Item_func_sformat` declaration in `item_strfunc.h`, the
`Create_func_sformat` builder in `item_create.cc`, and the native function
registry row.

Do not add a replacement `SFORMAT` builder. With
`MYLITE_DISABLE_STORED_FUNCTION_LOOKUP=ON`, an unregistered native function
fails through MyLite's existing fail-closed missing-function path with a normal
MariaDB diagnostic (`ER_SP_DOES_NOT_EXIST`, SQLSTATE `42000`). That is
explicit failure, not silent success or semantic emulation.

## Affected Subsystems

- Embedded SQL native function registration.
- String function implementation.
- Minsize build profile.
- MyLite open/close smoke unsupported-function coverage.

## Single-File And Embedded-Lifecycle Impact

No file-format, catalog, storage-engine, locking, or runtime lifecycle behavior
changes. The slice removes a SQL convenience function whose implementation is
pure expression evaluation.

## Public API Or File-Format Impact

No public API change and no file-format change.

## Binary-Size Impact

Linked-size savings are meaningful because `SFORMAT()` instantiates fmtlib's
header-only formatting machinery, including argument stores, parser helpers,
float formatting, and fmtlib exception metadata.

Measured against `build/mariadb-minsize-direct-dispatch`, the clean
`build/mariadb-minsize-no-sformat` build changed:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 25,283,578 | 24,991,890 | -291,688 |
| `mylite/libmylite.a` | 76,688 | 76,688 | 0 |
| `mylite/mylite-open-close-smoke` | 6,436,248 | 6,322,840 | -113,408 |
| stripped `mylite-open-close-smoke` copy | 4,524,384 | 4,452,104 | -72,280 |

The stripped smoke binary is now 4,452,104 bytes, down 14,879,800 bytes from
the original 19,331,904-byte size-research baseline. Linked symbol inspection
found no remaining `fmt::` symbols in `mylite-open-close-smoke`.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sformat \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sformat \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sformat \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sformat \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sformat \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh \
  tools/run-libmylite-open-close-smoke.sh \
  tools/run-embedded-bootstrap-smoke.sh \
  tools/run-storage-engine-smoke.sh \
  tools/run-compatibility-test-harness.sh
git diff --check
```

Measure:

- `libmysqld/libmariadbd.a`,
- `mylite/libmylite.a`,
- unstripped and stripped `mylite-open-close-smoke`,
- `size` section totals, and
- linked `fmt::` symbol absence.

Verification completed:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sformat
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sformat
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sformat
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sformat
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sformat
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`

The open/close report contains
`exec_sformat_message=FUNCTION SFORMAT does not exist`.

## Acceptance Criteria

- The aggressive minsize build passes.
- Current smokes and compatibility harness pass.
- `SFORMAT()` returns an explicit unsupported/missing-function MariaDB
  diagnostic.
- `FORMAT()` still returns normal formatted numeric output.
- Linked MyLite smoke no longer contains `fmt::` symbols from `SFORMAT()`.
- Size results are recorded here and in
  `docs/research/production-size-analysis.md`.

## Risks And Unresolved Questions

- `SFORMAT()` is a documented MariaDB function, so this belongs only to the
  aggressive minsize profile unless product scope later accepts it as a default
  omission.
- If another retained SQL path starts using fmtlib, this slice will not remove
  all `fmt::` symbols. The verification step must check linked symbols, not
  just source guards.
