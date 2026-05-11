# row-index-storage

## Problem Statement

MyLite can now persist table definitions in the primary `.mylite` file and
recover the previous valid catalog generation after a corrupted latest catalog
payload. Tables still behave as empty skeletons: `INSERT` is unsupported by the
base `handler` implementation, table scans always return EOF, and `UPDATE` or
`DELETE` cannot operate on stored rows.

This slice starts the row/index storage roadmap area by adding the first durable
heap row store behind the `MYLITE` handler. It should prove that MariaDB can
insert, scan, update, delete, and reopen simple MyLite rows through the normal
handler contract without `.frm` or engine sidecars.

## Scope

- Store simple in-row record images for DDL-created MyLite tables in the
  primary `.mylite` catalog payload.
- Assign each stored row a MyLite-owned hidden 64-bit row id.
- Implement `write_row()`, `rnd_init()`, `rnd_next()`, `rnd_pos()`,
  `position()`, `update_row()`, `delete_row()`, and `info()` for heap rows.
- Keep row storage under the existing recoverable catalog publication protocol.
- Persist row data across fresh embedded smoke processes.
- Preserve DDL table-definition persistence and recovery behavior from the
  previous slices.
- Add smoke coverage for:
  - insert/select,
  - update through a table scan,
  - delete through a table scan,
  - fresh-process row persistence,
  - recovery fallback preserving the previous row generation.

## Non-Goals

- Do not implement secondary indexes, primary-key lookup, range scans, or
  unique-key enforcement yet.
- Do not support BLOB/TEXT columns in the first row-image encoding.
- Do not implement a page allocator, B-tree, row compression, free-list
  compaction, undo, redo, WAL, or rollback journal.
- Do not implement transactional rollback or multi-statement atomicity.
- Do not implement cross-process writer locking.
- Do not claim durable autoincrement state yet.
- Do not expose public SQL execution APIs through `libmylite`.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/handler.h` defines storage-engine row mutation
  primitives as private virtual methods called through the public `ha_*`
  wrappers. The default `write_row()`, `update_row()`, and `delete_row()`
  implementations return `HA_ERR_WRONG_COMMAND`.
- `handler.h` requires every engine to implement `rnd_init()`, `rnd_next()`,
  `rnd_pos()`, `position()`, and `info()`. `rnd_next()` returns
  `HA_ERR_END_OF_FILE` at scan end.
- `handler.h` documents `HA_REC_NOT_IN_SEQ`: if table scans do not expose rows
  in simple record-offset order, filesort and related paths call `position()`
  to remember row positions.
- `storage/example/ha_example.cc` documents the basic handler method order:
  `write_row()` receives a native record buffer, `rnd_next()` fills a record
  buffer, `position()` stores engine-owned row position bytes in `ref`, and
  `rnd_pos()` reads a row back from that position.
- `storage/csv/ha_tina.cc` sets `ref_length`, stores positions with
  `my_store_ptr()`, retrieves them in `rnd_pos()`, and updates/deletes the row
  found by the previous scan call.
- `storage/heap/ha_heap.cc` calls `update_auto_increment()` from `write_row()`
  before writing when `table->next_number_field` is active. MyLite should not
  claim autoincrement until it also persists the next value and defines its
  recovery behavior.
- `sql/field.h` documents `Field::pack_length()` as the in-memory size used in
  a table record. Raw record images are a reasonable first bridge for fixed and
  inline variable fields, but BLOB/TEXT fields store pointer-bearing state and
  need a real field-aware row encoding before they can be durable.

## Proposed Design

Extend the catalog payload with row records while keeping the v1 header and
append-only publication protocol unchanged.

The logical payload keeps the existing table record:

```text
TABLE\t<hex-db>\t<hex-table>\t<hex-frm-image>
```

Add row state records:

```text
NEXTROWID\t<hex-db>\t<hex-table>\t<decimal-next-rowid>
ROW\t<hex-db>\t<hex-table>\t<decimal-rowid>\t<hex-record-image>
```

The first implementation stores complete MariaDB record images with
`table->s->reclength` bytes. This is intentionally a bridge format, not the
final row-page layout. It preserves MariaDB's current in-memory field packing
for simple non-BLOB rows and lets the handler prove the row lifecycle before a
larger page-store design lands.

Per table, keep:

- copied binary frm image,
- `next_rowid`, initialized to 1,
- vector of row records:
  - `rowid`,
  - deletion marker for in-process scans,
  - record image bytes.

Write policy:

1. `write_row()` rejects unsupported BLOB/TEXT, user-key, and autoincrement
   table definitions.
2. It copies `table->s->reclength` bytes from the supplied record buffer.
3. It assigns `next_rowid`, increments the counter, stores the row, and
   publishes a new catalog generation.

Read policy:

1. `rnd_init()` positions the handler's scan cursor at the first row.
2. `rnd_next()` skips deleted rows, verifies the stored image length matches
   the opened table's `reclength`, copies the image into the supplied record
   buffer, and remembers the current row id.
3. `position()` writes the current row id into `ref`.
4. `rnd_pos()` reads a row id from `pos` and copies that row image back.

Update/delete policy:

- `update_row()` updates the row id found by the last successful `rnd_next()`
  or `rnd_pos()` call, then publishes a new catalog generation.
- `delete_row()` marks that same row deleted in memory, removes it from the
  serialized payload by omitting deleted rows, and publishes a new catalog
  generation.

DDL behavior:

- `CREATE TABLE` initializes empty row state.
- `DROP TABLE` removes table rows with the table definition.
- `RENAME TABLE` moves row state with the table definition.
- Copy `ALTER TABLE` can copy rows through MariaDB's normal handler read/write
  path, but broad ALTER-with-data coverage is deferred until the simple DML
  smoke is stable.

## Affected Subsystems

- MyLite storage-engine handler methods and per-handler scan state.
- MyLite catalog payload serialization/parsing.
- Storage-engine smoke executable and wrapper script.
- Slice docs and roadmap.

No SQL parser, optimizer, `libmylite` public API, or CMake profile change is
required.

## DDL Metadata Routing Impact

DDL metadata routing remains frm-image based. The row store attaches row state
to the table definition already managed by the catalog. DDL that removes or
renames a definition must remove or move its row vector as part of the same
catalog mutation.

## Single-File And Embedded-Lifecycle Implications

Rows live inside the primary `.mylite` file by extending the catalog payload.
This keeps the first DML proof inside the existing single-file recovery path,
but it deliberately grows the whole-file catalog payload on every row mutation.
That is acceptable only for this first storage proof; a later page-store slice
must move rows and indexes out of the catalog payload.

Fresh-process tests remain required because in-process embedded restart is not
safe in the current MariaDB baseline.

## Public API Or File-Format Impact

The public C API does not change.

The internal payload format gains `NEXTROWID` and `ROW` records. The v1 file
header does not change. Future file-format migration must account for these
records or deliberately replace the v1 payload.

## Binary-Size Impact

Expected impact is first-party vector manipulation, record copying, and payload
parsing code only. No new dependency is allowed. Record measured artifacts after
implementation.

## License, Trademark, And Dependency Impact

No new dependency. New code remains GPL-2.0-only. No trademark or packaging
surface changes.

## Test And Verification Plan

- Run `tools/run-storage-engine-smoke.sh`.
- Extend the process-local DDL/DML phase to:
  - create a simple MyLite table,
  - insert at least two rows,
  - select them through a table scan,
  - update one row,
  - delete one row,
  - verify final count and values.
- Extend persistence phases to verify inserted rows survive a fresh embedded
  process.
- Extend recovery phases to verify corruption of the latest row payload falls
  back to the previous valid row generation.
- Verify no `.frm`, `.tmp` catalog sidecar, or dynamic plugin artifacts appear.
- Run `tools/run-libmylite-open-close-smoke.sh`.
- Run `tools/run-embedded-bootstrap-smoke.sh`.
- Run `bash -n` for changed shell scripts.
- Run `git diff --check`.

## Acceptance Criteria

- `INSERT`, table-scan `SELECT`, table-scan `UPDATE`, and table-scan `DELETE`
  work for simple non-BLOB MyLite tables.
- Stored rows are serialized in the primary `.mylite` file and reopened by a
  fresh embedded process.
- `position()` and `rnd_pos()` use stable hidden row ids for remembered rows.
- Recovery fallback keeps the previous valid row generation readable after the
  latest row payload is corrupted.
- DDL table definition persistence and recovery smokes still pass.
- Unsupported BLOB/TEXT, user-key, and autoincrement row storage fails
  explicitly rather than persisting pointer-bearing or unenforced record
  semantics.
- No `.frm` or dynamic plugin artifacts are introduced.

## Implementation Result

The MyLite storage engine now stores simple non-BLOB, keyless table rows in the
primary `.mylite` payload. The implementation adds:

- hidden 64-bit row ids per table,
- `NEXTROWID` and `ROW` payload records,
- `write_row()`, `update_row()`, `delete_row()`, `rnd_next()`, `rnd_pos()`,
  `position()`, and exact `info()` row counts,
- `HA_REC_NOT_IN_SEQ`, `HA_NO_TRANSACTIONS`, and exact-record statistics flags,
- explicit rejection of BLOB/TEXT, user-key, and autoincrement table shapes.

Rows are still raw MariaDB record images, not the final row-page format. They
remain protected by the existing v1 two-header catalog publication protocol.

Verification passed:

```sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
bash -n tools/run-storage-engine-smoke.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

Observed reports after implementation:

- `mylite-storage-engine-report.txt`: `row_count=2`, `row_notes=one,two`,
  `row_updated_note=deux`, `row_deleted_count=1`,
  `unsupported_blob=rejected`, `unsupported_key=rejected`,
  `unsupported_autoincrement=rejected`, no `.frm` artifacts.
- `mylite-catalog-read-report.txt`: `persisted_count=2`,
  `persisted_notes=seven,eight`, no `.frm` artifacts, no catalog sidecars.
- `mylite-catalog-recovery-read-report.txt`: `persisted_count=2`,
  `persisted_notes=seven,eight`, `recovery_marker=absent`, no `.frm`
  artifacts, no catalog sidecars.

Observed artifacts after this slice:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,293,306 bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 29,698 bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,688,656
  bytes.
- `build/mariadb-minsize/mylite/mylite-open-close-smoke`: 22,690,088 bytes.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,688,272
  bytes.
- `build/mariadb-minsize/mylite-catalog-persistence/catalog.mylite`: 11,957
  bytes.
- `build/mariadb-minsize/mylite-catalog-recovery/catalog.mylite`: 14,332
  bytes.

## Risks And Unresolved Questions

- Raw record images are not the final durable row format.
- There is no index structure, uniqueness enforcement, or range access yet.
- There is no autoincrement persistence yet.
- Row mutations rewrite the full logical payload, so performance is intentionally
  poor for large tables.
- There is no transaction rollback or cross-process writer lock.
