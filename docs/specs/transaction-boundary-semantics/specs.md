# Transaction Boundary Semantics Slice

## Problem Statement

MyLite now has durable catalog, row, index, and allocator page publication, but
it does not have SQL transaction rollback, savepoints, undo, redo, WAL, or
cross-session isolation. The storage engine currently advertises
`HA_NO_TRANSACTIONS`, so MariaDB should treat MyLite tables as
non-transactional tables.

That boundary needs to be explicit and tested. Until a later journal/WAL slice
adds real rollback semantics, users must not infer that `START TRANSACTION`,
`ROLLBACK`, `SAVEPOINT`, isolation levels, or XA provide transactional MyLite
row undo. The next implementation step should make the current behavior
intentional in code, compatibility tests, and docs.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Official MariaDB documentation:
  - <https://mariadb.com/kb/en/start-transaction/> states that DDL and
    transaction management statements cause implicit commits, and notes that
    transactions still acquire metadata locks for non-transactional engines.
  - <https://mariadb.com/docs/server/reference/sql-statements/transactions/rollback>
    documents `ROLLBACK` as the SQL operation that destroys transaction data
    changes when the participating storage engines can roll them back.
  - <https://mariadb.com/kb/en/mariadb-transactions-and-isolation-levels-for-sql-server-users/>
    documents that transactions are implemented by storage engines and that
    most MariaDB storage engines are not transactional.
- `vendor/mariadb/server/storage/mylite/ha_mylite.h` currently returns
  `HA_BINLOG_STMT_CAPABLE | HA_NO_TRANSACTIONS | HA_REC_NOT_IN_SEQ |
  HA_STATS_RECORDS_IS_EXACT` from `ha_mylite::table_flags()`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc` currently sets
  `mylite_hton->flags` to `HTON_NO_PARTITION | HTON_TEMPORARY_NOT_SUPPORTED`
  and implements `ha_mylite::external_lock()` as a no-op.
- `vendor/mariadb/server/sql/handler.h` defines:
  - `HA_NO_TRANSACTIONS` as the table flag for engines that do not support
    transactions,
  - `HTON_NO_ROLLBACK` as the handlerton flag for engines that cannot roll back
    transactions,
  - `handler::has_transactions()`, `handler::has_transaction_manager()`, and
    `handler::has_rollback()` in terms of those flags.
- `vendor/mariadb/server/sql/handler.cc` documents MariaDB statement and normal
  transaction lifecycle, including the important rule that SQL statements using
  non-transactional engines do not affect the connection transaction state.
  It also marks read-write transaction participants as `TRX_NO_ROLLBACK` when
  the handlerton has `HTON_NO_ROLLBACK`.
- `vendor/mariadb/server/sql/transaction.cc` routes `COMMIT`, `ROLLBACK`, and
  statement-end commit/rollback through `ha_commit_trans()` and
  `ha_rollback_trans()` only for registered transactional participants.

## Scope

This slice will:

- make MyLite's current non-transactional status explicit at both table and
  handlerton levels,
- add focused storage smoke coverage for MyLite DML inside explicit transaction
  syntax,
- verify that `ROLLBACK` does not pretend to undo MyLite rows before a
  rollback log exists,
- verify that MyLite rows modified inside an explicit transaction remain
  readable after rollback and fresh-process reopen,
- record the MariaDB warning or diagnostic behavior observed for rollback over
  non-transactional MyLite changes,
- document that MyLite's current durability boundary is statement/generation
  publication, not SQL transaction rollback,
- keep this as a compatibility boundary slice rather than a transaction engine.

## Non-Goals

- Do not implement undo records, redo records, WAL, rollback journal pages, MVCC,
  savepoint storage, XA, or two-phase commit.
- Do not claim `START TRANSACTION`, `ROLLBACK`, `SAVEPOINT`, or isolation level
  support for MyLite tables.
- Do not change the public `libmylite` C API.
- Do not introduce companion journal/WAL files.
- Do not add cross-session or cross-process concurrency claims.
- Do not change MariaDB parser or generic transaction SQL behavior.

## Proposed Design

### Engine Flags

Keep `HA_NO_TRANSACTIONS` in `ha_mylite::table_flags()`. Add
`HTON_NO_ROLLBACK` to `mylite_hton->flags` so the handlerton-level transaction
participant state matches the table-level claim if MariaDB ever registers the
engine in a transaction path.

Do not add `handlerton::commit`, `handlerton::rollback`, `prepare`, savepoint,
or XA hooks in this slice. Adding those hooks before a durable rollback design
would make the engine appear more transactional than it is.

### SQL Behavior Boundary

MyLite tables should behave as non-transactional MariaDB tables for now:

- `START TRANSACTION` and `ROLLBACK` are accepted by the SQL layer, but MyLite
  DML is not undone by `ROLLBACK`.
- `COMMIT` is effectively a boundary for SQL-layer transaction state, not a
  MyLite storage-engine commit protocol.
- DDL continues to use existing MariaDB implicit-commit behavior and MyLite's
  durable catalog publication.
- Savepoints and isolation-level options may affect SQL-layer state or other
  engines, but they do not create MyLite row undo.

The important product rule is that this behavior must be visible in docs and
tests, not silently assumed to be transactional.

### Test Shape

Extend the storage smoke with a transaction-boundary phase that:

1. creates a MyLite table and inserts a baseline row,
2. starts an explicit transaction,
3. inserts, updates, and deletes MyLite rows,
4. issues `ROLLBACK`,
5. verifies the MyLite table reflects non-transactional effects rather than
   transactional rollback,
6. records any warning from `SHOW WARNINGS` after `ROLLBACK`,
7. flushes and reopens the catalog in a fresh embedded process,
8. verifies the same post-rollback row state persists.

The test should be direct about the expected state. If MariaDB emits a warning
such as an incomplete rollback warning, the smoke should record and assert the
stable code/message shape observed on the selected base. If the selected base
does not warn for this exact path because the engine was never registered in a
transaction participant list, the smoke should record that as the current
source-grounded behavior and keep the row-state assertion as the release gate.

## Affected Subsystems

- MyLite handlerton flags in `ha_mylite.cc`.
- MyLite table flags in `ha_mylite.h` only if review finds they are incomplete.
- Storage engine smoke C++ and shell verification.
- Compatibility harness if the grouped transaction boundary should be exposed
  as a separate report entry.
- Architecture and roadmap docs.

No file-format fields, row/index/allocator pages, parser code, or public C API
are expected to change.

## DDL Metadata Routing Impact

DDL metadata routing is unchanged. The slice should document that MyLite DDL is
not transactional in the SQL rollback sense. Existing `CREATE`, copy `ALTER`,
`RENAME`, and `DROP` smokes continue to prove that durable `.frm` sidecars are
not introduced.

## Single-File And Embedded-Lifecycle Implications

No new files are introduced. The primary `.mylite` file remains the only
durable MyLite database asset. The storage smoke should verify the transaction
boundary scenario through a fresh embedded process so the observed
non-transactional row state is not only in-memory state.

## Public API And File-Format Impact

The public C API is unchanged. The file format is unchanged. Documentation must
state that transaction rollback is unsupported for MyLite tables until a later
journal/WAL slice adds the required durable state.

## Binary-Size Impact

Expected binary-size impact is negligible: one handlerton flag and focused
smoke coverage.

Measured after implementation with `MYLITE_BUILD_JOBS=8` and the
Docker-based `mariadb-minsize` profile:

| Artifact | Size |
| --- | ---: |
| `build/mariadb-minsize/libmysqld/libmariadbd.a` | 44,391,138 bytes |
| `build/mariadb-minsize/mylite/libmylite.a` | 29,698 bytes |
| `build/mariadb-minsize/mylite/mylite-storage-engine-smoke` | 22,769,400 bytes |
| `build/mariadb-minsize/mylite/mylite-compatibility-smoke` | 22,704,248 bytes |
| `build/mariadb-minsize/mylite/mylite-open-close-smoke` | 22,770,928 bytes |
| `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke` | 22,703,384 bytes |

## License, Trademark, And Dependency Impact

No new dependencies, license changes, or trademark changes.

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

The storage smoke should report:

- MyLite remains visible as a supported storage engine,
- MyLite transaction-boundary DML survives `ROLLBACK` according to
  non-transactional engine semantics,
- warning/diagnostic behavior after `ROLLBACK` is recorded,
- post-rollback MyLite row state survives fresh-process reopen,
- no persistent `.frm`, engine sidecars, dynamic plugin artifacts, or catalog
  temporary sidecars are introduced.

## Acceptance Criteria

- MyLite is consistently marked as non-transactional/no-rollback in engine
  flags.
- A focused smoke test proves current `ROLLBACK` behavior over MyLite rows and
  records any MariaDB warning behavior for the selected base.
- Fresh-process reopen observes the same post-rollback row state.
- Docs state that MyLite currently has durable statement/generation
  publication, not SQL transaction rollback.
- Existing storage, compatibility, embedded lifecycle, and `libmylite`
  lifecycle smokes pass.
- The slice does not add transaction hooks or recovery files without a real
  durable rollback design.

## Implementation Result

Implemented in `vendor/mariadb/server/storage/mylite/ha_mylite.cc`,
`vendor/mariadb/server/mylite/storage_engine_smoke.cc`, and
`tools/run-storage-engine-smoke.sh`.

- `mylite_hton->flags` now includes `HTON_NO_ROLLBACK` while MyLite table flags
  continue to include `HA_NO_TRANSACTIONS`.
- Storage smoke adds `transaction-write` and `transaction-read` phases using a
  fresh `.mylite` file and a fresh-process reopen.
- Observed MariaDB 11.8.6 rollback warning after MyLite DML inside
  `START TRANSACTION`: warning code `1196`, message "Some non-transactional
  changed tables couldn't be rolled back."
- Observed MyLite row state after `ROLLBACK` and after reopen:
  `transaction_rows=2:deux,3:three`.
- No transaction hooks, journal/WAL files, or file-format fields were added.

## Risks And Unresolved Questions

- The exact warning behavior may depend on whether MariaDB registers MyLite as a
  transaction participant despite `HA_NO_TRANSACTIONS`; the implemented smoke
  asserts the observed MariaDB 11.8.6 warning and row state.
- Some users may expect `START TRANSACTION` to be rejected for MyLite tables.
  Rejecting generic transaction SQL is a broader SQL-layer policy decision and
  is not part of this slice.
- A later rollback/WAL design will need to replace this non-transactional
  boundary with real transaction hooks, savepoint state, recovery rules, and
  compatibility tests.
