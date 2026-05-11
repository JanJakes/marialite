# Internal temporary spill lifecycle

## Problem

MyLite supports user-created temporary tables in session memory, but the
architecture docs still mark temporary spill-file storage as deferred. Common
application SQL can require MariaDB internal temporary tables for grouping,
distinct, derived-table materialization, sorting, and duplicate elimination.
When those internal tables outgrow memory or are forced to disk, the current
MyLite build profile falls back to MariaDB's MyISAM internal temporary-table
path because Aria is disabled.

That path is acceptable only if it is treated as controlled transient scratch:
it must stay under MyLite's configured temporary runtime directory, be deleted
before shutdown, leave no persistent `.MYD`/`.MYI` sidecars, and behave the
same when reading MyLite source tables as it does against the MariaDB/MyISAM
reference engine.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/sql_select.cc:Create_tmp_table::choose_engine()`
  selects HEAP for small in-memory internal temporary tables and
  `TMP_ENGINE_HTON` when a query has BLOB fields, uses a unique constraint,
  has `big_tables` enabled without `SELECT_SMALL_RESULT`, forces MyISAM, or
  sets `tmp_memory_table_size=0`.
- `vendor/mariadb/server/sql/sql_class.h` defines `TMP_ENGINE_HTON` as
  `maria_hton` only when Aria is compiled and `USE_ARIA_FOR_TMP_TABLES` is
  enabled; otherwise it defines `TMP_ENGINE_HTON` as `myisam_hton`.
- `vendor/mariadb/server/sql/sql_select.cc:create_internal_tmp_table()` calls
  `mi_create()` in the MyISAM branch with `HA_CREATE_TMP_TABLE` and
  `HA_CREATE_INTERNAL_TABLE`.
- `vendor/mariadb/server/sql/sql_select.cc:open_tmp_table()` opens internal
  temporary tables with `HA_OPEN_TMP_TABLE | HA_OPEN_INTERNAL_TABLE`.
- `vendor/mariadb/server/sql/sql_select.cc:free_tmp_table()` calls
  `entry->file->drop_table(entry->s->path.str)` for created internal temporary
  tables and does not route through durable table DDL.
- `vendor/mariadb/server/sql/sys_vars.cc` documents
  `tmp_memory_table_size`/`tmp_table_size` as the memory-to-disk conversion
  threshold for internal temporary tables and `tmp_disk_table_size` as the
  maximum size for internal on-disk MyISAM or Aria table data.
- `tools/build-mariadb-minsize.sh` configures `MYLITE_DISABLE_ARIA=ON`,
  `PLUGIN_ARIA=NO`, and `USE_ARIA_FOR_TMP_TABLES=OFF`, so the current MyLite
  profile uses the MyISAM internal temporary-table fallback.

## Scope

This slice will:

- force a disk-backed internal temporary table in the reference-vs-MyLite
  compatibility smoke by setting `tmp_memory_table_size=0` and `big_tables=1`
  around a grouped query;
- verify the query result against the MariaDB/MyISAM reference fingerprint;
- verify `Created_tmp_disk_tables` increments during the forced case;
- reset session variables before the remaining query cases;
- keep the sidecar scan responsible for proving no persistent MyISAM temp files
  survive after the MyLite embedded process exits;
- update docs to describe the current supported transient spill lifecycle and
  the future native-spill-format question separately.

## Non-Goals

- Do not replace MariaDB's MyISAM internal temporary-table implementation in
  this slice.
- Do not claim a native MyLite spill-file format.
- Do not persist internal temporary tables in the `.mylite` primary file.
- Do not support cross-process visibility or recovery for internal temporary
  scratch data.
- Do not compare exact temporary file names, because MariaDB allocates those
  internally and removes them during cleanup.

## Design

Extend `run_query_cases()` in `mylite-compatibility-smoke`:

1. Store the current session `Created_tmp_disk_tables` value in a user
   variable.
2. Set `tmp_memory_table_size=0` and `big_tables=1`.
3. Execute a grouped `SQL_BIG_RESULT` query over MyLite source rows and record
   a deterministic result string.
4. Record `query_internal_spill_status=spilled` only when
   `Created_tmp_disk_tables` increased.
5. Reset `tmp_memory_table_size` and `big_tables` to defaults before the
   remaining temporary CTAS and query-driven DML cases run.

The compatibility harness already runs the same smoke binary against an
isolated MyISAM reference engine and against MyLite. A mismatch in query
results or spill status fails the normalized fingerprint diff. The existing
sidecar scan fails if any persistent `.MYD`, `.MYI`, Aria, InnoDB, binlog, or
catalog-temp sidecar remains under MyLite runtime directories after shutdown.

## Affected Subsystems

- `vendor/mariadb/server/mylite/compatibility_smoke.cc`
- Compatibility harness reports and sidecar evidence.
- Roadmap and single-file storage documentation.

No production handler or file-format change is expected.

## Single-File and Embedded Lifecycle

The current spill path may create transient MyISAM scratch files while a
statement is executing. Those files are under the configured temporary runtime
path and are deleted by MariaDB's internal temporary-table cleanup before the
embedded process exits. Persistent `.MYD`/`.MYI` files in MyLite runtime
directories remain forbidden and are caught by the sidecar scan.

## Public API and File Format Impact

No public API or `.mylite` file-format change.

## Binary Size Impact

No new production object code is expected. The comparison smoke executable grows
slightly from added test cases. MyISAM is already part of the current embedded
profile.

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

- The reference and MyLite fingerprints both report the forced grouped query
  result and `query_internal_spill_status=spilled`.
- The grouped compatibility harness still passes.
- The sidecar scan reports `unexpected_sidecars=none` after forced internal
  temporary spill.
- Documentation no longer says temporary spill-file storage is simply
  deferred; it distinguishes the supported inherited transient spill path from
  a future native MyLite spill format.

## Implementation Result

The compatibility smoke now forces MariaDB's disk-backed internal temporary
table path inside `run_query_cases()` by setting `tmp_memory_table_size=0` and
`big_tables=1` around a grouped `SQL_BIG_RESULT` query over MyLite source
rows. It records the deterministic grouped result and verifies that the session
`Created_tmp_disk_tables` counter increased.

The matching reference and MyLite fingerprints now include:

- `query_internal_spill_rows=1:2:alpha+beta,2:1:gamma,3:1:delta,99:1:orphan`
- `query_internal_spill_status=spilled`

The smoke resets `tmp_memory_table_size` and `big_tables` before continuing to
temporary CTAS and query-driven DML cases.

Verification passed:

```sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

The grouped harness reported `status=0` for every group, and the sidecar scan
reported no unexpected or known inherited sidecars after the forced spill case.

The rebuilt `mylite-compatibility-smoke` artifact was 22,404,184 bytes in the
local minsize build.

## Risks and Unresolved Questions

- The inherited MyISAM internal temporary-table path is transient but not a
  native MyLite spill format. Replacing it may be useful for binary-size,
  naming, encryption, or stricter file-lifecycle control.
- The smoke proves cleanup after statement/runtime exit, not file presence
  during execution. A lower-level instrumentation slice would be needed to
  assert exact temporary filenames while a statement is active.
