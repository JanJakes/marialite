# unsupported-index-ddl-rejection

> Superseded note: this slice documented the earlier unsupported boundary.
> `docs/specs/descending-hash-index-storage/specs.md` later added support for
> descending BTREE key parts and `USING HASH` indexes, and
> `docs/specs/fulltext-index-storage/specs.md` later added metadata-backed
> FULLTEXT DDL plus common `MATCH ... AGAINST` search support.
> `docs/specs/geometry-spatial-storage/specs.md` later added GEOMETRY storage
> and metadata-backed SPATIAL key support.

## Problem Statement

MyLite currently supports BTREE-compatible primary and secondary indexes over
the row-storage bridge, including nullable key parts and BLOB/TEXT prefix key
parts. MariaDB exposes additional index forms whose semantics MyLite has not
implemented: FULLTEXT, SPATIAL, HASH, and descending key parts. The handler
already rejects unsupported key metadata at table-create time, but the smoke
only proves descending key rejection.

This slice expands coverage and documentation so unsupported specialized index
DDL fails deliberately and leaves no MyLite table definition behind.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:871` handles MyLite
  `create()` and returns `HA_ERR_UNSUPPORTED` when row or key metadata is not
  supported.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1989` iterates
  `TABLE_SHARE::key_info` and rejects any key that
  `mylite_key_supports_storage()` does not support.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:2007` rejects non-BTREE
  algorithms, FULLTEXT flags, SPATIAL flags, and generated keys.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:2027` rejects
  `HA_REVERSE_SORT` key parts.
- `vendor/mariadb/server/include/my_base.h:111` defines `ha_key_alg`,
  including `HA_KEY_ALG_BTREE` and `HA_KEY_ALG_HASH`.
- `vendor/mariadb/server/include/my_base.h:291` defines
  `HA_FULLTEXT_legacy`, `my_base.h:293` defines `HA_SPATIAL_legacy`, and
  `my_base.h:336` defines `HA_REVERSE_SORT`.
- `vendor/mariadb/server/sql/sql_table.cc:3632` maps FULLTEXT DDL to
  `HA_FULLTEXT_legacy`, `sql_table.cc:3641` maps SPATIAL DDL to
  `HA_SPATIAL_legacy`, and `sql_table.cc:3841` records descending key parts.

## Scope

This slice will:

- prove FULLTEXT key DDL is rejected for MyLite tables,
- prove SPATIAL key DDL is rejected for MyLite tables,
- prove HASH key algorithm DDL is rejected for MyLite tables,
- keep existing descending-key rejection coverage,
- verify rejected unsupported-index tables are absent from MyLite discovery,
- document the supported index boundary.

## Non-Goals

- Do not implement FULLTEXT indexes or MATCH/AGAINST behavior.
- Do not implement SPATIAL indexes, geometry storage, or GIS predicates.
- Do not implement HASH indexes.
- Do not implement descending index order.
- Do not change supported BTREE-compatible key behavior.

## Proposed Design

No storage-engine code change is expected. MyLite's existing
`mylite_table_supports_key_storage()` boundary already rejects the unsupported
forms before `mylite_store_table_definition()` persists the frm image. Add
storage smoke coverage for representative DDL and table-absence checks. If a
test shows any statement succeeds or leaves a table definition behind, fix the
key metadata validation path narrowly.

## Affected Subsystems

- Storage engine smoke same-process DDL coverage.
- Roadmap and single-file storage architecture docs.

## DDL Metadata Routing Impact

Rejected unsupported-index DDL must fail before MyLite stores the table
definition image in the `.mylite` catalog. `SHOW TABLES` must not discover the
failed table names after rejection.

## Single-File And Embedded-Lifecycle Implications

No file-format change. The slice preserves the current single-file catalog
boundary by preventing MyLite from storing table definitions whose index
semantics cannot be maintained from the current `INDEXPAGE` payload format.

## Public API Or File-Format Impact

No public `libmylite` API change and no file-format version bump.

## Binary-Size Impact

Expected size impact is zero apart from smoke-test code. Post-implementation
`MinSizeRel` artifact sizes will be recorded.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc`:
  - reject `FULLTEXT KEY` on a text column,
  - reject `SPATIAL KEY` on a geometry column,
  - reject `KEY ... USING HASH`,
  - keep descending-key rejection,
  - verify each rejected table is absent from `SHOW TABLES`.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `bash -n tools/run-storage-engine-smoke.sh
    tools/run-compatibility-test-harness.sh`
  - `git diff --check`

## Acceptance Criteria

- Unsupported FULLTEXT, SPATIAL, HASH, and descending-key DDL returns an error.
- Rejected unsupported-index table names are not discoverable through MyLite
  table discovery.
- Existing supported BTREE key, nullable key, BLOB/TEXT prefix key,
  autoincrement, copy ALTER, transaction, and public API smoke coverage keeps
  passing.
- Docs and roadmap describe FULLTEXT, SPATIAL, HASH, and descending indexes as
  unsupported until separately designed.

## Risks And Unresolved Questions

- SPATIAL key rejection is coupled to the current GEOMETRY storage rejection;
  future geometry storage will still need separate SPATIAL index design.
- MariaDB may normalize some unsupported key syntax before handler create. The
  smoke should use syntax known to set the relevant `KEY` algorithm or flags.

## Implementation Result

Implemented as storage-smoke coverage. No handler code change was needed:
`mylite_table_supports_key_storage()` already rejects unsupported key
algorithms and flags before table-definition persistence.

The storage smoke now rejects and verifies table absence for:

- `FULLTEXT KEY` on a `TEXT` column,
- `SPATIAL KEY` on a `GEOMETRY` column,
- `KEY ... USING HASH`,
- descending key parts.

Report evidence from `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`:

- `build/mariadb-minsize/mylite-storage-engine-report.txt`:
  - `status=0`
  - `message=ok`
  - `unsupported_fulltext_key=rejected`
  - `unsupported_spatial_key=rejected`
  - `unsupported_hash_key=rejected`
  - `unsupported_reverse_key=rejected`
- `build/mariadb-minsize/mylite-catalog-read-report.txt`:
  - `status=0`
  - `row_payloads` does not include the rejected unsupported-index tables.
  - `index_payloads` does not include the rejected unsupported-index tables.

Verification run:

- `git diff --check`
- `bash -n tools/run-storage-engine-smoke.sh
  tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`

Measured `MinSizeRel` artifacts from
`build/mariadb-minsize/mylite-build-report.txt` and `ls -l`:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,413,682 bytes,
  571 objects.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,839,088
  bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 87,206 bytes.
