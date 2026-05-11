# nullable-key-storage

## Problem Statement

MyLite now supports durable primary and secondary indexes, BLOB/TEXT prefix
keys, and unique enforcement, but it still rejects every key that contains a
nullable part. That excludes ordinary MariaDB schemas such as
`KEY(note)` where `note` is nullable, and `UNIQUE KEY(email)` where MariaDB
allows multiple `NULL` values but rejects duplicate non-`NULL` values.

This slice adds nullable key parts to the current raw-record and durable
`INDEXPAGE` bridge.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/sql_table.cc:2823` rejects nullable key parts at
  DDL time unless the handler advertises `HA_NULL_IN_KEY`.
- `vendor/mariadb/server/sql/table.cc:3176` records each nullable key part's
  `null_offset` and `null_bit`, and sets `HA_NULL_PART_KEY` on the key.
- `vendor/mariadb/server/sql/key.cc:115` `key_copy()` writes one NULL marker
  byte before nullable key-part payload bytes and skips value bytes for a NULL
  key part.
- `vendor/mariadb/server/sql/key.cc:488` `key_cmp()` treats NULL as lower than
  any non-NULL value for range comparisons.
- `vendor/mariadb/server/sql/key.cc:649` `key_tuple_cmp()` compares two key
  image tuples and handles nullable key-part marker bytes.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1969` currently rejects
  keys with `HA_NULL_PART_KEY`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1985` currently rejects
  key parts whose field can be NULL.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:2005` enforces unique
  keys by comparing candidate key images to every stored live row.

## Scope

This slice will:

- advertise `HA_NULL_IN_KEY` so MariaDB allows nullable key-part DDL for
  `ENGINE=MYLITE`,
- allow nullable key parts in MyLite's storage-engine key-shape validator,
- continue to reject reverse-sort, fulltext, spatial, generated, hash-only,
  GEOMETRY, and other unsupported key shapes,
- keep existing MariaDB key-image storage for nullable fixed, variable,
  BLOB, and TEXT key parts,
- preserve sorted `INDEXPAGE` payloads using `key_tuple_cmp()`,
- enforce MariaDB-style unique nullable keys: if any user key part in the
  candidate key is NULL, the row does not conflict with other rows for that
  unique key; duplicate all-non-NULL tuples still fail,
- verify same-process DML and fresh-process reopen for nullable primary or
  secondary key access where MariaDB permits the definition.

## Non-Goals

- Do not support reverse-sort indexes.
- Do not support fulltext or spatial indexes.
- Do not support GEOMETRY columns or GEOMETRY indexes.
- Do not change the row or index page file format.
- Do not add a final B-tree format or page-local row reuse.

## Proposed Design

The durable index payload format already stores opaque MariaDB key-image bytes.
No on-disk format change is needed for NULL markers.

DDL support needs two gates opened:

1. Add `HA_NULL_IN_KEY` to `ha_mylite::table_flags()`.
2. Remove `HA_NULL_PART_KEY` and `Field::real_maybe_null()` from MyLite's
   unsupported-key checks.

Key-image generation remains unchanged. `key_copy()` writes nullable marker
bytes into the key image. For stored rows, MyLite already decodes BLOB/TEXT
values before key generation; nullable fixed-record bytes are present in the
stored record prefix.

Unique enforcement needs one new helper:

```c++
static bool mylite_key_has_null_part(
    TABLE *table,
    uint key_index,
    const uchar *record);
```

For a unique key, if the candidate record has any nullable user key part set to
NULL, MyLite should skip duplicate checks for that key. That matches MariaDB's
common unique-index behavior and the upstream row-event logic that does not
treat nullable unique keys as candidate keys. If the candidate key has no NULL
parts, MyLite can continue comparing key images against stored non-NULL rows.
Stored rows with NULL parts can be skipped before key comparison.

## Affected Subsystems

- MyLite handler capability flags.
- MyLite storage-engine key-shape validation.
- Unique constraint enforcement.
- Durable index payload reuse through existing key-image bytes.
- Storage smoke and storage architecture docs.

## DDL Metadata Routing Impact

`CREATE TABLE ... KEY(nullable_col) ENGINE=MYLITE` and
`CREATE TABLE ... UNIQUE KEY(nullable_col) ENGINE=MYLITE` should now pass when
the remaining key shape is supported. Unsupported nullable key shapes still fail
through the existing handler create path.

No `.frm` sidecar behavior changes.

## Single-File And Embedded-Lifecycle Implications

No new primary-file or companion-file object is introduced. Nullable key images
are stored in existing `INDEXPAGE` payload chains and loaded on fresh-process
reopen.

## Public API Or File-Format Impact

No public `libmylite` API change. No file-format version bump is required
because nullable key marker bytes are part of MariaDB's existing key image.

## Binary-Size Impact

Expected growth is small: one helper, one capability-flag change, validator
changes, and smoke coverage. Post-implementation `MinSizeRel` artifact sizes
will be recorded after verification.

## License, Trademark, And Dependency Impact

No new dependency. All changes remain in existing GPL-2.0-only MyLite and
MariaDB-derived source files.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc`:
  - replace the nullable-key rejection with supported nullable key coverage,
  - create a table with nullable non-unique and unique keys,
  - insert multiple rows with `NULL` in a unique key and verify they succeed,
  - verify composite unique keys allow duplicate tuples when either key part is
    `NULL`,
  - reject duplicate non-NULL unique key values,
  - verify forced nullable-key reads for `IS NULL` and non-NULL equality,
  - verify update/delete maintain nullable index entries,
  - verify fresh-process reopen can read through a nullable key index.
- Keep reverse-sort and GEOMETRY unsupported coverage explicit.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` for changed shell scripts
  - `git diff --check`

## Acceptance Criteria

- Supported nullable key tables can be created.
- Nullable non-unique indexes serve `IS NULL` and non-NULL index reads before
  and after fresh-process reopen.
- Unique nullable keys allow multiple rows with NULL in any user key part.
- Unique nullable keys reject duplicate all-non-NULL key tuples.
- Existing non-null key, BLOB/TEXT key, recovery, transaction, and public API
  smoke coverage keeps passing.
- Docs and roadmap describe nullable key support and remaining unsupported
  reverse/fulltext/spatial/GEOMETRY limits.

## Risks And Unresolved Questions

- MariaDB optimizer range construction for `IS NULL` must supply key images
  compatible with `key_tuple_cmp()`. The smoke should force the nullable index
  to prove the handler path.
- Composite unique keys with mixed NULL/non-NULL parts should follow the same
  rule as single-part unique nullable keys: any NULL part means no duplicate
  conflict. This slice should implement the general helper even if smoke keeps
  the table small.
- Nullable prefix BLOB/TEXT key parts rely on the previous slice's stored-row
  decode path. If a nullable BLOB/TEXT edge fails, the implementation should
  fix that path instead of narrowing support back to fixed fields.

## Implementation Result

Implemented.

- `ha_mylite::table_flags()` now advertises `HA_NULL_IN_KEY`.
- MyLite no longer rejects `HA_NULL_PART_KEY` or nullable key fields in the
  supported BTREE/undefined key-shape validator.
- Unique-key enforcement skips duplicate checks for a unique key when any user
  key part is NULL in the candidate record. Stored rows with NULL in the same
  unique key are skipped before key-image comparison.
- Durable index payloads continue using MariaDB key-image bytes; no file-format
  version bump was needed.
- Storage smoke now covers nullable secondary indexes, nullable unique keys,
  composite unique keys with leading and trailing NULL parts, nullable
  BLOB/TEXT prefix indexes, duplicate all-non-NULL rejection, update/delete
  index maintenance, unsupported reverse/GEOMETRY coverage, and fresh-process
  reopen reads through nullable indexes.

Verification on 2026-05-11:

- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `bash -n tools/run-compatibility-test-harness.sh tools/run-storage-engine-smoke.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
- `git diff --check`

Observed report evidence:

- `nullable_key_null_ids=1,2`
- `nullable_key_lookup_id=4`
- `nullable_key_duplicate=rejected`
- `nullable_key_updated_null_ids=1,2,4`
- `nullable_key_deleted_count=2`
- `nullable_composite_key_head_null_ids=1,2`
- `nullable_composite_key_tail_null_ids=3,4`
- `nullable_composite_key_duplicate=rejected`
- `nullable_blob_key_null_ids=1,2`
- `nullable_blob_key_lookup_id=3`
- `nullable_blob_key_duplicate=rejected`
- `persisted_nullable_key_null_ids=1,2`
- `persisted_nullable_key_lookup_id=4`

Measured `MinSizeRel` artifacts after the storage-smoke build:

- `build/mariadb-minsize/mylite/libmylite.a`: 87,206 bytes.
- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,417,706 bytes.
- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 571 archive objects.
- Dynamic plugin artifacts: none.
