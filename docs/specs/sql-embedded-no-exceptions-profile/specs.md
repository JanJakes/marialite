# SQL Embedded No Exceptions Profile

## Problem Statement

The current aggressive minsize linked smoke still carries C++ exception metadata:

- `.eh_frame`: 426,808 bytes,
- `.gcc_except_table`: 33,036 bytes.

Earlier global `-fno-exceptions` testing failed in MariaDB thread-pool code and
would also weaken first-party MyLite allocation diagnostics. This slice tests a
narrower build-only reduction: compile only the retained `sql_embedded` target
without C++ exception support while preserving public MyLite API and storage
engine exception handling.

This is not a SQL compatibility-removal slice. If the flag requires cutting
ordinary MariaDB SQL behavior, the attempt is rejected.

## Source Findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

Relevant local source:

- `vendor/mariadb/server/libmysqld/CMakeLists.txt` builds the embedded SQL
  runtime as `ADD_CONVENIENCE_LIBRARY(sql_embedded ${SQL_EMBEDDED_SOURCES})`.
- `vendor/mariadb/server/cmake/libutils.cmake` implements
  `ADD_CONVENIENCE_LIBRARY` as a static CMake target, so target-scoped compile
  options can be applied to `sql_embedded`.
- `vendor/mariadb/server/sql/item_strfunc.cc` contains the retained SQL
  target's active `catch (const fmt::format_error&)` path only inside the
  `SFORMAT()` implementation, which is already guarded by
  `MYLITE_DISABLE_SFORMAT_FUNCTION` in the current aggressive minsize profile.
- `vendor/mariadb/server/mylite/mylite.cc` and
  `vendor/mariadb/server/storage/mylite/ha_mylite.cc` catch
  `std::bad_alloc` in first-party API and storage code. Those files are outside
  `sql_embedded` and must keep exception support.
- `MYLITE_DISABLE_UNWIND_TABLES` already removes nonessential unwind tables,
  but mandatory exception metadata remains in the linked artifact.

## Proposed Design

Add an off-by-default CMake option:

```text
MYLITE_DISABLE_SQL_EMBEDDED_EXCEPTIONS
```

When enabled, it applies `-fno-exceptions` only to C++ sources in the
`sql_embedded` target. The option requires `MYLITE_DISABLE_SFORMAT_FUNCTION`
because retained `SFORMAT()` uses a real C++ `catch` block around fmtlib.

Enable the option in `tools/build-mariadb-minsize.sh` for the aggressive size
profile. Do not add global `-fno-exceptions`, and do not apply the flag to
`libmylite`, `ha_mylite`, tests, storage engines, mysys, tpool, or client code.

## Non-Goals

- Do not remove SQL features to satisfy the compiler flag.
- Do not disable exceptions in first-party MyLite API or storage code.
- Do not change non-minsize builds.
- Do not claim this is safe for every future MariaDB source import without
  rechecking retained `sql_embedded` exception use.

## Affected Subsystems

- Embedded build profile and generated SQL object code.
- Linked C++ exception metadata.
- No intended SQL parser, analyzer, optimizer, execution, storage, public API,
  file-format, or lifecycle behavior change.

## Single-File And Embedded-Lifecycle Impact

None intended. This changes compiler behavior for one static target only. It
does not change `.mylite` files, MyLite-owned companions, startup, shutdown,
locking, recovery, or catalog routing.

## Public API Or File-Format Impact

No public C API, ABI, or `.mylite` file-format change.

## Binary-Size Impact

The theoretical upper bound is the currently linked `.gcc_except_table` plus
any exception-related entries in `.eh_frame` for `sql_embedded` objects. The
current stripped linked open-close smoke is 4,452,104 bytes before this
attempt.

Measurements must record:

- `libmysqld/libmariadbd.a`,
- `mylite/libmylite.a`,
- unstripped `mylite/mylite-open-close-smoke`,
- stripped `mylite/mylite-open-close-smoke`,
- `.eh_frame` and `.gcc_except_table` section sizes.

Measured against `build/mariadb-minsize-no-sformat`:

| Artifact | SFORMAT profile | No SQL exceptions profile | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 24,991,890 | 22,437,126 | -2,554,764 |
| `mylite/libmylite.a` | 76,688 | 76,696 | +8 |
| `mylite-open-close-smoke` | 6,322,840 | 5,858,840 | -464,000 |
| stripped `mylite-open-close-smoke` copy | 4,452,104 | 3,993,848 | -458,256 |

Linked section deltas:

| Section | SFORMAT profile | No SQL exceptions profile | Delta |
| --- | ---: | ---: | ---: |
| `.eh_frame` | 426,808 | 20,640 | -406,168 |
| `.gcc_except_table` | 33,036 | 6,756 | -26,280 |
| `size` decimal total | 4,666,200 | 4,207,508 | -458,692 |

A one-off minimal consumer linked against the same archives and calling only
`mylite_open()` and `mylite_close()` measured 5,734,200 bytes unstripped and
3,886,072 bytes after `strip --strip-unneeded`.

## Test And Verification Plan

Run in a separate build directory:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-exceptions \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-exceptions \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-exceptions \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-exceptions \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-exceptions \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

## Acceptance Criteria

- The minsize build completes with `MYLITE_DISABLE_SQL_EMBEDDED_EXCEPTIONS=ON`.
- Current smoke tests and the compatibility harness pass.
- First-party `libmylite` and MyLite storage engine code are not compiled with
  `-fno-exceptions`.
- No additional SQL feature is disabled to make this compile.
- Size deltas and section deltas are recorded in this spec and in
  `docs/research/production-size-analysis.md`.
- If savings are negligible or behavior changes, reject the attempt and
  document why.

## Risks And Unresolved Questions

- If a C++ exception from another library crosses a `sql_embedded` frame, the
  runtime behavior can change. Current retained SQL code is expected to use
  MariaDB diagnostics and MEM_ROOT-style error handling instead of C++
  exception propagation, but that must be rechecked after upstream imports.
- Future source changes may add a real `try` or `catch` to `sql_embedded`; the
  CMake guard only covers the known current `SFORMAT()` case.

## Implementation Result

Implemented as `MYLITE_DISABLE_SQL_EMBEDDED_EXCEPTIONS`. The option is
off-by-default and enabled only by the aggressive minsize script. CMake rejects
the combination unless `MYLITE_DISABLE_SFORMAT_FUNCTION` is also enabled.

`build.ninja` confirms `-fno-exceptions` on `sql_embedded` C++ objects such as
`sql_parse.cc.o`, and confirms no `-fno-exceptions` flag on
`mylite/mylite.cc.o` or `storage/mylite/ha_mylite.cc.o`.

Verification run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-exceptions \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-exceptions \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-exceptions \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-exceptions \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-exceptions \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

All passed.
