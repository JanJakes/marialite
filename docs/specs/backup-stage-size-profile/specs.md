# Backup Stage Size Profile

## Problem Statement

The aggressive embedded minsize profile still compiles MariaDB's `BACKUP
STAGE`, `BACKUP LOCK`, and backup DDL logging implementation. That subsystem
exists for external server backup tools and can create an inherited `ddl.log`
sidecar during backup. MyLite's embedded runtime does not expose an external
server backup workflow; file-owned backup and snapshot semantics need a
MyLite-owned design later.

Current baseline after `optimizer-trace-size-profile`:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 30,229,492 |
| `backup.cc.o` object | 16,240 |
| stripped `mylite-open-close-smoke` | 5,743,824 |

## Source Findings

- Imported MariaDB base: `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/backup.cc` implements `BACKUP STAGE`, external
  backup locks, and DDL logging for backup tools.
- `backup.cc::start_ddl_logging()` creates `ddl.log` under
  `mysql_data_home`.
- `vendor/mariadb/server/sql/sql_parse.cc` dispatches `SQLCOM_BACKUP_STAGE` to
  `run_backup_stage()` and `SQLCOM_BACKUP_LOCK` to `backup_lock()`.
- Many DDL paths call `backup_log_ddl()` so a backup process can observe DDL
  changes. In an embedded no-backup profile, those calls should become no-ops.
- `backup_set_alter_copy_lock()` and `backup_reset_alter_copy_lock()` adjust
  backup MDL locks around `ALTER TABLE` copy paths. With backup commands
  rejected, those adjustments are inert.
- `backup_unlock()` is called during THD cleanup and should still release an
  already-held `mdl_backup_lock` defensively.

## Scope

Add a minsize option that removes the full backup implementation from the
embedded library. The option will:

- remove `../sql/backup.cc` from `SQL_EMBEDDED_SOURCES`;
- add a MyLite-owned backup stub;
- reject `BACKUP STAGE` through `run_backup_stage()`;
- reject `BACKUP LOCK` through `backup_lock()`;
- leave internal backup DDL logging as a no-op; and
- preserve defensive backup-lock cleanup.

## Non-Goals

- Do not remove MDL backup-lock enum values or parser syntax.
- Do not design MyLite-native file backup, snapshots, or online backup.
- Do not change table DDL semantics except removing backup-tool logging.
- Do not change public `libmylite` API or `.mylite` file format.

## Proposed Design

Add `MYLITE_DISABLE_BACKUP_STAGE` to
`vendor/mariadb/server/libmysqld/CMakeLists.txt` and enable it in
`tools/build-mariadb-minsize.sh`.

Create `vendor/mariadb/server/libmysqld/mylite_backup_stub.cc`. The stub will
keep `backup_stage_names` for parser/system wiring, make `backup_init()` and
`backup_log_ddl()` no-ops, reject user-facing backup commands with
`ER_OPTION_PREVENTS_STATEMENT`, preserve `backup_unlock()` cleanup, and make
ALTER-copy backup lock adjustments inert.

## Affected Subsystems

- Embedded minsize SQL source list.
- User-facing `BACKUP STAGE` and `BACKUP LOCK` behavior.
- Inherited backup DDL logging side effects.
- Binary-size documentation.

## Single-File And Embedded-Lifecycle Impact

This removes the inherited `ddl.log` backup sidecar path from the aggressive
embedded profile. It does not implement MyLite-native backup semantics.

## Public API Or File-Format Impact

No public `libmylite` API or `.mylite` file-format change.

## Binary-Size Impact

Expected archive savings are close to the current 16,240-byte `backup.cc.o`
member minus the small replacement stub. Linked-runtime savings should be
small but measurable because backup symbols are present in the current smoke.

Measured after implementation:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 30,229,492 | 30,218,906 | -10,586 |
| unstripped `mylite-open-close-smoke` | 7,983,152 | 7,979,416 | -3,736 |
| stripped `mylite-open-close-smoke` | 5,743,824 | 5,740,424 | -3,400 |

The archive no longer contains `backup.cc.o`; it contains
`mylite_backup_stub.cc.o`, measured at 4,880 bytes. The archive object count
remains 422.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-backup-stage \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-backup-stage \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-backup-stage \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Measure:

- archive bytes and object count;
- unstripped and stripped linked smoke bytes;
- absence of `backup.cc.o` in `libmariadbd.a`; and
- presence of the replacement stub.

## Acceptance Criteria

- The minsize build completes. Passed with
  `build/mariadb-minsize-no-backup-stage`.
- The open/close smoke and compatibility harness pass. Passed.
- The embedded archive no longer contains `backup.cc.o`. Passed.
- The sidecar scan still reports no unexpected or known inherited sidecars.
  Passed through the compatibility harness.
- Size results are recorded here and in
  `docs/research/production-size-analysis.md`. Passed.

## Risks And Unresolved Questions

- `BACKUP STAGE` and `BACKUP LOCK` become explicitly unsupported in the
  aggressive profile.
- MDL backup-lock classes remain compiled from `mdl.cc`; removing those would
  require a broader metadata-locking source change.
