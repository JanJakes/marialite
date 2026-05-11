# check-constraint-enforcement

## Problem Statement

MariaDB owns CHECK-constraint parsing, metadata, expression evaluation, and
diagnostics. MyLite stores MariaDB table-definition images and should inherit
that behavior for supported row storage, but the current smoke does not prove
CHECK constraints reject invalid DML or survive fresh-process reopen.

This slice adds CHECK-constraint enforcement coverage for MyLite tables.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/table.cc:1181` loads table CHECK constraints
  into `TABLE::check_constraints`.
- `vendor/mariadb/server/sql/table.cc:1975` reads table CHECK-constraint
  counts from frm metadata.
- `vendor/mariadb/server/sql/table.cc:3345` parses virtual-column metadata,
  default values, and CHECK constraints from the frm image.
- `vendor/mariadb/server/sql/table.cc:6616` implements
  `TABLE::verify_constraints()`, where false non-NULL CHECK expressions raise
  `ER_CONSTRAINT_FAILED`.
- `vendor/mariadb/server/sql/table.cc:6612` routes view/table check-option
  verification to `TABLE::verify_constraints()`.
- `vendor/mariadb/server/sql/sql_table.cc:12838` verifies constraints while
  MariaDB copies rows during copy ALTER.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:1226` stores the exact
  MariaDB frm image in the MyLite catalog.

## Scope

This slice will:

- prove table and column CHECK constraints are accepted for MyLite tables,
- prove invalid INSERT and UPDATE statements are rejected before corrupting
  MyLite row storage,
- prove valid rows remain intact after failed CHECK statements,
- prove CHECK metadata survives fresh-process reopen and still rejects invalid
  DML,
- document CHECK constraints as supported through MariaDB SQL-layer semantics.

## Non-Goals

- Do not implement MyLite-owned expression evaluation.
- Do not normalize CHECK expressions into a MyLite-native catalog format.
- Do not change `OPTION_NO_CHECK_CONSTRAINT_CHECKS` behavior.
- Do not support generated columns as part of CHECK support.

## Proposed Design

No storage-engine code change is expected. MyLite already persists the frm image
that contains CHECK metadata, and MariaDB evaluates CHECK expressions before
handler writes and during copy ALTER. Add smoke coverage that proves this
inherited behavior at MyLite's storage boundary.

If a test fails, fix the narrow boundary causing the issue: table-definition
image persistence, reopen discovery, record conversion before handler writes,
or statement rollback after a failed DML statement.

## Affected Subsystems

- Storage smoke same-process DML coverage.
- Storage smoke fresh-process persistence coverage.
- Single-file storage architecture docs and roadmap.

## DDL Metadata Routing Impact

CHECK-constrained table definitions must remain stored inside the `.mylite`
catalog as MariaDB frm images. Failed DML against a CHECK-constrained table
must not mutate the row payload or rebuilt indexes.

## Single-File And Embedded-Lifecycle Implications

No file-format change. CHECK metadata remains inside the persisted frm image.
Fresh-process reopen must rediscover the constraints without `.frm` sidecars.

## Public API Or File-Format Impact

No public `libmylite` API change. No file-format version bump.

## Binary-Size Impact

Expected size impact is zero apart from smoke-test code. Post-implementation
`MinSizeRel` artifact sizes will be recorded.

## License, Trademark, And Dependency Impact

No new dependency. All changes remain in existing GPL-2.0-only MyLite and
MariaDB-derived source files.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc`:
  - create a table with column and table CHECK constraints,
  - insert valid rows,
  - reject invalid INSERT,
  - reject invalid UPDATE,
  - verify valid rows remain unchanged,
  - add a persisted CHECK-constrained table,
  - verify valid rows and invalid INSERT rejection after fresh-process reopen.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` for changed shell scripts
  - `git diff --check`

## Acceptance Criteria

- MyLite tables can be created with supported CHECK constraints.
- Invalid INSERT and UPDATE statements fail and leave existing rows intact.
- Fresh-process reopen preserves CHECK metadata and enforcement.
- Existing generated-column rejection, FK rejection, copy ALTER, storage,
  transaction, recovery, and public API coverage keeps passing.
- Docs and roadmap identify CHECK constraints as supported through inherited
  MariaDB semantics.

## Risks And Unresolved Questions

- CHECK expressions that reference generated columns remain unsupported because
  generated columns are rejected.
- Disabling CHECK enforcement through MariaDB session options is not changed by
  this slice.

## Implementation Result

Implemented. No storage-engine expression evaluator was added; the slice relies
on MariaDB's existing CHECK metadata and SQL-layer verification while proving
that MyLite persists and rediscovers the generated table-definition image.

The storage smoke now creates MyLite tables with column and table CHECK
constraints, verifies failed invalid INSERT and UPDATE statements, confirms
the valid rows remain `1:5:ok,2:3:NULL`, and repeats the persisted-table
enforcement check after fresh-process reopen.

Report evidence from `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`:

- `build/mariadb-minsize/mylite-storage-engine-report.txt`:
  - `status=0`
  - `check_constraint_insert=rejected`
  - `check_constraint_update=rejected`
  - `check_constraint_rows=1:5:ok,2:3:NULL`
- `build/mariadb-minsize/mylite-catalog-write-report.txt`:
  - `status=0`
  - `persisted_check_rows=1:5:ok,2:3:NULL`
  - `persisted_check_insert=rejected`
- `build/mariadb-minsize/mylite-catalog-read-report.txt`:
  - `status=0`
  - `persisted_check_rows=1:5:ok,2:3:NULL`
  - `persisted_check_insert=rejected`

Verification run:

- `git diff --check`
- `bash -n tools/run-compatibility-test-harness.sh
  tools/run-storage-engine-smoke.sh tools/run-libmylite-open-close-smoke.sh
  tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`

Measured `MinSizeRel` artifacts from
`build/mariadb-minsize/mylite-build-report.txt` and `ls -l`:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,417,786 bytes,
  571 objects.
- `build/mariadb-minsize/mylite/libmylite.a`: 87,206 bytes.
