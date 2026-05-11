# truncate-table-lifecycle

## Problem Statement

MyLite supports row DML, durable indexes, autoincrement state, copy ALTER, and
standalone supported index DDL. It does not yet support `TRUNCATE TABLE` for
MyLite tables. The inherited MariaDB path reaches the storage handler, but
MyLite does not override `delete_all_rows()` or `truncate()`, so the base
handler returns `HA_ERR_WRONG_COMMAND`.

This slice implements supported MyLite truncate semantics by clearing table
rows inside the primary `.mylite` file, rebuilding empty index roots, resetting
the table-local autoincrement counter, and proving the state survives a fresh
embedded-process reopen without durable sidecars.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Official MariaDB `TRUNCATE TABLE` documentation says the statement empties
  the table, requires the `DROP` privilege, causes an implicit commit, and
  resets `AUTO_INCREMENT` values:
  <https://mariadb.com/docs/server/reference/sql-statements/table-statements/truncate-table>.
- `vendor/mariadb/server/sql/sql_truncate.cc:185` opens and locks the table
  for the handler-based truncate path, then calls
  `table->file->ha_truncate()` at `sql_truncate.cc:251`.
- `vendor/mariadb/server/sql/sql_truncate.cc:299` determines whether the
  storage engine can use recreate-based truncate through `HTON_CAN_RECREATE`.
- `vendor/mariadb/server/sql/sql_truncate.cc:445` chooses between
  `dd_recreate_table()` for engines with `HTON_CAN_RECREATE` and
  `handler_truncate()` otherwise.
- `vendor/mariadb/server/sql/sql_truncate.cc:589` requires `DROP_ACL` before
  executing the truncate.
- `vendor/mariadb/server/sql/handler.h:1772` defines `HTON_CAN_RECREATE`.
- `vendor/mariadb/server/sql/handler.h:5404` has the base
  `delete_all_rows()` implementation returning `HA_ERR_WRONG_COMMAND`.
- `vendor/mariadb/server/sql/handler.h:5424` has the base `truncate()`
  implementation call `delete_all_rows()` and then `reset_auto_increment(0)`.
- `vendor/mariadb/server/sql/handler.cc:5603` marks the table read-write and
  routes `ha_truncate()` through the virtual `truncate()` implementation.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:603` sets MyLite
  handlerton flags without `HTON_CAN_RECREATE`, which means MariaDB will use
  the handler truncate path instead of table recreation.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1155` already implements
  `reset_auto_increment()` by setting MyLite's catalog autoincrement state to
  `1` when MariaDB passes `0`.

## Scope

This slice will:

- implement `TRUNCATE TABLE` for supported durable MyLite tables,
- clear live row records and durable row payload references through the
  existing catalog publication path,
- clear durable index roots so forced index reads see an empty table after
  truncate,
- rely on inherited `handler::truncate()` to call MyLite's
  `reset_auto_increment(0)` and reset the next generated value to `1`,
- verify same-process row count, secondary-index reuse, and autoincrement
  restart after truncate,
- verify persistence across a fresh embedded-process reopen,
- keep sidecar scans proving no durable `.frm` or engine sidecars are
  introduced.

## Non-Goals

- Do not set `HTON_CAN_RECREATE` or route MyLite truncate through
  `dd_recreate_table()`.
- Do not implement a separate MyLite SQL-layer truncate command.
- Do not make truncate crash-safe across a process exit in the middle of
  catalog publication.
- Do not add a DDL journal, WAL, MVCC, XA, or page-level undo/redo.
- Do not implement foreign-key-aware truncate, because MyLite foreign keys are
  still explicitly unsupported.
- Do not add file-format records or bump the current file format.

## Proposed Design

Keep MyLite on MariaDB's handler-based truncate path. Because the MyLite
handlerton does not advertise `HTON_CAN_RECREATE`, `Sql_cmd_truncate_table`
opens the table with an exclusive lock and calls `handler::ha_truncate()`.
Override `ha_mylite::truncate()` directly so row clearing and autoincrement
reset publish as one MyLite catalog mutation. Also implement
`ha_mylite::delete_all_rows()` for bulk-delete paths that must clear rows
without resetting the user-visible autoincrement counter.

Use a MyLite-owned helper that mirrors the existing row mutation shape:

1. reject read-only catalogs with `HA_ERR_TABLE_READONLY`;
2. validate the table shape with MyLite's supported key-storage predicate;
3. load and find the catalog definition under `mylite_catalog_mutex`;
4. call `mylite_prepare_dml_mutation_locked(thd)` so the current transaction
   bridge captures statement and transaction snapshots;
5. snapshot catalog and pending allocator state for rollback on local errors;
6. collect old index payload ranges for free-page reuse;
7. clear the table rows vector and reset the internal `next_rowid` to `1`;
8. when called from `truncate()`, reset `auto_increment_next` to `1` for
   autoincrement tables;
9. rebuild index roots, yielding no durable roots for an empty table;
10. publish through `mylite_flush_catalog_for_thd_locked(thd)`.

`delete_all_rows()` uses the same helper with autoincrement reset disabled, so
`DELETE FROM table`-style bulk deletion does not gain truncate semantics.

If testing shows MariaDB's implicit-commit behavior does not call the current
transaction hooks in the expected order, fix the root transaction/publication
integration rather than special-casing the smoke test.

## Affected Subsystems

- MyLite storage handler public methods and row mutation helpers.
- MyLite catalog row and index publication.
- Storage engine smoke same-process and persistence coverage.
- Roadmap and single-file storage architecture docs.

## DDL Metadata Routing Impact

`TRUNCATE TABLE` must not rewrite or replace the table definition. The existing
frm-backed definition image remains valid and must still be discoverable after
truncate. Only row payload roots, index payload roots, hidden row ids, and
autoincrement state change.

Because MyLite avoids the recreate path, this slice should not create
replacement table names, backup table names, or temporary durable `.frm` files.

## Single-File And Embedded-Lifecycle Implications

All truncate effects remain in the primary `.mylite` file. Successful truncate
must publish an accepted catalog generation where the table has no live rows
and no non-empty index payload roots. A fresh embedded-process reopen must
discover the same table definition, observe the post-truncate rows, and use the
reset autoincrement counter for new inserts.

Crash recovery during truncate is limited to the current catalog generation
publication guarantee. Full transactional DDL recovery remains deferred.

## Public API Or File-Format Impact

No public `libmylite` API change and no file-format version bump are expected.
The implementation uses existing row, index, catalog, and allocator payload
records.

## Binary-Size Impact

Expected library size impact is small: one handler override and a bounded
helper in the MyLite storage engine. Smoke-test code will grow. Post-
implementation `MinSizeRel` artifact sizes will be recorded.

## License, Trademark, And Dependency Impact

No new dependency, license change, or trademark change.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc` same-process
  coverage:
  - create a supported MyLite table with an autoincrement primary key and
    secondary key,
  - insert rows,
  - run `TRUNCATE TABLE`,
  - verify `COUNT(*)` is `0`,
  - insert new rows without explicit ids,
  - verify generated ids restart at `1`,
  - verify forced secondary-index lookup and ordering work after truncate.
  - run `TRUNCATE TABLE` inside explicit transaction syntax and verify a
    following `ROLLBACK` does not undo truncate's implicit-commit effect.
- Extend persistence write/read coverage:
  - create and truncate `mylite.persisted_truncate`,
  - insert post-truncate rows,
  - verify count, generated ids, and forced secondary-index reads before close,
  - after fresh-process reopen, verify the same post-truncate rows and index
    reads.
- Run:
  - `git diff --check`
  - `bash -n tools/run-storage-engine-smoke.sh
    tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`

## Acceptance Criteria

- `TRUNCATE TABLE` succeeds for supported durable MyLite tables.
- Live rows and durable index roots are cleared after truncate.
- Autoincrement restarts from `1` after truncate.
- Inserts and forced index reads work after truncate in the same embedded
  process.
- Fresh-process reopen observes the post-truncate state.
- No durable `.frm`, external engine, log, or plugin sidecars are introduced.
- Existing DDL, DML, copy ALTER, standalone index DDL, transaction, recovery,
  public API, and compatibility coverage keeps passing.

## Risks And Unresolved Questions

- The implementation relies on MariaDB continuing to route MyLite through
  handler-based truncate while `HTON_CAN_RECREATE` remains unset.
- The current catalog generation publication model gives all-or-previous
  generation behavior, not a full DDL recovery protocol.
- Clearing rows currently rewrites whole row and index payload chains. A later
  B-tree or page-level storage design should make truncate cheaper.

## Implementation Result

Implemented in `vendor/mariadb/server/storage/mylite/ha_mylite.cc`,
`vendor/mariadb/server/storage/mylite/ha_mylite.h`, and
`vendor/mariadb/server/mylite/storage_engine_smoke.cc`.

The implementation overrides `ha_mylite::truncate()` and
`ha_mylite::delete_all_rows()`. Both route through a shared MyLite helper that
clears rows, resets the internal row id, rebuilds empty index roots, and
publishes through the existing transaction-aware catalog path. `truncate()`
also resets table-local autoincrement state to `1`; `delete_all_rows()` leaves
autoincrement state unchanged for bulk-delete semantics.

Verification on 2026-05-11:

- `git diff --check`
- `bash -n tools/run-storage-engine-smoke.sh
  tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`

Observed report fields:

- `build/mariadb-minsize/mylite-storage-engine-report.txt`:
  `status=0`, `truncate_count=0`, `truncate_autoincrement_ids=1,2`,
  `truncate_key_lookup_id=2`, `truncate_key_order_ids=1,2`,
  `truncate_transaction_rows=1:after`.
- `build/mariadb-minsize/mylite-catalog-write-report.txt`:
  `status=0`, `persisted_truncate_count=2`,
  `persisted_truncate_autoincrement_ids=1,2`,
  `persisted_truncate_key_lookup_id=2`,
  `persisted_truncate_key_order_ids=1,2`.
- `build/mariadb-minsize/mylite-catalog-read-report.txt`:
  `status=0`, `persisted_truncate_count=2`,
  `persisted_truncate_autoincrement_ids=1,2`,
  `persisted_truncate_key_lookup_id=2`,
  `persisted_truncate_key_order_ids=1,2`,
  `row_payloads` includes `mylite.persisted_truncate`, and
  `index_payloads` includes `mylite.persisted_truncate:0` and
  `mylite.persisted_truncate:1`.
- `build/mariadb-minsize/mylite-compatibility-harness-report.txt`: all groups
  reported `status=0`, including `sidecar_scan` with
  `unexpected_sidecars=none`.

Post-implementation `MinSizeRel` artifact observations:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,414,670 bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 87,206 bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,839,088
  bytes.
- `build/mariadb-minsize/mylite/mylite-compatibility-smoke`: 22,773,336 bytes.
