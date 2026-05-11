# Primary File Locking Slice

## Problem Statement

MyLite currently uses a process-local `mylite_catalog_mutex`, but no
cross-process guard protects the primary `.mylite` file. Two embedded runtimes
can open the same path, load the same accepted generation, mutate independent
in-memory catalogs, and publish conflicting header generations or free-page
state. That is incompatible with the product rule that cross-process write
concurrency must not be claimed before locking and recovery are designed and
tested.

This slice should add a conservative single-process file ownership guard:
while one process has a MyLite catalog file open, another process attempting to
open the same primary file should fail explicitly instead of racing.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc` currently owns catalog
  state through process globals: `mylite_catalog_mutex`,
  `mylite_catalog_loaded`, `mylite_loaded_catalog_header`,
  `mylite_free_page_ranges`, and `mylite_pending_free_page_ranges`.
- `mylite_load_catalog_locked()` currently opens and closes
  `mylite_catalog_file` for reading every time it loads a configured catalog.
- `mylite_write_catalog_locked()` currently opens and closes the same path for
  each durable publication.
- `mylite_deinit_func()` clears in-memory catalog state, making it the natural
  place to release a process-held primary-file lock.
- `vendor/mariadb/server/mysys/my_lock.c` implements `my_lock()` using
  `fcntl(F_SETLK/F_SETLKW)` on POSIX and a Windows lock backend. It normalizes
  lock conflicts to `EAGAIN`.
- `vendor/mariadb/server/include/my_sys.h` exposes `my_lock()` plus flags such
  as `MY_NO_WAIT` and `MY_FORCE_LOCK`.
- `vendor/mariadb/server/sql/handler.h` and
  `vendor/mariadb/server/sql/handler.cc` define `external_lock()`, but that is
  a table-level SQL execution lock. It is not sufficient for this slice because
  the catalog file must be owned before table opens, DDL, discovery, and first
  durable writes.

## Scope

This slice will:

- acquire an exclusive advisory lock on the primary `.mylite` file when a
  configured catalog path is first loaded or created,
- keep that lock for the process lifetime of the loaded MyLite storage engine,
- release the lock during MyLite storage-engine deinitialization,
- fail explicitly when another process already holds the lock,
- use the locked file descriptor for catalog load and write I/O so POSIX
  record-lock lifetime is not accidentally lost by opening and closing another
  descriptor for the same file,
- keep the implementation conservative: one process owns the primary file,
  with no cross-process reader/writer sharing claims,
- add smoke coverage that proves an externally held advisory lock prevents
  MyLite from opening/writing the same `.mylite` file,
- document the temporary single-owner concurrency boundary.

## Non-Goals

- Do not implement cross-process concurrent readers or writers.
- Do not implement lock promotion, shared locks, WAL, rollback journal, MVCC, or
  page-level latching.
- Do not add a persistent lock sidecar.
- Do not add stale-lock recovery beyond OS advisory-lock release when the
  owning process exits.
- Do not change the public `libmylite` C API.
- Do not make table-level `external_lock()` responsible for primary-file
  ownership.

## Proposed Design

### Lock Acquisition

Add MyLite-owned process globals for the catalog file descriptor and locked
path. When `mylite_ensure_catalog_loaded_locked()` sees a configured
`mylite_catalog_file`, it should:

1. open the primary file with `O_RDWR | O_CREAT | O_CLOEXEC`,
2. acquire an exclusive whole-file lock using `my_lock()` with
   `F_WRLCK`, offset `0`, length `F_TO_EOF`, and
   `MY_FORCE_LOCK | MY_NO_WAIT`,
3. keep the descriptor open until `mylite_deinit_func()`.

`MY_FORCE_LOCK` is required because the embedded smoke currently starts MariaDB
with `--skip-external-locking`, which sets MariaDB's global external-locking
state for traditional table locks. MyLite's primary-file ownership guard must
remain active even when generic external table locking is disabled.

If the lock attempt fails with `EAGAIN` or another error, MyLite should log a
clear catalog lock error and fail catalog loading. It should not silently fall
back to process-local locking.

### Descriptor Lifetime

POSIX `fcntl()` record locks are process-associated and can be released when a
descriptor for the locked file is closed. To avoid accidentally dropping the
lock, catalog load and write paths should use the retained locked descriptor
instead of opening and closing independent descriptors for the same file.

This means `mylite_load_catalog_locked()` and `mylite_write_catalog_locked()`
should operate on the retained descriptor once a catalog path is configured.
They may still use local variables for clarity, but they must not close a
separate descriptor that could release the process lock.

### Lock Release

`mylite_deinit_func()` should unlock the retained descriptor with
`my_lock(F_UNLCK, 0, F_TO_EOF, MY_FORCE_LOCK | MY_NO_WAIT)` and then close it.
Cleanup must tolerate partially initialized state: failed lock acquisition,
failed load, and repeated deinit should not double-close.

### Error Surface

A second process should fail early with a storage-engine smoke report showing a
nonzero status and a catalog lock/open failure. MyLite should not block
indefinitely for this first slice; a nonblocking failure makes CI coverage and
application behavior deterministic. Later lock-wait policy can add a timeout
configuration if product requirements justify it.

## Affected Subsystems

- MyLite catalog load/write/deinit paths in `ha_mylite.cc`.
- Storage smoke shell coverage for external advisory-lock conflict.
- Architecture and roadmap docs.

No page format, catalog format, allocator format, SQL parser, or public C API
surface should change.

## DDL Metadata Routing Impact

DDL metadata routing is unchanged. The lock must be acquired before DDL can
mutate in-memory catalog state for a configured primary file, so DDL cannot
race another process that already owns the same `.mylite` path.

## Single-File And Embedded-Lifecycle Implications

The lock is advisory OS state on the primary `.mylite` file itself. It does not
introduce a persistent sidecar. The lock lifecycle is tied to the embedded
process/plugin lifecycle and is released by process exit even if the process
crashes.

This deliberately narrows current concurrency semantics to one process per
primary file. That is acceptable until a later WAL/locking design introduces
tested cross-process reader/writer behavior.

## Public API And File-Format Impact

The public C API is unchanged. The file format is unchanged. Opening a
configured catalog path may now create an empty `.mylite` file earlier than the
first durable write so the process can lock the primary asset before DDL or DML
mutates in-memory state.

## Binary-Size Impact

The implementation adds a small first-party descriptor lifetime path and a
storage smoke conflict test. It adds no dependency. Measured artifacts after
implementation:

- `libmariadbd.a`: 44,393,530 bytes.
- `libmylite.a`: 29,698 bytes.
- `mylite-storage-engine-smoke`: 22,769,864 bytes.
- `mylite-compatibility-smoke`: 22,770,152 bytes.
- `mylite-open-close-smoke`: 22,705,168 bytes.
- `mylite-embedded-bootstrap-smoke`: 22,703,552 bytes.

## License, Trademark, And Dependency Impact

No new dependency. The implementation should use MariaDB's existing GPLv2
`my_lock()` abstraction.

## Implementation Result

The implemented storage-engine path retains one locked descriptor per
configured catalog path in `ha_mylite.cc`. `mylite_ensure_catalog_loaded_locked()`
acquires the lock before loading or creating a catalog, `mylite_load_catalog_locked()`
and `mylite_write_catalog_locked()` use that retained descriptor for I/O, and
`mylite_deinit_func()` unlocks and closes it during storage-engine teardown.
Transient lock/open failures do not mark the catalog permanently invalid, so a
later operation in the same process can retry acquisition after the lock holder
exits.

The storage smoke now starts a helper process that holds a POSIX advisory lock
on the same `.mylite` file, verifies that MyLite fails with
`MyLite: catalog lock failed`, releases the helper lock, and verifies a normal
write phase succeeds against the same file.

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

The storage smoke should add an external lock-conflict check:

1. create or open a `.mylite` path,
2. hold an exclusive advisory lock on that file from a helper process,
3. run the MyLite storage smoke against the same path,
4. assert that MyLite fails instead of loading or publishing a generation,
5. release the helper lock,
6. run a normal MyLite phase against the same path and assert success.

The normal storage smoke should continue to verify no persistent `.frm`,
engine sidecars, dynamic plugin artifacts, catalog temporary sidecars, or lock
sidecars are introduced.

## Acceptance Criteria

- MyLite holds an exclusive primary-file lock for the lifetime of a configured
  loaded catalog.
- Catalog load and write use the retained locked descriptor and do not
  accidentally release the lock by closing another descriptor for the same
  file.
- A second process or external advisory-lock holder causes a clear MyLite load
  failure instead of a concurrent write race.
- Releasing the external lock allows a later MyLite open/write to succeed.
- No persistent lock sidecar or file-format field is added.
- Existing storage, compatibility, embedded lifecycle, and `libmylite`
  lifecycle smokes pass.

## Risks And Unresolved Questions

- `fcntl()` locking semantics differ from BSD `flock()` and from Windows file
  locking. Using MariaDB's `my_lock()` keeps platform behavior aligned with the
  imported source, but cross-platform smoke coverage may need expansion later.
- Creating an empty `.mylite` file at first configured load changes the current
  lazy-create behavior. The tradeoff is intentional: locking after DDL/DML
  mutates memory would still allow multi-process races.
- This does not provide stale-lock detection because OS advisory locks are
  released by process exit. If a future companion lock file carries metadata,
  it needs its own lifecycle and recovery spec.
