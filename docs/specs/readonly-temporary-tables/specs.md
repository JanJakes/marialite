# Read-only temporary tables

## Problem

MyLite supports public read-only opens for the primary `.mylite` file and
supports user-created temporary MyLite tables as session-scoped in-memory
definitions. Those two features do not compose yet: the handler rejects
`CREATE TEMPORARY TABLE` and temporary table DML when `mylite_read_only` is
set, even though temporary rows and indexes do not mutate the primary file.

Applications commonly open databases read-only for reporting or inspection
while still using temporary tables for intermediate result sets. MyLite should
allow that common SQL surface without weakening the durable read-only
guarantee.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- MyLite's public read-only open starts the embedded runtime with
  `--mylite-read-only=1` and verifies durable `INSERT` and `CREATE TABLE`
  rejections in `vendor/mariadb/server/mylite/open_close_smoke.cc`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:ha_mylite::create()`
  checks `mylite_catalog_read_only()` before testing whether
  `HA_CREATE_INFO` describes a temporary table.
- `write_row()`, `update_row()`, `delete_row()`, `delete_all_rows()`,
  `truncate()`, and `reset_auto_increment()` also check read-only before
  routing through the temporary definition lookup.
- `mylite_prepare_dml_mutation_locked()` rejects every mutation when the
  catalog is read-only. It is called after row/update/delete helpers have
  already found the target `Mylite_table_definition`, so it can distinguish
  durable and temporary mutations if passed that scope.
- Existing temporary-table storage marks temporary definitions with
  `Mylite_table_definition::temporary`, keeps them out of catalog
  serialization and discovery, and skips durable row/index payload publication.

## Scope

Allow MyLite temporary tables under public read-only opens:

- `CREATE TEMPORARY TABLE ... ENGINE=MYLITE`;
- `INSERT`, `UPDATE`, `DELETE`, indexed reads, autoincrement, and
  `TRUNCATE TABLE` on the temporary table;
- `DROP TEMPORARY TABLE`;
- unchanged rejection for durable MyLite DDL and DML under the same read-only
  handle;
- unchanged primary file bytes before and after the read-only session.

## Non-Goals

- Do not allow durable table DDL, DML, schema-object DDL, or durable
  autoincrement changes under read-only opens.
- Do not add temporary spill files.
- Do not add temporary foreign-key metadata.
- Do not change read-only locking or allow read-write opens while read-only
  handles are active.

## Design

Adjust read-only checks to account for the target storage lifetime:

1. `ha_mylite::create()` should allow read-only execution only when
   `mylite_create_info_is_temporary(create_info)` is true.
2. Row DML, delete-all, truncate, and reset-autoincrement handler methods
   should allow read-only execution only when `mylite_table_is_temporary(table)`
   is true.
3. Change `mylite_prepare_dml_mutation_locked()` to accept a
   `durable_mutation` boolean. It should keep the read-only rejection for
   durable mutations and skip it for temporary mutations.
4. Pass `!definition->temporary` from row, update, delete, delete-all, and
   autoincrement reservation helpers after the target definition has been
   resolved.
5. Track durable transaction dirtiness separately from general in-memory
   dirtiness. Temporary-only mutations still need snapshots and writer
   ownership, but commit must not flush the primary file.
6. Keep transaction and statement snapshot behavior for temporary mutations so
   rollback and savepoint behavior remains consistent with current temporary
   table semantics.

## Affected Subsystems

- MyLite handler read-only guards.
- MyLite DML mutation preparation.
- Public `libmylite` open/close read-only smoke.
- Single-file storage docs and roadmap.

## Single-File and Embedded Lifecycle

Temporary read-only DDL/DML must not write the primary `.mylite` file or create
unexpected sidecars. The existing read-only smoke already snapshots the primary
file before and after the read-only session; this slice extends that session
with temporary table SQL.

## Public API and File Format Impact

No public API or file-format change. The behavior changes only for SQL executed
through an existing read-only MyLite handle.

## Binary Size Impact

Expected code-size impact is minimal: a few read-only guard branches plus smoke
coverage.

## License and Dependency Impact

No new dependency or licensing change.

## Test Plan

- Extend `run-libmylite-open-close-smoke.sh` read-only mode to:
  - create a temporary MyLite table;
  - insert, update, delete, query through an index, use autoincrement, and
    truncate it;
  - drop the temporary table;
  - verify durable read-only `INSERT` and `CREATE TABLE` still fail;
  - verify the primary file snapshot is unchanged.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` on the smoke scripts
  - `git diff --check`

## Acceptance Criteria

- Temporary MyLite DDL and DML work through a public read-only handle.
- Durable MyLite DDL and DML still return read-only diagnostics.
- The primary `.mylite` file bytes remain unchanged after the read-only
  session.
- The sidecar scan remains clean.

## Implementation Result

Implemented in `vendor/mariadb/server/storage/mylite/ha_mylite.cc` and
covered by `vendor/mariadb/server/mylite/open_close_smoke.cc`.

- `CREATE TEMPORARY TABLE ... ENGINE=MYLITE` is allowed under
  `MYLITE_OPEN_READONLY`, while durable `CREATE TABLE` remains rejected.
- Temporary table `INSERT`, `UPDATE`, `DELETE`, indexed reads,
  autoincrement, `TRUNCATE TABLE`, and `DROP TEMPORARY TABLE` are allowed
  under the same read-only handle.
- The transaction context now separates any in-memory dirty state from durable
  dirty state so temporary-only statements can commit without attempting a
  primary-file flush.
- The read-only smoke report records
  `readonly_temporary_rows=2:alpha,1:delta,8:eight,7:seven`,
  `readonly_temporary_truncate_count=0`, and an unchanged
  `readonly_file` size, timestamp, and content-checksum pair.

Verification:

- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
- `git diff --check`

## Risks and Unresolved Questions

- Temporary table rollback behavior remains inherited from the current
  temporary-table snapshot machinery. This slice does not broaden transaction
  semantics.
- Temporary foreign-key metadata remains a separate compatibility decision.
