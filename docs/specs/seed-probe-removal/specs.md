# seed-probe-removal

## Problem Statement

MyLite still injects a hard-coded `mylite.probe` table into every new catalog.
That table was useful for the early storage-engine discovery slice, before
durable table definitions, schema namespaces, and DDL routing existed. It is now
product debt: new `.mylite` files should not contain a magic user-visible table
that was never created by the application.

The next slice should remove the hard-coded seed table while keeping the
existing `mylite` default schema for current smoke coverage and documented
namespace behavior.

## Scope

- Remove the in-engine hard-coded `mylite.probe` table and SQL-string seed
  discovery path.
- Keep the built-in `mylite` schema as the default catalog namespace.
- Keep `DROP DATABASE mylite` and `CREATE OR REPLACE DATABASE mylite`
  explicitly unsupported until the default-schema policy is redesigned.
- Update storage smoke coverage to prove:
  - `SHOW TABLES FROM mylite` is empty in a new database before user DDL,
  - `SELECT COUNT(*) FROM mylite.probe` fails because the probe table no longer
    exists,
  - ordinary MyLite tables created by user DDL still discover and scan through
    the catalog-backed `frm_image` path,
  - fresh-process reopen behavior is unchanged for user-created tables,
  - no schema directories or `.frm` sidecars appear.
- Update docs that describe `mylite.probe` as a current bootstrap artifact.

## Non-Goals

- Do not remove the `mylite` default schema in this slice.
- Do not make `DROP DATABASE mylite` succeed.
- Do not normalize table metadata beyond the existing persisted `frm_image`
  bridge.
- Do not change row, index, transaction, or public `libmylite` APIs.
- Do not add system-schema replacement tables.
- Do not add a production bundle-size analysis; record sizes only as normal
  slice evidence.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `storage/mylite/ha_mylite.cc` still defines `mylite_seed_table`,
  `mylite_seed_sql`, and a static `mylite_catalog` entry for
  `mylite.probe`.
- `mylite_discover_table()` reads either a persisted `frm_image` or a
  hard-coded SQL string, then calls
  `TABLE_SHARE::init_from_binary_frm_image()` or
  `TABLE_SHARE::init_from_sql_statement_string()`.
- `TABLE_SHARE::init_from_sql_statement_string()` is the MariaDB discovery
  helper that parses a CREATE TABLE string and can avoid writing a `.frm` when
  called with `write=false`. MyLite no longer needs that path for its own seed
  table because user-created table definitions are stored as binary
  `frm_image` catalog payloads.
- `mylite_parse_catalog_payload_locked()` preloads any table definition with
  non-empty `seed_sql` before parsing the on-disk catalog payload. That is what
  makes `mylite.probe` appear even though it is not serialized.
- `mylite_clear_frm_definitions_locked()` resets the schema catalog and keeps
  seed-SQL definitions while clearing persisted table definitions. Once there
  are no seed-SQL tables, the catalog reset can clear all table definitions.
- `mylite_reset_schema_catalog_locked()` seeds the `mylite` schema. Keeping
  that schema preserves current default namespace behavior without injecting a
  table.
- `storage_engine_smoke.cc` still expects `SHOW TABLES FROM mylite` to return
  `probe` and `SELECT COUNT(*) FROM mylite.probe` to return zero before any
  user DDL. Those assertions need to become absence checks.
- The `mylite-engine-discovery` spec explicitly described `mylite.probe` as a
  temporary seed-catalog bridge and warned that it must not escape into
  user-facing compatibility claims.

## Proposed Design

Remove the hard-coded seed table from `ha_mylite.cc`:

- delete `mylite_seed_table` and `mylite_seed_sql`,
- initialize `mylite_catalog` as an empty vector,
- remove `Mylite_table_definition::seed_sql`,
- simplify `mylite_discover_table()` and `mylite_read_table_definition()` to
  use only persisted `frm_image` definitions,
- make catalog payload parsing start from an empty loaded table list,
- make catalog reset clear all table definitions while still seeding the
  `mylite` schema.

Keep `mylite_seed_db` for the default schema name in this slice. Renaming that
symbol can be done as a small cleanup inside the implementation if it improves
readability, but the product behavior should remain unchanged: the `mylite`
schema exists, and reserved-schema policy still protects it from drop/replace.

Update the storage smoke:

- replace the seed table discovery check with a default-schema-empty check,
- add a direct missing-probe assertion,
- keep existing DDL/DML, persistence, schema namespace, and sidecar checks,
- rename report fields that mention the seed table when they now refer only to
  the default schema.

## Affected Subsystems

- MyLite storage-engine discovery and catalog-loading code.
- Storage-engine smoke executable and reports.
- Roadmap and single-file storage documentation.

## DDL Metadata Routing Impact

User-created MyLite table DDL remains routed through the existing persisted
`frm_image` catalog path. Removing `mylite.probe` means all discoverable MyLite
tables in a new file must come from application DDL or a loaded pre-release
catalog generation, not hard-coded SQL strings.

## Single-File And Embedded-Lifecycle Implications

The slice removes one user-visible bootstrap artifact without adding files. A
new primary `.mylite` file still has the default `mylite` schema namespace, but
it starts with no tables. Runtime sidecar expectations remain unchanged: no
schema directories, `.frm` files, dynamic plugin artifacts, or inherited engine
logs should appear.

## Public API Or File-Format Impact

No public `libmylite` API change.

The pre-release catalog format does not gain a new record type. Existing
pre-release files that never serialized `mylite.probe` will simply stop seeing
that hard-coded table after this slice. Persisted user table definitions remain
loadable.

## Binary-Size Impact

Expected size impact is very small and likely neutral-to-slightly-smaller:
removing one SQL string discovery path and a tiny static seed table should not
meaningfully affect the MariaDB embedded archive. Record measured artifact
sizes after implementation.

## License, Trademark, And Dependency Impact

No new dependency. All changes remain in existing GPL-2.0-only MyLite and
MariaDB-derived source files.

## Test And Verification Plan

- Run `git diff --check`.
- Run `bash -n tools/run-storage-engine-smoke.sh
  tools/run-compatibility-test-harness.sh`.
- Run `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`.
- Run `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`.
- Verify the storage report records the default schema as empty before user
  DDL and records the missing probe table as rejected.
- Verify existing user-created table, persistence, schema namespace, and
  transaction smoke results still pass.
- Verify observed files still exclude schema directories, `.frm` sidecars,
  dynamic plugin artifacts, and Aria log/control files.
- Record artifact sizes after the passing build.

## Acceptance Criteria

- New MyLite databases no longer expose `mylite.probe`.
- The `mylite` default schema still exists and remains protected from
  drop/replace.
- User-created MyLite tables still discover through persisted catalog
  `frm_image` definitions.
- Storage and compatibility harnesses pass.
- Documentation no longer describes `mylite.probe` as a current bootstrap
  artifact after implementation.

## Risks And Unresolved Questions

- Some smoke phases currently assume the default schema contains one table.
  Updating those checks must not reduce coverage for table discovery; the
  existing DDL and persistence phases should continue proving real catalog
  discovery.
- The default `mylite` schema remains a bootstrap policy choice. A later slice
  should decide whether applications can rename or remove it, or whether it is
  a permanent default namespace.

## Implementation Result

The hard-coded `mylite.probe` seed table has been removed from
`storage/mylite/ha_mylite.cc`. MyLite discovery now only returns persisted
catalog `frm_image` table definitions, and catalog loads no longer preload
non-serialized seed-SQL definitions.

The storage smoke now verifies new databases start with an empty default schema
and that `SELECT COUNT(*) FROM mylite.probe` is rejected. It still exercises
user-created table discovery through the existing DDL, DML, persistence,
schema namespace, and transaction phases.

Report evidence:

- `mylite-storage-engine-report.txt`: `status=0`,
  `default_schema_table_count=0`, `missing_probe=rejected`,
  `reserved_default_schema_drop=rejected`,
  `reserved_default_schema_replace=rejected`, `Observed Runtime Files=none`,
  `FRM Artifacts=none`, and `Schema Directory Artifacts=none`.

Verification completed:

- `bash -n tools/run-storage-engine-smoke.sh
  tools/run-compatibility-test-harness.sh`.
- `git diff --check`.
- `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`.
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`.

Measured artifacts after implementation:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 43,405,432 bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 87,206 bytes.
- `build/mariadb-minsize/storage/mylite/libmylite_embedded.a`: 303,480 bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,314,136
  bytes.
- `build/mariadb-minsize/mylite/mylite-compatibility-smoke`: 22,248,248
  bytes.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,247,368
  bytes.
