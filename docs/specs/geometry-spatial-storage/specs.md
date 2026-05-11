# geometry-spatial-storage

## Problem Statement

MyLite still rejects GEOMETRY columns and SPATIAL indexes. That leaves one
major application-facing MariaDB data type family and one specialized index
class unsupported after the FULLTEXT slice. WordPress itself rarely needs GIS
columns, but location, mapping, directory, and ecommerce plugins commonly use
POINT, POLYGON, spatial predicates, or `SPATIAL KEY` metadata.

MariaDB already owns WKB validation, geometry type checks, spatial functions,
and table-definition metadata above the handler. MyLite's current BLOB/TEXT row
bridge can persist blob-backed payload bytes, but it explicitly rejects
`MYSQL_TYPE_GEOMETRY` and `HA_SPATIAL_legacy` key metadata. This slice removes
that artificial storage boundary while avoiding a false claim that MyLite has a
physical R-tree page format.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/handler.h:136` defines `HA_CAN_GEOMETRY`, the
  table capability gate for GEOMETRY columns.
- `vendor/mariadb/server/sql/handler.h:180` defines `HA_CAN_RTREEKEYS`, the
  table capability gate for RTREE/SPATIAL keys.
- `vendor/mariadb/server/sql/sql_type_geom.cc:310` through
  `sql_type_geom.cc:320` rejects GEOMETRY columns unless the handler advertises
  `HA_CAN_GEOMETRY`, then routes them through blob-field preparation.
- `vendor/mariadb/server/sql/sql_type_geom.cc:358` through
  `sql_type_geom.cc:371` initializes SPATIAL key parts as four double MBR
  coordinates.
- `vendor/mariadb/server/sql/sql_table.cc:3641` through `sql_table.cc:3644`
  maps SPATIAL DDL to `HA_SPATIAL_legacy` and `HA_KEY_ALG_RTREE`.
- `vendor/mariadb/server/sql/sql_table.cc:3701` through `sql_table.cc:3712`
  rejects RTREE metadata unless the handler advertises `HA_CAN_RTREEKEYS` and
  the key is a one-part SPATIAL key.
- `vendor/mariadb/server/sql/sql_type_geom.cc:880` through
  `sql_type_geom.cc:940` validates and stores geometry WKB bytes through
  `Field_blob` storage.
- `vendor/mariadb/server/sql/sql_type_geom.cc:991` through
  `sql_type_geom.cc:1001` builds raw or MBR key images for geometry fields.
- `vendor/mariadb/server/sql/item_geofunc.cc:1057` through
  `item_geofunc.cc:1124` builds MBR range predicates for spatial relation
  functions when a usable RTREE key exists.
- `vendor/mariadb/server/storage/myisam/ha_myisam.cc:724` through
  `ha_myisam.cc:730` and `vendor/mariadb/server/storage/maria/ha_maria.cc:972`
  through `ha_maria.cc:978` show existing engines advertise both GEOMETRY and
  RTREE support.
- `vendor/mariadb/server/storage/myisam/ha_myisam.cc:753` through
  `ha_myisam.cc:764` shows MyISAM marks RTREE reads as non-ROR and disables
  index condition pushdown.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:3700` through
  `ha_mylite.cc:3711` currently rejects GEOMETRY fields in the blob storage
  bridge.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:3741` through
  `ha_mylite.cc:3766` currently rejects SPATIAL key flags and RTREE
  algorithms.

## Scope

This slice will:

- advertise `HA_CAN_GEOMETRY` so MariaDB accepts GEOMETRY columns for MyLite
  tables,
- persist GEOMETRY payload bytes through the existing BLOB/TEXT row and
  overflow bridge,
- accept SPATIAL/RTREE key metadata when it is a single non-null GEOMETRY key
  part,
- keep SPATIAL keys out of ordered durable `INDEXPAGE` roots,
- avoid exposing normal ordered index reads for SPATIAL keys until a real
  spatial access path exists,
- verify geometry values survive insert, update, copy ALTER, and
  fresh-process reopen,
- verify SPATIAL metadata survives `SHOW CREATE TABLE`, copy ALTER, and
  fresh-process reopen,
- verify spatial predicates still return correct rows through MariaDB's
  SQL-layer geometry functions.

## Non-Goals

- Do not implement a physical R-tree page format.
- Do not claim indexed spatial predicate performance.
- Do not implement handler-level MBR range scans in this first slice.
- Do not implement generated virtual GEOMETRY indexes.
- Do not change MariaDB spatial function semantics.

## Proposed Design

Treat GEOMETRY as a blob-backed MariaDB field that MyLite can persist, and
treat SPATIAL keys as metadata-backed definitions until a later R-tree storage
slice exists.

1. Handler flags: add `HA_CAN_GEOMETRY` and `HA_CAN_RTREEKEYS` so MariaDB
   accepts GEOMETRY columns and SPATIAL key metadata for MyLite tables.
2. Row storage: stop rejecting `MYSQL_TYPE_GEOMETRY` in
   `mylite_table_supports_blob_storage()`. `Field_geom` inherits
   `Field_blob`, and MyLite's existing encode/decode path already stores the
   fixed record prefix with cleared native pointer bytes plus appended payload
   bytes.
3. Key validation: add a `mylite_key_is_spatial()` helper. Accept RTREE/SPATIAL
   metadata only when the key has one key part, the part field is a GEOMETRY
   field, and the part is not nullable. Continue rejecting mixed FULLTEXT and
   SPATIAL flags and unsupported algorithms.
4. Durable index roots: skip SPATIAL keys when refreshing normal `INDEXPAGE`
   roots and when building ordered key-entry streams. The MariaDB
   table-definition image still persists the SPATIAL key metadata.
5. Optimizer exposure: return no ordered/range index flags for SPATIAL keys in
   `index_flags()`. This keeps SQL correct through table scans and SQL-layer
   spatial predicate evaluation without routing MBR ranges into the ordered
   key-image comparator.

This mirrors the current FULLTEXT/HASH precedent: support the SQL and metadata
surface first without inventing a misleading physical index format.

## Affected Subsystems

- MyLite handler capability flags.
- MyLite row-shape validation.
- MyLite key-shape validation.
- Durable index-root refresh.
- Storage smoke DDL, DML, metadata, and persistence coverage.
- Roadmap and single-file architecture docs.

## DDL Metadata Routing Impact

GEOMETRY and SPATIAL table definitions must persist only in the primary
`.mylite` file through the stored MariaDB table-definition image. MyLite must
not create MyISAM/Maria `.MYI`, `.MYD`, `.MAI`, `.MAD`, or auxiliary spatial
index sidecars.

## Single-File And Embedded-Lifecycle Implications

No companion files are introduced. There is no file-format version bump for
this first metadata-backed implementation because GEOMETRY bytes fit the
existing row/overflow payload representation and SPATIAL key metadata lives in
the table-definition image.

## Public API Or File-Format Impact

No public `libmylite` API change. The SQL surface expands to include GEOMETRY
columns, spatial functions over stored MyLite rows, and SPATIAL index metadata.
The primary file format remains compatible with current pre-release v3 catalog
files.

## Binary-Size Impact

Expected binary growth is small: validation helpers and smoke coverage only.
MariaDB spatial function code is already part of the imported server profile.
No new dependency is added.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc`:
  - create a table with a GEOMETRY column,
  - insert POINT/POLYGON values through `ST_GeomFromText()`,
  - verify `ST_AsText()`, `GeometryType()`, and spatial predicates over stored
    rows,
  - update a GEOMETRY value and verify the changed WKB survives,
  - add a GEOMETRY column through copy `ALTER TABLE`,
  - create a table with `SPATIAL KEY`,
  - verify `SHOW CREATE TABLE` exposes the SPATIAL key,
  - add a SPATIAL key through copy `ALTER TABLE`,
  - persist GEOMETRY/SPATIAL tables and verify metadata/results after
    fresh-process reopen,
  - verify no ordered `INDEXPAGE` root is published for the SPATIAL key.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
  - `git diff --check`

## Acceptance Criteria

- `CREATE TABLE ... GEOMETRY ... ENGINE=MYLITE` succeeds.
- GEOMETRY payloads survive insert, update, copy ALTER, and fresh-process
  reopen.
- MariaDB spatial functions return correct values for stored MyLite rows.
- `CREATE TABLE ... SPATIAL KEY ... ENGINE=MYLITE` succeeds for valid non-null
  one-part GEOMETRY keys.
- SPATIAL metadata survives `SHOW CREATE TABLE`, copy ALTER, and fresh-process
  reopen.
- SPATIAL keys are not stored as ordinary ordered `INDEXPAGE` roots.
- Existing BLOB/TEXT, nullable key, descending/HASH, FULLTEXT,
  generated-column, FK, CHECK, transaction, recovery, lifecycle, and sidecar
  checks keep passing.

## Risks And Unresolved Questions

- This does not provide physical R-tree performance. Query plans should avoid
  treating the SPATIAL key as an ordered key until a real MBR access path is
  implemented.
- `key_copy()` can build MBR images for RTREE keys, but MyLite's current
  ordered key-image comparator is not an R-tree and should not be reused as if
  it were one.
- Generated virtual GEOMETRY expressions remain unsupported because current
  generated-column support still rejects virtual BLOB/GEOMETRY storage shapes.

## Implementation Result

Implemented as metadata-backed GEOMETRY/SPATIAL support in the MyLite handler.
`ha_mylite` now advertises `HA_CAN_GEOMETRY` and `HA_CAN_RTREEKEYS`, persists
GEOMETRY values through the existing blob-backed row payload path, accepts
valid SPATIAL/RTREE key metadata, skips SPATIAL keys when publishing ordered
`INDEXPAGE` roots, and returns no ordered/range index flags for SPATIAL keys.

The storage smoke now covers:

- `CREATE TABLE ... GEOMETRY ... ENGINE=MYLITE`,
- `ST_AsText()` and `GeometryType()` over stored rows,
- spatial predicates over stored MyLite rows,
- GEOMETRY update visibility,
- copy `ALTER TABLE ... ADD COLUMN ... GEOMETRY`,
- `CREATE TABLE ... SPATIAL KEY ... ENGINE=MYLITE`,
- `SHOW CREATE TABLE` exposure of SPATIAL metadata,
- copy `ALTER TABLE ... ADD SPATIAL KEY`,
- persistence of SPATIAL metadata and spatial predicate results after
  fresh-process reopen.

Report evidence from `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`:

- `build/mariadb-minsize/mylite-storage-engine-report.txt`:
  - `status=0`
  - `message=ok`
  - `geometry_show_create=present`
  - `geometry_rows=1:POINT:POINT(1 2),2:POINT:POINT(5 6)`
  - `geometry_predicate_ids=1`
  - `geometry_updated_ids=1,2`
  - `geometry_alter_rows=1:POINT(7 8)`
  - `spatial_show_create=present`
  - `spatial_predicate_ids=1`
  - `spatial_alter_show_create=present`
  - `spatial_alter_ids=1`
- `build/mariadb-minsize/mylite-catalog-write-report.txt`:
  - `status=0`
  - `persisted_spatial_show_create=present`
  - `persisted_spatial_ids=1,3`
- `build/mariadb-minsize/mylite-catalog-read-report.txt`:
  - `status=0`
  - `persisted_spatial_show_create=present`
  - `persisted_spatial_ids=1,3`
  - `index_payloads` includes only the table's ordered key roots, not a
    SPATIAL root for `persisted_spatial`.
