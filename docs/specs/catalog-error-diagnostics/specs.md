# Catalog Error Diagnostics Slice

## Problem Statement

MyLite catalog failures currently collapse through generic handler errors such
as `HA_ERR_CRASHED`, which MariaDB reports as index corruption. After primary
file locking, an externally held advisory lock is correctly detected and logged
as `MyLite: catalog lock failed`, but the SQL-facing smoke result can still
surface as a misleading table/index failure.

This slice should make catalog lock, open, load, and write failures produce
stable, accurate MariaDB handler diagnostics without changing MyLite's public C
API or file format.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc` returns
  `HA_ERR_CRASHED` for many `mylite_ensure_catalog_loaded_locked()` failures
  and for `mylite_flush_catalog_locked()` write failures.
- `mylite_ensure_catalog_file_locked()` logs advisory-lock conflicts through
  MariaDB's error log, but the handler return path does not preserve that
  cause for SQL diagnostics.
- `vendor/mariadb/server/sql/handler.cc` maps `HA_ERR_CRASHED` to
  `ER_NOT_KEYFILE`, whose user text is index corruption oriented.
- `handler::print_error()` has specific mappings for normal handler errors
  including `EACCES`, `EAGAIN`, `HA_ERR_LOCK_WAIT_TIMEOUT`, `HA_ERR_DISK_FULL`,
  `HA_ERR_RECORD_FILE_FULL`, and `HA_ERR_NO_SUCH_TABLE`.
- `handler::get_error_message()` is available for engine-specific messages
  when a handler returns an error without a built-in MariaDB mapping.
- `handler::ha_create()` returns the engine error from `create()` and higher
  SQL code may wrap it as `ER_CANT_CREATE_TABLE`, so the returned handler code
  must itself be meaningful.

## Scope

This slice will:

- introduce a small MyLite-owned catalog error mapping layer,
- return `HA_ERR_LOCK_WAIT_TIMEOUT` for primary-file advisory-lock conflicts,
- preserve relevant OS errors such as `EACCES`, `EAGAIN`, and `ENOSPC` when
  catalog file operations fail with an errno MariaDB already maps clearly,
- keep invalid/corrupt catalog payloads on corruption-style handler errors,
  but make that path explicit instead of using it for lock contention,
- update MyLite storage smokes to assert that primary lock conflicts no longer
  surface as index corruption,
- document the distinction between temporary busy/lock failures and durable
  catalog corruption.

## Non-Goals

- Do not add a new public `libmylite` result code or diagnostic API.
- Do not invent a custom MariaDB error-number range for MyLite.
- Do not change file locking, catalog recovery, or page format behavior.
- Do not change unsupported SQL feature diagnostics outside catalog-owned
  storage-engine failures.

## Proposed Design

Add a narrow helper in `ha_mylite.cc` that records the last catalog-operation
handler error for the current thread. Catalog paths that currently return
`false` after an OS or lock failure should set this helper before returning.
Callers that need to translate a failed catalog load/write into a handler
return code should use the recorded code, falling back to `HA_ERR_CRASHED` only
when no better cause is known.

The first mappings should be conservative:

- `my_lock()` conflict after `MY_NO_WAIT` becomes `HA_ERR_LOCK_WAIT_TIMEOUT`;
- `open`, `fstat`, `pwrite`, `fsync`, and `close` failures preserve positive
  `errno` values where MariaDB already maps them;
- invalid on-disk catalog contents remain corruption-style failures, because
  they are not temporary busy conditions.

The implementation should avoid broad changes to SQL-layer error handling.
It should use existing MariaDB handler error codes and the existing MyLite
storage-engine call sites.

## Affected Subsystems

- MyLite catalog load/write helpers in `ha_mylite.cc`.
- MyLite DDL/DML handler return paths that translate catalog failures.
- Storage smoke conflict assertions.
- Roadmap and single-file storage diagnostics documentation.

## DDL Metadata Routing Impact

DDL routing behavior is unchanged. `CREATE`, `DROP`, `RENAME`, and copy
`ALTER` should keep using the same MyLite catalog operations. The difference is
that a catalog busy/open/write failure should now report a meaningful MariaDB
handler diagnostic instead of an unrelated index-corruption message.

## Single-File And Embedded-Lifecycle Implications

The primary file remains one `.mylite` file with no new diagnostic sidecar.
Temporary lock conflicts should be treated as retryable operation failures; they
must not mark the catalog permanently invalid for the process. Durable catalog
corruption should remain distinct and should continue to stop unsafe writes.

## Public API And File-Format Impact

No public API or file-format change. This is a SQL-facing and smoke-facing
diagnostic improvement only.

## Binary-Size Impact

The implementation adds a small first-party handler error mapping path and one
storage-smoke conflict phase. It adds no dependency. Measured artifacts after
implementation:

- `libmariadbd.a`: 44,395,386 bytes.
- `libmylite.a`: 29,698 bytes.
- `mylite-storage-engine-smoke`: 22,769,960 bytes.
- `mylite-compatibility-smoke`: 22,704,712 bytes.
- `mylite-open-close-smoke`: 22,705,360 bytes.
- `mylite-embedded-bootstrap-smoke`: 22,703,744 bytes.

## License, Trademark, And Dependency Impact

No new dependency. The implementation stays inside existing GPL-2.0-only
MariaDB-derived handler code.

## Implementation Result

`ha_mylite.cc` now records a thread-local catalog handler error while loading
or writing the catalog. Advisory-lock conflicts map to
`HA_ERR_LOCK_WAIT_TIMEOUT`; errno-backed catalog open, stat, write, fsync, and
close failures preserve their errno-derived handler diagnostics; invalid
catalog contents still use corruption-style handler errors.

The storage smoke now has a `lock-conflict` phase that bypasses seed-table
discovery and exercises the catalog-mutating `CREATE TABLE` path directly while
an external helper holds the primary file lock. The report now shows
`errno: 146 "Lock timed out; Retry transaction"` and still records the MyLite
catalog lock log line. The smoke asserts that the old `Index is corrupted`
surface does not reappear.

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

The storage smoke should assert that the primary-file lock conflict report:

- contains the MyLite catalog lock log line,
- fails with a lock/busy handler diagnostic, and
- does not contain the previous index-corruption text.

## Acceptance Criteria

- Advisory-lock conflicts return a lock/busy MariaDB handler diagnostic instead
  of an index-corruption diagnostic.
- Catalog OS failures preserve useful errno-derived diagnostics where MariaDB
  already maps them.
- Invalid catalog contents still fail as catalog corruption and are not
  misclassified as temporary lock contention.
- Existing storage, compatibility, embedded lifecycle, and `libmylite`
  lifecycle smokes pass.
- Docs describe the temporary-busy versus durable-corruption distinction.

## Risks And Unresolved Questions

- MariaDB's `CREATE TABLE` wrapper can add its own `ER_CANT_CREATE_TABLE`
  context around handler codes. The slice should verify the smoke-visible
  message rather than assuming every SQL surface prints the raw handler text.
- A future MyLite public C API diagnostic layer may need richer error
  categories than MariaDB handler codes expose. That should be designed when
  the public SQL execution API exists.
