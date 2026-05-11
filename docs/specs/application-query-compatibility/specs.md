# Application query compatibility

## Problem

The current MyLite compatibility harness compares scalar expressions, basic
row DML, primary-key lookup, duplicate-key errors, and autoincrement behavior
against a MariaDB reference engine. That proves the first storage bridge, but
it does not cover the ordinary SQL composition that applications and WordPress
plugins rely on: joins, left joins, grouping, derived tables, subqueries,
unions, `CREATE TEMPORARY TABLE ... SELECT`, and DML sourced from queries.

These statements are mostly owned by MariaDB's parser, resolver, optimizer, and
executor, but MyLite still supplies the handler row, index, temporary-table,
and mutation paths under those plans. The harness should compare those paths
against MariaDB instead of assuming they work.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/sql_select.cc` routes ordinary SELECT execution
  through `mysql_select()`, `JOIN::exec()`, `JOIN::exec_inner()`, and
  `do_select()`. Those paths compose scans, index reads, join predicates,
  grouping, ordering, HAVING, derived tables, and subqueries above the handler
  API.
- `vendor/mariadb/server/sql/sql_union.cc` runs UNION branches through the
  same select machinery and materializes UNION results through MariaDB's
  temporary-result infrastructure.
- `vendor/mariadb/server/sql/sql_insert.cc` implements
  `INSERT ... SELECT` through `mysql_insert_select_prepare()` and
  `select_insert`, while `select_create::create_table_from_items()` handles
  `CREATE TABLE ... SELECT`.
- `vendor/mariadb/server/sql/sql_derived.cc` materializes derived tables by
  invoking `mysql_select()` over the derived table unit.
- MyLite's relevant handler and temporary-table hooks live in
  `vendor/mariadb/server/storage/mylite/ha_mylite.cc`, including table scan,
  index cursor, row mutation, duplicate-row, and session temporary-table
  storage paths.

## Scope

Extend the reference-vs-MyLite comparison smoke for common application SQL:

- inner joins and left joins;
- grouped aggregate queries with HAVING;
- derived-table ORDER/LIMIT queries;
- `IN` and `EXISTS` subqueries;
- `UNION ALL`;
- `CREATE TEMPORARY TABLE ... SELECT`;
- `INSERT ... SELECT`;
- multi-table `UPDATE ... JOIN`;
- multi-table `DELETE ... LEFT JOIN`.

## Non-Goals

- Do not compare optimizer plans or exact EXPLAIN output in this slice; compare
  observable SQL results and normalized diagnostics.
- Do not import the MariaDB MTR suite.
- Do not add new SQL semantics in MyLite when MariaDB already owns the SQL
  layer. If a gap appears, fix the narrow MyLite handler/storage path exposed
  by the query.
- Do not implement temporary spill files. The cases should stay small enough
  to run in memory under the current embedded profile.

## Design

Add a `run_query_cases()` phase to `mylite-compatibility-smoke`. The phase
creates three indexed tables under the selected engine:

- authors with status indexes;
- posts with author and score indexes;
- comments with post indexes.

The phase records stable one-row fingerprints for representative application
queries, then mutates rows through query-driven DML and records the final
ordered state. The harness already runs the same binary twice, once with the
isolated MariaDB/MyISAM reference engine and once with MyLite plus
`--enforce-storage-engine=MYLITE`; any behavior mismatch appears as a
fingerprint diff.

The cases deliberately use deterministic `GROUP_CONCAT(... ORDER BY ...
SEPARATOR ',')` result strings rather than comparing row-stream order.

## Affected Subsystems

- `vendor/mariadb/server/mylite/compatibility_smoke.cc`
- Compatibility harness reports and fingerprints.
- Roadmap and single-file storage documentation.

No production MyLite storage code is expected to change unless the new
comparison exposes a handler bug.

## DDL Metadata Routing Impact

The phase creates and drops ordinary and temporary tables through normal
MariaDB DDL. MyLite must continue routing both durable and temporary definitions
through the MyLite engine without durable `.frm`, `.MYD`, `.MYI`, `.ibd`,
Aria, or binlog sidecars in the MyLite runtime.

## Single-File and Embedded Lifecycle

The reference runtime may create MyISAM sidecars in its isolated datadir. The
MyLite runtime must keep all durable table rows, indexes, and definitions in
the configured primary `.mylite` file. The temporary CTAS table must remain
session-local and disappear before shutdown.

## Public API and File Format Impact

No public API or file-format change is expected.

## Binary Size Impact

Only the comparison smoke executable grows. The embedded library and public
`libmylite` API should not gain new production code unless a bug fix is
required.

## License and Dependency Impact

No new dependency or licensing change.

## Test Plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

## Acceptance Criteria

- The MariaDB/MyISAM and MyLite fingerprints match for the expanded query
  surface.
- The comparison report records stable labels for every new case.
- The grouped harness still reports `mariadb_comparison` and `sidecar_scan`
  as passing.
- The MyLite runtime sidecar scan remains clean.

## Implementation Result

The compatibility smoke now includes `run_query_cases()` after the existing
scalar, row, key, and autoincrement phases. The expanded reference-vs-MyLite
fingerprint covers:

- `query_inner_join=10:Ada,11:Ada,13:Cid`
- `query_left_join=12:Ben,14:NULL`
- `query_group_having=1:2:13,3:1:9,99:1:4`
- `query_derived_order_limit=13:delta,11:beta,10:alpha`
- `query_subquery_in=10,11,12`
- `query_subquery_exists=10,11,14`
- `query_union_all=a:Ada,a:Ben,p:beta,p:delta`
- `query_temp_ctas_rows=13:delta,11:beta,10:alpha`
- `query_mutation_rows=10:1:alpha:5:1,11:1:beta:8:1,12:2:gamma:3:0,13:3:delta:10:1,20:2:Ben-copy:12:1`

The first draft used a fully qualified multi-table DELETE without selecting a
default schema. MariaDB/MyISAM and MyLite both rejected that form with
`ER_NO_DB_ERROR`, so the final test selects the `mylite` schema before running
the query batch and keeps the DELETE as a genuine multi-table statement.

Verification passed:

```sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

The grouped harness reported `status=0` for `embedded_lifecycle`,
`libmylite_lifecycle`, `storage_single_file`, `mariadb_comparison`, and
`sidecar_scan`, with `unexpected_sidecars=none`.

The rebuilt `mylite-compatibility-smoke` artifact was 22,338,784 bytes in the
local minsize build.

## Risks and Unresolved Questions

- MyISAM is not a complete behavioral oracle for transactional engines, but it
  is an embedded MariaDB reference path that avoids external durable engines in
  the MyLite profile.
- These cases do not force disk-based internal temporary spill files; that
  remains a separate storage lifecycle slice.
- Broader WordPress plugin behavior should continue to be expanded through
  focused schema and query batches rather than a single unreviewable test dump.
