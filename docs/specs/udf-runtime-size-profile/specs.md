# UDF Runtime Size Profile

## Problem Statement

MyLite's embedded profile already rejects UDF DDL and dynamic plugin loading is
compiled out, but the linked runtime-style smoke still contains UDF lookup and
execution code. Live symbols include `find_udf()`, `udf_init()`,
`Create_udf_func`, `udf_handler`, and the `Item_func_udf*` /
`Item_sum_udf*` vtables.

UDFs load process-global external code from plugin files and depend on
`mysql.func` metadata. That does not fit the default file-owned embedded
environment, so the aggressive minsize profile should compile out the UDF
runtime path as well as the DDL path.

## Source Findings

- `docs/specs/schema-object-ddl-rejection/specs.md` already rejects
  `CREATE FUNCTION` and `DROP FUNCTION` in embedded builds.
- `docs/specs/unsupported-server-surface/specs.md` identifies UDF loading as
  a server-oriented unsupported surface.
- `vendor/mariadb/server/sql/sql_yacc.yy` probes `find_udf()` while parsing
  generic function calls and can create UDF aggregate/function items.
- `vendor/mariadb/server/sql/item_create.cc` defines `Create_udf_func`, which
  constructs `Item_func_udf*` and `Item_sum_udf*` instances.
- `vendor/mariadb/server/sql/sql_udf.cc` initializes the UDF hash from
  `mysql.func`, opens dynamic libraries, and owns `find_udf()`/`free_udf()`.
- Before this slice, `vendor/mariadb/server/libmysqld/CMakeLists.txt` compiled
  `../sql/sql_udf.cc` into the embedded archive.

## Proposed Design

Add `MYLITE_DISABLE_UDF_RUNTIME` as an off-by-default MariaDB CMake option.
The aggressive minsize script enables it.

When enabled:

- compile UDF declarations to no-op stubs in `sql_udf.h`,
- skip parser-side UDF lookup and UDF item construction,
- skip `Create_udf_func`,
- skip UDF execution method bodies in `item_func.cc` and `item_sum.cc`,
- omit `sql_udf.cc` from `libmysqld` embedded sources,
- keep existing embedded UDF DDL rejection behavior.

Unknown non-native function calls will continue falling through to the stored
function resolver. That preserves the current user-facing shape for ordinary
unknown function names without loading UDF metadata.

## Affected Subsystems

- SQL parser generic function-call resolution.
- UDF runtime lookup/execution.
- Embedded startup, because `udf_init()` becomes a no-op in the disabled
  profile.
- No MyLite storage-engine, public C API, or file-format changes.

## Single-File And Embedded-Lifecycle Impact

This improves the embedded lifecycle by removing a `mysql.func` and dynamic
library startup path from the aggressive profile. It does not affect `.mylite`
catalog format or table storage.

## Public API Or File-Format Impact

No public MyLite C API or `.mylite` file-format change.

## Binary-Size Impact

Measured against `build/mariadb-minsize-no-unwind`:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 31,864,556 | 31,748,626 | -115,930 |
| unstripped `mylite-open-close-smoke` | 8,324,928 | 8,272,992 | -51,936 |
| stripped `mylite-open-close-smoke` copy | 5,962,256 | 5,926,048 | -36,208 |

The disabled profile removes `sql_udf.cc.o` from `libmariadbd.a`; the linked
smoke no longer contains `Create_udf_func`, `Item_func_udf*`,
`Item_sum_udf*`, `udf_handler`, `find_udf`, `udf_init`,
`mysql_create_function`, or `mysql_drop_function` symbols.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-udf \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-udf \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-udf \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Compare:

- `libmysqld/libmariadbd.a`
- unstripped and stripped `mylite-open-close-smoke`
- presence of `sql_udf.cc.o`, `Create_udf_func`, `Item_func_udf*`,
  `Item_sum_udf*`, `udf_handler`, and `find_udf` symbols

Verification completed with the commands above. `bash -n
tools/build-mariadb-minsize.sh` and `git diff --check` also pass.

## Acceptance Criteria

- The minsize build completes.
- The open/close smoke and compatibility harness pass.
- UDF runtime symbols are absent from the linked smoke.
- `sql_udf.cc.o` is absent from the embedded archive in the disabled profile.
- Existing embedded DDL rejections remain intact.

## Risks And Unresolved Questions

- SQL-visible UDF execution is removed from the aggressive profile. This is a
  compatibility loss if an embedding target expects process-local UDF plugins.
- Unknown-function diagnostics may differ when a function name previously
  matched a registered UDF. In the disabled profile, UDFs cannot be registered.
