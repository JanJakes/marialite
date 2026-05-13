# Binlog Cache Dir Size Profile

## Problem

The aggressive embedded minsize profile disables binary logging, but startup
still calls `init_binlog_cache_dir()`. That function computes the inherited
`#binlog_cache_files` directory, scans it, deletes it when binlogging is off,
and creates it when binlogging is on. MyLite's no-binlog embedded profile
should not create or manage this daemon binlog cache directory.

Current reference point after `binlog-sysvar-size-profile`:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 25,281,364 |
| stripped `mylite-open-close-smoke` | 4,527,880 |

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/mysqld.cc` calls `init_binlog_cache_dir()` during
  server startup after opening the binlog when `opt_bin_log` is enabled.
- `vendor/mariadb/server/sql/log_cache.cc` defines `binlog_cache_dir` and
  `init_binlog_cache_dir()`. When `opt_bin_log` is false, the function still
  probes for `#binlog_cache_files` and deletes it if present.
- `vendor/mariadb/server/sql/log.cc` already compiles
  `THD::binlog_setup_trx_data()` to return `nullptr` under
  `MYLITE_DISABLE_BINLOG_CORE`, so the binlog cache manager and temporary
  binlog cache files are not used by the no-binlog embedded runtime.
- The current linked smoke binary still retains `init_binlog_cache_dir()` and
  `binlog_cache_dir`.

## Official Documentation References

- MariaDB Binary Log documents the binary log as a server log used for
  replication and recovery:
  <https://mariadb.com/docs/server/server-management/server-monitoring-logs/binary-log>

## Scope

Add `MYLITE_DISABLE_BINLOG_CACHE_DIR`, enabled only by the minsize build
script. When set for an embedded no-binlog build, skip
`init_binlog_cache_dir()` during startup.

## Non-Goals

This slice does not:

- remove or replace the remaining `MYSQL_BIN_LOG` object,
- change transaction coordinator selection,
- remove ordinary SQL behavior,
- change non-minsize builds, or
- claim support for inherited binlog cache files.

## Proposed Design

Add a CMake option in `vendor/mariadb/server/libmysqld/CMakeLists.txt`:

- `MYLITE_DISABLE_BINLOG_CACHE_DIR`

Require it to be used with `MYLITE_DISABLE_BINLOG_CORE`.

In `mysqld.cc`, guard the startup `init_binlog_cache_dir()` call behind the new
flag for embedded builds. This keeps the upstream function available for
non-minsize builds and avoids modifying `log_cache.cc` until measurement proves
source-list pruning is worthwhile.

## Affected Subsystems

- Embedded startup.
- Inherited binlog cache directory cleanup/creation.
- Linked string and BSS sections in minsize smoke binaries.

## Single-File and Embedded-Lifecycle Impact

The change aligns with MyLite's file-owned lifecycle by not probing, deleting,
or creating an inherited server binlog cache directory when binary logging is
disabled. The MyLite sidecar scan remains the guard for unexpected persistent
files.

## Public API and File-Format Impact

No public `libmylite` API change and no file-format change.

## Binary-Size Impact

Measured against `binlog-sysvar-size-profile`, this slice reduced:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 25,281,364 | 25,281,276 | -88 |
| unstripped `mylite-open-close-smoke` | 6,440,584 | 6,438,680 | -1,904 |
| stripped `mylite-open-close-smoke` | 4,527,880 | 4,526,352 | -1,528 |
| `llvm-size` decimal total | 4,740,522 | 4,739,950 | -572 |

The linked smoke no longer defines `init_binlog_cache_dir()` or
`binlog_cache_dir`. The archive impact is intentionally small because
`log_cache.cc.o` still builds into the merged static archive.

## Test and Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-cache-dir MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-cache-dir MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-cache-dir MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-cache-dir MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-cache-dir MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh
git diff --check
```

Measure:

- `libmysqld/libmariadbd.a`,
- unstripped and stripped `mylite-open-close-smoke`,
- `llvm-size` section deltas,
- absence of `init_binlog_cache_dir` and `binlog_cache_dir` from the linked
  smoke binary if section GC can drop them.

## Acceptance Criteria

- The new flag is off by default and enabled only in the minsize script.
- Non-minsize builds remain unchanged.
- Current MyLite smokes and compatibility harness pass.
- The runtime no longer initializes or cleans inherited binlog cache
  directories in the aggressive embedded no-binlog profile.
- Size measurements are recorded in `docs/research/production-size-analysis.md`.

## Verification Results

Passed:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-cache-dir MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-cache-dir MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-cache-dir MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-cache-dir MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-binlog-cache-dir MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

The open/close smoke now asserts the linked smoke binary does not define
`init_binlog_cache_dir` or `binlog_cache_dir`.

## Risks and Unresolved Questions

- Skipping inherited cleanup means an unrelated stale `#binlog_cache_files`
  directory is no longer deleted opportunistically. MyLite should not create
  that directory in this profile, and unexpected sidecar scans should catch it
  if it appears in the MyLite runtime area.
- This does not address the larger `MYSQL_BIN_LOG` object, vtable, or
  transaction participant shell. That remains a separate design problem.
