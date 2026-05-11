# Statement Error Rollback Slice

## Problem Statement

The current MyLite transaction bridge captures a statement snapshot before the
first supported row mutation and relies on MariaDB statement rollback to restore
that snapshot if a statement fails after partial mutation. The storage smoke
now covers full transaction rollback, commit, and savepoints, but it does not
prove that a multi-row statement error cannot leak earlier rows from the same
failed statement.

This slice should add focused coverage for statement-level rollback on MyLite
DML errors in autocommit and explicit transaction modes. It should keep the
existing implementation if the behavior already holds, and fix the transaction
context if the new smoke exposes a leak.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Official MariaDB documentation:
  - <https://mariadb.com/kb/en/mariadb-transactions-and-isolation-levels-for-sql-server-users/>
    states that transactional engines roll back statements and transactions,
    while non-transactional engines cannot.
  - <https://mariadb.com/kb/en/insert/>
    documents multi-row `INSERT` and duplicate-key behavior.
- `vendor/mariadb/server/sql/transaction.cc`:
  - `trans_commit_stmt()` calls `ha_commit_trans(thd, FALSE)` for statement
    participants and then resets the statement transaction list,
  - `trans_rollback_stmt()` calls `ha_rollback_trans(thd, FALSE)` for
    statement participants and then resets the statement transaction list.
- `vendor/mariadb/server/sql/handler.cc`:
  - `ha_commit_trans(thd, false)` routes to each statement participant's
    `commit(thd, false)`,
  - `ha_rollback_trans(thd, false)` routes to each statement participant's
    `rollback(thd, false)`.
- `vendor/mariadb/server/sql/sql_insert.cc`:
  - multi-row insert execution calls `table->file->ha_write_row()` for rows as
    they are processed,
  - duplicate key errors can occur after earlier rows in the same statement
    have already reached the handler,
  - statement cleanup paths call `trans_rollback_stmt(thd)` for failed
    statements.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc`:
  - `mylite_prepare_dml_mutation_locked()` captures a statement snapshot before
    the first DML mutation,
  - `mylite_rollback(thd, false)` restores the statement snapshot and clears it,
  - `mylite_commit(thd, false)` publishes in autocommit mode and clears only
    the statement snapshot inside an explicit transaction.

## Scope

This slice will:

- add storage smoke coverage for a failed multi-row `INSERT` in autocommit
  mode after an earlier row in the same statement has been accepted,
- add storage smoke coverage for a failed multi-row `INSERT` inside an explicit
  transaction, proving the failed statement rolls back while earlier successful
  statements in the transaction remain visible and commit normally,
- verify duplicate-key statement errors do not emit warning `1196`,
- verify fresh-process reopen sees only committed rows after the explicit
  transaction commits,
- fix MyLite statement snapshot handling if either test exposes a leak.

## Non-Goals

- Do not add page-level undo, redo, WAL, MVCC, or new file-format records.
- Do not change savepoint behavior except where statement rollback interacts
  with existing savepoint snapshots.
- Do not make DDL transactional.
- Do not broaden DML type support beyond the currently supported row subset.
- Do not change the public `libmylite` API.

## Proposed Design

Use the storage-engine smoke because it already owns MyLite SQL lifecycle
coverage and writes report fields consumed by the grouped compatibility
harness.

Add a statement-error phase to the existing transaction-boundary write/read
path:

1. create a table with a primary key,
2. insert a baseline row,
3. run a multi-row insert whose first row is unique and whose second row
   duplicates the baseline primary key,
4. expect the statement to fail,
5. verify the first row from the failed statement is absent and the baseline
   row remains,
6. verify warning summary is `none` rather than warning `1196`,
7. start an explicit transaction,
8. insert one successful transaction row,
9. run another failed multi-row duplicate-key statement,
10. verify the transaction row remains visible but the first row from the
    failed statement is absent,
11. commit and verify fresh-process reopen sees only the baseline and committed
    transaction row.

The implementation should prefer report fields over ad hoc output so failure
evidence remains in `build/mariadb-minsize/*report.txt`.

## Affected Subsystems

- Storage smoke C++ transaction phase.
- Compatibility harness report evidence if new fields are scanned.
- Roadmap and this slice spec.
- MyLite transaction context only if the smoke exposes an implementation bug.

## DDL Metadata Routing Impact

None. This slice exercises row DML only.

## Single-File And Embedded-Lifecycle Implications

No new files are introduced. Failed statements should not publish a durable
generation containing partial statement state. Fresh-process reopen should
observe only rows committed by successful statements.

## Public API And File-Format Impact

No public API or file-format change.

## Binary-Size Impact

Expected impact is limited to smoke coverage unless a small handler fix is
needed. No dependency or new compiled MariaDB subsystem should be added.
Measured sizes should be recorded after implementation.

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

Storage smoke should verify:

- failed autocommit multi-row insert leaves only the baseline row,
- failed explicit-transaction multi-row insert leaves prior successful
  transaction statements visible,
- failed statement rows do not persist after commit and fresh-process reopen,
- duplicate-key statement rollback produces no warning `1196`,
- no persistent `.frm`, journal/WAL companions, dynamic plugin artifacts, or
  catalog temporary sidecars are introduced.

## Acceptance Criteria

- Autocommit statement rollback restores the pre-statement MyLite row state.
- Explicit-transaction statement rollback restores the pre-statement row state
  without rolling back earlier successful statements in the transaction.
- The transaction can still commit after a duplicate-key statement failure.
- Fresh-process reopen observes only committed rows from successful statements.
- Existing storage, compatibility, embedded lifecycle, and `libmylite`
  lifecycle smokes pass.

## Risks And Unresolved Questions

- MariaDB error cleanup paths differ across DML statement types. This slice
  starts with duplicate-key `INSERT` because it is deterministic and exercises
  partial handler mutation.
- Broader statement-failure coverage for `UPDATE`, `DELETE`, killed queries,
  and storage write failures can be added later once this boundary is proven.
