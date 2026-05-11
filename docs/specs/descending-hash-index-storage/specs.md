# descending-hash-index-storage

## Problem Statement

MyLite currently maintains durable key-image streams for supported primary and
secondary indexes, but it still rejects two application-facing index forms that
the current representation can handle safely:

- descending BTREE key parts, such as `KEY created_at_desc(created_at DESC)`,
- `USING HASH` secondary indexes, where MariaDB records `HA_KEY_ALG_HASH`.

The remaining rejected specialized forms, FULLTEXT and SPATIAL, need separate
search and geometry semantics. This slice narrows the unsupported index set
without changing the current single-file page format.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/include/my_base.h:111` defines key algorithms,
  including `HA_KEY_ALG_BTREE` and `HA_KEY_ALG_HASH`.
- `vendor/mariadb/server/include/my_base.h:336` defines
  `HA_REVERSE_SORT` for descending key parts.
- `vendor/mariadb/server/sql/sql_table.cc:3675` stores the selected key
  algorithm in `KEY::algorithm`.
- `vendor/mariadb/server/sql/sql_table.cc:3834` through
  `sql_table.cc:3842` preserves `DESC` only for BTREE-compatible indexes and
  records it on each `KEY_PART_INFO`.
- `vendor/mariadb/server/sql/key.cc:488` implements `key_cmp()` for range
  comparisons and applies `HA_REVERSE_SORT`.
- `vendor/mariadb/server/sql/key.cc:649` implements `key_tuple_cmp()` for key
  images but does not apply `HA_REVERSE_SORT`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1888` rejects non-BTREE
  algorithms.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:2308` rejects
  descending key parts.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1931` sorts durable
  index entries by MariaDB key-image comparison.

## Scope

This slice will:

- accept `HA_KEY_ALG_HASH` keys and store them in the current durable index
  payloads,
- accept descending BTREE key parts,
- compare durable key images in index order with `HA_REVERSE_SORT` applied per
  key part,
- preserve unique-key enforcement with direction-insensitive equality,
- verify same-process and fresh-process reads through descending and HASH
  indexes,
- keep FULLTEXT and SPATIAL index DDL rejected explicitly.

## Non-Goals

- Do not implement FULLTEXT indexes or `MATCH ... AGAINST`.
- Do not implement SPATIAL indexes, RTREE, GEOMETRY storage, or GIS predicates.
- Do not add a physical hash table layout. `USING HASH` is maintained through
  the existing exact-key and ordered key-image stream; this preserves SQL
  results even though it is not a hash access structure yet.
- Do not change the index page format or bump the pre-release file format.
- Do not change foreign-key, generated-column, temporary-table, view, trigger,
  routine, package, or event support.

## Proposed Design

### HASH Keys

Allow `HA_KEY_ALG_HASH` in `mylite_key_supports_storage()`. MariaDB's
`Field::image_type()` treats HASH as raw key-image storage, so the current
`key_copy()` and durable `INDEXPAGE` payload format can store and compare the
same bytes used by supported BTREE-compatible keys. This gives correct exact
lookups and preserves rows across reopen. Ordered range scans may be more
capable than a true hash access path, but they remain SQL-correct because the
engine owns the physical implementation behind the advertised handler methods.

### Descending Key Parts

Remove the `HA_REVERSE_SORT` rejection and replace MyLite's use of
`key_tuple_cmp()` for index ordering with a MyLite helper that mirrors
MariaDB's tuple comparison while multiplying each key-part result by the
part's sort direction.

Unique checks still compare equality only, so direction does not affect
duplicate detection. The same helper can return zero for equal tuples and can
therefore be reused safely in duplicate checks.

## Affected Subsystems

- MyLite storage-engine key-shape validation.
- MyLite durable index entry sorting and validation.
- MyLite handler index positioning and range-count helpers.
- Storage smoke same-process and fresh-process coverage.
- Architecture, roadmap, and slice documentation.

## DDL Metadata Routing Impact

`CREATE TABLE`, `CREATE INDEX`, and copy-ALTER paths that add descending BTREE
or HASH indexes must now persist the table-definition image and matching
durable index roots inside the `.mylite` file. Failed FULLTEXT and SPATIAL DDL
must continue to leave no visible catalog table.

## Single-File And Embedded-Lifecycle Implications

No new sidecar files or catalog records are introduced. Descending and HASH
index entries are stored in existing `INDEXPAGE` page chains and referenced by
existing catalog `INDEXPAGE` records.

## Public API Or File-Format Impact

No public `libmylite` API change. No file-format bump is required because the
persisted key-image bytes and root records are unchanged.

## Binary-Size Impact

Expected binary growth is small: one comparison helper, validation changes, and
smoke assertions. Post-implementation artifact sizes will be recorded by the
existing build report.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc`:
  - create a table with a descending secondary key,
  - insert rows out of order and force the descending key for reads,
  - verify `ORDER BY note DESC` and range predicates use the expected row
    order,
  - verify fresh-process reopen reads through the descending key,
  - create a table with `KEY ... USING HASH`,
  - verify exact lookup through the HASH key and fresh-process reopen,
  - keep FULLTEXT and SPATIAL rejection checks.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `git diff --check`

## Acceptance Criteria

- Descending BTREE index DDL succeeds for MyLite tables.
- HASH index DDL succeeds for MyLite tables.
- Durable index payloads for descending and HASH keys survive fresh-process
  reopen.
- Forced descending index scans return rows in MariaDB index order.
- Forced HASH key equality lookups return the expected rows.
- FULLTEXT and SPATIAL index DDL remains explicitly rejected and absent from
  MyLite discovery.
- Existing storage, recovery, transaction, public API, and sidecar checks keep
  passing.

## Implementation Result

Implemented in `vendor/mariadb/server/storage/mylite/ha_mylite.cc` by
accepting `HA_KEY_ALG_HASH`, accepting `HA_REVERSE_SORT` key parts, and
replacing MyLite's durable key-image ordering with a MyLite comparator that
applies each key part's sort direction.

Extended `vendor/mariadb/server/mylite/storage_engine_smoke.cc` to verify:

- same-process descending secondary-key order and range scans,
- same-process HASH-key equality lookup,
- copy-ALTER adding HASH and descending secondary keys,
- fresh-process reopen through persisted descending and HASH secondary keys,
- continued FULLTEXT and SPATIAL DDL rejection.

Verification passed on May 13, 2026:

- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
- `git diff --check`

Representative storage reports:

- `mylite-storage-engine-report.txt`: `status=0`, `message=ok`,
  `descending_key_order_ids=2,3,1`, `descending_key_range_ids=2,3`,
  `hash_key_lookup_id=2`, `index_alter_hash_lookup_id=2`,
  `index_alter_desc_order_ids=2,1`.
- `mylite-catalog-read-report.txt`: `status=0`, `message=ok`,
  `persisted_descending_key_order_ids=2,3,1`,
  `persisted_descending_key_range_ids=2,3`,
  `persisted_hash_key_lookup_id=2`, and catalog payloads include
  `mylite.persisted_desc_hash_keyed` row and index records.
- `mylite-compatibility-harness-report.txt`: all groups and the harness result
  reported `status=0`.
- `mylite-build-report.txt`: `libmariadbd.a` remained 43,405,592 bytes with
  500 archive objects in the MinSizeRel profile.

## Risks And Unresolved Questions

- `USING HASH` is implemented as a logical key algorithm over the existing
  sorted key-image stream. If users depend on hash-specific performance rather
  than SQL results, a future physical hash-index slice should add a real hash
  payload.
- Descending composite keys with mixed ASC/DESC parts rely on range keys built
  by MariaDB's optimizer. Smoke coverage should include a range predicate, but
  broader MariaDB test-suite coverage is still needed.
