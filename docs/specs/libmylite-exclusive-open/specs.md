# libmylite-exclusive-open

## Problem Statement

The public header defines `MYLITE_OPEN_EXCLUSIVE`, but
`mylite_open_v2()` still rejects it as an unsupported flag. Applications need
a stable way to create a new `.mylite` primary file and fail if that path
already exists, matching the create-or-fail behavior expected from an
exclusive open flag.

This slice implements the narrow first semantics for `MYLITE_OPEN_EXCLUSIVE`.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/mylite/include/mylite.h` defines
  `MYLITE_OPEN_EXCLUSIVE` as part of the public open flag set.
- `vendor/mariadb/server/mylite/mylite.cc:167` routes `mylite_open()` through
  `mylite_open_v2()` with `MYLITE_OPEN_READWRITE | MYLITE_OPEN_CREATE`, so
  default opens must remain non-exclusive.
- `vendor/mariadb/server/mylite/mylite.cc:781` validates open flags and
  currently excludes `MYLITE_OPEN_EXCLUSIVE` from the supported mask.
- `vendor/mariadb/server/mylite/mylite.cc:834` prepares the primary file with
  POSIX `open()`. This is the right layer to add `O_EXCL` when the caller
  requested exclusive create.
- `vendor/mariadb/server/mylite/mylite.cc:193` enforces the current
  process-global runtime path constraint before file preflight. Exclusive opens
  should keep that constraint and not imply multi-path runtime support.

## Scope

This slice will:

- accept `MYLITE_OPEN_EXCLUSIVE` only with
  `MYLITE_OPEN_READWRITE | MYLITE_OPEN_CREATE`,
- reject `MYLITE_OPEN_EXCLUSIVE` with read-only opens or without create,
- add `O_EXCL` to the primary-file preflight open when exclusive create is
  requested,
- keep `mylite_open()` default behavior unchanged,
- extend the `libmylite` smoke with a fresh-process exclusive-open pass that
  proves:
  - exclusive create succeeds for a missing primary file,
  - the created file exists after close,
  - a second exclusive create for the same existing path returns
    `MYLITE_CANTOPEN`,
  - invalid exclusive flag combinations return `MYLITE_MISUSE`.

## Non-Goals

- Do not implement `MYLITE_OPEN_URI`.
- Do not implement multi-path runtime support.
- Do not change storage-engine catalog locking or read-only mode.
- Do not add a new public API function.
- Do not make exclusive create a lock or concurrency claim. It only controls
  path creation.

## Proposed Design

`validate_open_inputs()` will include `MYLITE_OPEN_EXCLUSIVE` in the supported
mask and require:

```c++
exclusive_open -> readwrite && create && !readonly
```

`prepare_primary_file()` will add `O_EXCL` only when
`MYLITE_OPEN_EXCLUSIVE` is set. Existing behavior remains:

- read-only opens use `O_RDONLY` and never create,
- read-write opens use `O_RDWR`,
- create opens add `O_CREAT`,
- exclusive create opens add `O_EXCL`.

The resulting error remains `MYLITE_CANTOPEN` with a handle-owned diagnostic
when the file already exists, because the failure is an open/preflight failure
before a MariaDB connection exists.

## Affected Subsystems

- MyLite C API open flag validation and primary-file preflight.
- Open/close smoke executable and wrapper script.
- API docs, roadmap, and this slice spec.

## DDL Metadata Routing Impact

None. This slice changes only primary-file open preflight behavior. Once the
exclusive file exists and the runtime starts, DDL metadata routing behaves as
it does for normal read-write opens.

## Single-File And Embedded-Lifecycle Implications

Exclusive create is a primary-file lifecycle rule. It prevents accidentally
opening an existing primary `.mylite` file for a call that asked to create a
new one. It does not change the current process-scoped embedded runtime
constraint, runtime directory side effects, or storage-engine advisory locks.

## Public API Or File-Format Impact

No new symbols and no file-format version change. The existing public
`MYLITE_OPEN_EXCLUSIVE` flag becomes supported for the documented first
combination.

## Binary-Size Impact

Expected growth is negligible: validation, one `O_EXCL` branch, and smoke
coverage. The post-implementation `MinSizeRel` artifact sizes will be recorded
after verification.

## License, Trademark, And Dependency Impact

No new dependency. All changes remain in existing GPL-2.0-only MyLite source.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/open_close_smoke.cc` with a
  `--mode=exclusive` run.
- Extend `tools/run-libmylite-open-close-smoke.sh` to run that fresh-process
  mode against a separate primary file.
- Verify exclusive create success on a missing file and failure on an existing
  file.
- Verify invalid exclusive combinations return `MYLITE_MISUSE`.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` for changed shell scripts
  - `git diff --check`

## Acceptance Criteria

- `MYLITE_OPEN_EXCLUSIVE` is accepted only with read-write create.
- Exclusive create succeeds when the primary path does not exist.
- Exclusive create fails with `MYLITE_CANTOPEN` when the primary path already
  exists.
- Invalid exclusive flag combinations fail with `MYLITE_MISUSE`.
- Default `mylite_open()` and non-exclusive `mylite_open_v2()` behavior remain
  unchanged.
- Docs and smoke reports describe the exclusive-open behavior.

## Risks And Unresolved Questions

- The exact diagnostic message for an existing path comes from `open()` and is
  platform-specific. Tests should assert the result code and that a diagnostic
  exists, not an exact `strerror()` string.
- This does not reserve a future path across processes beyond the atomic file
  create operation. Advisory locking remains the storage-engine concern after
  runtime startup.

## Implementation Result

Pending.
