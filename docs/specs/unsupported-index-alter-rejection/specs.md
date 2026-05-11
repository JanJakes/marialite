# unsupported-index-alter-rejection

## Problem Statement

The `unsupported-index-ddl-rejection` slice proves unsupported specialized
indexes fail during `CREATE TABLE`. `ALTER TABLE ... ALGORITHM=COPY` is a
separate mutation path: MariaDB builds a replacement definition and can copy
rows into a rebuilt table. MyLite needs coverage that unsupported index
additions fail there too, without replacing or corrupting the original table.

This slice proves failed unsupported-index ALTER statements leave existing
MyLite rows and table discovery intact.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/sql_table.cc:3448` initializes key metadata
  during table creation and ALTER preparation through `init_key_info()`.
- `vendor/mariadb/server/sql/sql_table.cc:3632` maps FULLTEXT DDL to
  `HA_FULLTEXT_legacy`, `sql_table.cc:3641` maps SPATIAL DDL to
  `HA_SPATIAL_legacy`, and `sql_table.cc:3841` records descending key parts.
- `vendor/mariadb/server/sql/sql_table.cc:4935` calls `ha_create_table()` for
  the prepared table definition.
- `vendor/mariadb/server/sql/sql_table.cc:12764` begins copy-ALTER row reads,
  and `sql_table.cc:12861` writes copied rows into the replacement handler.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:871` handles MyLite
  `create()` and returns `HA_ERR_UNSUPPORTED` when key metadata is not
  supported.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1989` iterates
  `TABLE_SHARE::key_info` and rejects unsupported keys before MyLite stores a
  table-definition image.

## Scope

This slice will:

- create a populated supported MyLite table,
- reject `ALTER TABLE ... ADD FULLTEXT KEY`,
- reject `ALTER TABLE ... ADD KEY ... USING HASH`,
- reject `ALTER TABLE ... ADD KEY (... DESC)`,
- verify the original rows remain intact after each failed ALTER,
- verify the original table remains discoverable and writable after failures.

## Non-Goals

- Do not implement any unsupported specialized index.
- Do not broaden copy ALTER support beyond the already supported row/key subset.
- Do not test every ALTER algorithm variant; use copy ALTER as the current
  MyLite row-preservation path.

## Proposed Design

No handler code change is expected. The same key metadata validation used by
`CREATE TABLE` should reject the replacement table definition before MyLite
publishes it. Add storage smoke coverage that exercises failed copy ALTER
statements and then queries the original table.

If any failed ALTER mutates rows, drops the table, or leaves unsupported index
metadata behind, fix the ALTER/create boundary rather than relaxing the test.

## Affected Subsystems

- Storage smoke same-process DDL and DML coverage.
- Roadmap and slice documentation.

## DDL Metadata Routing Impact

Failed unsupported-index ALTER statements must not publish a new MyLite catalog
generation for the altered table. The original table definition and row/index
payload roots must remain the accepted state.

## Single-File And Embedded-Lifecycle Implications

No file-format change. This slice verifies MyLite does not replace a valid
single-file catalog definition with unsupported key metadata during copy ALTER.

## Public API Or File-Format Impact

No public `libmylite` API change and no file-format version bump.

## Binary-Size Impact

Expected size impact is zero apart from smoke-test code. Post-implementation
`MinSizeRel` artifact sizes will be recorded.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc`:
  - create and populate a supported table,
  - reject copy ALTER adding a FULLTEXT key,
  - reject copy ALTER adding a HASH key,
  - reject copy ALTER adding a descending key,
  - verify the original rows after each failed ALTER,
  - insert another row after the failed ALTER sequence and verify final order.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `bash -n tools/run-storage-engine-smoke.sh
    tools/run-compatibility-test-harness.sh`
  - `git diff --check`

## Acceptance Criteria

- Unsupported index ALTER statements fail.
- Existing rows remain intact after each failed ALTER.
- The table remains writable after the failed ALTER sequence.
- Existing create-time unsupported-index rejection and copy ALTER row
  preservation coverage keeps passing.

## Risks And Unresolved Questions

- SPATIAL ALTER is left to the create-time coverage for now because geometry
  storage itself remains unsupported. A future geometry slice should add
  SPATIAL ALTER coverage when geometry columns can exist without SPATIAL
  indexes.

## Implementation Result

Implemented as storage-smoke coverage. No handler code change was needed:
the replacement table definition reaches the same MyLite key metadata boundary
as create-time DDL and is rejected before catalog publication.

The storage smoke now creates a supported table, inserts two rows, rejects
copy ALTER attempts that add a FULLTEXT key, HASH key, and descending key,
checks the original rows after each failure, inserts a third row after the
failure sequence, and verifies the final row set.

Report evidence from `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`:

- `build/mariadb-minsize/mylite-storage-engine-report.txt`:
  - `status=0`
  - `message=ok`
  - `unsupported_index_alter_fulltext=rejected`
  - `unsupported_index_alter_hash=rejected`
  - `unsupported_index_alter_reverse=rejected`
  - `unsupported_index_alter_rows=1:a:alpha,2:b:beta,3:c:gamma`

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
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,773,424
  bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 87,206 bytes.
