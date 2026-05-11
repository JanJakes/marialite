# libmylite-readonly-open

## Problem Statement

`mylite_open_v2()` accepts `MYLITE_OPEN_READONLY`, but the current behavior is
only a primary-file preflight. The storage engine still receives the same
runtime configuration as read-write opens, opens the catalog with `O_RDWR |
O_CREAT`, takes an exclusive write lock, and permits DDL/DML paths to publish
new `.mylite` generations.

This slice makes read-only opens an enforced public mode for the current
process-scoped embedded runtime.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/mylite/mylite.cc:173` implements
  `mylite_open_v2()` and currently starts the process-global runtime without
  carrying the requested open mode into the storage engine.
- `vendor/mariadb/server/mylite/mylite.cc:920` builds the embedded server
  argument vector and already passes `--mylite-catalog-file=<path>`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:548` declares the
  `catalog_file` storage-engine sysvar as a read-only command-line argument.
  The same mechanism is suitable for a startup-only read-only mode.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:846`,
  `:856`, `:862`, `:887`, `:906`, `:1118`, and `:1163` are the handler DDL,
  DML, autoincrement reset, and rename entry points that must reject mutation
  when the catalog is read-only.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:2557` opens the catalog
  path with `O_RDWR | O_CREAT` and takes `F_WRLCK`. Read-only mode needs
  `O_RDONLY` and a shared advisory read lock.
- `vendor/mariadb/server/sql/handler.cc:484` and `:497` map
  `HA_ERR_READ_ONLY_TRANSACTION` and `HA_ERR_TABLE_READONLY` to MariaDB's
  read-only diagnostics. MyLite should use these existing handler errors
  rather than inventing a new SQL-layer error.
- `vendor/mariadb/server/include/handler_state.h` maps both handler read-only
  errors to SQLSTATE `25000`.
- The SQL layer can wrap handler read-only errors before they reach the C API:
  observed `INSERT` and prepared `INSERT` failures preserve MariaDB errno 1036
  (`ER_OPEN_AS_READONLY`) with SQLSTATE `HY000`, while `CREATE TABLE` preserves
  errno 1005 (`ER_CANT_CREATE_TABLE`) with the handler errno 165 text embedded
  in the message.

## Scope

This slice will:

- add a startup-only `mylite_read_only` storage-engine option,
- pass that option from `libmylite` when `MYLITE_OPEN_READONLY` starts the
  embedded runtime,
- keep the runtime open mode process-global and reject same-path handles that
  request a different mode after startup,
- open the primary catalog file with `O_RDONLY` and a shared advisory read lock
  in read-only mode,
- reject MyLite DDL and row/autoincrement mutations with MariaDB read-only
  handler diagnostics,
- map SQLSTATE class `25` and the observed MariaDB read-only errno/message
  shapes through the public C API to `MYLITE_READONLY`,
- extend the `libmylite` smoke with a fresh-process read-only pass that proves
  existing rows can be read and DDL/DML cannot change the primary file.

## Non-Goals

- Do not implement per-handle mixed read/write mode inside one process-global
  embedded runtime.
- Do not add cross-process writer concurrency.
- Do not remove temporary runtime-directory or Aria startup side effects.
- Do not implement a strict no-temp-files mode.
- Do not change unsupported `MYLITE_OPEN_EXCLUSIVE` or `MYLITE_OPEN_URI`
  behavior.
- Do not make all MariaDB schemas read-only. This slice enforces MyLite
  primary-file mutations in the `MYLITE` engine.

## Proposed Design

Add a storage-engine sysvar:

```c++
static char mylite_read_only= 0;

static MYSQL_SYSVAR_BOOL(
  read_only,
  mylite_read_only,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "Open the primary MyLite catalog file in read-only mode.",
  nullptr,
  nullptr,
  0);
```

`mylite_open_v2()` computes the requested read-only mode from `flags`. If the
runtime is already started for the same path but with a different mode, the
open returns `MYLITE_BUSY` with a handle diagnostic. This is conservative: the
current embedded runtime and storage-engine sysvars are process-global, so
claiming per-handle read-only behavior would be false.

When starting the runtime for a read-only open, `build_server_args()` adds
`--mylite-read-only=ON`. The existing default `mylite_open()` path continues to
start read-write with create.

`mylite_ensure_catalog_file_locked()` uses the read-only sysvar to choose:

- `O_RDONLY | O_CLOEXEC` and `F_RDLCK` when read-only,
- `O_RDWR | O_CREAT | O_CLOEXEC` and `F_WRLCK` when read-write.

Mutation entry points return `HA_ERR_TABLE_READONLY` before changing in-memory
catalog or allocator state. `mylite_flush_catalog_locked()` also refuses writes
in read-only mode as a backstop. `mylite_reserve_auto_increment()` remains a
mutation because it advances persistent autoincrement state, so it must fail
before reserving a value.

`prepare_primary_file()` should avoid creating parent directories for
read-only opens. It should only ensure the parent directory when the requested
flags include `MYLITE_OPEN_CREATE`.

Public error classification should map SQLSTATE class `25`,
`ER_OPEN_AS_READONLY`, and `ER_CANT_CREATE_TABLE` messages that carry
`"Table is read only"` to `MYLITE_READONLY`, while preserving the original
SQLSTATE and MariaDB errno for detailed diagnostics.

## Affected Subsystems

- MyLite C API runtime startup and open-mode validation.
- MyLite storage-engine sysvars, file locking, and mutation guards.
- `libmylite` result-code classification.
- Open/close smoke test and wrapper script.
- API docs, storage architecture docs, and roadmap.

## DDL Metadata Routing Impact

Read-only mode must reject `CREATE`, `DROP`, `ALTER`, and `RENAME` before the
MyLite catalog changes. This slice does not change the DDL metadata routing
format; it adds a mode guard around the existing routed catalog mutations.

## Single-File And Embedded-Lifecycle Implications

The primary `.mylite` file must not be opened writable or modified by the
MyLite engine during a read-only open. Temporary runtime files may still be
created under the derived `.mylite-runtime` directory until a later strict
no-temp-file mode exists. The smoke should compare primary-file size and mtime
around failed read-only mutations.

Read-only mode is process-global for the active embedded runtime. A read-only
runtime can accept additional read-only handles for the same path. It must not
accept read-write handles until the process exits, matching the existing
process-scoped runtime constraint.

## Public API Or File-Format Impact

No new public function is added. This slice gives existing
`MYLITE_OPEN_READONLY` and `MYLITE_READONLY` definitions real behavior.

No file-format version bump is required because read-only mode does not change
on-disk structures. It only changes access mode and mutation policy.

## Binary-Size Impact

Expected growth is small: one storage-engine boolean sysvar, a mode flag in
`RuntimeState`, read-only guards, and smoke coverage. The post-implementation
`MinSizeRel` artifact sizes will be recorded after verification.

## License, Trademark, And Dependency Impact

No new dependency. All changes remain in existing GPL-2.0-only MyLite and
MariaDB-derived source files.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/open_close_smoke.cc` with a
  `--mode=readonly` run.
- Keep the existing default smoke as the write-mode seed and regression pass.
- In the read-only smoke process:
  - open the existing database with `MYLITE_OPEN_READONLY`,
  - select rows written by the default pass,
  - verify a second read-only handle for the same path succeeds,
  - verify a same-path read-write open returns `MYLITE_BUSY`,
  - verify `INSERT` and `CREATE TABLE` return `MYLITE_READONLY`,
  - verify MariaDB read-only errno/message details are preserved,
  - verify primary-file size and mtime do not change after failed mutations.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` for changed shell scripts
  - `git diff --check`

## Acceptance Criteria

- `MYLITE_OPEN_READONLY` does not create a missing primary file or parent
  directory.
- A read-only MyLite runtime opens the primary catalog file `O_RDONLY`, holds a
  shared advisory lock, and can read existing MyLite rows.
- MyLite DDL, DML, and autoincrement mutations fail before changing catalog or
  allocator state.
- Public C API execution and prepared-statement mutation failures classify as
  `MYLITE_READONLY` while preserving MariaDB errno and SQLSTATE.
- The smoke proves failed read-only mutations leave the primary file unchanged.
- Same-path handles with incompatible open modes are rejected honestly while
  the embedded runtime remains process-global.
- Docs and roadmap describe the implemented read-only semantics and limits.

## Risks And Unresolved Questions

- MariaDB may perform non-MyLite internal writes in the runtime directory even
  for read-only opens. This is acceptable for this slice but not for a future
  strict no-temp-file profile.
- The read-only sysvar is process-global. Per-handle read/write mixing requires
  a deeper runtime/session model and is deliberately deferred.
- Shared read locks allow multiple read-only processes only when no writer
  holds the exclusive catalog lock. This is a correctness step, not a complete
  concurrent-reader product guarantee.

## Implementation Result

Implemented as a startup-only runtime and storage-engine mode. Read-only opens
now pass `--mylite-read-only=ON` into the embedded runtime, reject later
same-path read-write opens in the same process, open the primary file
`O_RDONLY`, hold a shared advisory lock, and reject MyLite DDL/DML mutation
paths with `HA_ERR_TABLE_READONLY`.

The `libmylite` diagnostic mapper now classifies MariaDB read-only shapes as
`MYLITE_READONLY` while preserving the underlying MariaDB errno and SQLSTATE.
The read-only smoke verifies existing-row reads, second read-only handles,
read-write mode rejection, `INSERT`, `CREATE TABLE`, and prepared `INSERT`
rejection, unchanged primary-file size/mtime, and no dynamic plugin artifacts.

The post-implementation `MinSizeRel` build records:

| Artifact | Size |
| --- | ---: |
| `build/mariadb-minsize/mylite/libmylite.a` | 87,174 bytes |
| `build/mariadb-minsize/libmysqld/libmariadbd.a` | 44,415,928 bytes |

The build report still records 571 `libmariadbd.a` archive objects and no
dynamic plugin artifacts.
