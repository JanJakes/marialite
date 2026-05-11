# foreign-key-restrict-constraints

> Status note: this slice implemented the first restrict/no-action FK support.
> The later `foreign-key-actions` slice added `CASCADE` and `SET NULL` parent
> update/delete actions while keeping `SET DEFAULT` unsupported.

## Problem Statement

MyLite currently rejects all `FOREIGN KEY` DDL. That avoids silent corruption,
but it leaves a common application constraint surface unsupported. The first
safe implementation step is to support persisted `RESTRICT` and `NO ACTION`
foreign keys, expose them through MariaDB's handler metadata hooks, and enforce
referential checks through MyLite's durable key-image indexes.

`CASCADE`, `SET NULL`, and `SET DEFAULT` actions require child-table mutation
from parent-table update/delete paths. That is a separate slice because it
needs safe child table metadata opening, recursive FK handling, and atomic
multi-table row rewrites.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/sql_lex.cc:12491` builds `Foreign_key` objects
  and appends the generated supporting key to `Alter_info::key_list`.
- `vendor/mariadb/server/sql/sql_class.h:532` defines `Foreign_key` with child
  columns, referenced table/columns, and update/delete actions.
- `vendor/mariadb/server/sql/sql_table.cc:3459` validates FK column lists and
  fills omitted referenced columns from the child column list.
- `vendor/mariadb/server/sql/sql_table.cc:3646` skips `Key::FOREIGN_KEY` while
  building `KEY` metadata, and `sql_table.cc:3668` marks auto-generated
  supporting keys with `HA_GENERATED_KEY`.
- `vendor/mariadb/server/sql/sql_table.cc:9401` asks
  `handler::get_foreign_key_list()` for existing FK metadata during copy
  `ALTER`, then re-adds non-dropped FKs to the replacement table definition.
- `vendor/mariadb/server/sql/handler.h:4505` defines
  `is_fk_defined_on_table_or_index()`.
- `vendor/mariadb/server/sql/handler.h:4507` defines
  `get_foreign_key_create_info()` for `SHOW CREATE TABLE`.
- `vendor/mariadb/server/sql/handler.h:4531` and `handler.h:4545` define child
  and parent FK list hooks used by information schema, prelocking, truncate,
  and copy ALTER.
- `vendor/mariadb/server/sql/handler.h:4547` defines
  `referenced_by_foreign_key()`.
- `vendor/mariadb/server/include/my_base.h:494` through `my_base.h:496` define
  `HA_ERR_CANNOT_ADD_FOREIGN`, `HA_ERR_NO_REFERENCED_ROW`, and
  `HA_ERR_ROW_IS_REFERENCED`.
- `vendor/mariadb/server/storage/innobase/handler/ha_innodb.cc:15522`
  illustrates how an engine maps internal FK metadata to
  `FOREIGN_KEY_INFO`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1078` currently rejects
  pending FK DDL before storing a table definition.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:4178` serializes MyLite's
  catalog text payload; FK metadata needs durable catalog records alongside
  existing table, row, and index page records.

## Scope

This slice will:

- accept `FOREIGN KEY` definitions whose update and delete actions are
  `RESTRICT`, `NO ACTION`, or unspecified,
- reject `CASCADE`, `SET NULL`, and `SET DEFAULT` actions explicitly,
- persist child FK metadata in the MyLite catalog,
- persist key-definition metadata needed to locate child and parent key-image
  prefixes after reopen,
- allow generated FK supporting indexes in MyLite key validation,
- expose FK metadata through MariaDB's handler hooks,
- enforce child insert/update parent existence while
  `@@foreign_key_checks=1`,
- enforce parent update/delete/truncate restriction while
  `@@foreign_key_checks=1`,
- keep `@@foreign_key_checks=0` behavior compatible by skipping row checks
  while still preserving metadata,
- cover same-process and fresh-process FK behavior in storage smoke.

## Non-Goals

- Do not implement `CASCADE`, `SET NULL`, or `SET DEFAULT`.
- Do not implement cross-engine FKs.
- Do not implement partitioned-table FKs.
- Do not implement deferred constraints; MariaDB/InnoDB semantics are immediate.
- Do not use durable datadir sidecars or `mysql.*` system tables for FK
  metadata.

## Proposed Design

### Catalog Metadata

Add MyLite-owned metadata structs:

- key definitions: key index, key name, key part field names, cumulative
  key-image prefix lengths, and nullable-part markers,
- foreign-key definitions: constraint id, child table, parent table,
  child/parent field names, child/parent key indexes, child/parent prefix
  lengths, and update/delete actions.

Serialize these as catalog text records after each table definition. This does
not add sidecar files and does not change row or index page payload formats.
Because MyLite's file format is still pre-release, this slice may bump the
catalog header version if the compatibility policy for new records requires it;
old catalogs without FK/key-definition records remain readable.

### DDL

`ha_mylite::create()` will stop blanket-rejecting FKs and instead extract FK
definitions from `HA_CREATE_INFO::alter_info`. The SQL layer already includes
existing non-dropped FKs in copy ALTER, so replacing the table's FK vector from
`alter_info->key_list` is sufficient for `ADD FOREIGN KEY` and
`DROP FOREIGN KEY`.

MyLite will reject unsupported FK actions during create/alter before storing
the replacement definition. It will also reject a FK when the child or parent
key-image prefix cannot be located in MyLite's stored key definitions.

Generated supporting FK keys must be accepted by MyLite's durable key-image
storage, because MariaDB materializes them as ordinary keys with
`HA_GENERATED_KEY`.

### Enforcement

MyLite can enforce restrict/no-action FKs without opening another `TABLE`
object by comparing durable key-image prefixes:

- child insert/update builds the child supporting key image from the current
  `TABLE` and requires a matching parent index entry,
- parent update/delete builds the parent referenced key image from the current
  `TABLE` and rejects the operation if any child index entry matches,
- parent update skips the check when the referenced key prefix is unchanged,
- child rows with any NULL FK part are accepted under standard match-simple
  semantics,
- non-NULL nullable key parts are compared after removing MariaDB's nullable
  key-image marker byte so nullable child columns can reference non-null parent
  keys,
- checks are skipped when `OPTION_NO_FOREIGN_KEY_CHECKS` is set.

This design relies on MariaDB's key-image generation for type, charset, and
collation equality rather than comparing raw record bytes.

### Handler Metadata Hooks

Implement:

- `get_foreign_key_list()` for child FKs,
- `get_parent_foreign_key_list()` for FKs referencing the current table,
- `referenced_by_foreign_key()`,
- `is_fk_defined_on_table_or_index()`,
- `get_foreign_key_create_info()` and `free_foreign_key_create_info()` for
  `SHOW CREATE TABLE`.

Returned `FOREIGN_KEY_INFO` values must be allocated on the supplied THD memroot
except `get_foreign_key_create_info()`, which follows the handler API and
returns a heap string freed by `free_foreign_key_create_info()`.

## Affected Subsystems

- MyLite handler create validation.
- MyLite catalog serialization and parsing.
- MyLite row insert/update/delete/truncate paths.
- MyLite handler metadata hooks used by SQL introspection and ALTER.
- Storage smoke same-process and fresh-process coverage.
- Architecture and roadmap documentation.

## DDL Metadata Routing Impact

Accepted FK DDL must store all FK metadata inside the primary `.mylite` file.
Failed FK DDL must not leave child table definitions, replacement tables, FK
records, row changes, or orphaned index roots.

## Single-File And Embedded-Lifecycle Implications

No companion file is introduced. FK metadata lives in the catalog payload and
participates in the existing double-header generation and recovery path. Row
checks operate under the existing process-local catalog mutex and transaction
snapshot/rollback machinery.

## Public API Or File-Format Impact

No public `libmylite` API change. The catalog text payload gains FK and key
definition records. A catalog version bump may be used if needed to make the
new record set explicit.

## Binary-Size Impact

Expected binary growth is moderate: catalog metadata parsing/serialization,
handler hook allocation, key-prefix helpers, enforcement scans, and smoke
coverage. No new third-party dependency is added.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc`:
  - create parent and child MyLite tables with a FK,
  - verify valid child inserts,
  - verify orphan child insert/update returns FK failure,
  - verify parent delete/update/truncate of referenced rows returns FK failure,
  - verify `foreign_key_checks=0` skips row checks,
  - verify `SHOW CREATE TABLE` and information schema expose the FK,
  - verify `ALTER TABLE ... DROP FOREIGN KEY` removes enforcement,
  - verify unsupported cascade/set-null actions are rejected without mutating
    rows or catalog state,
  - verify fresh-process reopen preserves FK metadata and enforcement.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
  - `git diff --check`

## Acceptance Criteria

- `RESTRICT` and `NO ACTION` FK DDL succeeds for MyLite tables.
- FK metadata survives fresh-process reopen.
- MariaDB introspection surfaces show the FK.
- Child insert/update rejects missing parent rows while checks are enabled.
- Parent update/delete/truncate rejects referenced rows while checks are
  enabled.
- `foreign_key_checks=0` skips row checks without dropping metadata.
- `CASCADE`, `SET NULL`, and `SET DEFAULT` remain explicit rejections.
- Existing storage, index, transaction, recovery, lifecycle, and sidecar
  checks keep passing.

## Risks And Unresolved Questions

- Key-image prefix equality still needs broader multi-column FK coverage.
  The smoke includes a case-insensitive string FK, but more collations and
  composite string keys should be added in later compatibility batches.
- `SHOW CREATE TABLE` formatting must match MariaDB enough for application SQL
  dump/restore workflows.
- FK metadata hooks are used by ALTER and prelocking code; missing or
  incomplete metadata would be more dangerous than rejecting the DDL.
- Cascading actions need a separate child-table mutation design before they can
  be safely enabled.

## Implementation Result

Implemented in `ha_mylite` and the storage smoke. The handler now accepts and
persists `RESTRICT` and `NO ACTION` FKs, persists key-definition metadata with
nullable key-part markers, exposes FK metadata through MariaDB handler hooks,
and enforces child and parent checks from durable key-image prefixes. Prefix
comparison strips non-NULL nullable marker bytes and uses MariaDB field
`key_cmp()` so string FK equality follows the open table's collation. `CASCADE`,
`SET NULL`, and `SET DEFAULT` remain explicit rejections.

Report evidence from `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`:

- `build/mariadb-minsize/mylite-storage-engine-report.txt`:
  - `status=0`
  - `foreign_key_show_create=present`
  - `foreign_key_information_schema_count=1`
  - `foreign_key_missing_parent_insert=rejected`
  - `foreign_key_missing_parent_update=rejected`
  - `foreign_key_parent_delete=rejected`
  - `foreign_key_parent_update=rejected`
  - `foreign_key_parent_truncate=rejected`
  - `foreign_key_checks_off_count=3`
  - `unsupported_foreign_key_cascade=rejected`
- `build/mariadb-minsize/mylite-catalog-read-report.txt`:
  - `status=0`
  - `persisted_foreign_key_information_schema_count=1`
  - `persisted_foreign_key_missing_parent_insert=rejected`
  - `persisted_foreign_key_parent_delete=rejected`
- `build/mariadb-minsize/mylite-catalog-recovery-read-report.txt`:
  - `status=0`
  - `persisted_foreign_key_information_schema_count=1`
  - `persisted_foreign_key_missing_parent_insert=rejected`
  - `persisted_foreign_key_parent_delete=rejected`

Verification run:

- `git diff --check`
- `bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
