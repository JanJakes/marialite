# Deferred Transaction Publication Slice

## Problem Statement

MyLite currently persists each supported DDL or DML operation by mutating the
process-global catalog state and publishing a new `.mylite` catalog generation
immediately. The engine advertises `HA_NO_TRANSACTIONS` and `HTON_NO_ROLLBACK`,
and the existing transaction-boundary smoke intentionally proves that MyLite
row changes survive `ROLLBACK`.

The next implementation step should replace that documented non-transactional
boundary with the first real MariaDB transaction participant behavior for the
currently supported DML subset. Because current row and index storage is still
loaded into memory as table-local vectors, the lowest-risk first design is to
defer durable generation publication until MariaDB asks the engine to commit,
and to restore captured in-memory snapshots when MariaDB asks the engine to
roll back.

This is not the final WAL or pager design. It is a bounded transition that
gives correct single-writer rollback and crash-before-commit behavior for the
existing storage bridge while preserving a later path to page-level undo/redo.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Official MariaDB documentation:
  - <https://mariadb.com/kb/en/start-transaction/> documents that
    `START TRANSACTION`, `COMMIT`, and `ROLLBACK` are SQL transaction
    boundaries; it also documents implicit commits for DDL and notes that
    transactions still acquire metadata locks for non-transactional engines.
  - <https://mariadb.com/kb/en/e1196/> identifies warning `1196`
    (`ER_WARNING_NOT_COMPLETE_ROLLBACK`) as the warning for non-transactional
    changed tables that could not be rolled back.
  - <https://mariadb.com/kb/en/mariadb-transactions-and-isolation-levels-for-sql-server-users/>
    states that MariaDB transactions are optionally implemented by storage
    engines.
- `vendor/mariadb/server/sql/handler.h` defines:
  - `HA_NO_TRANSACTIONS` as the table flag for engines without transaction
    support,
  - `HTON_NO_ROLLBACK` as the handlerton flag for engines that cannot roll
    back transactions,
  - `handlerton::commit`, `handlerton::rollback`, `prepare`, savepoint hooks,
    `prepare_ordered`, and `commit_ordered`,
  - `trans_register_ha()` as the storage-engine registration entry point.
- `vendor/mariadb/server/sql/handler.cc` documents that engines must register
  statement and transaction participation with `trans_register_ha()`, normally
  from `handler::external_lock()`. `commit_one_phase_2()` calls registered
  engines' `commit(thd, all)`, and `ha_rollback_trans()` calls registered
  engines' `rollback(thd, all)`.
- `vendor/mariadb/server/sql/handler.cc` marks registered read-write
  participants as no-rollback when the handlerton has `HTON_NO_ROLLBACK`.
- `vendor/mariadb/server/sql/transaction.cc` routes `COMMIT`, `ROLLBACK`,
  statement commit, statement rollback, savepoint creation, and rollback to the
  handler transaction functions.
- `vendor/mariadb/server/storage/innobase/handler/ha_innodb.cc`,
  `vendor/mariadb/server/storage/rocksdb/ha_rocksdb.cc`,
  `vendor/mariadb/server/storage/maria/ha_maria.cc`, and
  `vendor/mariadb/server/storage/federatedx/ha_federatedx.cc` show the common
  pattern: register statement participation, and register normal transaction
  participation when `OPTION_NOT_AUTOCOMMIT` or `OPTION_BEGIN` is active.
- `vendor/mariadb/server/storage/mylite/ha_mylite.h` currently returns
  `HA_NO_TRANSACTIONS` from `ha_mylite::table_flags()`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc` currently sets
  `HTON_NO_ROLLBACK`, leaves `external_lock()` as a no-op, and flushes through
  `mylite_flush_catalog_locked()` from row insert, update, delete,
  autoincrement, DDL catalog, row page, index page, and allocator mutation
  paths.

## Scope

This slice will:

- make MyLite a MariaDB transaction participant for supported row DML,
- remove `HA_NO_TRANSACTIONS` and `HTON_NO_ROLLBACK` only for the covered DML
  subset,
- register MyLite with MariaDB's statement and normal transaction lists from
  `ha_mylite::external_lock()`,
- add `handlerton::commit` and `handlerton::rollback` hooks,
- capture in-memory catalog and allocator snapshots before the first DML
  mutation in a statement and before the first DML mutation in a normal
  transaction,
- defer durable `.mylite` generation publication until statement commit in
  autocommit mode or normal transaction commit in explicit transaction mode,
- restore the statement snapshot on statement rollback,
- restore the transaction snapshot on normal transaction rollback,
- keep at most one dirty MyLite writer per process and fail other sessions'
  MyLite access while an uncommitted writer owns the global in-memory catalog,
- update storage smoke coverage so supported DML inside `START TRANSACTION`
  is undone by `ROLLBACK` and persisted only by `COMMIT`,
- verify fresh-process reopen after rollback and commit,
- update docs that currently describe MyLite as non-transactional.

## Non-Goals

- Do not implement page-level undo, redo, WAL, shadow paging, MVCC, or
  companion journal files.
- Do not implement savepoints in this slice. Once MyLite participates in a
  transaction, MariaDB will reject `SAVEPOINT` if MyLite is in the transaction
  and the engine has no savepoint hook; that explicit unsupported behavior is
  acceptable until a savepoint slice exists.
- Do not implement XA or two-phase commit. MyLite will not set `prepare`,
  `prepare_ordered`, `commit_ordered`, or recovery hooks.
- Do not make DDL transactional. MariaDB already gives many DDL statements
  implicit-commit behavior, and MyLite DDL/catalog routing remains a separate
  durable generation operation.
- Do not promise cross-process readers or writers beyond the existing
  exclusive primary-file advisory lock.
- Do not solve cross-session snapshot isolation. The first implementation
  serializes dirty MyLite access so other sessions cannot observe uncommitted
  in-memory state.
- Do not change the public `libmylite` C API.

## Proposed Design

### Transaction Registration

`mylite_init_func()` should install `mylite_commit()` and
`mylite_rollback()` in the handlerton. MyLite should stop advertising
`HA_NO_TRANSACTIONS` and `HTON_NO_ROLLBACK` after rollback tests exist.

`ha_mylite::external_lock(thd, lock_type)` should register MyLite on lock
acquisition:

- always call `trans_register_ha(thd, false, mylite_hton, 0)` for a statement,
- additionally call `trans_register_ha(thd, true, mylite_hton, 0)` when
  `thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN)` is true,
- do not register on `F_UNLCK`.

The existing handler wrappers call `mark_trx_read_write()` around write
methods, so MariaDB can mark MyLite participation as read-write after
registration.

### Snapshot Ownership

Add a small MyLite transaction context stored in THD handlerton data. It should
record:

- whether a statement snapshot is active,
- whether a normal transaction snapshot is active,
- whether the context has dirty MyLite changes,
- the captured `mylite_catalog` vector for statement rollback,
- the captured `mylite_catalog` vector for normal transaction rollback,
- the captured `mylite_pending_free_page_ranges` vector for each snapshot.

The context is first-party MyLite state. It must be freed and cleared from THD
handlerton data when the normal transaction finishes or when an autocommit
statement finishes.

### Dirty Writer Serialization

The current bridge keeps catalog, row, index, and allocator state in
process-global memory. Until that state is per-transaction, one dirty writer
must own the in-memory catalog. Add process-global dirty-writer tracking under
`mylite_catalog_mutex`:

- the first context that mutates rows while no dirty writer exists becomes the
  dirty writer,
- the same THD can continue reading and writing,
- another THD attempting MyLite access while the dirty writer has uncommitted
  changes receives `HA_ERR_LOCK_WAIT_TIMEOUT`,
- commit or rollback clears dirty-writer ownership.

This is conservative and intentionally narrower than the final concurrency
target. It prevents a second session from seeing uncommitted rows that live in
the shared in-memory catalog.

### DML Mutation Flow

Before `write_row`, `update_row`, `delete_row`, or autoincrement reservation
mutates `mylite_catalog`, the handler should call a MyLite helper with the
current `THD`:

1. ensure the catalog is loaded,
2. ensure the current THD can access the dirty writer state,
3. create or fetch the THD transaction context,
4. capture a statement snapshot if none is active,
5. capture a normal transaction snapshot if the THD is inside an explicit
   transaction and no normal snapshot exists,
6. mark the context dirty and mark the current THD as the dirty writer.

After a DML mutation succeeds, the helper that currently calls
`mylite_flush_catalog_locked()` should instead call a transaction-aware publish
helper:

- if the current THD has a dirty context, return success and leave publication
  deferred,
- otherwise keep the existing immediate publication behavior for DDL and any
  non-transactional internal path.

If a mutation fails after modifying in-memory state, the existing local
`before` rollback remains useful. It should restore the local pre-call state
before returning an error. MariaDB statement rollback remains the broader
fallback for multi-row statement failures.

### Commit And Rollback

`mylite_commit(thd, all)`:

- for `all == false` inside an explicit transaction, clear only the statement
  snapshot; do not publish;
- for `all == false` outside an explicit transaction, publish the deferred
  generation and clear the transaction context;
- for `all == true`, publish the deferred generation and clear the transaction
  context;
- on publication failure, leave the context dirty so MariaDB can surface commit
  failure and the process still has enough in-memory state to roll back if
  MariaDB asks it to.

`mylite_rollback(thd, all)`:

- for `all == false`, restore the statement snapshot and clear statement
  snapshot state;
- for `all == true`, restore the normal transaction snapshot if present,
  otherwise restore the statement snapshot if that is the only snapshot;
- clear dirty-writer ownership when no dirty changes remain for the context;
- do not publish a new generation while rolling back uncommitted changes, since
  the last published header already represents the durable state.

### Crash Behavior

Uncommitted DML remains only in memory. If the process crashes before commit,
the previous accepted `.mylite` generation remains the recovery point. If the
process crashes during commit, the existing payload-write, fsync, header-write,
fsync publication protocol decides whether the previous or new generation is
accepted on reopen.

This gives atomic commit for the current whole-catalog publication bridge. It
does not yet give page-level undo/redo for future out-of-memory or streaming
storage paths.

## Affected Subsystems

- MyLite handlerton initialization in `ha_mylite.cc`.
- MyLite table flags in `ha_mylite.h`.
- MyLite `external_lock()`, DML methods, autoincrement reservation, and catalog
  flush helpers in `ha_mylite.cc`.
- Storage smoke C++ and shell assertions.
- Compatibility harness expected transaction-boundary behavior.
- Roadmap and single-file storage architecture docs.

## DDL Metadata Routing Impact

DDL remains outside this slice's transactional guarantee. MariaDB's generic DDL
implicit-commit behavior stays authoritative. MyLite DDL paths should keep
publishing catalog generations immediately through the existing metadata
routing path, and tests should continue checking that no durable `.frm`
sidecars are created.

If a later slice wants transactional DDL for MyLite-specific metadata, it needs
its own source-grounded design because MariaDB SQL-layer DDL boundaries are not
the same as row DML transaction boundaries.

## Single-File And Embedded-Lifecycle Implications

No new files are introduced. The primary `.mylite` file remains the only
durable MyLite asset for this slice. Uncommitted state lives only in process
memory and is discarded on process exit or crash.

The existing exclusive advisory lock still owns the primary file for the
process lifetime of the configured storage-engine catalog. This slice adds
only an in-process dirty-writer guard, not a lock sidecar or cross-process
reader/writer protocol.

## Public API And File-Format Impact

The public C API is unchanged. The on-disk file format is unchanged: commits
continue to publish the existing v3 catalog header with catalog, allocator,
row, and index page roots.

The behavior contract changes for SQL DML on supported MyLite tables:
`ROLLBACK` should undo supported row changes after this slice, and the old
warning `1196` non-transactional result should disappear from the storage smoke
for covered DML.

## Binary-Size Impact

The implementation adds one transaction context type, two handlerton callbacks,
registration code, and smoke coverage. It adds no dependency or new compiled
MariaDB subsystem. Measured after implementation with `MYLITE_BUILD_JOBS=8`
and the Docker-based `mariadb-minsize` profile:

| Artifact | Size |
| --- | ---: |
| `build/mariadb-minsize/libmysqld/libmariadbd.a` | 44,401,294 bytes |
| `build/mariadb-minsize/mylite/libmylite.a` | 29,698 bytes |
| `build/mariadb-minsize/mylite/mylite-storage-engine-smoke` | 22,770,984 bytes |
| `build/mariadb-minsize/mylite/mylite-compatibility-smoke` | 22,771,472 bytes |
| `build/mariadb-minsize/mylite/mylite-open-close-smoke` | 22,706,664 bytes |
| `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke` | 22,770,512 bytes |

## License, Trademark, And Dependency Impact

No new dependencies, license changes, or trademark changes. The implementation
stays inside existing GPL-2.0-only MariaDB-derived handler code.

## Implementation Result

Implemented in `vendor/mariadb/server/storage/mylite/ha_mylite.cc`,
`vendor/mariadb/server/storage/mylite/ha_mylite.h`,
`vendor/mariadb/server/mylite/storage_engine_smoke.cc`, and architecture and
roadmap docs.

- MyLite no longer advertises `HA_NO_TRANSACTIONS` or `HTON_NO_ROLLBACK` for
  the covered row-DML path.
- The handlerton now installs `commit` and `rollback` callbacks.
- `external_lock()` registers statement participation and registers normal
  transaction participation for write locks inside explicit transactions.
- DML mutation paths also register write participation defensively before
  taking a mutation snapshot.
- DML captures statement and normal-transaction snapshots of `mylite_catalog`
  and `mylite_pending_free_page_ranges`, defers publication while dirty
  transaction context exists, publishes on commit, and restores snapshots on
  rollback.
- A single dirty writer THD owns the process-global in-memory catalog until
  commit or rollback; another THD attempting MyLite access receives a lock
  timeout.
- DDL catalog operations keep immediate durable generation publication and are
  not made transactional by this slice.

Observed storage-smoke transaction report:

- engine metadata: `transactions=YES`,
- rollback state: `transaction_rollback_rows=1:one,2:two`,
- commit state: `transaction_rows=2:dos,3:three`,
- rollback warnings: `transaction_rollback_warnings=none`,
- fresh-process reopen state: `transaction_rows=2:dos,3:three`,
- catalog sidecars: none.

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

Storage smoke should add or update phases that verify:

- MyLite reports transaction support through MariaDB engine metadata,
- `START TRANSACTION; INSERT; UPDATE; DELETE; ROLLBACK;` restores the
  pre-transaction row state,
- the rolled-back state is what a fresh embedded process sees,
- `START TRANSACTION; INSERT; UPDATE; DELETE; COMMIT;` persists the committed
  state across fresh-process reopen,
- no warning `1196` is emitted for covered DML rollback,
- unsupported savepoint behavior is explicit if a MyLite transaction is active,
- no persistent `.frm`, engine sidecars, journal/WAL companions, dynamic plugin
  artifacts, or catalog temporary sidecars are introduced.

Compatibility harness should stop expecting MyLite DML to survive rollback and
should make the new rollback behavior part of the storage/recovery group.

## Acceptance Criteria

- MyLite registers with MariaDB transaction management for supported row DML.
- MyLite row DML no longer advertises `HA_NO_TRANSACTIONS` or
  `HTON_NO_ROLLBACK`.
- Supported DML inside an explicit transaction is undone by `ROLLBACK`.
- Supported DML inside an explicit transaction is persisted by `COMMIT`.
- Fresh-process reopen observes the post-rollback and post-commit durable
  states expected by the smoke.
- A process crash before commit can only recover the last published generation;
  no uncommitted generation is published before commit.
- Dirty in-memory state is not exposed to another THD; the first implementation
  may fail other sessions with a lock timeout.
- Savepoints, XA, page-level WAL, and transactional DDL remain explicitly
  unsupported or deferred.
- Existing storage, compatibility, embedded lifecycle, and `libmylite`
  lifecycle smokes pass.

## Risks And Unresolved Questions

- The design relies on current whole-catalog in-memory storage. It is not
  suitable once row/index storage becomes streaming or too large to snapshot
  cheaply.
- The dirty-writer guard serializes more than the final product should. It is a
  correctness boundary, not a desired concurrency model.
- DDL interactions with active MyLite transactions need careful smoke coverage
  because MariaDB performs implicit commits around many DDL statements.
- Savepoint SQL will become a more visible unsupported surface after MyLite
  starts participating in transactions. A later savepoint slice should add
  per-savepoint catalog snapshots or a page-level undo design.
- Commit failure recovery inside one process needs focused follow-up tests. The
  existing durable generation protocol protects fresh-process recovery, but a
  failed `fsync` or write during `COMMIT` may leave in-memory dirty state that
  must be handled conservatively.
