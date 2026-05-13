# Vector Type Size Profile

## Problem Statement

The aggressive minsize profile already omits MariaDB vector SQL functions and
the MHNSW vector-index implementation, but it still links the `VECTOR` data
type core through `sql_type_vector.cc`. MyLite has no vector storage, vector
index, or vector function product path today, and MyLite storage rejects vector
indexes explicitly. Keeping the type handler in the smallest embedded profile
is therefore inconsistent with the rest of the current vector omission.

## Source Base

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Local baseline before this slice:
  `986c5e8da5e71978aa34ee88e5e7cc6d1a88e613`.

## Source Findings

- `docs/specs/vector-function-size-profile/specs.md` records the earlier
  decision to omit `item_vectorfunc.cc` and `vector_mhnsw.cc` while retaining
  `sql_type_vector.cc`.
- [vendor/mariadb/server/libmysqld/CMakeLists.txt](../../../vendor/mariadb/server/libmysqld/CMakeLists.txt)
  still includes `../sql/sql_type_vector.cc` in `SQL_EMBEDDED_SOURCES`.
- [vendor/mariadb/server/sql/sql_type_vector.cc](../../../vendor/mariadb/server/sql/sql_type_vector.cc)
  defines `type_handler_vector`, `Type_collection_vector`, and
  `Field_vector`.
- [vendor/mariadb/server/sql/sql_type.cc](../../../vendor/mariadb/server/sql/sql_type.cc)
  includes `sql_type_vector.h` and returns `&type_handler_vector` from
  `Type_handler::handler_by_name()` when a user names the `vector` type.
- A non-destructive archive probe that removed only `sql_type_vector.cc.o`
  reduced `libmariadbd.a` by 143,164 bytes but failed to link only because
  `Type_handler::handler_by_name()` still referenced `type_handler_vector`.
- [vendor/mariadb/server/storage/mylite/ha_mylite.cc](../../../vendor/mariadb/server/storage/mylite/ha_mylite.cc)
  rejects non-BTREE key algorithms, so vector indexes are not a supported
  MyLite table surface.

## Scope

Add a minsize option that removes the `VECTOR` type handler from the embedded
library when vector functions are already disabled. The option will:

- remove `../sql/sql_type_vector.cc` from the embedded source list;
- compile `sql_type.cc` without `sql_type_vector.h`;
- make `Type_handler::handler_by_name()` stop recognizing `vector`; and
- keep vector SQL function rejection coverage.

## Non-Goals

- Do not remove parser tokens for vector index syntax in this slice.
- Do not change full MariaDB server target behavior.
- Do not change MyLite's existing unsupported-index DDL policy.
- Do not add public API or file-format changes.
- Do not implement vector storage, functions, or indexes.

## Proposed Design

Add `MYLITE_DISABLE_VECTOR_TYPE` to the embedded library build. When enabled:

- `libmysqld/CMakeLists.txt` defines `MYLITE_DISABLE_VECTOR_TYPE` and removes
  `../sql/sql_type_vector.cc` from `SQL_EMBEDDED_SOURCES`;
- CMake rejects configurations that disable the vector type without also
  disabling vector SQL functions;
- `sql_type.cc` skips `sql_type_vector.h` and the hardcoded
  `type_handler_vector` lookup; and
- `tools/build-mariadb-minsize.sh` enables the option in the aggressive
  minsize profile.

User `VECTOR` type declarations should now fail through MariaDB's
`ER_UNKNOWN_DATA_TYPE` path before MyLite stores a table definition.

## Affected Subsystems

- Embedded minsize SQL source list.
- MariaDB data-type lookup by name.
- Open/close smoke unsupported vector coverage.
- Binary-size documentation.

## DDL Metadata Routing Impact

`CREATE TABLE ... VECTOR(...)` should fail before MyLite persists a table
definition. This slice must not create `.frm` sidecars or MyLite catalog
records for vector columns.

## Single-File And Embedded-Lifecycle Impact

No file-format, catalog, recovery, lock, or lifecycle change. This is a type
lookup and build-profile change only.

## Public API Or File-Format Impact

No public `libmylite` API or `.mylite` file-format change.

## Binary-Size Impact

Current object-size evidence from
`build/mariadb-minsize-no-explain-runtime/libmysqld/libmariadbd.a`:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 28,896,338 |
| `sql_type_vector.cc.o` object | 131,040 |

Removing `sql_type_vector.cc.o` from a copied archive produced a 143,164-byte
archive reduction before link failure. Final savings should be close to that
because this slice removes the remaining `type_handler_vector` reference.

The final `build/mariadb-minsize-no-vector-type` attempt measures:

| Artifact | EXPLAIN runtime baseline | VECTOR type omitted | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 28,896,338 | 28,752,966 | -143,372 |
| `mylite-open-close-smoke` | 7,663,144 | 7,647,976 | -15,168 |
| stripped `mylite-open-close-smoke` | 5,496,920 | 5,489,760 | -7,160 |
| stripped `mylite-compatibility-smoke` | 5,386,424 | 5,379,088 | -7,336 |

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-vector-type \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-vector-type \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-vector-type \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-vector-type \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Add open/close smoke coverage that:

- existing vector function checks still fail through MariaDB's
  unknown-function path; and
- `CREATE TABLE mylite.vector_type_rejected (v VECTOR(3))` fails with
  `ER_UNKNOWN_DATA_TYPE` and does not create a MyLite table.

Measure archive bytes, stripped linked smoke bytes, and the absence of
`sql_type_vector.cc.o` from `libmariadbd.a`.

## Acceptance Criteria

- The minsize build completes with `MYLITE_DISABLE_VECTOR_TYPE=ON`. Verified
  with `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-vector-type
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`.
- Embedded bootstrap, open/close smoke, and compatibility harness pass.
  Verified with the commands in the test plan.
- `VECTOR` type DDL fails before catalog persistence. The open/close smoke
  verifies `CREATE TABLE mylite.vector_type_rejected (v VECTOR(3))` reports
  `Unknown data type: 'VECTOR'`.
- The embedded archive no longer contains `sql_type_vector.cc.o`. Verified by
  archive listing.
- Size results are recorded here and in
  `docs/research/production-size-analysis.md`.

## Risks

- Removing `VECTOR` type parsing is a stronger SQL compatibility loss than the
  previous vector-function slice. It belongs only in the most aggressive size
  profile.
- Parser syntax for `VECTOR INDEX` still exists, but MyLite already rejects
  unsupported index algorithms before storing table definitions.
- If MyLite later supports vector storage or indexes, this profile decision
  must be revisited with a compatibility matrix.
