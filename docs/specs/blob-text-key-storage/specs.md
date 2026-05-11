# blob-text-key-storage

## Problem Statement

The `blob-text-row-storage` slice made non-key BLOB/TEXT values durable inside
the primary `.mylite` file, but BLOB/TEXT key parts are still rejected. That
means common MariaDB definitions such as `KEY(note(16))` on `TEXT` columns and
`UNIQUE KEY(payload(8))` on `BLOB` columns cannot be created even though the
row payload itself can now be stored.

This slice adds support for non-null BLOB/TEXT prefix key parts in the current
raw-record bridge.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1972` currently rejects
  key parts with `HA_BLOB_PART`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:2115` builds durable key
  images with `key_copy()`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1765` encodes BLOB/TEXT
  rows by clearing native pointer bytes in the fixed record and appending
  payload bytes after the fixed MariaDB record image.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1815` decodes stored
  BLOB/TEXT rows by reconstructing `Field_blob` pointers into a handler-owned
  read buffer before returning rows to MariaDB.
- `vendor/mariadb/server/sql/field.cc:9127` implements
  `Field_blob::get_key_image_itRAW()`: BLOB/TEXT key images store a
  `HA_KEY_BLOB_LENGTH` length prefix followed by the indexed payload prefix,
  with zero padding for short values.
- `vendor/mariadb/server/sql/field.cc:9161` and `:9176` implement BLOB/TEXT
  key comparison over the stored key-image prefix.
- `vendor/mariadb/server/sql/handler.cc:7337` uses `key_cmp()` for range
  comparisons over handler key images, matching the helper MyLite already uses
  through `key_tuple_cmp()`.

## Scope

This slice will:

- allow non-null BLOB/TEXT key parts that MariaDB marks with `HA_BLOB_PART`,
- keep nullable, reverse-sort, fulltext, spatial, generated, and unsupported
  key shapes rejected,
- generate key images for incoming MariaDB records with `key_copy()` as today,
- generate key images for stored MyLite rows by decoding the row into a
  temporary MariaDB record buffer first, so `Field_blob` pointers are valid
  during `key_copy()`,
- persist the resulting BLOB/TEXT prefix key images in existing `INDEXPAGE`
  payloads,
- verify non-unique and unique BLOB/TEXT prefix keys, duplicate-prefix
  rejection, index reads, and fresh-process reopen.

## Non-Goals

- Do not support nullable BLOB/TEXT key parts.
- Do not support fulltext or spatial indexes.
- Do not add a final typed column encoding or B-tree page format.
- Do not change BLOB/TEXT row payload layout.
- Do not support GEOMETRY indexes or GEOMETRY columns.

## Proposed Design

Keep the durable index payload format unchanged. BLOB/TEXT prefix key images
are already MariaDB key-image bytes; they can be stored in `Mylite_index_entry`
like fixed-field key images.

The important implementation distinction is the record source:

- For an incoming row buffer from MariaDB, `key_copy()` can read native
  `Field_blob` pointers directly.
- For a stored MyLite row, the fixed record has its BLOB/TEXT pointer bytes
  cleared. MyLite must call `mylite_decode_record()` into a temporary record
  buffer and use the decoded record for `key_copy()`.

Add a helper for stored-row key image generation:

```c++
static bool mylite_make_key_image_from_row(
    TABLE *table,
    uint key_index,
    const Mylite_row &row,
    std::vector<uchar> *key_image);
```

This helper allocates a `table->s->reclength` temporary record buffer and a
BLOB read buffer, decodes the row, then delegates to the existing
`mylite_make_key_image()` path. Existing call sites that build or compare keys
against stored rows should use the new helper. Incoming candidate rows should
continue using `mylite_make_key_image()` directly.

`mylite_key_part_supports_storage()` should permit `HA_BLOB_PART` when the key
part has a non-null field and no reverse sort. MariaDB has already limited the
key part to a prefix length during table creation.

## Affected Subsystems

- MyLite storage-engine key support checks.
- Unique constraint enforcement.
- Durable index root rebuild.
- Index page persistence and load validation through existing key-image bytes.
- Storage smoke and storage architecture docs.

## DDL Metadata Routing Impact

`CREATE TABLE ... KEY(text_col(prefix)) ENGINE=MYLITE` should now succeed for
non-null BLOB/TEXT columns. Unsupported BLOB/TEXT key shapes should still fail
through the existing `HA_ERR_UNSUPPORTED` path.

No `.frm` sidecar behavior changes.

## Single-File And Embedded-Lifecycle Implications

No new file or companion file is introduced. BLOB/TEXT key images are stored in
the existing primary-file `INDEXPAGE` payload chains. Fresh-process reopen must
load those key images and serve indexed reads without rebuilding from invalid
native BLOB pointers.

## Public API Or File-Format Impact

No public `libmylite` API change. No file-format version bump is required
because `INDEXPAGE` already stores opaque MariaDB key-image bytes by key length.

## Binary-Size Impact

Expected growth is small: one helper that decodes stored rows before
`key_copy()`, one support-check change, and smoke coverage. The
post-implementation `MinSizeRel` artifact sizes will be recorded after
verification.

## License, Trademark, And Dependency Impact

No new dependency. All changes remain in existing GPL-2.0-only MyLite and
MariaDB-derived source files.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc` to replace the
  current BLOB/TEXT-key rejection with supported coverage:
  - create a table with `TEXT NOT NULL`, `BLOB NOT NULL`, a non-unique text
    prefix key, and a unique BLOB prefix key,
  - insert rows with distinct prefixes,
  - verify forced index reads return expected rows,
  - verify duplicate unique prefixes fail,
  - verify update/delete maintain index entries,
  - verify fresh-process reopen can read through the BLOB/TEXT prefix index.
- Keep GEOMETRY and nullable/reverse unsupported key coverage explicit.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` for changed shell scripts
  - `git diff --check`

## Acceptance Criteria

- Non-null BLOB/TEXT prefix key tables can be created.
- MyLite enforces unique BLOB/TEXT prefix keys.
- Index reads using BLOB/TEXT prefix keys return the same rows before and
  after fresh-process reopen.
- Stored-row key-image generation does not read cleared native BLOB pointer
  bytes.
- Existing BLOB/TEXT row storage and fixed-field index tests keep passing.
- Docs and roadmap describe BLOB/TEXT prefix key support and remaining
  unsupported GEOMETRY/spatial/fulltext/nullable key limits.

## Risks And Unresolved Questions

- Prefix comparison must follow MariaDB character-set semantics. This slice
  relies on MariaDB's `Field_blob` key image and `key_tuple_cmp()` logic rather
  than reimplementing comparison.
- Decoding stored rows for key rebuilds has extra CPU and allocation cost. That
  is acceptable for the current whole-table index rebuild bridge; a future
  B-tree format should avoid rebuilding every key image on each write.
- Nullable BLOB/TEXT key semantics remain deferred with the broader nullable
  unique-key design.

## Implementation Result

Pending.
