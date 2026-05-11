# composite-autoincrement-storage

## Problem Statement

MyLite supports ordinary table-level `AUTO_INCREMENT` keys where the generated
column is the first part of the selected key. It still rejects a MariaDB table
shape used by MyISAM and Aria: an `AUTO_INCREMENT` column that appears after
one or more leading key parts, such as `PRIMARY KEY(tenant, id)`.

For that shape, MariaDB asks the handler for one generated value at a time and
the handler returns `MAX(id) + 1` for the current leading-key prefix. MyLite
already has ordered key-image indexes, so it can route this case through the
inherited handler index lookup path instead of rejecting the table.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/handler.h:152` defines `HA_AUTO_PART_KEY` for
  engines that support autoincrement columns in multi-part keys.
- `vendor/mariadb/server/sql/sql_table.cc:3829` accepts a non-leading
  `AUTO_INCREMENT` key part only when the handler advertises
  `HA_AUTO_PART_KEY`.
- `vendor/mariadb/server/sql/handler.cc:4320` implements
  `handler::update_auto_increment()`. When `next_number_keypart != 0`, it does
  not reserve an interval and calls the engine for each generated row.
- `vendor/mariadb/server/sql/handler.cc:4588` implements the default
  `handler::get_auto_increment()`. For non-leading autoincrement key parts, it
  copies the already-filled leading key prefix from `table->record[0]`, does an
  index prefix lookup on `next_number_index`, reads the last matching row, and
  returns the stored autoincrement value plus one.
- `vendor/mariadb/server/storage/myisam/ha_myisam.cc:728` advertises
  `HA_AUTO_PART_KEY`.
- `vendor/mariadb/server/storage/myisam/ha_myisam.cc:2335` implements the same
  prefix-max behavior directly in MyISAM.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:mylite_table_supports_key_storage()`
  currently rejects MyLite autoincrement tables unless
  `next_number_keypart == 0`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:ha_mylite::get_auto_increment()`
  currently always uses MyLite's table-level durable counter.

## Scope

This slice will:

- advertise `HA_AUTO_PART_KEY` from the MyLite handler,
- accept supported composite-key autoincrement table definitions,
- use the inherited handler prefix lookup for non-leading autoincrement key
  parts,
- preserve existing table-level durable autoincrement counters for leading
  autoincrement keys,
- verify per-prefix generation, explicit high values, update/delete behavior,
  secondary index preservation, and fresh-process reopen.

## Non-Goals

- Do not change MariaDB SQL-layer validation rules for invalid
  autoincrement definitions.
- Do not implement MyISAM-compatible table-level metadata display beyond the
  values MariaDB already requests through `update_create_info()`.
- Do not add a separate per-prefix autoincrement catalog format; the current
  prefix-max index lookup is deterministic from stored rows.

## Proposed Design

Add `HA_AUTO_PART_KEY` to `ha_mylite::table_flags()` so MariaDB accepts
non-leading autoincrement key parts.

Remove MyLite's explicit `next_number_keypart == 0` rejection from
`mylite_table_supports_key_storage()`.

Keep MyLite's existing table-level counter path in
`ha_mylite::get_auto_increment()` for leading autoincrement keys. For
non-leading autoincrement key parts, delegate to
`handler::get_auto_increment()`. The inherited implementation already uses the
handler's ordered index API and asks for one value at a time, which matches
MyLite's current key-image index bridge without adding a new catalog record.

`mylite_advance_auto_increment_locked()` may continue to advance the
table-level counter for explicit values, but that counter is not the source of
truth for non-leading autoincrement keys. The next generated value comes from
the current prefix's stored maximum via the index lookup.

## Affected Subsystems

- MyLite handler capability flags.
- MyLite table-definition support validation.
- MyLite autoincrement generation path.
- Storage smoke same-process and persistence coverage.
- Roadmap and storage design documentation.

## DDL Metadata Routing Impact

No new table-definition image format is needed. The existing frm-backed table
definition records already preserve the composite key and autoincrement column
metadata. Copy ALTER and fresh-process discovery continue to reopen that image.

## Single-File And Embedded-Lifecycle Implications

No durable file-format change. Per-prefix next values are derived from stored
row and index state in the primary `.mylite` file. This avoids a separate
per-prefix counter catalog and keeps rollback/recovery behavior aligned with
row/index publication.

## Public API Or File-Format Impact

No public `libmylite` API change and no file-format version bump.

## Binary-Size Impact

Expected size impact is negligible: one handler flag, removal of one rejection,
and smoke coverage.

## License, Trademark, And Dependency Impact

No new dependency, license, or trademark impact.

## Test And Verification Plan

Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc` to cover:

- creating `PRIMARY KEY(tenant, id)` with `id AUTO_INCREMENT`,
- multi-row generated inserts across multiple tenant prefixes,
- explicit high values advancing only that prefix,
- updates/deletes preserving future prefix generation,
- secondary indexed lookup/order on the composite-autoincrement table,
- persistence write/read for generated per-prefix values after fresh-process
  reopen.

Run:

- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
- `bash -n tools/run-storage-engine-smoke.sh
  tools/run-compatibility-test-harness.sh
  tools/run-libmylite-open-close-smoke.sh
  tools/run-embedded-bootstrap-smoke.sh
  tools/build-mariadb-minsize.sh`
- `git diff --check`

## Acceptance Criteria

- Supported composite-key autoincrement DDL succeeds.
- Generated values are per-prefix and match the current stored maximum for the
  leading key prefix.
- Explicit values advance only the matching prefix through stored row/index
  state.
- Existing leading-key autoincrement, DDL, DML, temporary table, index,
  constraint, transaction, recovery, and public API coverage keeps passing.

## Implementation Result

MyLite now accepts composite-key `AUTO_INCREMENT` definitions where the
generated column is a non-leading key part. The implementation:

- advertises `HA_AUTO_PART_KEY`,
- removes the previous `next_number_keypart == 0` support check,
- delegates non-leading autoincrement generation to MariaDB's inherited
  handler prefix lookup,
- keeps the existing MyLite table-local durable counter for leading
  autoincrement keys,
- adds same-process and fresh-process smoke coverage for per-prefix generated
  values, explicit high values, update/delete behavior, and secondary index
  reads.

Verification passed:

```sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

Observed reports after implementation:

- `mylite-storage-engine-report.txt`:
  `composite_autoincrement_rows=1:2:one-b-up,1:3:one-c,1:8:one-h,1:9:one-i,1:10:one-j,2:1:two-a,2:2:two-b,2:3:two-c,3:1:three-a`
  and `composite_autoincrement_lookup=1:9`.
- `mylite-catalog-write-report.txt`:
  `persisted_composite_autoincrement_rows=1:1:one-a,1:2:one-b,1:7:one-g,1:8:one-h,2:1:two-a,2:2:two-b`.
- `mylite-catalog-read-report.txt`:
  `persisted_composite_autoincrement_rows=1:1:one-a,1:2:one-b,1:7:one-g,1:8:one-h,1:9:one-i,2:1:two-a,2:2:two-b,2:3:two-c,3:1:three-a`.

Observed artifacts after this slice:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 43,505,836 bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,393,976
  bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 93,752 bytes.
- `build/mariadb-minsize/mylite/mylite-open-close-smoke`: 22,405,000 bytes.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,327,680
  bytes.

## Risks And Unresolved Questions

- The inherited handler implementation depends on ordered prefix lookup over
  `next_number_index`; MyLite's current key-image stream must preserve the same
  prefix ordering after updates, deletes, copy ALTER, and reopen.
- Table-level `AUTO_INCREMENT` metadata display for non-leading keys may differ
  from MyISAM because MyLite does not store per-prefix counters. SQL generation
  remains correct because it derives from rows.
