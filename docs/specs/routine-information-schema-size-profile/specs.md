# Routine Information Schema Size Profile

## Problem

The aggressive MyLite minsize profile rejects stored routine DDL and does not
support stored routine execution, but `INFORMATION_SCHEMA.ROUTINES` and
`INFORMATION_SCHEMA.PARAMETERS` still link MariaDB's `mysql.proc` scan and
stored-program metadata loader paths.

Stored routine metadata is a server-side catalog surface. MyLite does not yet
store routines in the `.mylite` catalog, and the default embedded profile should
not retain routine sidecar/table scanning only to return an empty result.

Current measurements from
`build/mariadb-minsize-no-dynamic-columns` show the relevant retained code:

| Object | Bytes |
| --- | ---: |
| `sql_show.cc.o` | 598,544 |
| `sp_head.cc.o` | 240,168 |
| `sp_instr.cc.o` | 271,920 |
| `sp.cc.o` | 170,272 |
| `sp_rcontext.cc.o` | 166,312 |

This slice targets the `sql_show.cc` routine metadata path only. Removing the
stored-program parser and runtime is a larger follow-up because parser state and
execution classes are referenced throughout MariaDB's SQL layer.

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

`vendor/mariadb/server/sql/sql_show.cc` defines the built-in Information Schema
table registry in `schema_tables[]`. The `PARAMETERS` and `ROUTINES` entries
both call `fill_schema_proc()`.

`fill_schema_proc()` opens the inherited `mysql.proc` table with
`open_proc_table_for_read()`, scans routine records, and dispatches to:

- `store_schema_proc()` for `INFORMATION_SCHEMA.ROUTINES`
- `store_schema_params()` for `INFORMATION_SCHEMA.PARAMETERS`

Those helpers use `Sp_handler`, `sp_head`, `sp_pcontext`, and routine privilege
checks to load stored routine metadata for display. In MyLite's minsize profile,
routine DDL is already rejected before persistent metadata can be created.

`docs/specs/schema-object-ddl-rejection/specs.md` documents `CREATE/ALTER/DROP
PROCEDURE` and stored-function DDL as unsupported. This slice keeps that product
decision and aligns routine introspection with it.

## Scope

This slice may:

- add `MYLITE_DISABLE_ROUTINE_INFORMATION_SCHEMA`,
- enable it from `tools/build-mariadb-minsize.sh`,
- keep the `ROUTINES` and `PARAMETERS` table definitions,
- make both tables empty in the minsize profile,
- compile out the `mysql.proc` scan and stored-program metadata formatting code,
  and
- add smoke coverage that the tables and related `SHOW` commands are empty.

## Non-Goals

This slice does not:

- remove stored-routine parser grammar,
- remove stored-program runtime objects outside the routine Information Schema
  path,
- add MyLite routine support,
- change routine DDL rejection behavior,
- change ordinary table metadata in `INFORMATION_SCHEMA.TABLES`, `COLUMNS`, or
  `STATISTICS`, or
- alter non-minsize builds.

## Proposed Design

Add a minsize-only CMake option. When enabled, `sql_show.cc` compiles the
routine metadata scan helpers out and routes the `PARAMETERS` and `ROUTINES`
schema-table entries to a tiny empty-table fill function.

The table definitions and old-format field selection stay intact so queries can
prepare and execute against stable MariaDB column shapes. The result set is
empty because MyLite cannot create routine metadata in this profile.

## Affected Subsystems

- Information Schema table population in `sql_show.cc`.
- Stored routine metadata display paths rooted from `mysql.proc`.
- MyLite open/close unsupported-profile smoke coverage.

## Single-File and Embedded Lifecycle Impact

This removes a retained inherited metadata scan for unsupported stored routines.
It does not change file format or storage semantics. It also avoids rooting a
future `mysql.proc` sidecar/table dependency through routine introspection in
the aggressive embedded profile.

## Public API and File-Format Impact

No public `libmylite` API change and no file-format change.

`INFORMATION_SCHEMA.ROUTINES` and `INFORMATION_SCHEMA.PARAMETERS` remain
queryable in the minsize profile, but return no rows. That is consistent with
routine DDL being unsupported.

## Binary-Size Impact

Expected savings are modest because this slice removes only one `sql_show.cc`
subpath and does not remove the stored-program parser/runtime. The direct
linked-runtime target is the `fill_schema_proc()` call graph and the routine
metadata formatter helpers. Static archive savings depend on how much code the
preprocessor can remove from the monolithic `sql_show.cc` object.

Measured against `build/mariadb-minsize-no-dynamic-columns`, the implemented
slice reduced:

- `libmysqld/libmariadbd.a` by 24,892 bytes,
- `sql_show.cc.o` by 24,552 bytes,
- the unstripped open-close smoke by 7,888 bytes,
- the stripped open-close smoke by 6,400 bytes,
- the unstripped compatibility smoke by 9,816 bytes, and
- the stripped compatibility smoke by 8,096 bytes.

## License, Trademark, and Dependency Impact

This is a GPL-2.0-only MariaDB-derived build-profile change. It adds no new
dependency and changes no trademark-facing packaging.

## Test and Verification Plan

Run:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-routine-is MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-routine-is tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-routine-is tools/run-storage-engine-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-routine-is tools/run-embedded-bootstrap-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-routine-is tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh`
- `git diff --check`

## Acceptance Criteria

- `INFORMATION_SCHEMA.ROUTINES` returns zero rows in the minsize profile.
- `INFORMATION_SCHEMA.PARAMETERS` returns zero rows in the minsize profile.
- `SHOW PROCEDURE STATUS` and `SHOW FUNCTION STATUS` execute and return no rows.
- Routine DDL remains explicitly unsupported through the existing bootstrap
  smoke.
- Current storage, bootstrap, open/close, and compatibility smokes pass.
- Measured size changes are recorded in
  `docs/research/production-size-analysis.md`.

Executed verification:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-routine-is MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-routine-is tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-routine-is tools/run-storage-engine-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-routine-is tools/run-embedded-bootstrap-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-routine-is tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh`
- `git diff --check`

## Risks and Unresolved Questions

- This does not remove the larger stored-program parser/runtime dependency. That
  remains a separate, higher-risk size slice.
- Returning empty routine metadata is correct while routine DDL is unsupported.
  If MyLite later supports routines, this profile must be revisited with a
  MyLite catalog-backed implementation.
