# index-autoincrement-storage

## Problem Statement

MyLite can now store simple non-BLOB, keyless heap rows in the primary
`.mylite` file. That proves the row lifecycle, but it still rejects any user
key or autoincrement column. MariaDB's optimizer and DML paths expect storage
engines to expose indexed lookup/navigation methods, enforce unique keys, and
provide durable autoincrement behavior for generated values.

This slice adds the first enforceable key path and table-local autoincrement
state while keeping the existing raw-record bridge format. It is not the final
B-tree or pager design.

## Scope

- Accept ordinary non-BLOB MyLite tables with supported user keys.
- Support key definitions whose user key parts are:
  - backed by stored non-BLOB fields,
  - non-nullable,
  - not reverse-sorted,
  - using `HA_KEY_ALG_UNDEF` or `HA_KEY_ALG_BTREE`.
- Continue to reject unsupported key shapes explicitly, including nullable key
  parts, BLOB/TEXT key parts, reverse-sorted key parts, fulltext/spatial/vector
  algorithms, and autoincrement that is not the first key part.
- Enforce `HA_NOSAME` primary and unique keys on insert and update.
- Implement basic ordered index access through `index_read_map()`,
  `index_next()`, `index_prev()`, `index_first()`, `index_last()`, and
  `records_in_range()`.
- Store durable table autoincrement state in the `.mylite` catalog payload.
- Preserve fresh-process row persistence and recovery fallback behavior.
- Add smoke coverage for primary-key lookup, ordered index navigation, unique
  duplicate rejection, update conflict rejection, and autoincrement reopen.

## Non-Goals

- Do not add durable index pages or a B-tree allocator.
- Do not add transactional rollback, undo, redo, WAL, or cross-process writer
  locking.
- Do not support nullable unique-key semantics yet.
- Do not support BLOB/TEXT columns or pointer-bearing record images.
- Do not support fulltext, spatial, vector, hash-only, descending, generated,
  invisible, or expression indexes.
- Do not support foreign keys.
- Do not expose public SQL execution APIs through `libmylite`.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/handler.h` defines the index capability flags
  returned by `index_flags()`: `HA_READ_NEXT`, `HA_READ_PREV`,
  `HA_READ_ORDER`, `HA_READ_RANGE`, `HA_ONLY_WHOLE_INDEX`,
  `HA_KEYREAD_ONLY`, and related flags.
- `handler.h` declares the engine methods MariaDB calls for indexed access:
  `index_read_map()`, `index_read_idx_map()`, `index_next()`,
  `index_prev()`, `index_first()`, `index_last()`, and
  `records_in_range()`.
- `vendor/mariadb/server/sql/handler.cc` routes public `ha_index_*` calls
  through those virtual methods, updates table status, and uses
  `read_range_first()` / `read_range_next()` to map range scans to
  `index_read_map()` plus `index_next()`.
- `handler.cc` implements `handler::update_auto_increment()`. Engines normally
  call it from `write_row()` before storing `table->record[0]`; it fills
  `table->next_number_field`, manages statement-local reserved intervals, and
  delegates first-value selection to `get_auto_increment()`.
- The default `handler::get_auto_increment()` computes the next value by
  reading the last row from the autoincrement key. MyLite needs a persisted
  table-local counter instead, because deleting all rows must not make a fresh
  process reuse old generated values.
- `vendor/mariadb/server/sql/key.h` and `sql/key.cc` expose reusable key-image
  helpers. `key_copy()` extracts a MariaDB key tuple from a record buffer, and
  `key_tuple_cmp()` compares two key-image buffers using the key part's field
  comparison semantics.
- `vendor/mariadb/server/storage/heap/ha_heap.cc` is a compact reference for
  handler method shape: `write_row()` calls `update_auto_increment()`,
  index methods delegate to the engine's key accessors, and create-time code
  maps MariaDB `KEY` / `KEY_PART_INFO` metadata into engine-private key
  definitions.
- `vendor/mariadb/server/include/my_base.h` defines key algorithms,
  `ha_rkey_function`, `key_range`, `HA_NOSAME`, `HA_NULL_PART_KEY`,
  `HA_AUTO_KEY`, and key-part flags such as `HA_BLOB_PART` and
  `HA_VAR_LENGTH_PART`.
- Current MyLite row storage rejects `table->s->keys != 0` and
  `table->next_number_field`; those gates must become a deliberate key-shape
  validator rather than a blanket rejection.

## Proposed Design

Keep storing table definitions and rows in the existing v1 catalog payload.
Add an autoincrement state record per table:

```text
AUTOINC\t<hex-db>\t<hex-table>\t<decimal-next-value>
```

`AUTOINC` is emitted for every persisted table. Tables without an
autoincrement column store `0`. Tables with autoincrement store the next value
to reserve. During load, `AUTOINC` must follow its owning `TABLE` record; rows
can still raise the counter if a future payload lacks or underreports it.

### Key Shape Validation

Replace the current keyless-only check with a validator:

1. Reject BLOB/TEXT row storage as before.
2. For each user key, accept only `HA_KEY_ALG_UNDEF` and `HA_KEY_ALG_BTREE`.
3. Reject nullable key parts for this slice.
4. Reject `HA_BLOB_PART`, reverse sort, fulltext, spatial, vector, expression,
   generated, and hash-only key shapes.
5. Accept autoincrement only when MariaDB reports it as the first part of the
   next-number key.

Unsupported shapes return `HA_ERR_UNSUPPORTED` at `CREATE TABLE` rather than
silently creating unenforced metadata.

### In-Memory Index Model

Do not persist separate index pages yet. Rebuild key images from durable row
records when a handler operation needs them:

- use `key_copy()` to build a key tuple from a row record,
- use `key_tuple_cmp()` to compare key tuples,
- sort by key tuple and then hidden row id for stable ordered scans,
- keep the index cursor in the handler instance as row ids plus a cursor
  offset.

This is intentionally O(n log n) for cursor build and O(n) for duplicate
checks. It is correct for small smoke coverage and keeps this slice from
designing a throwaway B-tree before the pager slice.

### Unique Enforcement

Before inserting a row:

1. If `table->next_number_field` is active for `table->record[0]`, call
   `update_auto_increment()` so the row image contains the generated value.
2. For each `HA_NOSAME` key, compare the new row's key tuple against existing
   live rows.
3. If a duplicate exists, return `HA_ERR_FOUND_DUPP_KEY`.
4. Store the row and advance the persisted autoincrement counter if the
   autoincrement field value is greater than or equal to the current next
   value.

Before updating a row:

1. Compare the new row's unique-key tuples against all live rows except the row
   being updated.
2. Reject conflicts with `HA_ERR_FOUND_DUPP_KEY`.
3. Store the new record image and advance autoincrement state if the
   autoincrement value was explicitly raised.

Deletes do not reduce the autoincrement counter.

### Index Access

`index_read_map()` builds an ordered cursor for `active_index` and positions it
according to the requested `ha_rkey_function`:

- `HA_READ_KEY_EXACT`,
- `HA_READ_KEY_OR_NEXT`,
- `HA_READ_KEY_OR_PREV`,
- `HA_READ_AFTER_KEY`,
- `HA_READ_BEFORE_KEY`,
- complete-prefix `HA_READ_PREFIX` and `HA_READ_PREFIX_LAST` where MariaDB
  supplies complete key parts.

`index_next()`, `index_prev()`, `index_first()`, and `index_last()` move within
that cursor and copy the selected record image into the supplied buffer.
`records_in_range()` returns exact counts for supported key ranges by walking
the live rows and comparing key tuples; it may return `HA_POS_ERROR` for
unsupported range shapes.

`index_flags()` should advertise only implemented behavior:

- `HA_READ_NEXT`,
- `HA_READ_PREV`,
- `HA_READ_ORDER`,
- `HA_READ_RANGE`.

Do not advertise `HA_KEYREAD_ONLY` because MyLite returns full row images from
the bridge row store.

### Autoincrement

Add table-local `auto_increment_next` state:

- initialized from `HA_CREATE_INFO::auto_increment_value` when present,
  otherwise `1` for autoincrement tables and `0` for non-autoincrement tables,
- returned from `get_auto_increment()` with at least the requested reservation
  size,
- advanced after a successful insert or update that stores a larger explicit
  value,
- persisted in `AUTOINC` records,
- exposed through `update_create_info()` for `SHOW CREATE TABLE` / DDL paths
  that ask the engine for the next value,
- reset by `reset_auto_increment()`.

This is not transactional yet. The implementation must document any generated
value gaps caused by failed later rows in a statement.

## Affected Subsystems

- MyLite handler implementation:
  `vendor/mariadb/server/storage/mylite/ha_mylite.cc` and `.h`.
- MyLite storage-engine smoke:
  `vendor/mariadb/server/mylite/storage_engine_smoke.cc`.
- Catalog payload parsing and serialization.
- Roadmap and single-file storage documentation.

## DDL Metadata Routing Impact

This slice broadens accepted `CREATE TABLE ... ENGINE=MYLITE` definitions from
keyless tables to supported keyed tables. The existing frm-image storage and
discovery path remains the source of table metadata. Unsupported key shapes
must fail before a durable `.frm` sidecar can become part of MyLite state.

Copy `ALTER TABLE` can continue to route through MariaDB's handler read/write
path. Adding an unsupported key by ALTER must fail explicitly; adding a
supported key should rebuild key behavior from copied row images.

## Single-File And Embedded-Lifecycle Implications

No new durable sidecar files are allowed. Autoincrement state lives in the
primary `.mylite` payload next to table definitions and rows. Index structures
are rebuilt from rows in memory, so this slice does not add index-page recovery
requirements beyond the existing catalog publication protocol.

Fresh embedded processes must reopen keyed tables, enforce duplicates, use
index lookups, and continue autoincrement from persisted state.

## Public API And File-Format Impact

The public `libmylite` C API is unchanged.

The catalog payload gains a new textual `AUTOINC` record. The outer v1 header,
payload checksum, and two-slot publication protocol remain unchanged. Since the
file format is pre-release, newly written payloads emit `AUTOINC` records for
every table while older local payloads without an `AUTOINC` record reopen with
the unsupported-table sentinel value until rewritten.

## Binary-Size Impact

Expected size impact is small: the slice adds handler-side vector sorting,
key-image comparison, and smoke coverage, without adding dependencies or
linking another storage component.

Record artifact sizes after implementation.

## License, Trademark, And Dependency Impact

No new dependency is introduced. The work remains inside the GPL-2.0-only
MariaDB-derived tree and MyLite-owned storage-engine code.

## Test And Verification Plan

Run the existing review gate:

```sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
bash -n tools/run-storage-engine-smoke.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

Extend storage-engine smoke to cover:

- `CREATE TABLE` with `PRIMARY KEY(id)` and secondary `KEY(note)`.
- Insert, select, update, delete, and fresh-process persistence for keyed rows.
- Duplicate primary-key insert returns an error.
- Update into an existing unique key returns an error.
- Forced indexed lookup returns the expected row.
- Ordered key scan returns rows in key order.
- Autoincrement insert, explicit high value, subsequent generated value, delete,
  and fresh-process reopen preserve the next generated value.
- Explicit-first autoincrement insert advances the next generated value.
- Unsupported nullable key, BLOB/TEXT key, reverse key, and non-leading
  autoincrement shapes are rejected explicitly.
- No persistent `.frm` or engine sidecars are introduced.

## Acceptance Criteria

- Supported keyed tables can be created without durable `.frm` sidecars.
- Primary and unique keys reject duplicates on insert and update.
- `SELECT ... FORCE INDEX` and range/order queries over supported keys return
  correct rows.
- Autoincrement values are generated, persisted, and not reduced by delete or
  fresh-process reopen.
- Unsupported key/autoincrement shapes fail explicitly.
- Existing keyless row persistence and recovery smokes still pass.
- `libmylite` open/close and embedded bootstrap smokes still pass.
- Binary size changes are recorded.

## Implementation Result

MyLite now accepts supported non-null BTREE/undefined keys and autoincrement
tables in the raw-record bridge storage. The implementation adds:

- `AUTOINC` catalog payload records,
- handler index cursor methods over sorted in-memory key images,
- unique-key checks for insert and update,
- lazy autoincrement counter initialization for MariaDB-created table metadata,
- durable autoincrement reservation and explicit-value advancement,
- exact `records_in_range()` counts for supported key ranges,
- explicit rejection for the unsupported key and autoincrement shapes covered
  by the smoke.

The later `composite-autoincrement-storage` slice supersedes this slice's
conservative rejection of non-leading `AUTO_INCREMENT` key parts. Leading-key
autoincrement still uses the table-local durable counter introduced here.

Indexes are not persisted as separate pages. The handler rebuilds key images
from durable row records with MariaDB's `key_copy()` and compares them with
`key_tuple_cmp()`. This keeps the slice correct and bounded while leaving the
real pager/B-tree design for a later storage slice.

Verification passed:

```sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
bash -n tools/run-storage-engine-smoke.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

Observed reports after implementation:

- `mylite-storage-engine-report.txt`: `key_lookup_note=two`,
  `key_order_ids=1,2,3`, `duplicate_key=rejected`,
  `update_duplicate_key=rejected`, `autoincrement_ids=1,2,10,12`,
  `explicit_autoincrement_ids=20,21`, no `.frm` artifacts.
- `mylite-catalog-read-report.txt`: `persisted_count=2`,
  `persisted_notes=seven,eight`,
  `persisted_autoincrement_ids=1,5,6,7`, no `.frm` artifacts, no catalog
  sidecars.
- `mylite-catalog-recovery-read-report.txt`: `persisted_count=2`,
  `persisted_notes=seven,eight`, `persisted_autoincrement_ids=1,5,6`,
  `recovery_marker=absent`, no `.frm` artifacts, no catalog sidecars.

Observed artifacts after this slice:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,313,538 bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 29,698 bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,692,464
  bytes.
- `build/mariadb-minsize/mylite/mylite-open-close-smoke`: 22,693,600 bytes.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,691,592
  bytes.
- `build/mariadb-minsize/mylite-catalog-persistence/catalog.mylite`: 43,120
  bytes.
- `build/mariadb-minsize/mylite-catalog-recovery/catalog.mylite`: 39,938
  bytes.

## Risks And Unresolved Questions

- Rebuilding index cursors from rows is intentionally inefficient; a later
  pager/B-tree slice must replace it before larger compatibility claims.
- Raw record images remain a bridge format and still exclude BLOB/TEXT.
- Nullable unique-key semantics are deferred because MariaDB allows multiple
  NULLs in unique keys and that needs explicit tests.
- Autoincrement is non-transactional in this slice; generated value gaps are
  acceptable, but reuse after successful insert/delete is not.
- Prefix and collation-heavy key behavior depends on MariaDB key-image helpers
  and needs broader compatibility tests later.
