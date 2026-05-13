# Binlog Object Init Size Profile

## Problem

The aggressive embedded minsize profile disables binary logging, but startup
still initializes and shutdown still cleans up the process-global
`mysql_bin_log` mutex and condition state. In a no-binlog embedded build the
object never opens, and its transaction/logging methods are already compiled to
no-op or fail-closed behavior.

Current reference point after `binlog-cache-dir-size-profile`:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 25,281,276 |
| stripped `mylite-open-close-smoke` | 4,526,352 |

Measured result after this slice:

| Artifact | Bytes | Delta |
| --- | ---: | ---: |
| `libmysqld/libmariadbd.a` | 25,280,988 | -288 |
| `mylite-open-close-smoke` | 6,437,912 | -768 |
| stripped `mylite-open-close-smoke` | 4,525,904 | -448 |
| `llvm-size` decimal total | 4,739,978 | +28 |

The linked file shrank, but the `llvm-size` decimal total is effectively flat:
`.text` decreased by 420 bytes while measured `bss` increased by 448 bytes.

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/mysqld.cc` calls
  `mysql_bin_log.init_pthread_objects()` during startup and
  `mysql_bin_log.cleanup()` during shutdown.
- `vendor/mariadb/server/sql/log.cc` compiles many `MYSQL_BIN_LOG` methods to
  no-ops under `MYLITE_DISABLE_BINLOG_CORE`, including `open()`, `close()`,
  write paths, GTID state file IO, and transaction write paths.
- `vendor/mariadb/server/sql/log.h` makes `MYSQL_BIN_LOG::is_open()` return
  false under `MYLITE_DISABLE_BINLOG_CORE`.
- Before this slice, the linked smoke retained
  `MYSQL_BIN_LOG::init_pthread_objects()` and `MYSQL_BIN_LOG::cleanup()` only
  because of the startup/shutdown calls.

## Scope

Add `MYLITE_DISABLE_BINLOG_OBJECT_INIT`, enabled only by the minsize build
script. When set for an embedded no-binlog build, skip `mysql_bin_log`
instrumentation-key setup, pthread-object initialization, and cleanup.

## Non-Goals

This slice does not:

- remove the process-global `mysql_bin_log` object,
- remove the `binlog_tp` transaction participant shell,
- remove ordinary SQL behavior,
- change transaction coordinator selection, or
- change non-minsize builds.

## Proposed Design

Add a CMake option in `vendor/mariadb/server/libmysqld/CMakeLists.txt`:

- `MYLITE_DISABLE_BINLOG_OBJECT_INIT`

Require it to be used with `MYLITE_DISABLE_BINLOG_CORE`.

In `mysqld.cc`, guard the startup `set_psi_keys()` and
`init_pthread_objects()` calls and the shutdown `cleanup()` call for
`mysql_bin_log`.

## Affected Subsystems

- Embedded startup and shutdown.
- Inherited binary-log synchronization primitives.
- Linked code sections in minsize smoke binaries.

## Single-File and Embedded-Lifecycle Impact

The change removes initialization of synchronization primitives for a disabled
server log that the embedded profile never opens. It does not change `.mylite`
file ownership, transaction durability, or allowed companion files.

## Public API and File-Format Impact

No public `libmylite` API change and no file-format change.

## Binary-Size Impact

The savings are tiny: the linked smoke retained only two small nonvirtual
methods because startup/shutdown called them. The process-global
`mysql_bin_log` object, vtable, and `binlog_tp` transaction participant remain.

## Test and Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-object-init MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-object-init MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-object-init MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-object-init MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-object-init MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh
git diff --check
```

Measure:

- `libmysqld/libmariadbd.a`,
- unstripped and stripped `mylite-open-close-smoke`,
- absence of `MYSQL_BIN_LOG::init_pthread_objects()` and
  `MYSQL_BIN_LOG::cleanup()` from the linked smoke binary.

## Acceptance Criteria

- The new flag is off by default and enabled only in the minsize script.
- Non-minsize builds remain unchanged.
- Current MyLite smokes and compatibility harness pass.
- The linked smoke no longer retains the unused binlog init/cleanup methods.
- Size measurements are recorded in `docs/research/production-size-analysis.md`.

## Verification Results

Passed:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-object-init MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-object-init MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-object-init MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-object-init MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-object-init MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh
git diff --check
```

The open/close smoke verifies that
`MYSQL_BIN_LOG::init_pthread_objects()` and `MYSQL_BIN_LOG::cleanup()` are no
longer linked into `mylite-open-close-smoke`.

## Risks and Unresolved Questions

- This relies on the no-binlog core invariant that `mysql_bin_log` is never
  opened. If a future profile re-enables any binary logging path, this flag
  must remain off.
- The larger `mysql_bin_log` object and vtable remain. Removing them requires a
  separate transaction/logging shell redesign.
