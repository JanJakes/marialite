# Constraint information schema

## Problem

MyLite supports primary keys, unique keys, CHECK constraints, and foreign keys,
including common FK actions. The storage smoke covers enforcement and
`information_schema.KEY_COLUMN_USAGE`, but common application migration tools
also inspect `information_schema.TABLE_CONSTRAINTS`,
`information_schema.REFERENTIAL_CONSTRAINTS`, and
`information_schema.CHECK_CONSTRAINTS`.

Those metadata surfaces should work for MyLite tables through the inherited
MariaDB table-open paths and MyLite's handler FK list bridge. This slice proves
that behavior across write, fresh-process reopen, and recovery read, and fixes
any MyLite-specific gaps found by the tests.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/sql_show.cc` registers
  `information_schema.TABLE_CONSTRAINTS` through `get_all_tables()` and
  `get_schema_constraints_record()` with `OPEN_TABLE_ONLY`.
- `get_schema_constraints_record()` opens each table, emits primary and unique
  constraints from `TABLE_SHARE::key_info`, emits CHECK constraints from
  `TABLE::check_constraints`, and emits FK constraints from
  `handler::get_foreign_key_list()`.
- `information_schema.REFERENTIAL_CONSTRAINTS` is populated by
  `get_referential_constraints_record()`, which also uses
  `handler::get_foreign_key_list()` for FK names, referenced keys, and
  update/delete rules.
- `information_schema.CHECK_CONSTRAINTS` is populated by
  `get_check_constraints_record()`, which prints table CHECK expressions from
  the opened table's check metadata.
- `information_schema.KEY_COLUMN_USAGE` is already covered by MyLite storage
  smoke assertions for FK metadata, and uses the same table-open and FK-list
  mechanisms.

## Scope

Cover metadata for MyLite tables in:

- `information_schema.TABLE_CONSTRAINTS`;
- `information_schema.REFERENTIAL_CONSTRAINTS`;
- `information_schema.CHECK_CONSTRAINTS`;
- write, reopen, and recovery-read storage smoke phases.

## Non-Goals

- Do not implement deferrable constraints; MariaDB/InnoDB semantics are
  immediate.
- Do not implement FK `SET DEFAULT`, which remains an explicit rejection
  because MariaDB/InnoDB do not implement it.
- Do not add new persistent metadata formats unless the inherited metadata
  paths fail.

## Design

Add focused storage smoke assertions against existing persistent test tables:

1. Query `TABLE_CONSTRAINTS` for a MyLite table with primary and unique keys.
2. Query `TABLE_CONSTRAINTS` and `CHECK_CONSTRAINTS` for a table with CHECK
   metadata.
3. Query `TABLE_CONSTRAINTS` and `REFERENTIAL_CONSTRAINTS` for active FK
   metadata while the FK exists during the write phase.
4. Query the same metadata after the ALTER migration drops CHECK/FK constraints
   to prove stale metadata is absent after reopen and recovery.
5. Query persisted FK action tables to prove `CASCADE` and `SET NULL` rules are
   visible through `REFERENTIAL_CONSTRAINTS`.

If the inherited paths fail, fix the narrow MyLite bridge that supplies table
share, check, key, or FK-list metadata rather than fabricating information
schema rows separately.

## Affected Subsystems

- MyLite storage smoke coverage.
- MyLite handler FK-list metadata only if a gap is found.
- Single-file storage docs and roadmap.

## Single-File and Embedded Lifecycle

Metadata queries must remain table/catalog reads only. They must not create
durable `.frm`, `mysql.*`, Aria, InnoDB, MyISAM, binlog, or schema-directory
sidecars.

## Public API and File Format Impact

No public API or file-format change is expected.

## Binary Size Impact

No meaningful binary-size impact is expected unless a small bridge fix is
needed. The likely change is smoke coverage only.

## License and Dependency Impact

No new dependency or licensing change.

## Test Plan

- Extend the storage-engine smoke with write, reopen, and recovery assertions
  for constraint metadata tables.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` on the smoke scripts
  - `git diff --check`

## Acceptance Criteria

- MyLite primary, unique, CHECK, and FK constraints are visible through
  standard information schema constraint tables while active.
- Dropped CHECK and FK constraints are absent after write, reopen, and recovery
  read.
- FK update/delete rules are visible through `REFERENTIAL_CONSTRAINTS`.
- The sidecar scan remains clean.

## Implementation Result

The inherited MariaDB information-schema paths worked through MyLite's existing
table discovery and handler metadata bridges. No new storage-engine metadata
format or synthetic information-schema row path was needed.

The storage smoke now records:

- `foreign_key_constraint_information_schema_count=1:1`
- `persisted_alter_key_constraint_metadata=1:1:0`
- `persisted_alter_check_information_schema_count=1:1`
- `persisted_alter_foreign_key_referential_constraints_count=1:1`
- `persisted_alter_constraint_final_information_schema_count=0:0:0:0`
- `persisted_foreign_key_referential_constraints_count=1:1`
- `persisted_foreign_key_action_referential_constraints_count=1:1`

Those values prove active primary, unique, CHECK, and FK constraint metadata,
FK action metadata, and absence of stale dropped CHECK/FK metadata across the
write, fresh-process reopen, and recovery-read phases.

## Risks and Unresolved Questions

- If a table is queried through an information schema path before MyLite has
  loaded its latest catalog generation, stale table-definition images would be
  visible. Existing table discovery should already prevent that, and this slice
  verifies the path through fresh processes.
- CHECK expression text is MariaDB-rendered and may differ in harmless
  whitespace. Tests should check for semantic markers rather than exact full
  expression strings.
