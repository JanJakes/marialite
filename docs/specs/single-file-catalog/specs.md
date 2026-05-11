# single-file-catalog

## Problem Statement

MyLite now routes DDL table definitions into a process-local engine catalog.
That proves the MariaDB handler boundary, but definitions disappear when the
process exits. The next slice should persist MyLite-owned table-definition
metadata in the primary `.mylite` file and prove a fresh embedded process can
rediscover a DDL-created table without a `.frm` file.

This is the first durable catalog slice, not the full file format, row store,
transaction manager, or recovery system.

## Scope

- Add a MyLite storage-engine startup option for the primary catalog file path.
- Pass the opened `.mylite` path from `libmylite` to the embedded runtime.
- Serialize frm-backed table definitions into the configured primary file.
- Load serialized table definitions lazily when the engine first needs the
  catalog.
- Keep the existing SQL seed table `mylite.probe` as code-owned test metadata.
- Preserve process-local behavior when no catalog file is configured, so the
  storage-engine smoke can still run isolated phases if needed.
- Add a persistence smoke that:
  - starts one embedded process with a catalog file,
  - creates a `MYLITE` table and leaves it in the catalog,
  - starts a fresh embedded process using the same catalog file,
  - discovers and scans that table without a `.frm` file.

## Non-Goals

- Do not implement row storage; persisted tables still scan as empty.
- Do not persist the seed `mylite.probe` table.
- Do not implement catalog transactions, rollback, WAL, or crash recovery.
- Do not remove the temporary compatibility datadir or schema directory.
- Do not implement schema DDL or a durable schema namespace.
- Do not normalize MariaDB frm images into a MyLite-native schema model.
- Do not add SQL execution APIs to `libmylite`.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- The `ddl-metadata-routing` slice stores DDL-created table definitions in
  `ha_mylite.cc` as copied binary frm images and reopens them with
  `TABLE_SHARE::init_from_binary_frm_image(..., write=false)`.
- `include/mysql/plugin.h` defines `MYSQL_SYSVAR_STR`, which exposes plugin
  string options as command-line server options.
- `storage/example/ha_example.cc` shows a storage-engine plugin declaring
  system variables in a `st_mysql_sys_var*` array and passing that array in
  `maria_declare_plugin()`.
- `mylite/mylite.cc:start_runtime()` builds the embedded server argument list
  before `mysql_server_init()`, and has the absolute primary `.mylite` path
  available as `runtime.filename`.
- `mylite` storage-engine discovery already avoids `.frm` files for both SQL
  seed definitions and frm-backed process-local definitions.
- A restart inside one process is not safe in the current embedded MariaDB
  baseline; previous `libmylite-open-close` work observed a segfault after
  `mysql_server_end()` followed by `mysql_server_init()`. Persistence must
  therefore be verified with separate smoke-process invocations.

## Proposed Design

Add a MyLite storage-engine system variable:

```text
--mylite-catalog-file=/absolute/path/to/database.mylite
```

`libmylite` should append that option to the embedded server arguments with
the exact primary path it opened. Storage-engine smokes can pass the same option
directly.

The engine catalog should remain one logical catalog implementation with two
backing modes:

- no configured catalog file: process-local definitions only,
- configured catalog file: load from the primary file and rewrite it after DDL
  catalog mutations.

Use a deliberately small v0 catalog encoding:

```text
MYLITE CATALOG 1\n
TABLE\t<hex-db>\t<hex-table>\t<hex-frm-image>\n
...
```

Hex keeps the parser simple and binary-safe without adding dependencies. The
format is explicitly an internal development format; later slices may replace
it with a page-based catalog while preserving the logical content.

Catalog write policy for this slice:

1. write the complete catalog to `<catalog-file>.tmp`,
2. flush and close the temporary file,
3. rename it over the primary catalog file.

This is not a complete recovery protocol, but it avoids partial in-place
metadata rewrites and gives the later recovery slice a clear replacement point.

Catalog load policy:

- empty or missing files mean an empty durable catalog,
- malformed files make discovery fail closed and log a diagnostic,
- seed SQL metadata is added independently and is never serialized.

DDL mutation behavior:

- `CREATE TABLE` stores the copied frm image and rewrites the catalog.
- copy `ALTER TABLE` stores the temporary handler-path entry and rewrites the
  catalog; later rename moves the durable entry.
- `RENAME TABLE` updates the entry key and rewrites the catalog.
- `DROP TABLE` removes the entry and rewrites the catalog.

## Affected Subsystems

- MyLite storage-engine catalog implementation.
- MyLite storage-engine plugin options.
- `libmylite` embedded runtime arguments.
- Storage-engine smoke executable and wrapper script.
- Slice docs and roadmap.

No public C API signature, row-storage, optimizer, parser, or SQL-layer fork is
required for this slice.

## DDL Metadata Routing Impact

The DDL-routing proof becomes durable for table definitions. DDL still does not
persist row data, indexes, autoincrement state, constraints beyond the frm
image, or schema namespace records as separate catalog objects.

## Single-File And Embedded-Lifecycle Implications

The primary `.mylite` file becomes non-empty after durable catalog DDL. It
stores table-definition metadata only. Runtime Aria logs and the temporary
schema directory remain compatibility artifacts under the controlled runtime
datadir and are not final product behavior.

Because the embedded runtime cannot currently restart safely in one process,
fresh-process persistence tests are required.

## Public API Or File-Format Impact

The public API does not change. The primary `.mylite` file receives an internal
development catalog format with magic/version text. This format is not stable
ABI and should be documented as replaceable before release.

## Binary-Size Impact

Expected impact is small first-party file parsing and serialization code.
Record measured artifacts after implementation.

## License, Trademark, And Dependency Impact

No new dependency. New code remains GPL-2.0-only.

## Test And Verification Plan

- Run `tools/run-storage-engine-smoke.sh`.
- Verify the existing DDL lifecycle still passes without `.frm` artifacts.
- Verify a persistence write phase leaves a DDL-created table in the configured
  catalog file.
- Verify a separate persistence read phase discovers that table from the same
  catalog file without a `.frm` file.
- Verify the read phase flushes table caches before assertions.
- Verify dynamic plugin artifacts remain absent.
- Run `tools/run-libmylite-open-close-smoke.sh`.
- Run `tools/run-embedded-bootstrap-smoke.sh`.
- Run `bash -n` for changed shell scripts.
- Run `git diff --check`.

## Acceptance Criteria

- `libmylite` passes the opened primary file path to the `MYLITE` storage
  engine as a catalog file option.
- DDL-created frm-backed table definitions are serialized into the configured
  primary file.
- A fresh embedded process can discover and scan a persisted `MYLITE` table
  without a `.frm` file.
- Existing process-local DDL routing behavior remains intact.
- The storage-engine smoke records no `.frm` or dynamic plugin artifacts.
- Existing embedded and `libmylite` smokes still pass.

## Risks And Unresolved Questions

- The v0 catalog format is not a final page layout and has no crash recovery.
- Atomic rename is not sufficient for full durability on every filesystem
  without directory fsync and recovery handling; the recovery slice must replace
  or extend this.
- The catalog file currently stores table definitions only, not row/index pages.
- Schema-directory compatibility remains unresolved.
- There is still no public SQL execution API to drive durable catalog DDL
  through `libmylite`; storage-engine smokes cover the engine path for now.
