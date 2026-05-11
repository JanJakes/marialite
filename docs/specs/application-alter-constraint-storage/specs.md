# Application ALTER constraint storage

## Problem

MyLite supports many create-time indexes and constraints, and it now supports
WordPress-shaped table creation. Application upgrades, including WordPress
plugins, often evolve existing tables with `ALTER TABLE` instead of recreating
them from scratch. The current smoke covers copy ALTER row preservation and
standalone `CREATE INDEX`/`DROP INDEX`, but it does not explicitly prove common
ALTER forms for adding, renaming, and dropping constraints and key definitions
on populated MyLite tables.

This slice adds focused coverage for application migration DDL and implements
any missing MyLite storage support discovered by those tests.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/sql_table.cc` routes many `ALTER TABLE` forms
  through `mysql_alter_table()` and can rebuild tables with
  `ALGORITHM=COPY`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:2123` stores the
  MariaDB-generated table-definition image in the MyLite catalog for new table
  definitions produced by DDL.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:4450` validates key
  storage support before table creation and copy-ALTER replacement tables are
  accepted.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc` already extracts and
  persists MyLite key-definition and foreign-key metadata from accepted table
  definitions, so ALTER-generated replacement definitions should update those
  catalog records if the copy ALTER path is complete.
- MariaDB upstream tests use `ALTER TABLE ... ADD CONSTRAINT ... CHECK` and
  `ALTER TABLE ... DROP CONSTRAINT` in `mysql-test/main/check_constraint.test`
  and `mysql-test/main/alter_table.test`, so those are normal application DDL
  rather than server administration.

## Scope

Add catalog persistence smoke coverage for:

- `ALTER TABLE ... ADD PRIMARY KEY`, `ADD UNIQUE KEY`, and `ADD KEY` on a
  populated table;
- `ALTER TABLE ... RENAME INDEX` and `DROP INDEX` while preserving rows;
- unique-key enforcement after `ADD UNIQUE KEY` and duplicate acceptance after
  dropping that unique key;
- `ALTER TABLE ... ADD CONSTRAINT ... CHECK` and `DROP CONSTRAINT`;
- `ALTER TABLE ... ADD CONSTRAINT ... FOREIGN KEY` and `DROP FOREIGN KEY`;
- fresh-process reopen and recovery read after the ALTER-generated metadata,
  row payloads, and index roots are published.

## Non-Goals

- Do not implement persistent views, triggers, stored routines, packages, or
  events in this slice.
- Do not implement partition maintenance DDL.
- Do not change MariaDB parser or optimizer behavior for ALTER syntax.
- Do not add new physical index formats.

## Design

Keep the implementation in the storage smoke unless tests expose a storage gap.
The test should use `ALGORITHM=COPY` where needed to force the path MyLite
currently owns and should verify the final table state through ordinary SQL,
information schema, forced indexed lookups, duplicate-key DML, and constraint
violations.

Persist the altered objects in the catalog write phase and verify them again in
the fresh-process read phase. Recovery read should transitively exercise the
same checks through the existing recovery smoke.

## Affected Subsystems

- MyLite storage-engine smoke.
- MyLite storage engine only if ALTER-generated replacement definitions expose a
  missing key, constraint, or metadata publication path.
- Roadmap and single-file storage docs if support or coverage changes.

## DDL Metadata Routing Impact

The slice relies on existing copy-ALTER metadata routing. It should prove that
accepted ALTER-generated definitions replace the previous MyLite catalog state
without durable `.frm` sidecars and without losing row/index payloads.

## Single-File and Embedded Lifecycle

All altered definitions, key roots, constraint metadata, and row payloads must
remain inside the primary `.mylite` file. Runtime directories must stay free of
persistent `.frm`, MyISAM, Aria, and InnoDB sidecars.

## Public API and File Format Impact

No public C API or file-format change is expected.

## Binary Size Impact

Expected binary-size impact is test code only unless storage gaps are found.

## License and Dependency Impact

No new dependency or licensing change.

## Test Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc` with persisted
  write/read checks for application ALTER constraint and index DDL.
- Verify:
  - added primary, unique, and secondary keys are visible in information schema;
  - forced lookups use renamed indexes;
  - unique-key duplicate rows are rejected while the key exists and accepted
    after the unique key is dropped;
  - CHECK constraints reject invalid rows after add and allow them after drop;
  - foreign keys added by ALTER reject missing parents and restrict parent
    deletes, then allow parent deletes after drop;
  - fresh-process read and recovery read see the final expected state;
  - sidecar scan remains clean.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` on the smoke scripts
  - `git diff --check`

## Acceptance Criteria

- Common application ALTER forms for keys, CHECK constraints, and foreign keys
  work on populated MyLite tables.
- Altered metadata, row payloads, and index roots survive fresh-process reopen
  and recovery read.
- Failed constraint violations do not mutate rows.
- The grouped compatibility harness still passes and reports no unexpected
  MyLite runtime sidecars.

## Implementation Result

The slice is implemented.

- The storage smoke now persists a populated key-migration table, adds a
  primary key, unique key, and secondary key through `ALTER TABLE`, renames the
  secondary index, rejects duplicate rows while the unique key exists, drops the
  unique key, and verifies duplicate rows are then accepted.
- The smoke persists a populated CHECK-migration table, adds a named CHECK
  constraint through `ALTER TABLE`, verifies invalid rows are rejected, drops
  the constraint, and verifies the formerly invalid row persists after reopen
  and recovery read.
- The smoke persists populated foreign-key parent and child tables, adds a
  named FK through `ALTER TABLE`, verifies missing-parent inserts and referenced
  parent deletes are rejected, drops the FK, and verifies the parent delete and
  resulting orphan child row persist across reopen and recovery read.
- No storage-engine code change was needed for this slice. The existing
  copy-ALTER catalog publication path already stores the MariaDB-generated
  replacement definitions, key metadata, FK metadata, row payloads, and final
  index roots in the primary `.mylite` file.

## Risks and Unresolved Questions

- This slice covers copy-ALTER-backed migration DDL. In-place ALTER remains a
  separate performance and upstream-delta concern.
- Future schema-object support for triggers may add additional interactions with
  `REPLACE`, ODKU, and cascaded FK actions.
