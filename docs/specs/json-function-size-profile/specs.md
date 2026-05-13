# JSON Function Size Profile

## Problem Statement

The aggressive embedded minsize profile still retains MariaDB's ordinary JSON
SQL functions. `JSON_TABLE` and `JSON_SCHEMA_VALID()` are already
omitted, but `item_jsonfunc.cc` and its helper still contribute a large SQL
function surface that is not required for MyLite's embedded file lifecycle.

Current baseline after `vector-type-size-profile`:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 28,752,966 |
| `item_jsonfunc.cc.o` object | 638,864 |
| `json_schema_helper.cc.o` object | 6,336 |
| stripped `mylite-open-close-smoke` | 5,489,760 |

## Source Findings

- Imported MariaDB base: `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/item_jsonfunc.cc` implements scalar JSON SQL
  functions including `JSON_VALID()`, `JSON_EXTRACT()`, `JSON_OBJECT()`,
  `JSON_ARRAY()`, `JSON_KEYS()`, `JSON_VALUE()`, and JSON path mutation
  helpers.
- `vendor/mariadb/server/sql/item_jsonfunc.h` declares the corresponding
  `Item_func_json_*` classes and is included through `item.h`.
- `vendor/mariadb/server/sql/item_create.cc` declares the JSON native function
  builders, defines their singleton builders, and registers the JSON names in
  `func_array`.
- `vendor/mariadb/server/sql/json_schema_helper.cc` remains only because
  retained JSON scalar code uses helper functions after
  `JSON_SCHEMA_VALID()` itself was removed.
- Current linked symbols include many JSON vtables, for example
  `Item_func_json_extract`, `Item_func_json_array_append`,
  `Item_func_json_object_filter_keys`, and `Item_func_json_normalize`.
- `vendor/mariadb/server/sql/sql_type_json.cc` is a separate JSON type/check
  implementation. This slice does not remove it.
- `JSON_ARRAYAGG` and `JSON_OBJECTAGG` are parser-backed aggregate surfaces
  declared in `item_jsonfunc.h` and implemented through methods in
  `item_jsonfunc.cc`, while sharing `Item_func_group_concat` code from
  `item_sum.cc`.

## Scope

Add a minsize option that removes ordinary JSON SQL functions from the
embedded library. The option will:

- require `MYLITE_DISABLE_JSON_SCHEMA_VALID=ON` and
  `MYLITE_DISABLE_JSON_TABLE=ON`;
- define `MYLITE_DISABLE_JSON_FUNCTIONS`;
- remove `../sql/item_jsonfunc.cc` and `../sql/json_schema_helper.cc` from
  `SQL_EMBEDDED_SOURCES`;
- exclude JSON scalar native-function builders and registrations from
  `item_create.cc`;
- replace parser-backed JSON aggregate runtime methods with explicit
  unsupported diagnostics;
- make representative JSON scalar calls fail through MariaDB's ordinary
  unknown-function path; and
- keep table DDL, text storage, the JSON type implementation, and previously
  unsupported `JSON_TABLE` behavior separate.

## Non-Goals

- Do not remove the `JSON` type alias or `sql_type_json.cc`.
- Do not remove the parser tokens for `JSON_ARRAYAGG` or `JSON_OBJECTAGG`.
  They become unsupported runtime surfaces in this profile.
- Do not remove shared JSON parser/writer utilities that are still referenced
  outside `item_jsonfunc.cc`.
- Do not change full MariaDB server target behavior.
- Do not add a public `libmylite` API or `.mylite` file-format change.

## Proposed Design

Add `MYLITE_DISABLE_JSON_FUNCTIONS` to
`vendor/mariadb/server/libmysqld/CMakeLists.txt` and enable it in
`tools/build-mariadb-minsize.sh`.

In `item_create.cc`, guard the JSON builder class declarations, singleton
definitions, builder methods, and `func_array` entries with
`#ifndef MYLITE_DISABLE_JSON_FUNCTIONS`. Keep the existing
`MYLITE_DISABLE_JSON_SCHEMA_VALID` guard nested where the broad JSON-function
guard is not enabled.

Add `vendor/mariadb/server/libmysqld/mylite_json_function_stub.cc` for the
small internal JSON support still required by the retained JSON type surface:
`Item_func_json_valid::val_bool()`, `is_json_type()`, JSON/string comparator
entry points, and explicit unsupported `JSON_ARRAYAGG` /
`JSON_OBJECTAGG` aggregate methods. The public scalar JSON function builders
remain absent, so ordinary scalar calls fail as unknown functions.

Update the open/close smoke so the aggressive profile verifies that
`JSON_VALID()` and `JSON_EXTRACT()` are rejected as unknown functions and that
`JSON_ARRAYAGG()` / `JSON_OBJECTAGG()` report explicit unsupported diagnostics.
The existing `JSON_TABLE` unsupported smoke remains a separate check.

## Affected Subsystems

- Embedded minsize SQL source list.
- Native SQL function registration.
- Open/close smoke unsupported SQL coverage.
- Binary-size analysis documentation.

## DDL Metadata Routing Impact

No direct DDL routing change is expected because this slice removes SQL
function execution, not persistent table metadata. JSON functions can appear in
generated columns, defaults, or check constraints in broader SQL, so MyLite's
aggressive profile should reject those expressions during parsing/resolution
through the missing native-function path instead of persisting an expression
that cannot execute.

## Single-File And Embedded-Lifecycle Impact

No file-format, catalog, recovery, locking, or sidecar-file change. This slice
removes SQL expression execution code that does not own persistent files.

## Public API Or File-Format Impact

No public `libmylite` API or `.mylite` file-format change. Future typed JSON
binding API design remains separate from this aggressive minsize experiment.

## Binary-Size Impact

Expected archive savings are bounded by `item_jsonfunc.cc.o` and
`json_schema_helper.cc.o`, minus the replacement internal stub, plus removed
JSON builder sections from `item_create.cc.o`. Linked-runtime savings should be
measurable because the current smoke binary retains many JSON `Item_func_*`
vtables and methods.

Measured after implementation:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 28,752,966 | 28,094,108 | -658,858 |
| unstripped `mylite-open-close-smoke` | 7,647,976 | 7,448,136 | -199,840 |
| stripped `mylite-open-close-smoke` | 5,489,760 | 5,352,064 | -137,696 |
| stripped `mylite-compatibility-smoke` | 5,379,088 | 5,240,144 | -138,944 |

The archive no longer contains `item_jsonfunc.cc.o` or
`json_schema_helper.cc.o`. It keeps `sql_type_json.cc.o` and the replacement
`mylite_json_function_stub.cc.o`.

## License, Trademark, And Dependency Impact

No new dependency or license impact. The imported MariaDB JSON sources remain
in the tree and are only omitted from the embedded minsize build profile.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-json-functions \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-json-functions \
  tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-json-functions \
  tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-json-functions \
  tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Measure:

- archive bytes;
- unstripped and stripped linked smoke bytes;
- absence of `item_jsonfunc.cc.o` and `json_schema_helper.cc.o` in
  `libmariadbd.a`; and
- absence of representative `Item_func_json_*` symbols from the linked smoke.

## Acceptance Criteria

- The minsize build succeeds with `MYLITE_DISABLE_JSON_FUNCTIONS=ON`. Passed
  with `build/mariadb-minsize-no-json-functions`.
- The open/close smoke and compatibility harness pass. Passed.
- `JSON_VALID()` and `JSON_EXTRACT()` fail as unknown functions. Passed.
- `JSON_ARRAYAGG()` and `JSON_OBJECTAGG()` fail with unsupported diagnostics.
  Passed.
- `JSON_TABLE` remains rejected through the existing unsupported diagnostic.
  Passed.
- The embedded archive no longer contains `item_jsonfunc.cc.o` or
  `json_schema_helper.cc.o`. Passed.
- Size results are recorded here and in
  `docs/research/production-size-analysis.md`. Passed.

## Verification

Validated on 2026-05-12 with:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-json-functions \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-json-functions \
  tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-json-functions \
  tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-json-functions \
  tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

The open/close report includes:

```text
exec_json_valid_message=FUNCTION JSON_VALID does not exist
exec_json_extract_message=FUNCTION JSON_EXTRACT does not exist
exec_json_arrayagg_message=This version of MariaDB doesn't yet support 'JSON_ARRAYAGG in the MyLite minsize profile'
exec_json_objectagg_message=This version of MariaDB doesn't yet support 'JSON_OBJECTAGG in the MyLite minsize profile'
```

## Risks And Unresolved Questions

- This removes a widely used MariaDB SQL feature. It belongs only in the most
  aggressive size profile or as a committed experiment for later comparison.
- The remaining JSON type surface can make JSON support partial. Removing it
  should be a separate slice with its own tests.
- Some retained code still uses generic JSON parser or writer helpers, so this
  slice should not claim to remove every JSON-related symbol.
