# Virtual generated LOB index storage

## Problem

MyLite now supports non-indexed non-stored virtual generated BLOB/TEXT and
GEOMETRY row values, but still rejects key parts backed by those virtual
fields. That leaves an application-visible gap after the generated-column,
BLOB/TEXT key, and FULLTEXT slices: deterministic virtual generated text values
should be usable in ordinary prefix indexes, unique prefix constraints, HASH
metadata, and FULLTEXT search definitions.

The existing row bridge already keeps virtual LOB/GEOMETRY values transient.
The remaining work is key-time materialization. MyLite must materialize
virtual generated key-part values before building ordinary key images and
before scoring FULLTEXT rows. Virtual generated GEOMETRY SPATIAL keys remain
outside this slice because MariaDB's grammar has no `NOT NULL` generated-column
attribute, while SPATIAL key validation requires non-null geometry fields.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/storage/mylite/ha_mylite.h` advertises
  `HA_CAN_INDEX_BLOBS`, `HA_CAN_FULLTEXT`, `HA_CAN_GEOMETRY`,
  `HA_CAN_RTREEKEYS`, and `HA_CAN_VIRTUAL_COLUMNS`.
- `vendor/mariadb/server/sql/sql_table.cc` maps FULLTEXT keys to
  `HA_FULLTEXT_legacy`/`HA_KEY_ALG_FULLTEXT` and SPATIAL keys to
  `HA_SPATIAL_legacy`/`HA_KEY_ALG_RTREE` when the handler advertises support.
- `vendor/mariadb/server/sql/sql_table.cc` calls
  `Column_definition::check_vcol_for_key()` for ordinary and SPATIAL keys;
  `vendor/mariadb/server/sql/field.cc` rejects only not-strictly-deterministic
  virtual columns there.
- `vendor/mariadb/server/sql/sql_yacc.yy` generated-column attributes are
  limited to `UNIQUE`, `COMMENT`, and `INVISIBLE`, so user SQL cannot declare a
  virtual generated geometry column `NOT NULL` for SPATIAL key validation.
- `vendor/mariadb/server/sql/sql_class.h:8127` wraps `ft_read()` with
  `handler::ha_ft_read()` and runs `TABLE::update_virtual_fields()` for rows
  returned into `table->record[0]`.
- `vendor/mariadb/server/sql/handler.cc` calls
  `TABLE::update_virtual_fields(..., VCOL_UPDATE_FOR_READ)` after ordinary
  handler reads, and has virtual BLOB pointer swap handling for long unique key
  checks.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:
  mylite_make_key_image()` already materializes supported virtual key parts by
  copying the source row into `table->record[0]`, calling
  `TABLE::update_virtual_field()`, and building the key with `key_copy()`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:
  mylite_key_supports_storage()` still rejects virtual generated FULLTEXT key
  parts explicitly.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:
  mylite_key_part_supports_storage()` still rejects non-stored virtual
  BLOB/TEXT and GEOMETRY key parts for ordinary keys.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:
  mylite_fulltext_score_record()` currently scores decoded rows without
  materializing virtual generated FULLTEXT key fields first.

## Scope

This slice will:

- accept ordinary BTREE/HASH indexes over deterministic non-stored virtual
  generated BLOB/TEXT key parts when MariaDB provides valid key metadata;
- enforce unique prefix constraints over those virtual generated BLOB/TEXT key
  parts through the existing durable key-image stream;
- support copy `ALTER` adding those virtual generated LOB indexes to populated
  MyLite tables;
- accept FULLTEXT key definitions over deterministic non-stored virtual
  generated text fields;
- materialize virtual generated FULLTEXT key fields before MyLite's full-scan
  FULLTEXT scorer reads key-part text;
- verify same-process DDL/DML/query behavior and fresh-process reopen.

## Non-Goals

- Do not add a physical inverted FULLTEXT index.
- Do not add a physical R-tree or handler-level MBR range scan.
- Do not persist generated virtual values in row payloads.
- Do not support foreign keys over virtual generated BLOB/TEXT or GEOMETRY key
  parts in this slice.
- Do not support SPATIAL keys over virtual generated GEOMETRY fields; MariaDB
  SQL cannot mark virtual generated columns `NOT NULL`, and SPATIAL keys require
  non-null geometry fields.
- Do not bypass MariaDB's generated-expression determinism, SQL-mode,
  FULLTEXT-column, or SPATIAL-column validation.

## Design

1. Split key-part validation by key family instead of using a single rejection
   for all virtual generated BLOB/TEXT/GEOMETRY fields.
2. Ordinary BTREE/HASH key validation should accept non-stored virtual
   generated BLOB/TEXT key parts because `mylite_make_key_image()` already
   materializes virtual key fields before `key_copy()`. Continue rejecting
   non-stored virtual generated GEOMETRY parts for ordinary ordered keys.
3. FULLTEXT key validation should accept non-stored virtual generated
   BLOB/TEXT key parts and continue rejecting GEOMETRY parts.
4. `mylite_fulltext_score_record()` should materialize virtual generated key
   fields for a FULLTEXT key before calling `mylite_fulltext_record_text()`.
   This keeps the full-scan scorer aligned with ordinary virtual generated key
   materialization.
5. Keep FULLTEXT keys out of ordinary durable `INDEXPAGE` roots, as the existing
   metadata-backed implementation does.

## Affected Subsystems

- MyLite key-shape validation.
- MyLite virtual generated key materialization.
- MyLite metadata-backed FULLTEXT scorer.
- Storage-engine smoke write, reopen, and recovery coverage.
- Roadmap and single-file storage documentation.

## DDL Metadata Routing Impact

The table-definition image continues to carry canonical MariaDB generated
column and key metadata. No `.frm`, FULLTEXT, or auxiliary index sidecar is
introduced. Failed DDL must still leave no discoverable MyLite table or
replacement-table mutation.

## Single-File and Embedded Lifecycle

Ordinary virtual generated LOB indexes use the existing durable `INDEXPAGE`
key-image stream. FULLTEXT virtual generated keys remain metadata-backed and do
not publish ordinary ordered roots. No new companion files are introduced.

## Public API and File Format Impact

No public API change is required. No file-format version change is expected:
ordinary virtual LOB key images reuse existing key payload pages, and FULLTEXT
metadata remains in the persisted table-definition image.

## Binary Size Impact

Expected growth is small: key validation helpers, one FULLTEXT materialization
path, and smoke assertions.

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

- `CREATE TABLE` succeeds for a virtual generated TEXT prefix index and unique
  prefix constraint.
- Forced lookup and ordered scan over the virtual generated TEXT prefix index
  return expected rows.
- Duplicate non-null virtual generated TEXT unique prefixes are rejected.
- Base-column updates refresh virtual generated TEXT key images.
- Copy `ALTER` can add a virtual generated TEXT prefix key to populated rows.
- FULLTEXT search over a virtual generated TEXT field returns expected rows,
  reflects updates/deletes, and persists across fresh-process reopen.
- Existing generated-column, virtual generated scalar index, BLOB/TEXT,
  GEOMETRY/SPATIAL, FULLTEXT, FK, CHECK, transaction, recovery, lifecycle, and
  sidecar checks keep passing.

## Risks and Unresolved Questions

- SPATIAL keys over virtual generated geometry remain constrained by MariaDB's
  generated-column grammar and SPATIAL non-null requirement. MyLite should
  follow MariaDB's SQL-layer decision rather than inventing a MyLite-specific
  syntax.
- Virtual generated FULLTEXT over base BLOB/TEXT fields depends on MariaDB
  pointer lifetimes after the MyLite decoded row is copied into
  `table->record[0]`; this slice should cover generated text over common
  VARCHAR/TEXT source expressions and avoid broad claims beyond verified
  behavior.
- Physical FULLTEXT and SPATIAL index formats remain separate performance and
  compatibility slices.

## Implementation Result

Implemented in `vendor/mariadb/server/storage/mylite/ha_mylite.cc` and
`vendor/mariadb/server/mylite/storage_engine_smoke.cc`.

- Ordinary BTREE/HASH key validation now accepts supported non-stored virtual
  generated BLOB/TEXT key parts and continues rejecting non-stored virtual
  generated GEOMETRY key parts.
- FULLTEXT key validation now accepts supported non-stored virtual generated
  BLOB/TEXT key parts and continues rejecting GEOMETRY key parts.
- `mylite_fulltext_score_record()` materializes virtual generated FULLTEXT key
  fields by copying the decoded source row into `table->record[0]`, calling
  MariaDB's virtual-field evaluator, scoring the materialized text, and then
  restoring the live record buffer.
- Ordinary virtual generated LOB indexes reuse the existing durable
  `INDEXPAGE` key-image stream. FULLTEXT definitions stay metadata-backed and
  do not add auxiliary index sidecars.
- SPATIAL keys over virtual generated GEOMETRY fields remain outside MyLite's
  supported SQL surface because MariaDB SQL cannot declare generated virtual
  columns `NOT NULL`, while SPATIAL key validation requires non-null geometry
  fields.
- Roadmap, generated-column notes, virtual-generated-index notes, virtual LOB
  row-shape notes, and single-file storage docs now describe the updated
  BLOB/TEXT key-shape support and the remaining GEOMETRY SPATIAL boundary.

Storage smoke coverage now verifies:

- same-process forced lookup, ordered scan, HASH metadata lookup, and unique
  duplicate rejection through virtual generated TEXT prefix keys;
- base-column updates refreshing virtual generated TEXT key images;
- copy `ALTER` adding a virtual generated TEXT prefix key to populated rows;
- same-process FULLTEXT search over virtual generated TEXT, including update
  and delete refresh behavior;
- persisted fresh-process reopen of virtual generated TEXT prefix/HASH keys and
  virtual generated FULLTEXT metadata/scoring.

Verification passed with:

- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
- `git diff --check`

Representative storage reports:

- `mylite-storage-engine-report.txt`: `status=0`, `message=ok`,
  `generated_virtual_lob_key_lookup_id=2`,
  `generated_virtual_lob_key_order_ids=1,2,3`,
  `generated_virtual_lob_hash_lookup_id=3`,
  `generated_virtual_lob_key_duplicate=rejected`,
  `generated_virtual_lob_key_updated_rows=3:aardvark:tri,1:alpha:one,2:beta:two`,
  `generated_virtual_lob_key_alter_ids=1,2`,
  `generated_virtual_fulltext_ids=1,3`,
  `generated_virtual_fulltext_updated_ids=1,2,3`, and
  `generated_virtual_fulltext_deleted_ids=2,3`.
- `mylite-catalog-write-report.txt`: `status=0`, `message=ok`,
  `persisted_generated_virtual_lob_key_lookup_id=2`,
  `persisted_generated_virtual_lob_key_order_ids=1,2,3`,
  `persisted_generated_virtual_lob_hash_lookup_id=3`, and
  `persisted_generated_virtual_fulltext_ids=1,3`.
- `mylite-catalog-read-report.txt`: `status=0`, `message=ok`, with the same
  persisted virtual generated LOB key and FULLTEXT values after fresh-process
  reopen.
- `mylite-compatibility-harness-report.txt`, `libmylite-open-close-report.txt`,
  and `mylite-embedded-bootstrap-report.txt` all reported `status=0`.
