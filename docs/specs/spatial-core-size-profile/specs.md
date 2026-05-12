# Spatial Core Size Profile

## Problem

The aggressive MyLite minsize profile already omits GIS SQL functions and
MyISAM RTREE support. MyLite storage also rejects GEOMETRY columns and SPATIAL
indexes. The linked embedded runtime still keeps MariaDB's core WKB/WKT
geometry implementation from `sql/spatial.cc`, even though there is no current
product path that can persist or index geometry values.

Current measured spatial-core object sizes from
`build/mariadb-minsize-myisam-rtree/libmysqld/CMakeFiles/sql_embedded.dir`:

| Object | File bytes | `size` total |
| --- | ---: | ---: |
| `__/sql/spatial.cc.o` | 138,160 | 38,032 |

The linked open/close smoke still contains live symbols such as
`Geometry::construct`, `Geometry::as_wkt`, `Geometry::get_key_image_itMBR`,
`Geometry::ci_collection`, and multiple `Gis_*::init_from_wkb()` helpers.

## Source Findings

MariaDB source references are from imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `libmysqld/CMakeLists.txt` includes `../sql/spatial.cc` in
  `SQL_EMBEDDED_SOURCES`.
- `sql/spatial.cc` implements geometry class registration, WKB/WKT parsing,
  geometry construction, WKT/JSON formatting, MBR extraction, and concrete
  `Gis_*` geometry classes.
- The existing `gis-function-size-profile` removed `item_geofunc.cc`,
  `gcalc_tools.cc`, and `gcalc_slicescan.cc`, but deliberately kept
  `sql_type_geom.cc` and `spatial.cc` so GEOMETRY type parsing and current
  GEOMETRY/SPATIAL rejection tests continued to work.
- `sql_type_geom.cc` still needs only these `spatial.cc` definitions in the
  current minsize object graph: `Geometry::construct`,
  `Geometry::get_key_image_itMBR`, `Geometry::as_wkt`, and
  `Geometry::ci_collection`.
- `item_create.cc` still references `native_func_registry_array_geom`, already
  provided by `mylite_gis_function_stub.cc` when GIS functions are disabled.
- `storage/mylite/ha_mylite.cc` rejects `MYSQL_TYPE_GEOMETRY` columns and
  `HA_SPATIAL_legacy` keys before MyLite storage accepts table definitions.

## Scope

Add a `MYLITE_DISABLE_SPATIAL_CORE` embedded minsize option that:

- requires `MYLITE_DISABLE_GIS_FUNCTIONS=ON`,
- removes `../sql/spatial.cc` from the embedded SQL source list,
- adds a small embedded-only stub for the remaining geometry symbols required
  by `sql_type_geom.cc`,
- keeps GEOMETRY type parsing and MyLite GEOMETRY/SPATIAL DDL rejection paths,
  and
- keeps GIS SQL functions unregistered.

## Non-Goals

This slice does not remove `sql_type_geom.cc` or `MYSQL_TYPE_GEOMETRY` from the
SQL type system. A full geometry-type removal is larger and riskier because the
type handler is referenced from generic type lookup, metadata unpacking, field
creation, and prepared-statement metadata.

This slice does not implement GEOMETRY storage, SPATIAL indexes, or GIS
functions.

This slice does not change the full MariaDB server target.

## Binary-Size Impact

Expected linked-runtime savings are bounded by the live portions of
`spatial.cc.o`, currently about 38 KiB of allocated sections before link layout
effects. Static archive savings should be close to the 138 KiB removed object
file, minus the replacement stub.

Measured on `build/mariadb-minsize-spatial-core`, this reduced
`libmysqld/libmariadbd.a` from 33,284,948 bytes to 33,144,206 bytes, saving
140,742 bytes. The stripped `mylite-open-close-smoke` copy dropped from
6,568,840 bytes to 6,532,968 bytes, saving 35,872 bytes.

## DDL Metadata Routing Impact

No MyLite catalog-format change. MyLite must continue rejecting GEOMETRY
columns and SPATIAL keys before table-definition persistence. If a geometry
value conversion path is accidentally reached in the minsize profile, the stub
must fail clearly rather than constructing a partial geometry value.

## Single-File And Embedded-Lifecycle Implications

No file-format, catalog, lock, recovery, or lifecycle changes are expected. The
grouped compatibility harness must continue to prove GEOMETRY/SPATIAL rejection
paths and sidecar scans.

## Public API Or File-Format Impact

No public `libmylite` API change and no `.mylite` file-format change.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-spatial-core MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-spatial-core MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-spatial-core MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
git diff --check
bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh tools/run-compatibility-test-harness.sh
```

Measure:

- `libmysqld/libmariadbd.a` bytes and object count,
- stripped `mylite-open-close-smoke` bytes,
- absence of `spatial.cc.o` from the merged archive, and
- absence of live `Gis_*` implementation symbols from the linked smoke.

## Acceptance Criteria

- Default minsize build links without `spatial.cc.o`.
- Open/close smoke and full compatibility harness pass.
- GIS SQL functions remain rejected as unknown functions.
- MyLite GEOMETRY and SPATIAL DDL remain rejected.
- Size deltas are recorded in `docs/research/production-size-analysis.md`.

## Verification Result

Verified with:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-spatial-core MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-spatial-core MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-spatial-core MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
git diff --check
bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh tools/run-compatibility-test-harness.sh
```

Measured artifacts:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 33,144,206 |
| `mylite/libmylite.a` | 122,792 |
| `storage/mylite/libmylite_embedded.a` | 388,440 |
| `mylite/mylite-open-close-smoke` | 8,961,760 |
| stripped `mylite-open-close-smoke` copy | 6,532,968 |

`spatial.cc.o` is absent from `libmariadbd.a`. The linked smoke retains only
the spatial-core stub symbols required by `sql_type_geom.cc`:
`Geometry::construct`, `Geometry::get_key_image_itMBR`, `Geometry::as_wkt`, and
`Geometry::ci_collection`.

## Risks

- This is a compatibility cut. Even though GEOMETRY storage is already rejected
  by MyLite, generic MariaDB SQL expressions can still mention geometry types.
  The minsize profile must fail those paths clearly.
- The savings are likely modest because most large GIS function code was
  already removed by `gis-function-size-profile`.
- A future MyLite GEOMETRY implementation would need to undo this profile or
  replace the stub with a MyLite-owned geometry codec.
