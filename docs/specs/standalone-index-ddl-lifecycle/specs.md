# standalone-index-ddl-lifecycle

## Problem Statement

MyLite supports durable primary and secondary index payloads, and populated
copy `ALTER TABLE` already proves index roots can be rebuilt while rows are
copied into a replacement table. It does not yet prove standalone
`CREATE INDEX` and `DROP INDEX` statements preserve MyLite rows, update the
persisted table definition, and expose the expected index metadata across a
fresh embedded-process reopen.

This slice adds that coverage and fixes any narrow routing issue it exposes.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/sql_parse.cc:4241` dispatches
  `SQLCOM_DROP_INDEX`, falls through to `SQLCOM_CREATE_INDEX`, and calls
  `mysql_alter_table()` after preparing an `Alter_info` copy.
- `vendor/mariadb/server/sql/sql_table.cc:10673` documents that
  `CREATE|DROP INDEX` are mapped onto `mysql_alter_table()`.
- `vendor/mariadb/server/sql/sql_table.cc:11559` creates the altered table
  definition image for copy/in-place decision making.
- `vendor/mariadb/server/sql/sql_table.cc:11805` creates the replacement
  storage-engine table for copy ALTER when in-place ALTER is not used.
- `vendor/mariadb/server/sql/sql_table.cc:11747` now removes temporary frm
  files created by an in-place ALTER probe before a discovery-based engine
  falls back to copy ALTER.
- `vendor/mariadb/server/sql/handler.cc:5818` makes handler operations outside
  the base in-place allowlist fall back to copy ALTER.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:871` validates supported
  row/key metadata in `ha_mylite::create()` and stores the replacement table
  definition.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1989` rejects unsupported
  key metadata before a table definition is accepted.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:2109` rebuilds durable
  index roots from table rows for supported keys.

## Scope

This slice will:

- prove standalone `CREATE INDEX` on a supported MyLite table succeeds,
- prove the created index can serve forced lookup and ordering reads,
- prove standalone `DROP INDEX` removes index metadata without losing rows,
- prove a dropped index stays absent and a recreated index stays usable after a
  fresh embedded-process reopen,
- document the supported standalone index DDL path as copy-ALTER-backed.

## Non-Goals

- Do not implement in-place or incremental index DDL.
- Do not implement unsupported index shapes such as FULLTEXT, SPATIAL, HASH, or
  descending key parts.
- Do not add B-tree pages, page splits, or incremental index maintenance.
- Do not make DDL transactional across process crashes.

## Proposed Design

MariaDB maps standalone `CREATE INDEX` and `DROP INDEX` to
`mysql_alter_table()`. MyLite should let that copy-ALTER path create a
replacement MyLite table definition, copy rows through the handler, rebuild
durable `INDEXPAGE` roots from copied rows, and swap the replacement
definition into the catalog through the existing rename/delete path.

The initial implementation exposed one SQL-layer routing defect: for default
standalone index DDL, MariaDB may write a temporary frm image for an in-place
ALTER probe before deciding to use copy ALTER. Discovery-based engines such as
MyLite do not use persistent frm files, so the fallback cleanup must delete
that probe frm before the later handler rename can install it as a durable
sidecar.

Add storage smoke coverage in two places:

1. Same-process lifecycle coverage creates a keyed table, adds a secondary
   index with `CREATE INDEX`, verifies forced lookup and order reads, drops the
   index, verifies the index metadata is absent, verifies rows remain readable,
   and drops the table.
2. Persistence coverage creates a similar table in the write phase, creates and
   verifies one index, drops it, creates a replacement index, and verifies after
   fresh-process reopen that the dropped index is still absent while the
   replacement index is discoverable and usable.

If the copy path reaches `ha_mylite::create()` with unsupported metadata or
leaves stale index roots, fix the narrow MyLite catalog/index-root path rather
than adding a SQL-layer special case.

## Affected Subsystems

- Storage engine smoke DDL/DML and persistence coverage.
- SQL-layer cleanup after an in-place ALTER probe falls back to copy ALTER for
  discovery-based engines.
- MyLite catalog table-definition and index-root rewrite behavior if the smoke
  exposes a defect.
- Roadmap and single-file storage documentation.

## DDL Metadata Routing Impact

Successful standalone index DDL must publish exactly one accepted MyLite
catalog generation for the resulting table definition and must not leave
durable `.frm` sidecars. `DROP INDEX` must remove the dropped index from the
persisted table definition and from future durable index payload references.

## Single-File And Embedded-Lifecycle Implications

No new companion files are allowed. The replacement table definition, rows, and
index roots must remain in the primary `.mylite` file and must survive a fresh
embedded-process reopen. Crash safety during the DDL swap remains outside this
slice and is still deferred with the broader transactional-DDL design.

## Public API Or File-Format Impact

No public `libmylite` API change and no file-format version bump are expected.
The slice exercises existing table-definition and `INDEXPAGE` records.

## Binary-Size Impact

Expected library size impact is zero unless a narrow catalog/index fix is
needed. Smoke-test code will grow slightly. Post-implementation `MinSizeRel`
artifact sizes will be recorded.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc` same-process
  coverage:
  - create a supported MyLite table with a primary key and no secondary index,
  - insert rows,
  - run `CREATE INDEX note_created ON ...`,
  - verify `information_schema.STATISTICS` sees `note_created`,
  - verify forced lookup and ordered reads through `note_created`,
  - run `DROP INDEX note_created ON ...`,
  - verify `information_schema.STATISTICS` no longer sees `note_created`,
  - verify rows remain readable,
  - drop the table.
- Extend persistence write/read coverage:
  - create `mylite.persisted_index_ddl`,
  - insert rows,
  - create and verify `note_created`,
  - drop `note_created` and verify absence,
  - create and verify `note_recreated`,
  - after fresh-process reopen, verify `note_created` remains absent,
    `note_recreated` remains present, and forced reads through
    `note_recreated` return the expected rows.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `bash -n tools/run-storage-engine-smoke.sh
    tools/run-compatibility-test-harness.sh`
  - `git diff --check`

## Acceptance Criteria

- Standalone `CREATE INDEX` succeeds for the supported MyLite key subset.
- Forced index lookup and ordered reads work after standalone index creation.
- Standalone `DROP INDEX` removes index metadata and preserves rows.
- Fresh-process reopen sees the final standalone index DDL state.
- Existing DDL, DML, copy ALTER, persistence, transaction, unsupported-index,
  and public API coverage keeps passing.

## Risks And Unresolved Questions

- Standalone index DDL is copy-ALTER-backed and inherits the current DDL crash
  recovery limitation.
- Complete sorted index payload rewrites remain inefficient until real B-tree
  pages or incremental index maintenance exist.

## Implementation Result

Implemented in `vendor/mariadb/server/mylite/storage_engine_smoke.cc` and
`vendor/mariadb/server/sql/sql_table.cc`.

The storage smoke now verifies:

- same-process standalone `CREATE INDEX note_created` metadata, forced lookup,
  ordered reads, standalone `DROP INDEX`, row preservation, and table cleanup;
- persisted `mylite.persisted_index_ddl` with `note_created` created and
  dropped, `note_recreated` created, and fresh-process reopen proving
  `note_created` remains absent while `note_recreated` remains discoverable and
  usable;
- no `.frm` artifacts in the same-process, catalog write, or catalog read
  storage smoke reports.

The implementation also removes temporary frm files created by MariaDB's
in-place ALTER probe when a discovery-based engine falls back to copy ALTER.
Without that cleanup, standalone index DDL passed SQL behavior but left
`datadir/mylite/persisted_index_ddl.frm` during the catalog write phase.

Verification on 2026-05-11:

- `git diff --check`
- `bash -n tools/run-storage-engine-smoke.sh
  tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`

Observed report fields:

- `build/mariadb-minsize/mylite-storage-engine-report.txt`:
  `status=0`, `index_ddl_created_count=1`, `index_ddl_lookup_id=3`,
  `index_ddl_order_ids=1,3,2`, `index_ddl_dropped_count=0`,
  `index_ddl_rows=1:one,2:two,3:three`, `FRM Artifacts=none`.
- `build/mariadb-minsize/mylite-catalog-write-report.txt`:
  `status=0`, `persisted_index_ddl_created_count=1`,
  `persisted_index_ddl_dropped_count=0`,
  `persisted_index_ddl_recreated_count=1`,
  `persisted_index_ddl_lookup_id=3`,
  `persisted_index_ddl_order_ids=1,3,2`, `FRM Artifacts=none`.
- `build/mariadb-minsize/mylite-catalog-read-report.txt`:
  `status=0`, `persisted_index_ddl_dropped_count=0`,
  `persisted_index_ddl_recreated_count=1`,
  `persisted_index_ddl_lookup_id=3`,
  `persisted_index_ddl_order_ids=1,3,2`,
  `index_payloads` includes `mylite.persisted_index_ddl:0` and
  `mylite.persisted_index_ddl:1`, `FRM Artifacts=none`.
- `build/mariadb-minsize/mylite-compatibility-harness-report.txt`:
  all groups reported `status=0`.

Post-implementation `MinSizeRel` artifact observations:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,413,714 bytes,
  571 objects.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,839,352
  bytes.
