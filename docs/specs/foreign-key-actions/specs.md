# foreign-key-actions

## Problem Statement

MyLite now persists and enforces `RESTRICT` and `NO ACTION` foreign keys, but it
still rejects `CASCADE` and `SET NULL`. That leaves a common application
constraint surface missing: parent updates and deletes should be able to mutate
referencing child rows atomically inside the same `.mylite` catalog change.

This slice extends MyLite's existing FK catalog records and key-image checks to
support `ON DELETE CASCADE`, `ON UPDATE CASCADE`, `ON DELETE SET NULL`, and
`ON UPDATE SET NULL` for MyLite child tables.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/table.h:2062` through `table.h:2063` defines
  `FK_OPTION_CASCADE`, `FK_OPTION_SET_NULL`, and `FK_OPTION_SET_DEFAULT`.
- `vendor/mariadb/server/sql/table.h:2151` through `table.h:2154` defines
  `fk_modifies_child()`, which treats options at or above `CASCADE` as
  child-mutating actions.
- `vendor/mariadb/server/sql/sql_base.cc:5138` through
  `sql_base.cc:5166` asks the parent handler for referencing FKs and prelocks
  child tables with write locks when delete/update actions modify children.
- `vendor/mariadb/server/storage/innobase/handler/ha_innodb.cc:12623`
  through `ha_innodb.cc:12659` rejects `SET NULL` when the child FK columns are
  `NOT NULL`.
- `vendor/mariadb/server/storage/innobase/handler/ha_innodb.cc:12664`
  through `ha_innodb.cc:12698` maps parsed FK options to delete/update cascade
  and set-null flags, while leaving `SET DEFAULT` as a TODO.
- `vendor/mariadb/server/storage/innobase/include/dict0mem.h:295` through
  `dict0mem.h:301` documents InnoDB's maximum recursive cascade depth of 15.
- `vendor/mariadb/server/sql/field.h:939` defines `Field::store_field()` for
  type-aware field-to-field assignment.
- `vendor/mariadb/server/sql/field.h:1454` through `field.h:1463` exposes
  record-based NULL checks and NULL bit mutation.
- `vendor/mariadb/server/sql/field.h:1586` through `field.h:1598` exposes
  `Field::move_field_offset()`, which lets MyLite read parent field values from
  the old or new parent record buffer.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:3071` through
  `ha_mylite.cc:3073` currently rejects non-restrict actions during FK
  extraction.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:3337` through
  `ha_mylite.cc:3384` currently rejects any referenced parent update/delete
  when matching child rows exist.

## Scope

This slice will:

- accept `CASCADE` and `SET NULL` FK actions in MyLite FK DDL,
- continue rejecting `SET DEFAULT` explicitly,
- reject `SET NULL` when any child FK column is `NOT NULL`,
- keep `RESTRICT` and `NO ACTION` behavior unchanged,
- find the SQL-layer prelocked child `TABLE` objects from `THD::open_tables`,
- collect child row IDs matching the old parent key image,
- delete matching child rows for `CASCADE` deletes,
- rewrite child FK columns for `CASCADE` parent updates,
- set child FK columns to `NULL` for `SET NULL` parent deletes/updates,
- recursively apply child-table parent actions for cascaded mutations,
- cap recursive cascades at 15 levels,
- refresh durable key-image roots for every mutated child table,
- flush the entire parent and child mutation as one catalog generation,
- preserve transaction, statement rollback, savepoint, and recovery behavior.

## Non-Goals

- Do not implement `SET DEFAULT`; MariaDB/InnoDB still leaves it unimplemented.
- Do not implement cross-engine FKs.
- Do not implement deferred constraints.
- Do not run SQL triggers for cascaded actions; inherited MySQL/MariaDB engine
  semantics do not fire triggers for FK cascades.
- Do not create datadir sidecars or engine-specific FK files.

## Proposed Design

### DDL

`mylite_foreign_key_action_is_supported()` will accept `CASCADE` and
`SET NULL` in addition to `RESTRICT`, `NO ACTION`, and unspecified actions.
`SET DEFAULT` remains rejected with `HA_ERR_UNSUPPORTED`.

During FK extraction, if either action is `SET NULL`, MyLite verifies all child
FK fields are nullable by looking up the child fields in the open create/alter
`TABLE`. This matches InnoDB's `SET NULL` sanity check and prevents runtime
mutation into impossible row states.

### Parent Restriction Checks

`mylite_check_parent_foreign_keys_locked()` will continue to scan child
key-image roots before a parent row changes. It will reject matching child rows
only when the relevant action is `RESTRICT` or `NO ACTION`. For `CASCADE` and
`SET NULL`, it will allow the parent mutation to continue so the action runner
can rewrite child rows inside the same catalog snapshot.

### Action Runner

After the parent row has been changed in memory and the parent index roots have
been refreshed, MyLite will apply parent FK actions before flushing the catalog.
This ordering lets child FK existence checks see the new parent key when an
`ON UPDATE CASCADE` changes the referenced key.

For each FK where the current table is referenced:

- skip updates when the referenced key prefix did not change,
- build the old parent referenced key image,
- collect matching child row IDs from the child FK index root,
- find the prelocked child `TABLE` in `THD::open_tables`,
- for `CASCADE` delete, mark each matching child row deleted and recursively
  apply child parent delete actions,
- for `CASCADE` update, decode each child row, copy the new parent referenced
  field values into the child FK fields with `Field::store_field()`, enforce
  child uniqueness and child FK existence checks, encode the record, and
  recursively apply child parent update actions,
- for `SET NULL`, decode each child row, set child FK columns to NULL, enforce
  child uniqueness and remaining child FK checks, encode the record, and
  recursively apply child parent update actions.

If a child table is not present in `THD::open_tables` for a child-mutating FK,
the action fails with an internal storage error rather than silently degrading.
Application SQL should reach this path only after MariaDB has prelocked the
child table through the handler FK metadata hooks.

### Whole-Table Delete

For `DELETE FROM parent` paths that call `handler::delete_all_rows()`, MyLite
will treat the operation as repeated parent row deletes when
`reset_auto_increment` is false. `TRUNCATE` continues to reject referenced
parents because MariaDB/InnoDB do not cascade `TRUNCATE`.

### Recursion Guard

MyLite will use a local maximum cascade depth of 15, matching InnoDB's
documented `FK_MAX_CASCADE_DEL`. Exceeding it returns a storage error and
rolls back the statement/catalog mutation.

## Affected Subsystems

- FK DDL validation.
- Parent update/delete and whole-table delete paths.
- Child row encode/decode and index-root refresh.
- Transaction snapshot and rollback behavior.
- Storage smoke same-process and fresh-process coverage.
- Roadmap and single-file architecture docs.

## DDL Metadata Routing Impact

No new metadata record type is required. Existing MyLite FK records already
store update/delete actions, child/parent fields, key indexes, and key-image
prefix lengths. The table-definition image and FK records remain inside the
primary `.mylite` file.

## Single-File And Embedded-Lifecycle Implications

No companion files are introduced. Cascaded child mutations participate in the
same in-memory catalog mutation, transaction snapshot, flush generation,
rollback, recovery, and sidecar checks as direct MyLite row changes.

## Public API Or File-Format Impact

No public `libmylite` API change. No file-format version bump is required
because the existing FK metadata stores the action enums already.

## Binary-Size Impact

Expected growth is moderate: recursive action helpers, child-table lookup,
field-copy helpers, and smoke coverage. No dependency is added.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc` to cover:
  - `ON DELETE CASCADE`,
  - `ON UPDATE CASCADE`,
  - `ON DELETE SET NULL`,
  - `ON UPDATE SET NULL`,
  - `SET NULL` DDL rejection for `NOT NULL` child columns,
  - `SET DEFAULT` DDL rejection,
  - recursive cascade across at least three tables,
  - `DELETE FROM parent` whole-table cascade,
  - transaction rollback of cascaded changes,
  - fresh-process reopen of FK action metadata and behavior.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
  - `git diff --check`

## Acceptance Criteria

- MyLite accepts `ON DELETE/UPDATE CASCADE` and `ON DELETE/UPDATE SET NULL`
  for valid MyLite FK definitions.
- `SET NULL` on non-null child columns fails before the table definition is
  stored.
- `SET DEFAULT` remains an explicit rejection.
- Parent row updates and deletes apply child actions atomically.
- Cascaded mutations refresh child indexes and preserve uniqueness checks.
- Recursive cascades work up to the documented depth limit.
- Transaction rollback and savepoint rollback restore parent and child rows.
- Fresh-process reopen preserves FK action metadata and behavior.
- Existing restrict/no-action FK, indexes, constraints, geometry/spatial,
  fulltext, generated columns, transaction, recovery, lifecycle, and sidecar
  checks keep passing.

## Risks And Unresolved Questions

- Cascaded updates use the prelocked child `TABLE` layouts supplied by MariaDB.
  If a future SQL path reaches MyLite without those child tables open, MyLite
  should fail closed rather than mutate raw catalog bytes with no field layout.
- Stored generated columns depending on cascaded FK columns may need a later
  targeted compatibility pass. This slice focuses on normal stored fields,
  which is the common application FK surface.
- The first implementation scans child index roots and rewrites rows in memory.
  It is correct for the current MyLite storage profile but not yet optimized for
  large cascading fanout.

## Implementation Result

Implemented in `ha_mylite` and the storage smoke. MyLite now accepts
`CASCADE` and `SET NULL` FK actions, keeps `SET DEFAULT` rejected, validates
`SET NULL` child columns at DDL time, and applies parent update/delete actions
as recursive catalog mutations. The action runner uses a MyLite-owned registry
of open MyLite `TABLE` objects to find MariaDB-prelocked child layouts without
peeking into `THD` internals, rewrites child records through MariaDB `Field`
operations, refreshes touched child indexes, and relies on existing
statement/transaction snapshots for rollback.

Report evidence from `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`:

- `build/mariadb-minsize/mylite-storage-engine-report.txt`:
  - `status=0`
  - `foreign_key_cascade_update_rows=10:1,11:20`
  - `foreign_key_cascade_delete_rows=11:20`
  - `foreign_key_set_null_rows=12:NULL,13:NULL`
  - `foreign_key_recursive_cascade_count=0`
  - `foreign_key_delete_all_cascade_count=0`
  - `foreign_key_cascade_rollback_rows=1:1`
  - `unsupported_foreign_key_set_null_not_null=rejected`
  - `unsupported_foreign_key_set_default=rejected`
- `build/mariadb-minsize/mylite-catalog-write-report.txt`:
  - `status=0`
  - `persisted_foreign_key_action_rows=1:10,2:NULL`
- `build/mariadb-minsize/mylite-catalog-read-report.txt`:
  - `status=0`
  - `persisted_foreign_key_action_rows=1:10,2:NULL`
  - `persisted_foreign_key_action_after_delete_rows=1:NULL,2:NULL`
- `build/mariadb-minsize/mylite-catalog-recovery-read-report.txt`:
  - `status=0`
  - `persisted_foreign_key_action_rows=1:10,2:NULL`
  - `persisted_foreign_key_action_after_delete_rows=1:NULL,2:NULL`

Verification run:

- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
