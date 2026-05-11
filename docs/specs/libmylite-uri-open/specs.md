# libmylite-uri-open

## Problem Statement

The public header defines `MYLITE_OPEN_URI`, but `mylite_open_v2()` still
rejects it as an unsupported open flag. Applications should be able to pass a
`file:` URI when they opt into URI interpretation, while existing callers that
pass ordinary filesystem paths should keep the current behavior.

This slice implements a narrow, explicit first URI surface for local files.

## Scope

- Accept `MYLITE_OPEN_URI` in `mylite_open_v2()`.
- Interpret strings beginning with `file:` as local file URIs only when
  `MYLITE_OPEN_URI` is present.
- Keep non-URI strings under `MYLITE_OPEN_URI` as ordinary paths for caller
  convenience.
- Support URI path percent-decoding for `%XX` bytes.
- Accept empty authority and `localhost` authority:
  - `file:/tmp/db.mylite`
  - `file:///tmp/db.mylite`
  - `file://localhost/tmp/db.mylite`
  - `file:relative/path.mylite`
- Reject non-local authorities.
- Support one query parameter, `mode`, with values:
  - `mode=ro` -> read-only,
  - `mode=rw` -> read-write without create,
  - `mode=rwc` -> read-write with create.
- Allow callers to omit read/write/create bits only when URI `mode` supplies
  them.
- When both flags and URI `mode` are supplied, require them to agree.
- Keep `MYLITE_OPEN_EXCLUSIVE` as a flag-only create-or-fail modifier; URI
  `mode=rwc` may be combined with it, but URI mode itself does not imply
  exclusive create.
- Reject unsupported query parameters, duplicate `mode`, malformed percent
  encodings, fragments, and unsupported mode values.
- Extend the `libmylite` smoke with a fresh-process URI mode.

## Non-Goals

- Do not support remote authorities, sockets, network paths, or daemon
  connections.
- Do not support `mode=memory`, VFS parameters, cache parameters, immutable
  flags, or SQLite's full URI surface.
- Do not change ordinary path handling when `MYLITE_OPEN_URI` is absent.
- Do not add a new public function.
- Do not change storage-engine locking, read-only enforcement, or exclusive
  open behavior beyond passing resolved flags into existing code.
- Do not add a production bundle-size analysis; record sizes only as normal
  slice evidence.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/mylite/include/mylite.h` defines
  `MYLITE_OPEN_URI` as `0x00000010u`.
- `vendor/mariadb/server/mylite/mylite.cc:mylite_open_v2()` currently calls
  `validate_open_inputs()` before resolving the path. That validation excludes
  `MYLITE_OPEN_URI`, so URI callers receive `MYLITE_MISUSE`.
- `validate_open_inputs()` also requires exactly one of
  `MYLITE_OPEN_READONLY` and `MYLITE_OPEN_READWRITE`, and handles
  `MYLITE_OPEN_CREATE` and `MYLITE_OPEN_EXCLUSIVE` compatibility.
- `make_absolute_path()` currently accepts only ordinary paths and resolves
  relative names against the current working directory.
- `prepare_primary_file()` already enforces the final read-only, create, and
  exclusive flags through POSIX `open()`.
- `open_close_smoke.cc` already has fresh-process modes for default,
  read-only, and exclusive open behavior. URI open can follow the same pattern
  without changing storage-engine tests.

## Proposed Design

Add a small MyLite-owned URI parser in `mylite.cc`, before open-input
validation:

```c++
bool resolve_open_filename_and_flags(
    const char *filename,
    unsigned input_flags,
    std::string *resolved_filename,
    unsigned *resolved_flags,
    std::string *message);
```

Behavior:

1. If `MYLITE_OPEN_URI` is not present, copy the original filename and flags
   unchanged.
2. If `MYLITE_OPEN_URI` is present but the filename does not start with
   `file:`, copy the original filename and clear only the URI flag before
   normal validation.
3. If the filename starts with `file:`, parse local authority, path, and query.
4. Percent-decode path and query parameter names/values. Invalid `%` escapes
   fail with `MYLITE_MISUSE`.
5. Reject fragments and unknown query parameters.
6. Translate `mode=ro|rw|rwc` into access flags when the caller omitted access
   bits. If access bits were supplied, require exact agreement.
7. Clear `MYLITE_OPEN_URI` before calling existing validation and open
   preflight.

Examples:

```c
mylite_open_v2("file:///tmp/app.mylite", &db,
               MYLITE_OPEN_URI | MYLITE_OPEN_READWRITE | MYLITE_OPEN_CREATE,
               NULL);

mylite_open_v2("file:relative.mylite?mode=rwc", &db,
               MYLITE_OPEN_URI,
               NULL);

mylite_open_v2("file:///tmp/app.mylite?mode=ro", &db,
               MYLITE_OPEN_URI,
               NULL);
```

The resolved local path then flows through `make_absolute_path()` unchanged, so
relative URI paths stay relative to the current process directory.

## Affected Subsystems

- `libmylite` open flag validation and filename resolution.
- Open/close smoke executable and wrapper script.
- Public API docs, roadmap, and this slice spec.

## DDL Metadata Routing Impact

None. URI parsing resolves a primary-file path and flags before the runtime
starts. All SQL and storage-engine behavior after open remains unchanged.

## Single-File And Embedded-Lifecycle Implications

URI support must not create new sidecars or change the runtime directory
lifecycle. A URI-resolved path still maps to one primary `.mylite` file and the
existing process-scoped runtime constraints.

## Public API Or File-Format Impact

No new symbol and no file-format change. The existing public `MYLITE_OPEN_URI`
flag becomes supported for local `file:` URIs.

`mylite_open_v2()` continues to return `MYLITE_MISUSE` for invalid flag
combinations, malformed URIs, unsupported URI query parameters, or incompatible
URI mode and flag combinations.

## Binary-Size Impact

Expected size growth is small: a local parser, percent decoder, and smoke
coverage. Record measured artifact sizes after implementation.

## License, Trademark, And Dependency Impact

No new dependency. All changes remain in existing GPL-2.0-only MyLite source.

## Test And Verification Plan

- Extend `open_close_smoke.cc` with fresh-process `--mode=uri` and
  `--mode=uri-readonly` runs.
- Extend `tools/run-libmylite-open-close-smoke.sh` to run both URI modes.
- Verify:
  - `file:` URI with `mode=rwc` creates and opens a primary file whose path
    contains percent-encoded bytes,
  - `mode=rw` opens the URI-created primary file without create,
  - `file://localhost/...` resolves as a local URI authority,
  - percent-decoded path bytes are honored,
  - `mode=ro` opens an existing primary file read-only,
  - non-URI strings with `MYLITE_OPEN_URI` still open as paths,
  - non-local authority, malformed percent escapes, unknown parameters,
    duplicate mode, unsupported mode, and mode/flag mismatch return
    `MYLITE_MISUSE`,
  - existing default, read-only, and exclusive open smokes still pass.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `bash -n tools/run-libmylite-open-close-smoke.sh
    tools/run-compatibility-test-harness.sh`
  - `git diff --check`

## Acceptance Criteria

- `MYLITE_OPEN_URI` is accepted for local `file:` URIs.
- URI mode can provide access flags when flags omit them.
- URI mode conflicts with explicit flags fail with `MYLITE_MISUSE`.
- Malformed or unsupported URI surfaces fail with `MYLITE_MISUSE`.
- Existing ordinary path behavior remains unchanged.
- `libmylite` and grouped compatibility smokes pass.

## Risks And Unresolved Questions

- URI parsing can expand if callers expect SQLite's full URI behavior. This
  slice intentionally supports only local `file:` URIs and rejects unknown
  query parameters so unsupported behavior is explicit.
- Windows drive-letter URI rules need a later platform-specific pass. The
  current reproducible build and smoke evidence remain Linux-container based.

## Implementation Result

`mylite_open_v2()` now resolves `MYLITE_OPEN_URI` before normal open validation.
Local `file:` URIs are percent-decoded, empty and `localhost` authorities are
accepted, `mode=ro|rw|rwc` can supply access flags, and URI mode conflicts with
explicit flags return `MYLITE_MISUSE`. Unsupported query parameters, duplicate
mode, unsupported mode values, remote authorities, fragments, and malformed
percent escapes also return `MYLITE_MISUSE`.

The `libmylite` smoke now runs two URI-specific fresh-process modes:

- `--mode=uri` verifies URI create with a percent-decoded path containing a
  space, unsupported URI errors, and non-URI strings under `MYLITE_OPEN_URI`.
- `--mode=uri-readonly` verifies `mode=ro` can read the URI-created database
  and rejects writes with `MYLITE_READONLY`.

Report evidence:

- `libmylite-open-close-uri-report.txt`: `status=0`,
  `uri_rows=1:one,2:two`, `mode=rw` and `localhost` URI cases pass, and all
  URI misuse cases pass.
- `libmylite-open-close-uri-readonly-report.txt`: `status=0`,
  `uri_readonly_rows=1:one,2:two`, and
  `uri_readonly_insert_rejected` returns `MYLITE_READONLY`.
- `mylite-compatibility-harness-report.txt`: `libmylite_lifecycle` and
  `sidecar_scan` report `status=0`; sidecar scan reports no unexpected or
  known inherited sidecars.

Verification completed:

- `bash -n tools/run-libmylite-open-close-smoke.sh
  tools/run-compatibility-test-harness.sh`.
- `git diff --check`.
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`.
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`.

Measured artifacts after implementation:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 43,405,432 bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 93,752 bytes.
- `build/mariadb-minsize/storage/mylite/libmylite_embedded.a`: 303,480 bytes.
- `build/mariadb-minsize/mylite/mylite-open-close-smoke`: 22,325,488 bytes.
- `build/mariadb-minsize/mylite/mylite-compatibility-smoke`: 22,248,248
  bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,314,136
  bytes.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,247,368
  bytes.
