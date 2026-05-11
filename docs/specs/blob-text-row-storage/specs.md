# BLOB/TEXT Row Storage Slice

## Problem Statement

MyLite currently rejects tables with BLOB/TEXT fields because the raw MariaDB
record image contains pointer-backed blob state. Persisting those pointer bytes
is not durable, and reading them back in a fresh process would leave invalid
addresses in the row buffer.

The row-page and row-overflow formats can already store variable-length row
payloads larger than one page. This slice should use that capacity to support
non-key BLOB/TEXT columns in the current raw-record bridge without designing
the final typed row encoding.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Official MariaDB documentation:
  - <https://mariadb.com/kb/en/blob/> documents BLOB storage classes.
  - <https://mariadb.com/kb/en/text/> documents TEXT storage classes.
- `vendor/mariadb/server/sql/field.h` and
  `vendor/mariadb/server/sql/field.cc`:
  - `Field_blob::pack_length()` is the row-buffer length bytes plus a native
    pointer,
  - `Field_blob::pack_length_no_ptr()` is only the length-byte portion stored
    in the row record before the pointer,
  - `Field_blob::get_length()` reads the length bytes,
  - `Field_blob::get_ptr()` reads the pointer-backed payload,
  - `Field_blob::set_ptr_offset()` writes length and pointer data for a row
    buffer at a nonzero record offset,
  - `Field_blob::val_str()` reads the pointer-backed payload.
- `vendor/mariadb/server/sql/table.cc` records blob field indexes in
  `TABLE_SHARE::blob_field` when `share->blob_fields` is nonzero.
- `vendor/mariadb/server/sql/handler.cc` checksum code treats BLOB, GEOMETRY,
  VARCHAR, and BIT specially because BLOB and VARCHAR fields have pointer or
  variable backing and need value extraction instead of raw-byte checksums.
- `vendor/mariadb/server/storage/archive/ha_archive.cc` demonstrates the
  bridge pattern MyLite can adapt: copy the fixed record image, append BLOB
  payloads in `table->s->blob_field` order, then on read set each
  `Field_blob` pointer to a handler-owned read buffer.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc` currently rejects
  `table->s->blob_fields != 0` in `mylite_table_supports_row_storage()`,
  stores `Mylite_row::record` as a variable-length byte vector, and already
  writes row vectors through row slot and overflow pages.

## Scope

This slice will:

- support BLOB/TEXT fields that are not part of a MyLite-supported key,
- keep `HA_BLOB_PART` key parts unsupported,
- encode rows with a sanitized fixed record prefix followed by BLOB/TEXT
  payload bytes in `table->s->blob_field` order,
- zero the native pointer bytes in the fixed record prefix before persistence,
- reconstruct BLOB/TEXT pointers into handler-owned read buffers on table scan,
  position read, and index read paths,
- keep existing non-BLOB rows byte-for-byte compatible,
- add storage smoke coverage for BLOB and TEXT insert, update, delete,
  transaction rollback, and fresh-process persistence.

## Non-Goals

- Do not support BLOB/TEXT key parts, FULLTEXT, SPATIAL, prefix indexes over
  BLOB/TEXT, GEOMETRY columns, or generated keys.
- Do not add a final typed row format, column-level encoding, compression, or
  per-column page layout.
- Do not add page-level undo, redo, WAL, or MVCC.
- Do not change public `libmylite` API or primary file header format.
- Do not support external durable sidecars for BLOB/TEXT payloads.

## Proposed Design

Add two record helpers:

- `mylite_encode_record(TABLE *table, const uchar *record,
  std::vector<uchar> *encoded)`,
- `mylite_decode_record(TABLE *table, const Mylite_row &row, uchar *record,
  std::vector<uchar> *blob_buffer)`.

For tables without BLOB fields, encoding remains the current raw
`table->s->reclength` copy and decoding remains a raw copy. This keeps existing
row payloads and compatibility fixtures unchanged.

For tables with BLOB fields:

1. copy the first `table->s->reclength` bytes into the encoded vector,
2. for each blob field index in `table->s->blob_field` order:
   - use `Field_blob::get_length()` and `Field_blob::get_ptr()` against the
     incoming row buffer,
   - validate that the payload fits in the field's recorded length,
   - zero the native pointer bytes in the encoded fixed prefix,
   - append exactly `length` payload bytes to the encoded vector,
3. store the encoded vector in `Mylite_row::record`.

On decode:

1. require the encoded vector to contain at least `table->s->reclength` bytes,
2. copy the fixed prefix into the output row buffer,
3. clear and resize the handler-owned `blob_buffer` to hold all blob payload
   bytes for the decoded row,
4. walk blob fields in `table->s->blob_field` order, read each field length
   from the fixed prefix, copy the corresponding payload bytes into
   `blob_buffer`, and call `Field_blob::set_ptr_offset()` with the output row
   offset and stable buffer pointer,
5. reject truncated or extra blob payload bytes as catalog corruption.

This keeps BLOB/TEXT row payloads inside existing row slot and overflow pages.
It avoids persisting native addresses while preserving enough raw-record shape
for current non-BLOB key extraction.

## Affected Subsystems

- MyLite row support checks in `ha_mylite.cc`.
- MyLite row write, update, table scan, position read, and index read decode
  paths.
- MyLite handler instance state in `ha_mylite.h` for read-time blob buffers.
- Storage smoke DML and persistence phases.
- Roadmap and single-file storage docs.

## DDL Metadata Routing Impact

`CREATE TABLE ... ENGINE=MYLITE` should accept BLOB/TEXT columns when they are
not part of a key MyLite already rejects. DDL still stores the MariaDB frm image
inside the `.mylite` catalog and must not create durable `.frm` sidecars.

## Single-File And Embedded-Lifecycle Implications

No new files are introduced. BLOB/TEXT bytes live inside the existing primary
`.mylite` row payload page chains and row overflow page chains. Handler-owned
read buffers are process memory and are discarded on close.

## Public API And File-Format Impact

No public API change. The primary file header and page types are unchanged.
Existing row payload records remain readable:

- non-BLOB rows continue to be stored as raw fixed record images,
- BLOB/TEXT rows are distinguishable by the table definition's blob metadata
  and an encoded length greater than or equal to `table->s->reclength`.

## Binary-Size Impact

Measured impact is small in scope: the implementation adds record
encode/decode helpers, a handler-owned blob read buffer, verifier logic, and
smoke coverage. It adds no dependency and no new compiled MariaDB subsystem.

The post-implementation `MinSizeRel` build report from
`MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh` records:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,415,256 bytes,
- archive objects: 571,
- dynamic plugin artifacts: none.

## License, Trademark, And Dependency Impact

No new dependencies, license changes, or trademark changes.

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

Storage smoke should verify:

- `CREATE TABLE` with non-key BLOB/TEXT fields succeeds,
- insert and scan return BLOB/TEXT lengths and edge bytes correctly,
- update replaces BLOB/TEXT payloads,
- delete removes rows with BLOB/TEXT payloads,
- transaction rollback restores BLOB/TEXT payload state,
- fresh-process reopen returns committed BLOB/TEXT payloads,
- BLOB/TEXT key parts remain rejected,
- GEOMETRY columns remain rejected,
- no `.frm`, journal/WAL companions, dynamic plugin artifacts, or catalog
  temporary sidecars are introduced.

## Implementation Result

Implemented in `vendor/mariadb/server/storage/mylite/ha_mylite.cc` and
`ha_mylite.h` with `mylite_encode_record()` and `mylite_decode_record()`.
Tables without BLOB fields keep the existing exact fixed-record encoding.
Tables with BLOB/TEXT fields store a fixed record prefix with pointer bytes
zeroed, followed by BLOB/TEXT bytes in `TABLE_SHARE::blob_field` order. Reads
validate the stored length, rebuild handler-owned BLOB buffers, and call
`Field_blob::set_ptr_offset()` for table scans, position reads, and index
reads.

The storage smoke now covers non-key BLOB/TEXT insert, update, delete,
rollback, and fresh-process reopen. It also keeps BLOB/TEXT key parts and
GEOMETRY columns rejected, and updates row-page verification to compare reuse
against explicit free-list ranges plus orphan gaps that the loader can reclaim
before allocation.

Observed report evidence:

- `blob_text_lengths=3000:5000,5:0`,
- `blob_text_edges=n:p,s:`,
- `blob_text_updated=4200:4100:u:q`,
- `unsupported_geometry=rejected`,
- `persisted_blob_text_lengths=5000:4200,700:800`,
- `persisted_blob_text_edges=x:a,z:w`,
- `row_overflow_payloads=mylite.persisted_large,mylite.persisted_blob_text`.

## Acceptance Criteria

- MyLite accepts non-key BLOB/TEXT columns and continues to reject BLOB/TEXT key
  parts.
- Persisted BLOB/TEXT rows do not contain durable native pointer values.
- Table scan, position read, and index read paths reconstruct valid
  `Field_blob` pointers for the current row.
- BLOB/TEXT insert, update, delete, transaction rollback, and reopen behavior
  pass storage and compatibility harnesses.
- Existing non-BLOB storage, transaction, savepoint, and statement-error
  smokes continue to pass.

## Risks And Unresolved Questions

- This is still a raw-record bridge, not the final typed row format.
- Handler-owned blob buffers are valid only until the next row read on the same
  handler instance; this matches the current handler-row lifecycle but should
  be revisited for cursor materialization and advanced executor paths.
- BLOB/TEXT key support needs a separate source-grounded design because key
  image generation and prefix semantics require more than row payload storage.
