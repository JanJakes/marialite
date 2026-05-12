# vector-function-size-profile

## Problem

The minsize profile still links MariaDB vector SQL functions and the MHNSW
vector-index implementation. MyLite does not yet have a documented vector
storage or vector-index product path, and the current storage engine rejects
non-BTREE key algorithms before table definitions can be persisted.

This slice tests whether MyLite can omit vector SQL functions and MHNSW vector
index implementation from the default minsize embedded profile while retaining
the `VECTOR` type handler as a narrower compatibility surface.

## Source baseline

- MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Current MyLite size baseline after `executable-export-size-profile`:
  - `libmariadbd.a`: 33,092,908 bytes,
  - stripped `mylite-open-close-smoke`: 12,959,352 bytes,
  - linked `size` total: 13,249,609 bytes.

## Source findings

- MariaDB's official vector overview describes `VECTOR(n)`, `VECTOR INDEX`,
  `VEC_FromText()`, and `VEC_DISTANCE()` as the vector feature surface:
  - <https://mariadb.com/docs/server/reference/sql-structure/vectors/vector-overview>
- MariaDB's official `VEC_DISTANCE` documentation describes the generic vector
  distance function and its index-dependent behavior:
  - <https://mariadb.com/docs/server/reference/sql-functions/vector-functions/vector-functions-vec_distance>
- MariaDB's official `VEC_FromText` documentation describes conversion from a
  JSON array string into the binary `VECTOR` representation:
  - <https://mariadb.com/docs/server/reference/sql-functions/vector-functions/vec_fromtext>
- `vendor/mariadb/server/libmysqld/CMakeLists.txt` includes
  `../sql/item_vectorfunc.cc`, `../sql/vector_mhnsw.cc`, and
  `../sql/sql_type_vector.cc` in `SQL_EMBEDDED_SOURCES`.
- `vendor/mariadb/server/sql/item_create.cc` registers native builders for
  `VEC_DISTANCE_EUCLIDEAN`, `VEC_DISTANCE_COSINE`, `VEC_DISTANCE`,
  `VEC_FROMTEXT`, and `VEC_TOTEXT`.
- `vendor/mariadb/server/sql/item_vectorfunc.cc` implements those vector
  functions and calls `mhnsw_uses_distance()` for index-aware distance
  selection.
- `vendor/mariadb/server/sql/vector_mhnsw.cc` implements MHNSW vector-index
  operations, exposes `mhnsw_index_options`, owns `mhnsw_plugin`, and declares
  the built-in `mhnsw` plugin.
- `vendor/mariadb/server/sql/sql_type_vector.cc` defines
  `type_handler_vector` and `Type_collection_vector`. This slice keeps it so
  `VECTOR` type parsing remains owned by MariaDB rather than becoming an
  accidental parse error.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc` rejects key algorithms
  other than undefined and BTREE in `mylite_key_supports_storage()`, so vector
  indexes are not a supported MyLite storage surface today.
- Current object-section sizes:
  - `item_vectorfunc.cc.o`: 24,295 bytes,
  - `vector_mhnsw.cc.o`: 20,462 bytes,
  - `sql_type_vector.cc.o`: 16,283 bytes.
- Current unstripped object-file sizes:
  - `item_vectorfunc.cc.o`: 158,208 bytes,
  - `vector_mhnsw.cc.o`: 60,112 bytes,
  - `sql_type_vector.cc.o`: 128,944 bytes.

## Proposed design

Add an embedded-library CMake option:

```cmake
MYLITE_DISABLE_VECTOR_FUNCTIONS
```

When enabled:

- remove `../sql/item_vectorfunc.cc` and `../sql/vector_mhnsw.cc` from
  `SQL_EMBEDDED_SOURCES`,
- add a small embedded-only stub source for the retained MHNSW symbols that
  generic SQL/table code still references,
- define `MYLITE_DISABLE_VECTOR_FUNCTIONS` for the embedded build,
- guard vector native-function builder classes and registry entries in
  `item_create.cc`,
- set `-DMYLITE_DISABLE_VECTOR_FUNCTIONS=ON` and `-DPLUGIN_MHNSW=NO` in
  `tools/build-mariadb-minsize.sh`.

The stub is expected to keep generic server code linked without registering
vector functions or the MHNSW plugin in the minsize profile.

## Non-goals

- Do not remove `VECTOR` type parsing.
- Do not remove `sql_type_vector.cc`.
- Do not change full MariaDB server target behavior.
- Do not add a public API change.
- Do not implement vector storage or vector indexes for MyLite.

## Affected subsystems

- Embedded library build graph.
- Native SQL function registration.
- Built-in plugin list for `mhnsw`.
- Generic high-level index hooks that reference MHNSW symbols.
- `libmylite` open/close smoke coverage for unsupported minsize SQL surfaces.

## DDL metadata routing impact

Vector indexes remain unsupported by MyLite because the storage engine rejects
non-BTREE key algorithms. This slice must not let vector-index metadata persist.

## Single-file and embedded-lifecycle impact

No file-format, catalog, lock, recovery, or lifecycle changes are expected. The
grouped compatibility harness must continue to pass.

## Public API and file-format impact

No public `libmylite` API change. No `.mylite` file-format change.

## Binary-size impact

Expected archive savings are bounded by the two removed vector-function and
MHNSW objects minus a small stub. Linked savings may be smaller because some
vector code may not be pulled into the open-close proxy after earlier symbol
export reductions. The final decision must use production artifact
measurements.

## License, trademark, and dependency impact

No new dependency or license impact. The change removes MariaDB-derived vector
function and MHNSW implementation code from the default minsize profile but
keeps the imported source in the tree.

## Test plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

Add open/close smoke assertions that representative vector functions fail in
the minsize profile through MariaDB's unknown-function path.

Measure:

```sh
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

## Acceptance criteria

- The minsize build succeeds with `MYLITE_DISABLE_VECTOR_FUNCTIONS=ON` and
  `PLUGIN_MHNSW=NO`.
- The generated built-in plugin evidence no longer includes `mhnsw`.
- Vector functions are rejected in the minsize profile.
- Build, open/close smoke, and compatibility harness pass.
- Size deltas are recorded in `docs/research/production-size-analysis.md`.

## Risks and unresolved questions

- Removing vector functions is a SQL compatibility tradeoff. It is acceptable
  only for the minsize profile while MyLite has no vector storage or index
  product path.
- Keeping `sql_type_vector.cc` means this does not remove every vector-related
  byte.
- If later MyLite implements vector storage or vector indexes, this profile
  decision must be revisited with a compatibility matrix.
