# Table maintenance statements

## Problem

Common application tooling issues metadata and maintenance statements such as
`CHECK TABLE`, `ANALYZE TABLE`, `OPTIMIZE TABLE`, `REPAIR TABLE`,
`CHECKSUM TABLE`, `SHOW INDEX`, `DESCRIBE`, and `EXPLAIN`. MyLite already
inherits most metadata and optimizer behavior from MariaDB, but the MyLite
handler does not override MariaDB's base admin-table methods. The inherited
defaults report `HA_ADMIN_NOT_IMPLEMENTED` for `CHECK`, `ANALYZE`, `OPTIMIZE`,
and `REPAIR`.

That makes healthy MyLite tables look unsupported to WordPress repair flows,
plugin diagnostics, migration tools, and application health checks. MyLite
should support the common statement surface with semantics that match the
single-file engine: validate readable table storage, refresh volatile stats,
and treat optimization/repair as successful no-ops for healthy tables until a
future compaction or repair format exists.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/sql_admin.cc` routes
  `ANALYZE TABLE`, `CHECK TABLE`, `OPTIMIZE TABLE`, and `REPAIR TABLE` through
  `mysql_admin_table()` and the handler entry points `handler::ha_analyze`,
  `handler::ha_check`, `handler::ha_optimize`, and `handler::ha_repair`.
- `vendor/mariadb/server/sql/handler.h` returns
  `HA_ADMIN_NOT_IMPLEMENTED` from the base `check()`, `analyze()`,
  `optimize()`, and `repair()` methods. `handler::ha_repair()` asserts that an
  engine returning `HA_ADMIN_OK` advertises `HA_CAN_REPAIR`.
- `vendor/mariadb/server/sql/sql_admin.cc` renders `HA_ADMIN_OK` as an
  `OK` status row and `HA_ADMIN_NOT_IMPLEMENTED` as a note saying the storage
  engine does not support the operation.
- `vendor/mariadb/server/sql/sql_table.cc:mysql_checksum_table()` implements
  `CHECKSUM TABLE`. Without engine-provided stored checksums it calls
  `handler::calculate_checksum()`, which scans handler rows through `rnd_*`.
  MyLite's row read path already reconstructs BLOB/TEXT row buffers for scans.
- `SHOW INDEX`, `DESCRIBE`, and `EXPLAIN` are MariaDB SQL-layer metadata and
  optimizer paths over opened table definitions and handler statistics. MyLite
  already supplies table discovery, key metadata, `info()`, indexed access,
  and `records_in_range()`.

## Scope

Support and test the common table-maintenance and diagnostic surface for
healthy MyLite tables:

- `CHECK TABLE`;
- `ANALYZE TABLE`;
- `OPTIMIZE TABLE`;
- `REPAIR TABLE`;
- `CHECKSUM TABLE` including `EXTENDED`;
- `SHOW INDEX`;
- `DESCRIBE`;
- `EXPLAIN`.

## Non-Goals

- Do not implement physical file compaction for `OPTIMIZE TABLE`.
- Do not implement offline salvage of unreadable or externally corrupted
  `.mylite` files for `REPAIR TABLE`.
- Do not add persistent optimizer statistics. MyLite still reports volatile
  exact row counts through `info()`.
- Do not support partition admin variants or server-wide maintenance commands.

## Design

Add MyLite handler overrides for the admin-table methods:

1. `check()` validates that the opened table definition still exists in the
   MyLite catalog, that live row images are large enough for the MariaDB table
   share, and that every physical MyLite BTREE/HASH index root still matches
   the opened key metadata. FULLTEXT and SPATIAL keys remain metadata-backed or
   scan-backed today, so they are intentionally skipped by the ordered-index
   root check.
2. `analyze()` calls `info()` to refresh volatile exact row-count statistics
   and returns `HA_ADMIN_OK`; MyLite does not persist optimizer histograms yet.
3. `optimize()` runs the same validation as `check()` and returns
   `HA_ADMIN_OK` for healthy tables. Physical compaction remains a future page
   allocator slice.
4. `repair()` runs the same validation as `check()` and returns
   `HA_ADMIN_OK` for healthy tables. Returning OK requires adding
   `HA_CAN_REPAIR` to MyLite table flags, matching MariaDB handler
   expectations. Corruption discovered by the validation path should return a
   failure/corruption admin status rather than silently claiming repair.
5. Keep `CHECKSUM TABLE` on MariaDB's inherited scan-based checksum path and
   add smoke coverage to prove it produces a non-NULL result for MyLite rows.

## Affected Subsystems

- MyLite handler admin methods.
- MyLite table flags.
- Storage-engine smoke coverage.
- Single-file storage docs and roadmap.

## Single-File and Embedded Lifecycle

These statements must not create durable `.frm`, `mysql.*`, Aria, InnoDB,
MyISAM, binlog, or schema-directory sidecars. `CHECK`, `ANALYZE`, `OPTIMIZE`,
and healthy `REPAIR` should not change the primary `.mylite` file in this
slice except for inherited statement bookkeeping outside MyLite's catalog.

## Public API and File Format Impact

No public `libmylite` API or file-format change is expected.

## Binary Size Impact

The handler overrides should be small and use existing row/index validation
helpers. No new dependency or large subsystem should be added.

## License and Dependency Impact

No new dependency or licensing change.

## Test Plan

- Extend the storage-engine smoke to execute the maintenance and diagnostic
  SQL against populated MyLite tables.
- Assert `CHECK`, `ANALYZE`, `OPTIMIZE`, and `REPAIR` return `status:OK`
  result rows instead of unsupported notes.
- Assert `CHECKSUM TABLE` and `CHECKSUM TABLE EXTENDED` return non-NULL
  numeric checksums.
- Assert `SHOW INDEX`, `DESCRIBE`, and `EXPLAIN` expose expected MyLite table,
  key, column, and plan metadata.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` on the smoke scripts
  - `git diff --check`

## Acceptance Criteria

- Healthy MyLite tables report OK for `CHECK TABLE`, `ANALYZE TABLE`,
  `OPTIMIZE TABLE`, and `REPAIR TABLE`.
- `CHECKSUM TABLE` returns a non-NULL checksum through MariaDB's scan path.
- `SHOW INDEX`, `DESCRIBE`, and `EXPLAIN` work against MyLite tables and expose
  expected key/column/plan metadata.
- The sidecar scan remains clean.

## Implementation Result

Implemented MyLite handler overrides for `check()`, `analyze()`,
`optimize()`, and `repair()`. The shared validation path checks the opened
catalog definition, live row-image sizes, and physical BTREE/HASH index roots.
MyLite now advertises `HA_CAN_REPAIR` so MariaDB's `REPAIR TABLE` path can
return a normal OK row for healthy opened tables.

The storage smoke records:

- `table_maintenance_admin_status=check:status:OK,analyze:status:OK,optimize:status:OK,repair:status:OK`
- `table_maintenance_checksum_status=nonnull:nonnull`
- `table_maintenance_show_index=1:1`
- `table_maintenance_describe=note`
- `table_maintenance_explain=maintenance_rows:note_key`

Those values prove the admin-table result rows, scan-based checksum path,
`SHOW INDEX`, `DESCRIBE`, and indexed `EXPLAIN` for a populated MyLite table.

## Risks and Unresolved Questions

- Returning `OK` from `REPAIR TABLE` is intentionally scoped to healthy tables.
  MyLite does not yet provide an offline repair utility for unreadable primary
  files; that belongs with a future durability and recovery slice.
- `OPTIMIZE TABLE` does not compact page chains yet. This is acceptable for
  application compatibility because the statement succeeds without changing SQL
  semantics, but physical compaction remains future work.
