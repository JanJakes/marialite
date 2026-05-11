# Orphan Page Reclaim Slice

## Problem Statement

`free-list-page-reuse` persists accepted `FREEPAGE` ranges and lets later row
and index rewrites reuse them. It still cannot reclaim pages written by a failed
or unpublished catalog generation, because those pages are never named by an
accepted catalog.

That leaves crash or write-failure orphans behind the two-header publication
protocol. This slice should reclaim unreferenced full pages after catalog load
accepts a generation, without adding transaction rollback, WAL, or compaction.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- MyLite's accepted-generation load path lives in
  `vendor/mariadb/server/storage/mylite/ha_mylite.cc`:
  `mylite_load_catalog_locked()`, `mylite_load_catalog_generation_locked()`,
  `mylite_read_catalog_payload()`, `mylite_load_row_payloads_locked()`,
  `mylite_load_index_payloads_locked()`, and
  `mylite_validate_free_page_ranges_locked()`.
- MyLite's page identity and page-count helpers live in the same file:
  `mylite_page_offset_is_valid()`, `mylite_payload_page_count()`,
  `mylite_add_payload_free_range_locked()`,
  `mylite_normalize_free_page_ranges_locked()`, and
  `mylite_page_range_fits_file()`.
- Prior recovery behavior is documented in `file-format-recovery`,
  `pager-page-store`, and `free-list-page-reuse`: loading evaluates header
  generations newest to oldest and accepts a generation only after referenced
  catalog, row, and index payloads validate.
- MariaDB durable engines maintain explicit allocation metadata. Aria's
  `ma_bitmap.c` records page status and InnoDB's `fsp0fsp.cc` tracks free
  extents and fragments. MyLite's first reclaim pass should stay smaller:
  derive unreferenced full pages from the accepted generation and add them to
  MyLite's existing free range list.

## Scope

This slice will:

- scan the primary file after a catalog generation is fully accepted,
- build protected ranges from the accepted catalog payload, row roots, index
  roots, and accepted `FREEPAGE` ranges,
- mark every other complete page id at or after page 2 as free,
- merge discovered orphan ranges into the in-memory free range list,
- publish reclaimed ranges on the next successful catalog write,
- update recovery smoke coverage to prove pages from a corrupted latest
  generation become reusable after fallback,
- record file-size and binary-size impact.

## Non-Goals

- Do not inspect or repair invalid orphan page contents.
- Do not truncate the primary file.
- Do not reclaim partial trailing bytes after the last complete page.
- Do not add rollback journal, WAL, undo, redo, or transaction rollback.
- Do not change row, index, catalog payload, or public C API formats.
- Do not implement page-local row reuse, B-trees, or compaction.

## Proposed Design

### Protected Ranges

After `mylite_load_catalog_generation_locked()` validates the catalog payload,
row payloads, index payloads, and accepted `FREEPAGE` records, build a normalized
protected range set:

- the accepted header's catalog payload range,
- every loaded table `ROWPAGE` range,
- every loaded `INDEXPAGE` range,
- every accepted `FREEPAGE` range.

The existing range validation already proves accepted `FREEPAGE` ranges do not
overlap live roots and fit inside the file. The reclaim scan should reuse the
same helpers so the protection policy has one representation.

### Full-Page Scan

Compute the number of complete file pages:

```text
complete_pages = file_size / 4096
```

Pages `0` and `1` are header slots and are never reclaimable. For each page id
from `2` to `complete_pages - 1`, add it to an orphan range if it is not
contained in any protected range. The scanner does not need the old page bytes
to validate, because any reclaimed page will be completely overwritten by
`mylite_write_page()` before becoming live again.

### Publication

Discovered orphan ranges are merged into the in-memory free list for the
accepted generation. They are not written immediately. The next successful
catalog write serializes them as normal `FREEPAGE` records.

This keeps load side effects read-only and preserves the current embedded
lifecycle: opening a file after recovery does not write unless a later SQL
operation changes storage state.

### Recovery Smoke

Extend the recovery smoke:

1. write a base generation,
2. write a later generation,
3. corrupt the latest generation so load falls back to the base generation,
4. run a fresh process that reads the base generation and then performs one
   durable write,
5. physically inspect the resulting latest catalog and prove `FREEPAGE` ranges
   now cover pages written only by the rejected generation.

The storage smoke reports the rejected-generation subranges as
`reclaimed_page_ranges`. It still verifies `FREEPAGE` records do not overlap
live roots. A row write after fallback remains part of the smoke so reclaimed
state is published by a normal durable catalog update. The live row allocation
does not have to land inside the rejected generation interval, because the
scanner intentionally reclaims every unprotected complete page and the allocator
may consume earlier reclaimed ranges first.

## Affected Subsystems

- MyLite catalog generation load and free-range helpers in `ha_mylite.cc`.
- Storage smoke recovery workflow in `storage_engine_smoke.cc` and
  `tools/run-storage-engine-smoke.sh`.
- Slice, architecture, and roadmap docs.

No SQL parser, optimizer, public API, or table definition image surface should
change.

## DDL Metadata Routing Impact

DDL metadata routing is unchanged. Reclaimed pages are storage allocation state
only; `CREATE`, copy `ALTER`, `DROP`, and `RENAME` continue to mutate catalog
metadata through the existing MyLite handler paths.

## Single-File And Embedded-Lifecycle Implications

Orphan reclaim stays inside the primary `.mylite` file and introduces no
companion files. The load path remains read-only; reclaim affects only in-memory
allocator state until the next successful catalog write.

## Public API And File-Format Impact

The public C API is unchanged.

No new page type or catalog record is added. Reclaimed orphan pages are
serialized through existing `FREEPAGE` records on the next write.

## Binary-Size Impact

Expected impact is small first-party code for protected-range construction and
file page scanning, plus smoke assertions. No dependency is allowed.

Measured after implementation:

- `libmariadbd.a`: 44,386,226 bytes
- `libmylite.a`: 29,698 bytes
- `mylite-storage-engine-smoke`: 22,703,056 bytes
- `mylite-compatibility-smoke`: 22,703,744 bytes
- `mylite-open-close-smoke`: 22,770,432 bytes
- `mylite-embedded-bootstrap-smoke`: 22,702,880 bytes

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

- recovery fallback still ignores the corrupted latest generation,
- a later write after fallback publishes pages left by that corrupted
  generation as `FREEPAGE` records,
- latest `FREEPAGE` records do not overlap live roots,
- normal row, index, autoincrement, overflow, persistence, and sidecar checks
  still pass.

Implementation evidence from the storage smoke:

- `orphan_page_interval=161:26`
- `reclaimed_page_ranges=161:2,164:11,176:7`
- `recovery_reclaim_row_range=392:66`
- `freepage_records=26`
- `freepage_pages=467`

## Acceptance Criteria

- Loading an accepted generation merges unreferenced complete page ids into the
  in-memory free list.
- Reclaim protects the accepted catalog payload, row roots, index roots, header
  pages, and existing accepted `FREEPAGE` ranges.
- Reclaimed ranges are serialized only on a later successful write.
- Storage smoke observes rejected-generation pages serialized as `FREEPAGE`
  ranges after fallback from a corrupted latest generation.
- Existing storage, recovery, compatibility, embedded lifecycle, and
  `libmylite` lifecycle smokes pass.
- No persistent `.frm`, engine sidecars, dynamic plugin artifacts, or catalog
  temporary sidecars are introduced.

## Risks And Unresolved Questions

- This is not transaction rollback; it only reclaims pages that no accepted
  generation protects.
- Tail truncation remains deferred.
- If a future format stores non-page-aligned durable state after the page area,
  the scan must learn those protected regions before that format lands.
