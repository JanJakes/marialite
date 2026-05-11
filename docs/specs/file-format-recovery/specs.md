# file-format-recovery

## Problem Statement

The primary `.mylite` file now stores frm-backed table definitions, but the
current v0 catalog is a text/hex file rewritten through a temporary sidecar and
rename. That proved persistence across fresh embedded processes, but it is not
a MyLite file format and it does not give an in-file recovery point.

This slice should replace the v0 catalog rewrite with the first recoverable
primary-file layout: fixed file headers, validated catalog payloads, and an
update protocol that can ignore a partial or torn latest catalog write and keep
using the previous valid catalog.

## Scope

- Add a v1 primary-file header layout for `.mylite` catalog metadata.
- Store the existing logical catalog payload as an append-only blob referenced
  by one of two fixed headers.
- Validate headers and catalog payloads with checksums before loading them.
- Publish catalog updates by writing a new payload first, then atomically
  publishing the inactive header with a higher generation.
- Keep the existing frm-backed table-definition content and DDL routing
  behavior.
- Add smoke coverage that proves a corrupted latest header or payload does not
  hide the previous valid catalog.
- Keep empty or missing primary files valid empty catalogs.
- Record measured binary-size and file-size impact after implementation.

## Non-Goals

- Do not implement row storage, indexes, transaction pages, undo, redo, WAL, or
  rollback journals.
- Do not compact old append-only catalog payloads.
- Do not claim full database crash recovery beyond catalog publication.
- Do not implement cross-process writer locking.
- Do not remove temporary compatibility datadir or schema-directory artifacts.
- Do not stabilize the file format as a public compatibility promise.
- Do not replace MariaDB binary frm images with a normalized schema model.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `docs/architecture/single-file-storage.md` requires an explicit primary-file
  header with magic bytes, format version, page size, checksum mode, catalog
  root/recovery pointers, and no torn catalog updates before user data is
  stored.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc` currently serializes the
  catalog as:

  ```text
  MYLITE CATALOG 1
  TABLE\t<hex-db>\t<hex-table>\t<hex-frm-image>
  ```

  and rewrites the whole file via `<catalog-file>.tmp`, `fsync()`, and
  `rename()`.
- `vendor/mariadb/server/sql/handler.cc:ha_create_table()` sets
  `write_frm_now` to false when the storage engine implements
  `discover_table`, then passes the generated frm image through
  `TABLE_SHARE::init_from_binary_frm_image()`.
- `vendor/mariadb/server/sql/table.cc:TABLE_SHARE::init_from_binary_frm_image()`
  loads a binary frm image into a `TABLE_SHARE`; when called with `write=false`
  it does not write a `.frm` file. MyLite should keep using this path for this
  slice.
- `vendor/mariadb/server/include/my_sys.h` exposes MariaDB `my_checksum()`,
  `my_pread()`, `my_pwrite()`, and `my_sync()`, but the current MyLite engine
  catalog code already uses direct POSIX file descriptors. A local checksum and
  positional I/O helper keeps this first-party file-format code dependency-light
  and isolated from upstream instrumentation choices.
- The `single-file-catalog` slice verified persistence with separate embedded
  smoke processes because the current inherited embedded runtime cannot safely
  restart in one process after `mysql_server_end()`.

## Proposed Design

Use a two-header, append-only catalog layout:

```text
offset 0      header slot 0, 4096 bytes
offset 4096   header slot 1, 4096 bytes
offset 8192+  append-only catalog payload blobs
```

Each header slot is exactly one 4096-byte page. Header fields are encoded
little-endian:

```text
bytes  0..15   magic: "MYLITEFMTCAT1\0\0\0"
bytes 16..19   format_version: 1
bytes 20..23   page_size: 4096
bytes 24..31   generation
bytes 32..39   catalog_payload_offset
bytes 40..47   catalog_payload_length
bytes 48..55   catalog_payload_checksum
bytes 56..63   header_checksum
bytes 64..4095 reserved, zero for now
```

The payload remains the current logical catalog text/hex encoding for this
slice. Keeping the payload stable reduces risk in the MariaDB discovery path;
the meaningful change is how a payload is published and recovered.

Header validation:

- magic, version, page size, and nonzero generation must match expectations,
- payload offset must be at or after byte 8192,
- payload length must be nonzero and contained in the file,
- header checksum must match the header page with the checksum field zeroed,
- payload checksum must match the referenced payload bytes.

Load policy:

1. Empty or missing file means an empty catalog.
2. Read and validate both header slots independently.
3. Choose the valid header with the highest generation.
4. Read and parse the referenced payload.
5. If neither header is valid and the file is non-empty, fail closed and report
   a catalog diagnostic.

Write policy:

1. Serialize the current logical catalog payload.
2. Open the primary file for read/write/create.
3. Read existing headers to identify the active generation and inactive slot.
4. Append the payload at EOF, never before byte 8192.
5. Flush the payload with `fsync()`.
6. Write the inactive header with `generation + 1`.
7. Flush the header with `fsync()`.

If a process or OS crash occurs before the new header is fully written and
flushed, the previous header still references the previous valid payload. If a
header is written but points at an incomplete or corrupted payload, the payload
checksum rejects it and the reader chooses the other valid header.

The first checksum should be a small first-party FNV-1a 64-bit checksum. This
is not a cryptographic integrity check; it is a dependency-free torn-write and
misdirected-pointer detector for the catalog publication protocol.

## Affected Subsystems

- MyLite storage-engine catalog file load/write code.
- Storage-engine smoke executable and wrapper script.
- Single-file storage design documentation.
- Roadmap and slice spec documentation.

No public C API signature, SQL parser, optimizer, row-storage handler method,
or MariaDB table-definition format change is required.

## DDL Metadata Routing Impact

DDL metadata routing remains the same logically. `CREATE`, copy `ALTER`,
`RENAME`, and `DROP` still mutate the MyLite engine catalog, but each mutation
publishes a new recoverable catalog payload instead of replacing the whole
primary file through a sidecar.

The stored table-definition image is still the MariaDB-generated binary frm
image. Discovery still uses `TABLE_SHARE::init_from_binary_frm_image(...,
write=false)`.

## Single-File And Embedded-Lifecycle Implications

This slice removes the persistent `<catalog-file>.tmp` rewrite sidecar from the
catalog write path. The primary `.mylite` file becomes the sole durable catalog
artifact for the implemented behavior. Runtime datadir, Aria, and schema
directory artifacts remain compatibility scaffolding under controlled temporary
paths and are not expanded by this work.

Catalog recovery must be tested with fresh smoke processes because in-process
MariaDB embedded restart remains unsafe in the current baseline.

## Public API Or File-Format Impact

The public C API does not change.

The primary `.mylite` file receives the first binary v1 file header. This is an
internal development format, but it is a real format boundary: future slices
must either read it, migrate it deliberately, or update the format version.

## Binary-Size Impact

Expected impact is small first-party helper code for fixed-width encoding,
checksums, positional I/O, and validation. No new dependency is allowed for this
slice. Record measured artifacts after implementation.

## License, Trademark, And Dependency Impact

No new dependency. New code remains GPL-2.0-only. No trademark or packaging
surface changes.

## Test And Verification Plan

- Run `tools/run-storage-engine-smoke.sh`.
- Verify normal DDL lifecycle still leaves no `.frm` artifacts.
- Verify catalog write/read persistence still discovers `mylite.persisted`
  across separate embedded processes.
- Add a recovery smoke phase that:
  - creates `mylite.persisted` and publishes a valid catalog generation,
  - publishes a second generation with a marker table,
  - corrupts the latest header or payload outside the process,
  - starts a fresh embedded process and verifies the previous generation is
    still readable.
- Verify the recovery smoke records no `.tmp` catalog sidecar.
- Run `tools/run-libmylite-open-close-smoke.sh`.
- Run `tools/run-embedded-bootstrap-smoke.sh`.
- Run `bash -n` for changed shell scripts.
- Run `git diff --check`.

## Acceptance Criteria

- The primary `.mylite` file has two fixed v1 header slots and append-only
  catalog payload blobs.
- Catalog load chooses the highest generation whose header and payload checksum
  validate.
- An invalid latest header or payload falls back to the previous valid
  generation.
- Malformed non-empty files with no valid header fail closed.
- Existing DDL metadata routing and fresh-process persistence smokes pass.
- Catalog writes do not create a persistent `<catalog-file>.tmp` sidecar.
- No `.frm` or dynamic plugin artifacts are introduced by the storage smoke.

## Implementation Result

The MyLite storage engine now writes catalog metadata through a v1 primary-file
layout:

- header slot 0 at offset 0,
- header slot 1 at offset 4096,
- append-only catalog payload blobs beginning at offset 8192.

Each header records the format version, page size, generation, payload offset,
payload length, payload checksum, and header checksum. Catalog loading validates
both headers independently, validates the referenced payload bytes, and chooses
the highest valid generation. Catalog writes append a new payload, `fsync()` it,
then publish the inactive header with the next generation and `fsync()` again.

The payload remains the existing logical text/hex catalog for frm-backed table
definitions. This keeps DDL metadata routing and MariaDB discovery behavior
unchanged while making catalog publication recoverable.

The storage-engine smoke now runs recovery phases:

- `recovery-base` publishes a valid catalog containing `mylite.persisted`,
- `recovery-latest` publishes a later generation containing
  `mylite.recovery_marker`,
- the wrapper corrupts the latest payload at its append offset,
- `recovery-read` starts a fresh embedded process and verifies
  `mylite.persisted` remains readable while `mylite.recovery_marker` is absent.

Verification passed:

```sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
bash -n tools/run-storage-engine-smoke.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

The first attempted concurrent run of the open/close and embedded smokes raced
inside CMake regeneration of the shared build directory. Rerunning the smokes
serially passed; this was a test-invocation race, not a MyLite runtime failure.

Observed reports after implementation:

- `mylite-catalog-read-report.txt`: `status=0`, `persisted_count=0`,
  `persisted_column=note`, no `.frm` artifacts, no catalog sidecars.
- `mylite-catalog-recovery-latest-report.txt`: `status=0`,
  `recovery_marker=present`, no `.frm` artifacts, no catalog sidecars.
- `mylite-catalog-recovery-read-report.txt`: `status=0`,
  `persisted_count=0`, `persisted_column=note`, `recovery_marker=absent`, no
  `.frm` artifacts, no catalog sidecars.

Observed artifacts after this slice:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,273,304 bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 29,698 bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,686,240
  bytes.
- `build/mariadb-minsize/mylite/mylite-open-close-smoke`: 22,688,088 bytes.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,686,000
  bytes.
- `build/mariadb-minsize/mylite-catalog-persistence/catalog.mylite`: 9,256
  bytes.
- `build/mariadb-minsize/mylite-catalog-recovery/catalog.mylite`: 11,237
  bytes.

## Risks And Unresolved Questions

- This is catalog recovery only; row data and indexes still do not exist.
- Old append-only payloads are not compacted, so repeated DDL will grow the file
  until a later checkpoint/compaction slice.
- FNV-1a is sufficient for accidental corruption detection in this slice but
  may be replaced by a stronger page checksum when the pager is designed.
- There is still no cross-process writer lock.
- A future WAL or rollback-journal slice must define transaction durability for
  rows and indexes rather than relying on header flipping alone.
