# procedure-analyse-size-profile

## Problem

The MyLite minsize profile still builds MariaDB's `PROCEDURE ANALYSE()`
implementation. This is a legacy server-side result-set analysis feature for
interactive schema tuning, not part of MyLite's embedded file-owned runtime
contract. It also carries a relatively isolated implementation object in the
SQL layer.

This slice removes the `PROCEDURE ANALYSE()` implementation from the minsize
embedded archive while preserving a clear unsupported-feature diagnostic.

## Source Findings

Base source: MariaDB Server `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- MariaDB documentation says `PROCEDURE ANALYSE()` examines a query result set
  and suggests optimal data types, and that it is defined in
  `sql/sql_analyse.cc`:
  <https://mariadb.com/docs/server/reference/sql-functions/secondary-functions/information-functions/procedure-analyse>.
- MariaDB documentation for the `PROCEDURE` SELECT clause says procedures are
  C procedures and that `ANALYSE` is currently the only available procedure:
  <https://mariadb.com/docs/server/reference/sql-statements/data-manipulation/selecting-data/procedure>.
- `vendor/mariadb/server/sql/sql_yacc.yy` parses a `PROCEDURE_SYM ident`
  clause after a `SELECT` query and stores the requested procedure in
  `LEX::proc_list`.
- `vendor/mariadb/server/sql/procedure.cc` defines the built-in procedure
  dispatch table and maps `analyse` to `proc_analyse_init`.
- `vendor/mariadb/server/sql/sql_analyse.h` declares `proc_analyse_init()`.
- `vendor/mariadb/server/sql/sql_analyse.cc` implements the result-set
  analyser and its `field_info` helpers. In the current minsize build,
  `sql_analyse.cc.o` is 144,400 bytes in the stripped embedded archive.

## Design

- Add a MyLite-owned `MYLITE_DISABLE_PROCEDURE_ANALYSE` CMake option in
  `vendor/mariadb/server/libmysqld/CMakeLists.txt`.
- Set `-DMYLITE_DISABLE_PROCEDURE_ANALYSE=ON` in
  `tools/build-mariadb-minsize.sh`.
- When the option is enabled, remove `../sql/sql_analyse.cc` from
  `SQL_EMBEDDED_SOURCES` and append a small
  `mylite_procedure_analyse_stub.cc`.
- Keep the parser and `procedure.cc` dispatch table intact. The stub provides
  `proc_analyse_init()` and returns `ER_NOT_SUPPORTED_YET`, so
  `PROCEDURE ANALYSE()` fails explicitly rather than as an unknown procedure or
  a link error.
- Extend the `libmylite` open/close smoke with a small MyLite table and a
  `SELECT ... PROCEDURE ANALYSE()` query that verifies the unsupported
  diagnostic.

## Affected Subsystems

- Build profile: `tools/build-mariadb-minsize.sh`.
- Embedded SQL source list: `vendor/mariadb/server/libmysqld/CMakeLists.txt`.
- SELECT post-processing procedure support: `PROCEDURE ANALYSE()`.
- Test coverage: `vendor/mariadb/server/mylite/open_close_smoke.cc`.

## DDL Metadata Routing Impact

None. The smoke creates a normal MyLite table only to reach the SELECT
procedure path, and existing storage/catalog smokes already verify MyLite DDL
routing. The feature removal itself does not alter table metadata routing.

## Single-File and Embedded Lifecycle Impact

Removing `PROCEDURE ANALYSE()` should reduce per-query analysis code and should
not create new sidecar files. The unsupported path must not open extra tables
or allocate long-lived procedure state.

## Public API and File Format Impact

No public `libmylite` C API change. No `.mylite` file-format change.

SQL compatibility impact: the minsize profile does not support
`PROCEDURE ANALYSE()`; it returns a stable unsupported-feature diagnostic.

## Binary-Size Impact

Expected savings are small-to-moderate. The stripped archive member
`sql_analyse.cc.o` is currently 144,400 bytes. A small stub will replace it,
and the linked smoke binary should also shrink if the SELECT procedure path was
pulling the full analyser into the linked artifact.

Measure after implementation:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
stat -c "%s" build/mariadb-minsize/libmysqld/libmariadbd.a
ar t build/mariadb-minsize/libmysqld/libmariadbd.a | wc -l
stat -c "%s" build/mariadb-minsize/mylite/mylite-open-close-smoke
cp build/mariadb-minsize/mylite/mylite-open-close-smoke \
  build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
strip --strip-unneeded \
  build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
stat -c "%s" build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
size build/mariadb-minsize/mylite/mylite-open-close-smoke
```

## License, Trademark, and Dependency Impact

No new dependency. No license or trademark impact beyond the existing
MariaDB-derived GPL-2.0-only project status.

## Test Plan

- Build the minsize profile.
- Run the `libmylite` open/close smoke.
- Run the grouped compatibility harness.
- Confirm the build report records `MYLITE_DISABLE_PROCEDURE_ANALYSE:BOOL=ON`.
- Confirm the linked smoke report records the `PROCEDURE ANALYSE()`
  unsupported-feature message.
- Confirm `libmariadbd.a` no longer includes `sql_analyse.cc.o`.

## Acceptance Criteria

- `tools/build-mariadb-minsize.sh` succeeds.
- `tools/run-libmylite-open-close-smoke.sh` succeeds.
- `tools/run-compatibility-test-harness.sh` succeeds.
- `SELECT ... PROCEDURE ANALYSE()` fails with `ER_NOT_SUPPORTED_YET` in the
  minsize profile.
- Artifact size deltas are recorded in this spec and in
  `docs/research/production-size-analysis.md`.

## Risks and Unresolved Questions

- The generic `PROCEDURE` clause framework remains compiled because it is
  intertwined with SELECT preparation. This slice removes only the sole built-in
  implementation.
- Parser syntax remains available, so unsupported `PROCEDURE ANALYSE()`
  statements fail during SELECT procedure setup rather than parse time.
