# Tpool Wait Stub Size Profile

## Problem Statement

The current MyLite minsize runtime links MariaDB's `tpool` target even though
the embedded profile does not use the SQL listener thread pool, InnoDB, or
native asynchronous I/O. With those subsystems absent, the only visible need in
the linked open/close smoke is `tpool_wait_begin()` and `tpool_wait_end()` from
`vendor/mariadb/server/sql/mdl.cc`.

This is an embedded-shape size cut, not a SQL compatibility cut. It should
avoid linking the inherited thread-pool runtime into MyLite artifacts while
keeping metadata-lock wait behavior unchanged for a no-thread-pool embedded
process.

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/tpool/CMakeLists.txt` builds `tpool` from the generic
  thread-pool, task, wait-notification, and Linux AIO sources.
- `vendor/mariadb/server/tpool/wait_notification.cc` defines
  `tpool_wait_begin()` and `tpool_wait_end()` as notifications to a
  thread-local `tpool::thread_pool`.
- `vendor/mariadb/server/sql/mdl.cc` calls those wait-notification functions
  around metadata-lock waits.
- `vendor/mariadb/server/libmysqld/CMakeLists.txt` links `sql_embedded` and
  `mysqlserver` with `tpool`.
- `vendor/mariadb/server/mylite/CMakeLists.txt` links MyLite libraries and
  smokes with `tpool`.
- The current minsize `libmariadbd.a` only has undefined references to
  `tpool_wait_begin` and `tpool_wait_end` from `mdl.cc.o`; it does not contain
  the thread-pool implementation.

## Scope

This slice may:

- add `MYLITE_DISABLE_TPOOL_RUNTIME`,
- enable it in `tools/build-mariadb-minsize.sh`,
- add a tiny no-op embedded stub for `tpool_wait_begin()` and
  `tpool_wait_end()`,
- avoid linking MyLite embedded/static targets with `tpool` when the option is
  enabled, and
- verify the linked smoke no longer defines inherited `tpool` runtime symbols.

## Non-Goals

This slice does not:

- remove MariaDB SQL syntax or SQL result behavior,
- remove metadata locking,
- remove pthread support,
- change non-minsize embedded builds,
- change InnoDB or daemon server behavior, or
- claim that `tpool` can be removed from a full MariaDB server build.

## Proposed Design

Add `MYLITE_DISABLE_TPOOL_RUNTIME` as an off-by-default option in the embedded
library CMake file.

When enabled, append `mylite_tpool_wait_stub.cc` to the embedded source list.
The stub should export C-linkage `tpool_wait_begin()` and `tpool_wait_end()`
functions that intentionally do nothing. That matches the current MyLite
embedded runtime, where no SQL thread-pool scheduler is active and there is no
thread-local pool to notify.

Conditionally avoid linking `sql_embedded`, `mysqlserver`, `libmylite`, and
the MyLite smoke binaries against `tpool` under the same option.

## Affected Subsystems

- Embedded build profile.
- Metadata-lock wait notification hooks.
- MyLite test executable link lines.

## DDL Metadata Routing Impact

No DDL routing change. `CREATE`, `ALTER`, `DROP`, `RENAME`, and table discovery
continue to use the same SQL and storage-engine paths.

## Single-File And Embedded-Lifecycle Impact

No file-format or lifecycle behavior changes. The removed dependency is a
server/thread-pool notification layer that has no documented MyLite companion
file or runtime state in the current embedded profile.

## Public API Or File-Format Impact

No public `libmylite` C API change and no `.mylite` file-format change.

## Binary-Size Impact

Before implementation, `mylite-open-close-smoke` defines only tiny tpool wait
notification symbols and a thread-local `tls_thread_pool`. The direct linked
savings are expected to be small. The packaging value is that MyLite artifacts
no longer require the separate `libtpool.a` link dependency in this profile.

Measured after implementation against
`build/mariadb-minsize-no-show-create`:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 25,389,516 | 25,391,070 | +1,554 |
| `mylite/libmylite.a` | 76,130 | 76,122 | -8 |
| `storage/mylite/libmylite_embedded.a` | 388,456 | 388,456 | 0 |
| unstripped `mylite-open-close-smoke` | 6,486,024 | 6,485,584 | -440 |
| stripped `mylite-open-close-smoke` | 4,552,192 | 4,551,888 | -304 |
| `size` decimal for `mylite-open-close-smoke` | 4,774,460 | 4,774,464 | +4 |

The static archive grows because the no-op wait hook moves into
`libmariadbd.a`, but the linked executable drops the inherited
`wait_notification.cc` thread-local state. This is a tiny linked-size and
dependency-shape win, not a major binary-size lever.

## License, Trademark, And Dependency Impact

This is a GPL-2.0-only MariaDB-derived build-profile change. It adds no
dependency and removes one internal static-library dependency from the MyLite
minsize link.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-tpool \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-tpool \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-tpool \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-tpool \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-tpool \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh \
  tools/run-libmylite-open-close-smoke.sh \
  tools/run-storage-engine-smoke.sh \
  tools/run-embedded-bootstrap-smoke.sh \
  tools/run-compatibility-test-harness.sh
git diff --check
```

Measure:

- `libmariadbd.a`;
- `libmylite.a`;
- unstripped and stripped `mylite-open-close-smoke`;
- `size` section totals; and
- absence of linked `tpool::` implementation symbols.

## Acceptance Criteria

- The minsize build completes without linking MyLite artifacts against
  `tpool`.
- Current MyLite smokes and compatibility harness pass.
- Linked MyLite runtime artifacts contain no inherited `tpool::thread_pool`
  implementation symbols.
- Size results are recorded here and in
  `docs/research/production-size-analysis.md`.

## Verification Results

Passed:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-tpool \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-tpool \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-tpool \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-tpool \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-tpool \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

The open/close smoke reports `libmylite tpool runtime symbols: none`. The
linked binary still defines the expected no-op `tpool_wait_begin()` and
`tpool_wait_end()` functions from the MyLite stub.

## Risks And Unresolved Questions

- The savings may be tiny because section GC already links only the
  wait-notification object from `libtpool.a`.
- A future profile that re-enables the SQL listener thread pool, InnoDB, or
  native asynchronous I/O must not use this option without a fresh design.
