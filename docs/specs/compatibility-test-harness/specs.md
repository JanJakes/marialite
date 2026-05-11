# Compatibility Test Harness Slice

## Problem Statement

MyLite now has several focused smokes, but they are still separate entry points
and the supported SQL subset is not compared against a MariaDB reference path.
The next slice should make verification repeatable as grouped compatibility
coverage:

- embedded runtime lifecycle,
- `libmylite` open/close lifecycle,
- single-file storage and recovery invariants,
- unsupported sidecar detection,
- MariaDB reference comparison for the currently supported DDL/DML subset.

The harness is not a full MariaDB Test Run replacement. It should be a small
bridge that keeps early MyLite behavior measurable while the storage engine is
still a raw-record bridge.

## MariaDB Base And Source References

- Base import: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Embedded runtime initialization uses MariaDB's embedded entry points declared
  in `vendor/mariadb/server/include/mysql.h`:
  `mysql_server_init()`, `mysql_server_end()`, `mysql_init()`, and
  `mysql_real_connect()`.
- The embedded startup path lives in
  `vendor/mariadb/server/libmysqld/lib_sql.cc`, where `init_embedded_server()`
  loads defaults, initializes server components, installs the embedded error
  handler, and calls `ddl_log_execute_recovery()` before marking the embedded
  server initialized.
- MariaDB DDL reaches storage engines through
  `vendor/mariadb/server/sql/handler.cc`:
  `ha_create_table()` decides whether an `.frm` image is written and
  `ha_discover_table()`/`ha_discover_table_names()` call storage-engine
  discovery hooks.
- MyLite's relevant hooks and handler methods live in
  `vendor/mariadb/server/storage/mylite/ha_mylite.cc`, including
  `mylite_discover_table()`, `mylite_discover_table_names()`,
  `mylite_discover_table_existence()`, row DML, index cursor methods, and
  autoincrement handling.
- MariaDB's own embedded test marker is
  `vendor/mariadb/server/mysql-test/main/mysql_embedded.test`, which keeps the
  upstream embedded smoke very small (`select 1`). Broader upstream `mysql-test`
  files often exclude embedded mode with `include/not_embedded.inc`, so MyLite's
  early compatibility harness should not pretend that the full MTR suite is
  immediately runnable under the embedded library profile.

## Scope

This slice will add:

- a top-level `tools/run-compatibility-test-harness.sh`,
- a new `mylite-compatibility-smoke` binary,
- a normalized fingerprint format for reference-vs-MyLite result comparison,
- grouped report output under the minsize build directory,
- unexpected-sidecar classification for MyLite runtime directories,
- documentation and roadmap updates.

The first comparison subset should cover only behavior that MyLite currently
claims:

- scalar expression execution through MariaDB's SQL layer,
- simple non-BLOB table DDL and DML,
- update and delete behavior,
- primary-key lookup and ordered key scans,
- duplicate primary-key errors,
- autoincrement generation, explicit high values, and post-delete progression.

## Non-Goals

- Do not import or adapt the full MariaDB MTR runner in this slice.
- Do not claim compatibility for BLOB/TEXT row storage, nullable keys,
  generated columns, foreign keys, views, triggers, routines, transactions, or
  server users.
- Do not hide existing inherited embedded startup side effects. The harness may
  classify `aria_log.*` as known current debt, but it must not present those
  files as final MyLite-owned companions.
- Do not change the public `libmylite` C API.
- Do not add a new dependency.

## Proposed Design

### Harness Script

Add `tools/run-compatibility-test-harness.sh` with the same outer shape as the
existing smoke scripts:

1. Outside the container, build or reuse the minsize Docker image and re-exec
   inside the container with the configured build directory and job count.
2. Inside the container, run the minsize build once, build the comparison smoke
   target, and execute grouped checks.
3. Continue after a group failure so the final harness report records all group
   statuses.
4. Return non-zero if any group fails.

The first groups are:

- `embedded_lifecycle`: existing embedded bootstrap smoke.
- `libmylite_lifecycle`: existing open/close smoke.
- `storage_single_file`: existing storage-engine smoke, including catalog
  persistence and recovery phases.
- `mariadb_comparison`: new reference-vs-MyLite comparison smoke.
- `sidecar_scan`: classify observed MyLite runtime files and fail unexpected
  persistent sidecars.

The harness report should list each group, its status, and the report paths it
produced.

### Comparison Smoke

Add `vendor/mariadb/server/mylite/compatibility_smoke.cc` and a
`mylite-compatibility-smoke` CMake target. The binary should:

- start the embedded runtime with controlled datadir and tmpdir paths,
- run the same case list against a caller-selected engine,
- write a detailed report,
- write a compact sorted fingerprint suitable for `diff -u`.

The harness runs the binary twice:

- reference run: `ENGINE=Aria`, in an isolated reference datadir,
- MyLite run: `ENGINE=MYLITE`, with a catalog file under the MyLite runtime
  root.

Aria is used as the first reference engine because it is already present in the
current minsize embedded profile and exercises MariaDB's DDL/DML path without
requiring a daemon. Aria sidecars are acceptable only in the reference runtime;
the MyLite runtime remains subject to sidecar scanning.

The binary should normalize only behavior that is intended to match. For the
initial subset, each case emits one stable `label=value` fingerprint line.
Duplicate primary-key behavior should normalize to MariaDB error number and
SQLSTATE, not the full human message.

### Sidecar Scan

The harness should scan only MyLite-owned runtime directories, not the reference
Aria runtime. It should fail if it observes persistent MariaDB table, log, or
plugin sidecars that are not already documented current embedded startup debt:

- `.frm`,
- `.ibd`,
- `.MYD`/`.MYI`,
- `.MAD`/`.MAI`,
- `ib_logfile*`,
- binlog/relay-log style files,
- dynamic plugin artifacts,
- lingering catalog temporary files.

`aria_log.00000001` and `aria_log_control` remain classified as known inherited
embedded startup files for now because current bootstrap and open/close smokes
already produce them. The harness must record them so a later runtime-cleanup or
minimal-profile slice can remove the exception deliberately.

## Affected Subsystems

- `tools/`: new top-level harness script.
- `vendor/mariadb/server/mylite/`: new comparison smoke binary and CMake target.
- `docs/`: new slice spec, roadmap update, and test-harness notes.

No production storage-engine or public API behavior should change in this slice.

## DDL Metadata Routing Impact

The comparison smoke creates and drops MyLite tables through normal MariaDB DDL.
It should verify the current routed path indirectly by comparing MyLite results
to the reference engine and by letting the sidecar scan catch `.frm` or engine
sidecars in MyLite runtime directories.

## Single-File And Embedded-Lifecycle Implications

The harness should make the current distinction explicit:

- MyLite user table definitions, rows, index bridge state, and autoincrement
  counters live in the `.mylite` catalog payload.
- The embedded MariaDB runtime still emits known Aria startup logs in its
  temporary datadir; these are not final MyLite-owned companions.
- Reference Aria comparison runs are isolated from MyLite runtime scans because
  they intentionally produce MariaDB engine sidecars.

## Public API And File-Format Impact

The public C API is unchanged.

The file format is unchanged. The comparison smoke should reuse the current
catalog file path option rather than adding new catalog records.

## Binary-Size Impact

The slice adds one test executable and one shell script. It should not change
`libmylite.a` or `libmariadbd.a` materially beyond build-system metadata. Record
artifact sizes after implementation.

## License, Trademark, And Dependency Impact

No dependency is introduced. The new code is first-party GPL-2.0-only test code
inside the MariaDB-derived tree. Public-facing reports should keep using the
MyLite name and should not imply MariaDB affiliation beyond compatibility
comparison.

## Test And Verification Plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/run-compatibility-test-harness.sh tools/run-storage-engine-smoke.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

The harness itself should run or reuse:

- `tools/run-embedded-bootstrap-smoke.sh`,
- `tools/run-libmylite-open-close-smoke.sh`,
- `tools/run-storage-engine-smoke.sh`,
- `mylite-compatibility-smoke` reference and MyLite passes,
- fingerprint diff between reference and MyLite,
- sidecar scan for MyLite runtime directories.

## Acceptance Criteria

- A single harness command runs the grouped compatibility checks and writes a
  readable group report.
- The harness returns non-zero when a group fails or a fingerprint differs.
- The comparison smoke proves the current supported DDL/DML subset matches a
  MariaDB reference engine for normalized outputs.
- MyLite runtime sidecar scanning fails `.frm`, engine table sidecars, dynamic
  plugin artifacts, catalog temporary sidecars, binlogs, relay logs, and InnoDB
  logs.
- Known inherited Aria startup logs are reported separately from unexpected
  sidecars.
- Existing individual smoke scripts still pass.
- Binary sizes and report paths are recorded.

## Risks And Unresolved Questions

- Aria is not a perfect storage-behavior oracle. It is a practical first
  embedded reference for supported SQL/handler behavior because it is already in
  the minsize profile. Later slices can add daemon-backed MariaDB comparison or
  selected MTR cases.
- Some MariaDB test suite cases are explicitly not embedded-safe; importing MTR
  requires a separate slice.
- Exact optimizer plans are not compared here. Fingerprints compare observable
  results and selected error identities.
- The sidecar scan intentionally carries a short-lived exception for inherited
  Aria startup logs. That exception should shrink, not grow.

## Implementation Result

MyLite now has a grouped compatibility harness:

- `tools/run-compatibility-test-harness.sh` runs the existing lifecycle and
  storage smokes, then runs a new MariaDB-reference comparison and MyLite
  sidecar scan.
- `mylite-compatibility-smoke` runs the supported scalar, row, key, duplicate,
  and autoincrement subset against either `ENGINE=Aria` or `ENGINE=MYLITE`.
- The harness writes detailed reference and MyLite reports plus compact
  fingerprints and fails on `diff -u` mismatches.
- The sidecar scan fails unexpected MyLite runtime `.frm`, engine, binlog,
  relay-log, InnoDB log, dynamic plugin, and lingering catalog temporary files.
- Known inherited `aria_log.*` startup files are reported separately from
  unexpected sidecars.

The comparison exposed a MyLite duplicate-key diagnostic mismatch during
implementation: MyLite returned SQLSTATE `23000`, but mapped primary-key
duplicates to errno `1022` instead of MariaDB's `1062`. The storage handler now
sets `lookup_errkey` for duplicate insert/update errors so MariaDB's common
`handler::print_error()` path emits `ER_DUP_ENTRY`.

Verification passed:

```sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/run-compatibility-test-harness.sh tools/run-storage-engine-smoke.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh
git diff --check
```

Observed harness groups:

- `embedded_lifecycle`: `status=0`.
- `libmylite_lifecycle`: `status=0`.
- `storage_single_file`: `status=0`.
- `mariadb_comparison`: `status=0`.
- `sidecar_scan`: `status=0`, `unexpected_sidecars=none`.

Observed comparison fingerprint for both `ENGINE=Aria` and `ENGINE=MYLITE`:

```text
scalar_add=3
scalar_concat=mylite
scalar_coalesce=fallback
row_count=2
row_values=2:TWO,3:three
key_lookup=two
key_order=1,2,3
key_duplicate=error:1062:23000
auto_values=1,2,10,12
```

Observed artifacts after this slice:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,313,562 bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 29,698 bytes.
- `build/mariadb-minsize/mylite/mylite-compatibility-smoke`: 22,692,632
  bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,692,464
  bytes.
- `build/mariadb-minsize/mylite/mylite-open-close-smoke`: 22,693,600 bytes.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,691,496
  bytes.
- `build/mariadb-minsize/mylite-compatibility-mylite/catalog.mylite`: 55,175
  bytes.
- `build/mariadb-minsize/mylite-catalog-persistence/catalog.mylite`: 43,120
  bytes.
- `build/mariadb-minsize/mylite-catalog-recovery/catalog.mylite`: 39,938
  bytes.
