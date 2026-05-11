# Virtual generated LOB and geometry storage

> Superseded note: this slice intentionally kept virtual generated
> BLOB/TEXT/GEOMETRY key parts out of scope.
> `docs/specs/virtual-generated-lob-index-storage/specs.md` later added
> ordinary prefix/HASH/unique and FULLTEXT key support for virtual generated
> BLOB/TEXT values. Virtual generated GEOMETRY SPATIAL keys remain constrained
> by MariaDB SQL because virtual generated columns cannot be declared
> `NOT NULL`.

## Problem

MyLite supports generated columns, generated-column indexes for supported
scalar virtual values, BLOB/TEXT row storage, and GEOMETRY row storage. It
still rejects non-stored virtual generated columns whose generated field is
BLOB/TEXT or GEOMETRY. That is an ordinary application DDL shape and no longer
needs a blanket rejection: non-stored virtual generated values are not durable
row payload. MariaDB owns their expression evaluation after handler reads.

The remaining MyLite-specific problem is the raw-record bridge. MariaDB includes
non-stored virtual BLOB fields in `TABLE_SHARE::blob_field`, while MyLite's row
encoder currently interprets every BLOB entry as durable payload. MyLite must
skip non-stored virtual BLOB/GEOMETRY fields when serializing and
deserializing stored rows, then let MariaDB compute their values in the record
buffer.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/field.h:1494` defines
  `Field::stored_in_db()` as false for non-stored virtual generated fields.
- `vendor/mariadb/server/sql/table.cc` increments
  `TABLE_SHARE::virtual_not_stored_blob_fields` for non-stored virtual BLOB
  fields but still records all BLOB fields in `TABLE_SHARE::blob_field`.
- `vendor/mariadb/server/sql/handler.cc` handler read wrappers call
  `TABLE::update_virtual_fields(..., VCOL_UPDATE_FOR_READ)` after engine reads.
- `vendor/mariadb/server/sql/table.cc:9243` through nearby code implements
  `TABLE::update_virtual_fields()` and has explicit swap handling for virtual
  BLOB values.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:mylite_encode_record()`
  and `mylite_decode_record()` currently iterate every `blob_field` entry and
  append/reconstruct BLOB payload bytes for all of them.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:
  mylite_table_supports_generated_columns()` currently rejects non-stored
  virtual BLOB/TEXT and GEOMETRY field shapes.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:
  mylite_key_part_supports_storage()` separately rejects non-stored virtual
  BLOB/TEXT and GEOMETRY key parts. That key-shape rejection remains correct
  until those index paths have a dedicated design.

## Scope

This slice will:

- accept non-indexed non-stored virtual generated BLOB/TEXT fields;
- accept non-indexed non-stored virtual generated GEOMETRY fields;
- skip those fields when encoding stored row payload bytes;
- skip those fields when validating and reconstructing stored row payload bytes;
- verify same-process reads and base-column updates;
- verify fresh-process reopen for persisted virtual generated TEXT and
  GEOMETRY rows;
- update docs from "rejected" to "supported for non-indexed virtual values".

## Non-Goals

- Do not support indexes, unique constraints, FULLTEXT indexes, SPATIAL
  indexes, or foreign keys over non-stored virtual BLOB/TEXT or GEOMETRY
  generated columns in this slice.
- Do not persist generated virtual values in row pages.
- Do not add a MyLite expression evaluator.
- Do not change MariaDB SQL-mode, determinism, or generated-expression
  validation rules.

## Design

Treat non-stored virtual generated fields as non-durable when the MyLite row
bridge walks BLOB metadata:

1. Add a small helper that returns true only for BLOB-like fields whose value is
   stored in the database record.
2. In `mylite_encode_record()`, keep copying the fixed stored record prefix,
   but append external BLOB payload bytes only for stored fields. Non-stored
   virtual BLOB/GEOMETRY fields are already outside the stored row payload;
   clear their fixed record bytes so transient native pointer values are not
   written, and do not append payload bytes for them.
3. In `mylite_decode_record()`, validate required BLOB payload bytes and
   reconstruct `Field_blob` pointers only for stored fields. Non-stored virtual
   values will be materialized by MariaDB's read wrapper.
4. Relax the row-shape generated-column gate so non-stored virtual
   BLOB/TEXT/GEOMETRY fields are accepted.
5. Keep key-shape validation unchanged so virtual generated BLOB/TEXT/GEOMETRY
   key parts remain explicit rejections.

## Affected Subsystems

- MyLite row encode/decode bridge.
- MyLite generated-column row-shape validation.
- Storage-engine smoke write and reopen phases.
- Roadmap and single-file storage documentation.

## Single-File and Embedded Lifecycle

No new durable records or companion files are introduced. The primary
`.mylite` file stores only base row columns and stored generated values.
Non-stored virtual BLOB/TEXT/GEOMETRY values remain transient MariaDB
record-buffer values.

## Public API and File Format Impact

No public API or file-format version change is expected. Existing row payloads
are compatible because virtual non-stored fields were previously rejected.

## Binary Size Impact

Expected growth is small: helper logic and smoke assertions only.

## License and Dependency Impact

No new dependency or licensing change.

## Test Plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

## Acceptance Criteria

- MyLite accepts and reads virtual generated TEXT values.
- Updating base columns changes virtual generated TEXT values without durable
  BLOB payload corruption.
- MyLite accepts and reads virtual generated GEOMETRY values through MariaDB
  spatial functions.
- Persisted virtual generated TEXT and GEOMETRY metadata survives
  fresh-process reopen.
- At this slice's completion, virtual generated BLOB/TEXT/GEOMETRY key parts
  remained rejected until a dedicated index design existed. The later
  `virtual-generated-lob-index-storage` slice added BLOB/TEXT prefix and
  FULLTEXT support; virtual generated GEOMETRY SPATIAL keys remain constrained
  by MariaDB SQL syntax.
- Existing generated-column index, BLOB/TEXT, GEOMETRY/SPATIAL, FK, CHECK,
  transaction, recovery, lifecycle, and sidecar checks keep passing.

## Risks and Unresolved Questions

- MariaDB's virtual BLOB handling uses `Field_blob::swap_value_and_read_value()`
  around read-time materialization. The slice relies on inherited handler read
  wrappers to keep that lifecycle correct.
- Indexed virtual LOB/GEOMETRY values may need prefix, fulltext, spatial, and
  foreign-key semantics that are different from scalar generated key images.

## Implementation Result

Implemented in `vendor/mariadb/server/storage/mylite/ha_mylite.cc` and
`vendor/mariadb/server/mylite/storage_engine_smoke.cc`.

- MyLite now accepts non-indexed non-stored virtual generated BLOB/TEXT and
  GEOMETRY row shapes.
- `mylite_encode_record()` clears non-stored virtual BLOB/GEOMETRY fixed record
  bytes and skips their external payloads while walking
  `TABLE_SHARE::blob_field`; `mylite_decode_record()` skips reconstructing
  pointers for those fields. MyLite stores only durable base/stored column
  bytes and MariaDB materializes virtual LOB/GEOMETRY values after handler
  reads.
- At this slice's completion, virtual generated BLOB/TEXT and GEOMETRY key
  parts remained explicitly rejected by the key-shape gate until prefix,
  FULLTEXT, SPATIAL, and related key-image semantics had a dedicated design.
  The later `virtual-generated-lob-index-storage` slice added BLOB/TEXT prefix
  and FULLTEXT support; virtual generated GEOMETRY SPATIAL keys remain
  constrained by MariaDB SQL syntax.
- Storage smoke coverage for this slice verified same-process virtual generated
  TEXT reads, base-column update rematerialization, virtual generated GEOMETRY
  reads through `ST_AsText()`, persisted fresh-process reopen for both row
  shapes, and the historical key-shape rejections.
- Roadmap, generated-column notes, virtual-generated-index notes, and
  single-file storage docs now describe the supported row-shape boundary.

Verification passed:

- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
- `git diff --check`

Representative storage reports:

- Historical `mylite-storage-engine-report.txt`: `status=0`, `message=ok`,
  `generated_virtual_lob_rows=1:alpha:alpha,2:beta:beta`,
  `generated_virtual_lob_updated_rows=1:gamma:gamma,2:beta:beta`,
  `generated_virtual_geometry_rows=1:POINT(1 2),2:POINT(3 4)`,
  `unsupported_generated_virtual_lob_key=rejected`, and
  `unsupported_generated_virtual_geometry_key=rejected`.
- `mylite-catalog-write-report.txt`: `status=0`, `message=ok`,
  `persisted_generated_virtual_lob_rows=1:amber:amber,2:blue:blue`, and
  `persisted_generated_virtual_geometry_rows=1:POINT(5 6),2:POINT(7 8)`.
- `mylite-catalog-read-report.txt`: `status=0`, `message=ok`, with the same
  persisted virtual generated LOB and GEOMETRY row values after fresh-process
  reopen.
- `mylite-compatibility-harness-report.txt`, `libmylite-open-close-report.txt`,
  and `mylite-embedded-bootstrap-report.txt` all reported `status=0`.
