# Pager Page Store Slice

## Problem Statement

MyLite's primary `.mylite` file has a recoverable two-header publication
protocol, but table definitions, rows, keys, and autoincrement counters still
live inside one logical text payload blob. Every mutation serializes the whole
catalog and writes a new arbitrary-length payload at EOF.

That bridge proved handler behavior, but it is the wrong substrate for real row
and index storage. The next storage slice should introduce a reusable page-store
layer beneath the catalog payload without also attempting a B-tree, free-list
reuse, transactions, or a final row format.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- MyLite's current catalog and file-format code lives in
  `vendor/mariadb/server/storage/mylite/ha_mylite.cc`:
  `mylite_load_catalog_locked()`, `mylite_write_catalog_locked()`,
  `mylite_find_latest_catalog_header()`, `mylite_read_catalog_header()`,
  `mylite_read_catalog_payload()`, `mylite_write_catalog_payload()`,
  `mylite_write_catalog_header()`, `mylite_read_all_at()`, and
  `mylite_write_all_at()`.
- The current header reserves two fixed 4096-byte slots and starts payloads at
  byte 8192. `mylite_catalog_page_size` is already 4096, but payloads are not
  page-structured yet.
- `docs/architecture/single-file-storage.md` calls for explicit file regions:
  header, catalog pages, table/index pages, undo/redo or append-log pages,
  free-space map, and integrity/checkpoint metadata.
- MariaDB examples for page-oriented storage are much broader than this slice:
  Aria block records use block sizes and page suffix/header data under
  `vendor/mariadb/server/storage/maria/`, and InnoDB uses typed file pages under
  `vendor/mariadb/server/storage/innobase/fil/` and `btr/`. MyLite should not
  copy those formats, but the source confirms that durable engine state is
  normally page-typed, checksummed, and addressed by page identity rather than
  by arbitrary text payload offsets.
- MariaDB's handler surface should not change for this slice. Existing DDL and
  DML calls continue to enter MyLite through the handler methods already
  implemented in previous slices.

## Scope

This slice will:

- update the internal `.mylite` file format to a pre-release v2 header,
- keep the two-header generation and recovery protocol,
- store the existing logical catalog text payload inside a chain of typed
  fixed-size pages,
- add first-party helpers for page ids, page offsets, page checksums, and page
  chain reads/writes,
- keep append-only allocation for now,
- add smoke evidence that the existing catalog persistence and recovery phases
  still pass through the page chain,
- record file-size and binary-size impact.

## Non-Goals

- Do not implement B-trees, durable secondary indexes, or index page splits.
- Do not move individual rows out of the logical catalog payload yet.
- Do not add free-list reuse or compaction in this slice.
- Do not add undo, redo, WAL, rollback journals, transaction rollback, or
  cross-process writer locking.
- Do not import SQLite, InnoDB, Aria, or another pager implementation.
- Do not stabilize the pre-release file format for external compatibility.
- Do not change the public `libmylite` C API.

## Proposed Design

### Header Version

Update the internal header to format version 2. The header remains one 4096-byte
page, and the two publication slots remain at offsets 0 and 4096:

```text
offset 0      header slot 0
offset 4096   header slot 1
offset 8192+  typed page-store pages
```

Header fields stay little-endian and keep the same offsets where practical:

```text
bytes  0..15   magic for page-store-backed catalog format
bytes 16..19   format_version: 2
bytes 20..23   page_size: 4096
bytes 24..31   generation
bytes 32..39   catalog_root_page_offset
bytes 40..47   catalog_payload_length
bytes 48..55   catalog_payload_checksum
bytes 56..63   header_checksum
bytes 64..4095 reserved, zero for now
```

The checksum still covers the whole header page with the checksum field zeroed.
Because MyLite is pre-release, this slice may fail closed on old v1 files
instead of adding migration logic.

### Page Layout

Each page-store page is exactly 4096 bytes. Page zero and page one are reserved
for header slots; page-store pages start at page id 2.

Use a compact page header:

```text
bytes  0..15   page magic
bytes 16..19   page_format_version: 1
bytes 20..23   page_type
bytes 24..31   page_id
bytes 32..39   next_page_id, or 0 for end of chain
bytes 40..43   used_payload_bytes
bytes 44..55   reserved, zero for now
bytes 56..63   page_checksum
bytes 64..4095 payload bytes
```

The page checksum covers the whole 4096-byte page with the checksum field
zeroed. The first page type is `catalog_payload`. Future slices can add row
page, index page, free-list page, and journal/log page types without changing
the header publication model again.

### Catalog Payload Storage

Keep the logical catalog serializer and parser unchanged for this slice. The
only change is the physical storage of that serialized text:

1. Serialize the logical catalog payload as today.
2. Compute the payload checksum over the logical bytes.
3. Append enough `catalog_payload` pages to hold the bytes.
4. Link pages with `next_page_id`.
5. Publish a new header generation whose root offset is the first catalog page
   and whose payload length/checksum describe the logical payload.

Reads reverse the process:

1. Validate both header slots independently.
2. For each candidate header, follow the catalog page chain and validate each
   page checksum, page type, page id, payload length, and next-page link.
3. Reconstruct the logical payload bytes and validate the header's payload
   checksum.
4. Choose the highest valid header generation.

### Allocation Policy

Allocation is append-only in this slice:

- find the current EOF,
- align it up to the page size,
- never allocate before page id 2,
- write the new page chain sequentially.

No free-list page exists yet. The page header reserves the concepts needed for
typed pages and page identity; free-space tracking is a later slice once row and
index data stop being serialized as one catalog payload.

## Affected Subsystems

- MyLite storage-engine file-format helpers in `ha_mylite.cc`.
- Storage-engine smoke recovery behavior, using the existing corruption point.
- Architecture and roadmap docs.

No SQL parser, optimizer, public C API, CMake target, or handler method surface
should change.

## DDL Metadata Routing Impact

DDL metadata routing is logically unchanged. `CREATE`, copy `ALTER`, `DROP`,
and `RENAME` still mutate the MyLite logical catalog. The page-store layer
changes only how the serialized catalog generation is physically written and
recovered.

## Single-File And Embedded-Lifecycle Implications

This is still a single primary `.mylite` file. The slice should not introduce
new durable companions or MariaDB engine sidecars.

Fresh embedded process tests remain necessary. The existing recovery smoke can
continue corrupting the latest generation at the previous EOF, because the new
catalog root page is append-aligned after the old generation.

## Public API And File-Format Impact

The public C API is unchanged.

The internal `.mylite` file format changes from v1 raw catalog payload blobs to
v2 typed catalog page chains. Since there are no released MyLite files yet,
failing closed on v1 inputs is acceptable for this slice.

## Binary-Size Impact

Expected impact is small first-party code for page headers, page checksums,
append allocation, and page-chain reconstruction. No new dependency is allowed.
Record measured artifacts after implementation.

## License, Trademark, And Dependency Impact

No new dependency. New code remains GPL-2.0-only first-party MyLite storage
code in the MariaDB-derived tree. No trademark or packaging surface changes.

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

The storage smoke should continue to verify:

- normal DDL/DML lifecycle,
- fresh-process catalog and row persistence,
- recovery fallback after corrupting the latest generation,
- no `.frm` artifacts,
- no catalog temporary sidecars.

The compatibility harness should continue to verify:

- MariaDB reference fingerprints match MyLite fingerprints,
- sidecar scan reports no unexpected MyLite sidecars.

## Acceptance Criteria

- New `.mylite` writes use a v2 header and typed 4096-byte catalog payload
  pages.
- Catalog loading reconstructs the logical payload from a validated page chain.
- A corrupted latest page chain falls back to the previous valid generation.
- Existing storage, recovery, compatibility, embedded lifecycle, and
  `libmylite` lifecycle smokes pass.
- No persistent `.frm`, engine table sidecars, dynamic plugin artifacts, or
  catalog temporary sidecars are introduced.
- Binary and file-size changes are recorded.

## Risks And Unresolved Questions

- This does not reduce write amplification yet; it creates the page substrate
  but still stores the logical catalog as one blob.
- Append-only page allocation grows files until a later free-list and compaction
  slice.
- The page checksum remains FNV-1a like the v1 header/payload checksum. A later
  durability slice may replace it with a stronger page checksum.
- Old v1 development files are not migrated.
- Row and index page formats remain open design work.
