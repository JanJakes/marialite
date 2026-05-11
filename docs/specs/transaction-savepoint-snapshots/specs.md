# Transaction Savepoint Snapshots Slice

## Problem Statement

The `deferred-transaction-publication` slice made supported MyLite row DML a
MariaDB transaction participant, but deliberately left savepoints unsupported.
Once MyLite participates in a normal transaction, MariaDB calls storage-engine
savepoint hooks for `SAVEPOINT`, `ROLLBACK TO SAVEPOINT`, and
`RELEASE SAVEPOINT`. Without those hooks, a transaction that has touched MyLite
returns a generic "SAVEPOINT not implemented" error.

The current MyLite transaction context already captures in-memory catalog and
allocator snapshots for statement and full-transaction rollback. This slice
should extend that mechanism to named savepoints for the supported row-DML
subset, without adding page-level undo, WAL, or a new file format.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Official MariaDB documentation:
  - <https://mariadb.com/kb/en/savepoint/> documents `SAVEPOINT`,
    `ROLLBACK TO SAVEPOINT`, and `RELEASE SAVEPOINT`, including that rollback
    to a savepoint undoes changes after the savepoint and erases later
    savepoints.
  - <https://mariadb.com/docs/server/reference/sql-statements/transactions/rollback>
    documents rollback to a specific savepoint as part of the `ROLLBACK`
    statement.
- `vendor/mariadb/server/sql/handler.h` documents `handlerton::savepoint_offset`
  and the savepoint hook storage area. Each engine can request a fixed number
  of bytes in the MariaDB `SAVEPOINT` object.
- `vendor/mariadb/server/sql/handler.cc`:
  - `ha_savepoint()` calls every registered participant's `savepoint_set()` and
    stores the transaction participant list in the savepoint,
  - `ha_rollback_to_savepoint()` calls `savepoint_rollback()` for engines that
    were already participating when the savepoint was set, then rolls back
    engines added later,
  - `ha_release_savepoint()` calls `savepoint_release()` when the engine
    provides it,
  - `ha_rollback_to_savepoint_can_release_mdl()` requires every participant to
    opt in before metadata locks can be released after partial rollback.
- `vendor/mariadb/server/sql/transaction.cc`:
  - `savepoint_add()` reuses an existing named savepoint after calling the
    release callback,
  - `trans_rollback_to_savepoint()` keeps the target savepoint and removes
    later savepoints from MariaDB's savepoint list,
  - `trans_release_savepoint()` removes the named savepoint from MariaDB's
    list.
- `vendor/mariadb/server/storage/federatedx/ha_federatedx.cc` shows the normal
  initialization shape: set `savepoint_offset`, `savepoint_set`,
  `savepoint_rollback`, and `savepoint_release` on the handlerton.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc` now has a
  THD-owned `Mylite_transaction_context` with statement and full-transaction
  snapshots, plus commit and rollback callbacks.

## Scope

This slice will:

- install MyLite savepoint hooks in the handlerton,
- reserve a small per-savepoint storage area for a MyLite savepoint ID,
- keep actual savepoint snapshots inside `Mylite_transaction_context`,
- capture `mylite_catalog` and `mylite_pending_free_page_ranges` when MyLite
  receives `savepoint_set()`,
- capture savepoints even when MyLite's current transaction participation is
  still clean, so a savepoint after a MyLite read can roll back later MyLite
  DML,
- restore the matching snapshot when MyLite receives `savepoint_rollback()`,
- discard newer savepoint snapshots after rollback to a savepoint,
- discard the named and newer savepoint snapshots when MyLite receives
  `savepoint_release()`, matching MariaDB's savepoint-list truncation behavior,
- add storage smoke coverage for rollback to savepoint, release savepoint, and
  fresh-process persistence after commit.

## Non-Goals

- Do not add page-level undo, redo, WAL, MVCC, shadow paging, or journal
  companions.
- Do not implement XA or two-phase savepoint recovery.
- Do not make DDL transactional.
- Do not change public `libmylite` API or file format.
- Do not broaden concurrency beyond the dirty-writer guard introduced by
  deferred transaction publication.

## Proposed Design

Add a `Mylite_savepoint_snapshot` record to the THD transaction context:

- monotonic `uint64_t id`,
- a `Mylite_transaction_snapshot` containing catalog and pending free ranges.

Set `mylite_hton->savepoint_offset = sizeof(uint64_t)` and install
`mylite_savepoint_set`, `mylite_savepoint_rollback`, and
`mylite_savepoint_release`.

The savepoint area supplied by MariaDB stores only the monotonic ID with
`memcpy`, avoiding C++ object lifetime inside MariaDB's raw savepoint memory.
The snapshot vectors stay owned by `Mylite_transaction_context` and are freed
when the context is cleared at commit, full rollback, or connection cleanup.

### Savepoint Set

`mylite_savepoint_set(thd, sv)` should:

1. get the current MyLite transaction context,
2. create a clean context if MyLite is a read-only participant so far,
3. increment the context's next savepoint ID,
4. capture the current catalog and pending free-range snapshot,
5. append the snapshot to the context,
6. store the ID in `sv`.

Capturing clean MyLite participation is required because MariaDB records MyLite
in the savepoint participant list after reads too. If a later write occurs and
the savepoint rollback path receives an ID `0` no-op, MariaDB will not roll the
engine back through the "engines added after the savepoint" path because MyLite
was already present in `sv->ha_list`.

If snapshot allocation fails, return `HA_ERR_OUT_OF_MEM` so MariaDB can fail
the `SAVEPOINT` statement.

### Rollback To Savepoint

`mylite_savepoint_rollback(thd, sv)` should:

1. read the ID from `sv`,
2. treat ID `0` as a no-op,
3. find the matching snapshot in the context,
4. restore catalog and pending free ranges from that snapshot,
5. discard snapshots newer than the restored savepoint while keeping the
   target savepoint active.

The target savepoint remains active because MariaDB keeps it in
`trans_rollback_to_savepoint()`.

### Release Savepoint

`mylite_savepoint_release(thd, sv)` should:

1. read the ID from `sv`,
2. treat ID `0` as a no-op,
3. discard the matching snapshot and all newer snapshots.

This follows MariaDB's list truncation in `trans_release_savepoint()` and also
handles the duplicate-name path in `savepoint_add()`, where MariaDB calls the
release callback before replacing the old savepoint.

## Affected Subsystems

- MyLite handlerton initialization in `ha_mylite.cc`.
- MyLite transaction context and snapshot helpers in `ha_mylite.cc`.
- Storage smoke transaction phase in `storage_engine_smoke.cc`.
- Compatibility harness report coverage if new savepoint report fields are
  added.
- Roadmap and single-file storage docs.

## DDL Metadata Routing Impact

DDL remains outside the savepoint guarantee. MariaDB implicit-commit behavior
around DDL stays authoritative, and MyLite DDL catalog writes continue to
publish immediately.

## Single-File And Embedded-Lifecycle Implications

No new files are introduced. Savepoint snapshots live in process memory only
and are discarded on commit, full rollback, connection cleanup, or process
exit. Crash recovery still uses the last published `.mylite` generation.

## Public API And File-Format Impact

No public API or file-format change. The behavior change is SQL-facing:
`SAVEPOINT`, `ROLLBACK TO SAVEPOINT`, and `RELEASE SAVEPOINT` should work for
the supported MyLite row-DML subset once MyLite is already participating in a
transaction.

## Binary-Size Impact

The implementation adds one savepoint snapshot vector, three handlerton
callbacks, and smoke coverage. It adds no dependency or new compiled MariaDB
subsystem. Measured after implementation with `MYLITE_BUILD_JOBS=8` and the
Docker-based `mariadb-minsize` profile:

| Artifact | Size |
| --- | ---: |
| `build/mariadb-minsize/libmysqld/libmariadbd.a` | 44,408,362 bytes |
| `build/mariadb-minsize/mylite/libmylite.a` | 29,698 bytes |
| `build/mariadb-minsize/mylite/mylite-storage-engine-smoke` | 22,772,056 bytes |
| `build/mariadb-minsize/mylite/mylite-compatibility-smoke` | 22,772,400 bytes |
| `build/mariadb-minsize/mylite/mylite-open-close-smoke` | 22,773,128 bytes |
| `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke` | 22,771,440 bytes |

## License, Trademark, And Dependency Impact

No new dependencies, license changes, or trademark changes.

## Implementation Result

Implemented in `vendor/mariadb/server/storage/mylite/ha_mylite.cc`,
`vendor/mariadb/server/mylite/storage_engine_smoke.cc`, and architecture and
roadmap docs.

- MyLite installs `savepoint_offset`, `savepoint_set`,
  `savepoint_rollback`, and `savepoint_release` handlerton fields.
- MariaDB savepoint storage contains only a MyLite `uint64_t` ID written with
  `memcpy`; snapshot vectors stay in `Mylite_transaction_context`.
- Savepoint set creates a clean transaction context when MyLite has only read
  participation so far, then captures catalog and allocator state.
- Statement-end commit inside an explicit transaction preserves a clean context
  that still owns savepoint snapshots, and normal commit or rollback clears it.
- Savepoint rollback restores the matching snapshot, keeps the target
  savepoint active, discards newer snapshots, and keeps the transaction dirty
  so commit/rollback still resolves through MariaDB.
- Savepoint release discards the named snapshot and any newer MyLite snapshots.
- Metadata-lock release remains conservative because MyLite does not set
  `savepoint_rollback_can_release_mdl()`.

Observed storage-smoke transaction report:

- engine metadata: `transactions=YES`,
- full rollback state: `transaction_rollback_rows=1:one,2:two`,
- clean read-before-write savepoint rollback:
  `transaction_clean_savepoint_rows=1:one,2:two`,
- rollback to earlier savepoint:
  `transaction_savepoint_rows=1:uno,2:two`,
- release savepoint state:
  `transaction_release_rows=1:uno,2:dos,4:four`,
- commit state: `transaction_rows=1:uno,2:dos,4:four`,
- rollback warnings: `transaction_rollback_warnings=none`,
- fresh-process reopen state: `transaction_rows=1:uno,2:dos,4:four`.

## Test And Verification Plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
bash -n tools/run-compatibility-test-harness.sh tools/run-storage-engine-smoke.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

Storage smoke should verify:

- MyLite transaction metadata remains `transactions=YES`,
- a savepoint set after a MyLite read but before the first MyLite write can
  roll back later DML,
- DML after `SAVEPOINT sp1` can be undone by `ROLLBACK TO SAVEPOINT sp1`,
- DML after a later savepoint is discarded when rolling back to an earlier
  savepoint,
- `RELEASE SAVEPOINT` removes a savepoint without rolling back changes,
- final `COMMIT` persists the expected post-savepoint state across fresh
  process reopen,
- no warning `1196`, `.frm` sidecars, journal/WAL companions, or catalog
  sidecars are introduced.

## Acceptance Criteria

- MyLite installs savepoint hooks and a savepoint storage offset.
- `SAVEPOINT` succeeds after a MyLite row-DML transaction has started.
- `SAVEPOINT` after a MyLite read but before the first MyLite write captures a
  rollback point for later DML.
- `ROLLBACK TO SAVEPOINT` restores the expected in-memory row state.
- `RELEASE SAVEPOINT` does not roll back changes but removes the savepoint.
- Later savepoint snapshots are discarded after rollback/release truncation.
- Full `ROLLBACK` and `COMMIT` still behave as implemented by
  `deferred-transaction-publication`.
- Existing storage, compatibility, embedded lifecycle, and `libmylite`
  lifecycle smokes pass.

## Risks And Unresolved Questions

- This remains whole-catalog snapshotting and has the same memory-scaling
  limits as deferred transaction publication.
- MariaDB's savepoint list truncation behavior is subtle; tests must cover
  both rollback to an earlier savepoint and release.
- Metadata-lock release remains conservative because MyLite should not opt into
  `savepoint_rollback_can_release_mdl()` until DDL and lock interactions have
  their own tests.
