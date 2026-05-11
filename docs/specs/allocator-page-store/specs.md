# Allocator Page Store Slice

## Problem Statement

`free-list-page-reuse` and `orphan-page-reclaim` persist reusable page ranges as
logical `FREEPAGE` records inside the catalog payload. That solved row and
index page reuse, but it leaves the catalog payload itself append-only: the
catalog payload cannot safely allocate from a free list that is serialized
inside the same payload bytes.

This slice should move persistent allocator metadata into its own typed page
chain and publish that chain from the file header. Once free-space state is no
longer self-described by the catalog payload, catalog payload chains can reuse
accepted free ranges while the free-list payload remains append-only for now.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- MyLite's current publication path lives in
  `vendor/mariadb/server/storage/mylite/ha_mylite.cc`:
  `mylite_write_catalog_locked()`, `mylite_write_catalog_header()`,
  `mylite_read_catalog_header()`, `mylite_read_catalog_payload()`,
  `mylite_write_catalog_payload()`, `mylite_read_page_chain()`,
  `mylite_write_page_chain()`, `mylite_allocate_page_range_locked()`,
  `mylite_serialize_catalog_with_free_ranges_locked()`, and
  `mylite_parse_catalog_payload_locked()`.
- The current header format stores only one page-chain root: catalog payload
  offset, length, and checksum at header bytes `32..55`. Header bytes after the
  checksum slot at `56..63` are available for a pre-release v3 extension.
- Current page types are catalog payload (`1`), row payload (`2`), and index
  payload (`3`). A dedicated allocator payload can use page type `4` without
  changing the page-store header layout.
- MariaDB engines persist allocator state explicitly. Aria's
  `vendor/mariadb/server/storage/maria/ma_bitmap.c` stores bitmap pages, marks
  pages reserved while writes are in progress, and flushes bitmap state at
  checkpoint or close. InnoDB's
  `vendor/mariadb/server/storage/innobase/include/fsp0fsp.h` defines
  `FSP_FREE`, `FSP_FREE_FRAG`, extent descriptor bitmaps, and
  `xdes_is_free()`, while
  `vendor/mariadb/server/storage/innobase/fsp/fsp0fsp.cc` initializes and
  extends free extent lists. MyLite should not copy those formats, but the
  source confirms allocator metadata is durable engine state, not table
  catalog text.

## Scope

This slice will:

- add a header v3 extension for a free-list page-chain root,
- add a typed allocator payload page type,
- serialize accepted free ranges into the allocator payload instead of the
  logical catalog payload for new writes,
- keep loading legacy v2 catalog payload `FREEPAGE` records for existing
  pre-release files,
- validate allocator payload ranges with the same live-root and file-boundary
  rules used today,
- allocate row, index, and catalog payload chains from accepted free ranges,
- keep the new allocator payload itself append-only for this slice,
- include the previous allocator payload chain in the next free list,
- update physical storage smoke coverage to prove catalog payload reuse,
- record file-size and binary-size impact.

## Non-Goals

- Do not implement a bitmap, extent tree, B-tree page allocator, or page-local
  free-space accounting.
- Do not reuse the allocator payload's own newly written pages in the same
  generation.
- Do not add rollback journal, WAL, undo, redo, transaction rollback, or
  cross-process writer locking.
- Do not truncate the primary file.
- Do not stabilize the pre-release file format for external compatibility.
- Do not change the public `libmylite` C API.

## Proposed Design

### Header V3

Keep the existing two fixed 4096-byte header slots and catalog header magic.
Bump the numeric catalog format version written at header byte `16` to `3`.
Readers should accept:

- v2 headers: catalog root only, with legacy `FREEPAGE` records parsed from the
  catalog payload,
- v3 headers: catalog root plus allocator root.

Use header bytes after the existing checksum slot for allocator metadata:

```text
64..71   free_payload_offset
72..79   free_payload_length
80..87   free_payload_checksum
```

The header checksum remains at bytes `56..63` and still covers the whole header
page with that checksum field zeroed. A v3 header with a zero allocator length
is invalid after this slice starts writing v3, but v2 headers remain valid for
pre-slice files.

### Allocator Payload

Add page type `4` for a dedicated allocator payload chain. The logical payload
uses a small ASCII format to reuse existing decimal range parsing:

```text
MYLITE FREE LIST 1
FREEPAGE <page_id> <page_count>
...
```

The records keep the current meaning: complete page ids, sorted and merged,
with `page_id >= 2` and nonzero `page_count`. The allocator payload may point
to old typed pages whose bytes are not zeroed. A page is free because the
accepted header and catalog no longer protect it.

### Load And Validation

Loading a candidate generation should:

1. read and parse the catalog payload,
2. for v2, parse legacy catalog `FREEPAGE` records into the free list,
3. for v3, read and parse the allocator payload page chain,
4. load row and index payloads,
5. validate free ranges against file size, the accepted catalog payload, the
   accepted allocator payload, row roots, and index roots,
6. run orphan-page reclaim and merge any unprotected complete pages into the
   in-memory free list.

For v3, the logical catalog payload should no longer serialize `FREEPAGE`
records. The parser can keep accepting legacy `FREEPAGE` records so older
pre-release v2 files continue to load.

### Write Protocol

`mylite_write_catalog_locked()` should continue to use one-generation-delayed
reuse:

1. copy the accepted in-memory free ranges into an allocator list,
2. collect pending obsolete ranges from DDL/DML,
3. collect old active row and index payload ranges,
4. collect the accepted catalog payload range,
5. collect the accepted allocator payload range when present,
6. write replacement row payloads using the allocator,
7. write replacement index payloads using the allocator,
8. serialize the logical catalog without `FREEPAGE` records,
9. write the catalog payload using the allocator,
10. build the next free list from allocator leftovers plus obsolete ranges,
11. append the allocator payload at EOF,
12. fsync page data, publish the alternate v3 header, and fsync the header.

Appending the allocator payload avoids a new self-reference: the free-list page
chain describes all free pages except its own newly written pages. Its previous
accepted chain becomes free for a later generation.

### Catalog Payload Reuse

Once `FREEPAGE` records are no longer stored inside the catalog payload, the
catalog payload can be allocated like row and index payloads. The storage smoke
should physically prove at least one latest catalog payload page range is
contained in a previously accepted free range.

## Affected Subsystems

- MyLite file header parsing and writing in `ha_mylite.cc`.
- MyLite catalog and allocator payload serialization/parsing.
- Page-chain allocation for catalog payloads.
- Storage smoke physical file inspection.
- Slice, architecture, and roadmap docs.

No SQL parser, optimizer, handler public method, or `libmylite` public API
surface should change.

## DDL Metadata Routing Impact

DDL metadata routing is unchanged. `CREATE`, copy `ALTER`, `DROP`, and
`RENAME` continue to mutate the MyLite catalog through existing handler paths.
The slice changes where allocator metadata is stored and how catalog payload
pages are allocated.

## Single-File And Embedded-Lifecycle Implications

The allocator payload lives inside the primary `.mylite` file as typed page
store data. No companion files are introduced. Loading remains read-only:
orphan pages discovered during load are merged in memory and serialized only by
a later durable write.

## Public API And File-Format Impact

The public C API is unchanged.

The pre-release file format gains:

- catalog header format version `3`,
- header allocator payload root fields,
- page type `4`,
- allocator payload magic `MYLITE FREE LIST 1`.

Readers must keep accepting v2 files with catalog-embedded `FREEPAGE` records.
New writes may publish v3 headers and stop writing catalog `FREEPAGE` records.

## Binary-Size Impact

Expected impact is small first-party code for header v2/v3 handling,
allocator-payload serialization/parsing, and physical smoke assertions. No new
dependency is allowed.

Measured after implementation with `MYLITE_BUILD_JOBS=8` and the
Docker-based `mariadb-minsize` profile:

| Artifact | Size |
| --- | ---: |
| `build/mariadb-minsize/libmysqld/libmariadbd.a` | 44,391,138 bytes |
| `build/mariadb-minsize/mylite/libmylite.a` | 29,698 bytes |
| `build/mariadb-minsize/mylite/mylite-storage-engine-smoke` | 22,703,560 bytes |
| `build/mariadb-minsize/mylite/mylite-compatibility-smoke` | 22,704,248 bytes |
| `build/mariadb-minsize/mylite/mylite-open-close-smoke` | 22,770,928 bytes |
| `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke` | 22,703,384 bytes |

## License, Trademark, And Dependency Impact

No new dependency. New code remains GPL-2.0-only first-party MyLite storage code
inside the MariaDB-derived tree. No trademark or packaging surface changes.

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

- latest headers use catalog format version `3`,
- allocator payload pages use page type `4` and magic
  `MYLITE FREE LIST 1`,
- new catalog payloads contain no `FREEPAGE` records,
- legacy v2 files with catalog `FREEPAGE` records still load before they are
  rewritten,
- latest free ranges do not overlap catalog, allocator, row, or index live
  roots,
- at least one latest catalog payload range is allocated from a previously
  accepted free range,
- recovery fallback still ignores a corrupted latest generation,
- normal row, index, autoincrement, overflow, persistence, and sidecar checks
  still pass.

## Acceptance Criteria

- Accepted v3 generations load catalog and allocator payloads from separate
  page chains.
- Accepted v2 generations with catalog `FREEPAGE` records still load.
- New writes publish free ranges only in allocator payload pages, not the
  logical catalog payload.
- Catalog payload allocation can consume accepted free ranges.
- The allocator payload itself remains append-only and is not listed as free in
  the generation that publishes it.
- Free range validation rejects overlaps with catalog, allocator, row, and
  index live roots.
- Existing storage, recovery, compatibility, embedded lifecycle, and
  `libmylite` lifecycle smokes pass.
- No persistent `.frm`, engine sidecars, dynamic plugin artifacts, or catalog
  temporary sidecars are introduced.

## Implementation Result

Implemented in `vendor/mariadb/server/storage/mylite/ha_mylite.cc` and
`tools/run-storage-engine-smoke.sh`.

- Header readers accept v2 and v3; new writes publish v3 headers with
  allocator payload root fields at bytes `64..87`.
- Page type `4` stores allocator payloads with magic `MYLITE FREE LIST 1`.
- New catalog payloads serialize no `FREEPAGE` records.
- The catalog payload writer now allocates from accepted free ranges; the
  allocator payload writer remains append-only.
- Accepted v2 catalog payload `FREEPAGE` records still load and are rewritten
  through the v3 path.
- Storage smoke evidence from the implemented run:
  `catalog_format_version=3`, `free_payload_page_types=4`,
  `catalog_freepage_records=0`,
  `catalog_reused_page_ranges=64:14`,
  `free_payload_magic=MYLITE FREE LIST 1`, legacy fixture
  `format_version=2`, legacy rewrite `latest_format_version=3`, and recovery
  reclaim `reclaimed_page_ranges=138:1`.

## Risks And Unresolved Questions

- This is still not a transactional free-space manager; crashes before header
  publication still leave orphan pages for the reclaim scan.
- The allocator payload remains append-only, so a later slice may need a stable
  root-page or checkpoint design for allocator metadata itself.
- Header v3 is still a pre-release internal format. If public file-format
  compatibility becomes a requirement, a formal upgrade policy is needed.
- The first allocator payload format is range-based, not page-density-aware.
  Page-local row reuse and B-tree page splits remain future work.
