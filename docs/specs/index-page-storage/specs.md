# Index Page Storage Slice

## Problem Statement

MyLite can enforce supported primary and unique keys, serve ordered index reads,
and compare its supported key behavior against MariaDB. The implementation still
rebuilds every index cursor from row images in memory whenever a handler index
operation runs.

That is acceptable for early compatibility, but it does not establish durable
index roots, does not validate indexed state during catalog generation load,
and keeps index behavior detached from the page-store architecture.

This slice should add the first durable sorted index payload pages for supported
keys. It is not a B-tree slice; it should persist complete sorted key-entry
streams addressed from the catalog and use them for index reads when available.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Current key behavior lives in
  `vendor/mariadb/server/storage/mylite/ha_mylite.cc`:
  `mylite_build_index_entries()`,
  `mylite_check_unique_constraints_locked()`,
  `mylite_make_key_image()`, `mylite_find_index_position()`, and
  `mylite_index_entry_in_range()`.
- Handler index entry points already use `mylite_build_index_entries()`:
  `ha_mylite::index_read_map()`, `ha_mylite::index_next()`,
  `ha_mylite::index_prev()`, and `ha_mylite::records_in_range()`.
- Durable row pages now load before index operations, so index payloads can be
  validated against live row ids during catalog generation load.
- MariaDB `KEY` metadata and `key_copy()` require an open `TABLE`. MyLite can
  build key images reliably from row records during handler DML/index paths,
  but not from catalog-only code that has no `TABLE`.

## Scope

This slice will:

- add an `index_payload` page type,
- add catalog `INDEXPAGE` records for supported table key indexes,
- write sorted full-index payload chains for touched tables during row DML,
- load and validate index payload chains during catalog generation load,
- use durable index entries in `mylite_build_index_entries()` when a matching
  loaded index root exists,
- keep rebuilding from rows as a legacy fallback when no durable index root is
  available,
- keep uniqueness checks authoritative from current row images for this slice,
- update storage and compatibility smokes to prove durable index roots exist and
  index behavior still matches the supported subset,
- record file-size and binary-size impact.

## Non-Goals

- Do not implement B-tree pages, page splits, or incremental index updates.
- Do not implement free-list reuse or compaction.
- Do not make durable index roots the source of truth for uniqueness yet.
- Do not support nullable, generated, fulltext, spatial, hash, reverse-sort, or
  BLOB/TEXT key parts.
- Do not change public `libmylite` APIs.
- Do not stabilize the pre-release file format.

## Proposed Design

### Catalog Roots

For each durable index payload, write:

```text
INDEXPAGE <db_hex> <table_hex> <key_index> <key_length> <root_offset> <payload_length> <payload_checksum>
```

`key_index` is the MariaDB key ordinal for the table definition. `key_length`
must match the `TABLE::key_info[key_index].key_length` when the handler later
uses the loaded root. Empty indexes publish no root in this first version; the
handler can rebuild an empty vector from rows.

### Index Payload Format

Use page type `index_payload` (`3`) with a logical binary payload:

```text
bytes  0..15   index payload magic
bytes 16..19   index_payload_format_version: 1
bytes 20..23   key_index
bytes 24..27   key_length
bytes 28..35   entry_count
bytes 36..end  repeated entries
```

Each entry is:

```text
bytes 0..7           rowid
bytes 8..(8+N-1)     key image, where N == key_length
```

Entries must be sorted by MariaDB `key_tuple_cmp()` order, with row id as the
tie breaker. On load, index entries must reference existing live rows.

### Write Protocol

Catalog-only writes do not have enough MariaDB `TABLE` metadata to rebuild key
images. Therefore the first durable index writer should be called from row DML
paths where the handler has a `TABLE`:

1. mutate the in-memory rows,
2. build sorted entries for every supported key on the touched table,
3. open the primary file and append row payload pages as today,
4. append index payload pages for the touched table,
5. publish catalog `ROWPAGE` and `INDEXPAGE` roots in the same header
   generation.

DDL and metadata-only writes may preserve previously loaded index roots when
rows have not changed. If no matching durable root exists, index reads fall
back to rebuilding entries from rows.

### Read And Validation

During catalog load:

1. parse catalog metadata and row roots,
2. load row payloads,
3. load index payloads,
4. validate key index ordinals, key lengths, sorted order, duplicate root
   records, and rowid ownership against loaded rows,
5. accept the generation only after all referenced payloads validate.

During handler index reads, `mylite_build_index_entries()` should use a loaded
durable index root only when the requested key ordinal and key length match the
open `TABLE`. Otherwise it should rebuild from rows as the current legacy path.

## Affected Subsystems

- MyLite storage-engine catalog metadata and page helpers in `ha_mylite.cc`.
- Row DML flush paths so they can refresh durable index roots with a `TABLE`.
- Handler index build path.
- Storage and compatibility smoke reports.
- Architecture, roadmap, and slice docs.

## Single-File And Embedded-Lifecycle Implications

Index payloads remain inside the primary `.mylite` file. No durable sidecars are
allowed. Append-only orphan index pages after an unpublished header are
acceptable until free-list and compaction work exists.

## Binary-Size Impact

Expected impact is first-party code for index payload serialization, parser
validation, catalog `INDEXPAGE` records, and smoke assertions. No new
dependency is allowed. Record measured artifacts after implementation.

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

The storage smoke should verify:

- key lookup and key ordering still pass,
- duplicate key insert/update still reject,
- fresh-process indexed table persistence uses durable index roots,
- physical catalog inspection finds `INDEXPAGE` records and page type `3`,
- row and index roots survive recovery fallback,
- no `.frm` artifacts,
- no catalog temporary sidecars.

The compatibility harness should continue to verify:

- MariaDB reference fingerprints match MyLite fingerprints for the supported
  subset,
- sidecar scan reports no unexpected MyLite sidecars.

## Acceptance Criteria

- New writes for touched keyed tables publish durable `INDEXPAGE` roots.
- Loaded index payloads validate sorted order, key length, row ownership, and
  payload checksum before a catalog generation is accepted.
- `mylite_build_index_entries()` uses durable index entries when they match the
  open table key metadata, with row rebuild as legacy fallback.
- Existing storage, recovery, compatibility, embedded lifecycle, and
  `libmylite` lifecycle smokes pass.
- No persistent `.frm`, engine table sidecars, dynamic plugin artifacts, or
  catalog temporary sidecars are introduced.
- Binary and file-size changes are recorded.

## Risks And Unresolved Questions

- Complete sorted index payload rewrites still scale poorly.
- Durable index roots are not yet a B-tree and do not provide page-level seek.
- Metadata-only flushes may preserve existing roots rather than refreshing all
  indexes, because catalog-only code lacks `TABLE` key metadata.
- Uniqueness checks still scan rows in this slice.
- Free-list, compaction, and transaction recovery remain open.
