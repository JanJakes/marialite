# Row Overflow Storage Slice

## Problem Statement

`row-slot-storage` writes compact page-local row directories, but it still
rejects any supported table whose fixed MariaDB record image is larger than one
row slot page. With the current 4096-byte page size, that means a practical
3984-byte ceiling for non-BLOB rows.

That limit keeps the row-slot format simple, but it blocks ordinary wider
`VARCHAR` rows even though MyLite already stores row payloads in typed page
chains. This slice should add overflow row payload pages for large non-BLOB
fixed record images while keeping BLOB/TEXT handling deferred.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/table.h` defines `TABLE_SHARE::reclength`,
  `stored_rec_length`, `blob_fields`, and `rec_buff_length`; MyLite currently
  persists `table->s->reclength` bytes from handler row buffers.
- `vendor/mariadb/server/sql/handler.cc` asserts field pointers live inside
  `table->record[0]` up to `table->s->reclength` for handler reads.
- `vendor/mariadb/server/sql/field.h` shows `Field_varstring::pack_length()`
  stores inline length bytes plus payload inside the record image, while
  `Field_blob::pack_length()` includes a pointer-sized component and exposes
  `pack_length_no_ptr()` for the actual stored bytes. MyLite should not persist
  raw BLOB pointer values as durable data.
- Current MyLite row storage lives in
  `vendor/mariadb/server/storage/mylite/ha_mylite.cc`:
  `mylite_table_supports_row_storage()`,
  `mylite_pack_row_slot_page_payloads_locked()`,
  `mylite_append_row_slot_page_payload()`,
  `mylite_parse_row_slot_pages_locked()`, and
  `mylite_parse_row_slot_page_payload_locked()`.
- Storage smoke currently records `unsupported_large_row=rejected` in
  `vendor/mariadb/server/mylite/storage_engine_smoke.cc` and verifies row page
  type `2` in `tools/run-storage-engine-smoke.sh`.

## Scope

This slice will:

- add a row overflow segment payload format inside existing row payload page
  type `2`,
- allow non-BLOB fixed record images larger than one row slot page,
- keep row slot pages for rows that fit in the current page-local directory,
- write large rows as one or more overflow segment pages in the table row
  payload chain,
- validate overflow segment ordering, bounds, row ownership, and checksum
  before accepting a catalog generation,
- keep reading legacy `MYLITE ROWS 1` text row payloads and current
  `MYLITEROWSLOT2` row slot pages,
- update storage smoke to persist and reopen a large non-BLOB row,
- update physical page inspection to prove overflow segment pages exist,
- record binary-size and file-size impact.

## Non-Goals

- Do not support BLOB, TEXT, JSON, GEOMETRY, or other pointer-backed row
  payloads.
- Do not add row compression, typed column encoding, or record-format
  normalization.
- Do not implement in-place row updates, free-list reuse, vacuum, or
  compaction.
- Do not implement B-trees or incremental index updates.
- Do not add undo, redo, WAL, transaction rollback, or cross-process locking.
- Do not change public `libmylite` APIs.
- Do not stabilize the pre-release file format.

## Proposed Design

### Row Storage Support Gate

`mylite_table_supports_row_storage()` should continue to reject tables with
`table->s->blob_fields != 0`, but it should stop rejecting non-BLOB tables only
because `table->s->reclength` is greater than
`mylite_row_slot_max_record_length`.

The handler continues to persist and restore exactly `table->s->reclength`
bytes for supported rows. This is still a raw MariaDB record-image bridge, not a
stable MyLite column encoding.

### Mixed Row Payload Chain

The catalog `ROWPAGE` root still points to a sequential page chain of outer page
type `row_payload` (`2`). The logical payload checksum remains the checksum of
the concatenated used payload bytes from every page in that chain.

New writes may mix two inner row payload formats:

- existing row slot pages for rows that fit in a page-local slot directory,
- new overflow segment pages for rows that do not fit.

Legacy `MYLITE ROWS 1` table-sized text streams remain load-only.

### Overflow Segment Payload

Use a new inner payload magic and format version:

```text
bytes  0..15   row-overflow magic: MYLITEROWOVF3
bytes 16..19   row_overflow_format_version: 3
bytes 20..27   rowid
bytes 28..35   total_record_length
bytes 36..43   segment_offset
bytes 44..47   segment_length
bytes 48..51   reserved, zero
bytes 52..end  segment bytes
```

Each overflow page stores one contiguous segment of one row's fixed record
image. Segment payload capacity is:

```text
4032 - 52 = 3980 bytes
```

Writers should emit segments for a row in increasing `segment_offset` order.
Readers should require contiguous segment order per row in this first version.
That keeps validation simple and matches the append-only writer.

### Write Protocol

For each table row payload rewrite:

1. iterate live rows in rowid order,
2. pack rows that fit into `MYLITEROWSLOT2` pages as today,
3. flush the current slot page before writing any overflow row,
4. split each oversized fixed record image into `MYLITEROWOVF3` segment pages,
5. append all pages sequentially as outer page type `2`,
6. compute the logical payload length and checksum over all inner payloads,
7. publish the table's catalog `ROWPAGE` root in the same header protocol.

Deletes still omit deleted rows from the next rewrite. Updates still rewrite
the table payload. Free-space reuse remains deferred.

### Load And Validation

During catalog generation load, MyLite should:

1. read each row payload page in the `ROWPAGE` chain,
2. dispatch by inner magic,
3. parse row slot pages as today,
4. parse overflow segment pages into per-row builders,
5. require row ids to be nonzero and unique across completed rows,
6. require each overflow row's segments to start at offset 0, be contiguous,
   and finish exactly at `total_record_length`,
7. reject segment lengths that do not match the page payload length,
8. reject malformed reserved bytes, duplicate row ids, empty rows, invalid page
   order, checksum mismatch, or non-terminal `next_page_id` at chain end.

Only after every row slot page and overflow row validates should the catalog
generation be accepted.

## Affected Subsystems

- MyLite storage-engine row payload support checks and row page pack/load code
  in `ha_mylite.cc`.
- Storage smoke DML and persistence coverage in `storage_engine_smoke.cc`.
- Storage smoke physical file-format inspection in
  `tools/run-storage-engine-smoke.sh`.
- Architecture and roadmap documentation.

The SQL parser, optimizer, public C API, and table-discovery surfaces should not
change.

## Single-File And Embedded-Lifecycle Implications

Overflow segments stay inside the primary `.mylite` file as typed row payload
pages. The slice must not introduce durable sidecars. Append-only orphan
overflow pages after an unpublished header remain acceptable until free-list and
compaction work exists.

## Binary-Size Impact

Expected impact is first-party code for overflow segment packing, parsing,
validation, and smoke assertions. No dependency is allowed. Record measured
artifacts after implementation.

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

- a `VARCHAR(5000)` or wider non-BLOB MyLite table can be created,
- large rows can be inserted, read, updated, deleted, flushed, and reopened,
- fresh-process persistence reads the full large value length and contents,
- recovery fallback still works when the latest generation is corrupted,
- physical catalog inspection finds `MYLITEROWOVF3` segment pages under row
  page type `2`,
- `unsupported_blob=rejected` remains true for BLOB/TEXT tables,
- no `.frm` artifacts,
- no catalog temporary sidecars.

The compatibility harness should continue to verify:

- MariaDB reference fingerprints match MyLite fingerprints for the supported
  subset,
- sidecar scan reports no unexpected MyLite sidecars.

## Acceptance Criteria

- Non-BLOB fixed row images larger than one row slot page are accepted and
  persisted.
- BLOB/TEXT tables remain explicitly unsupported.
- Small rows continue to use row slot pages.
- Overflow rows are split into validated row payload segment pages inside the
  primary `.mylite` file.
- Existing legacy text row payloads and current row slot pages still load.
- Index and autoincrement behavior remains unchanged for supported tables.
- Existing storage, recovery, compatibility, embedded lifecycle, and
  `libmylite` lifecycle smokes pass.
- No persistent `.frm`, engine table sidecars, dynamic plugin artifacts, or
  catalog temporary sidecars are introduced.
- Binary and file-size changes are recorded.

## Risks And Unresolved Questions

- Raw MariaDB record images remain a temporary bridge.
- Large rows still rewrite table payloads on every mutation.
- Overflow segments have no free-list, reuse, compaction, or partial update
  path.
- Very large fixed record images may need explicit size limits beyond what
  MariaDB table definitions already allow.
- BLOB/TEXT support requires a separate design because durable storage cannot
  preserve raw pointer values from `Field_blob`.
