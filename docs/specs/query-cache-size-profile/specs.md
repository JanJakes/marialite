# query-cache-size-profile

## Problem

The minsize profile still links MariaDB's query cache implementation even
though the cache is disabled by default and is a poor fit for MyLite's embedded
single-file default profile. The linked runtime keeps query-cache lookup,
insertion, invalidation, status, and embedded result serialization code on the
hot SQL path.

This slice tests whether MyLite can omit the query cache from the default
minsize embedded profile while preserving normal query execution and reporting
the cache as unavailable through MariaDB's existing `have_query_cache` surface.

## Source baseline

- MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Current MyLite size baseline after `json-schema-valid-size-profile`:
  - `libmariadbd.a`: 36,174,834 bytes,
  - stripped `mylite-open-close-smoke`: 8,413,768 bytes,
  - linked `size` total: 8,707,704 bytes.

## Source findings

- MariaDB's official query-cache documentation describes the feature as a
  cache of `SELECT` results for identical future queries and notes that it is
  disabled by default because it does not scale well on high-throughput
  multi-core systems:
  - <https://mariadb.com/docs/server/ha-and-performance/optimization-and-tuning/buffers-caches-and-threads/query-cache>
- The same documentation states that `have_query_cache` reports whether the
  server was built with query-cache support, and that `NO` means the cache
  cannot be enabled without rebuilding or reinstalling MariaDB.
- It also documents that setting `query_cache_type` or `query_cache_size` to
  `0` disables the cache and that both should be `0` to free the most
  resources.
- `vendor/mariadb/server/libmysqld/CMakeLists.txt` includes
  `emb_qcache.cc` and `../sql/sql_cache.cc` in `SQL_EMBEDDED_SOURCES`.
- Current archive member sizes in the JSON-schema profile:
  - `sql_cache.cc.o`: 67,352 bytes,
  - `emb_qcache.cc.o`: 14,216 bytes.
- `vendor/mariadb/server/sql/sql_cache.h` declares the `Query_cache` class,
  the global `query_cache`, and macros used throughout the SQL layer:
  `query_cache_send_result_to_client`, `query_cache_store_query`,
  `query_cache_invalidate3`, `query_cache_end_of_result`,
  `query_cache_abort`, `query_cache_destroy`, and initialization helpers.
- `vendor/mariadb/server/sql/sql_parse.cc` calls
  `query_cache_send_result_to_client()` before parsing a statement and calls
  `query_cache_store_query()` when executing cacheable `SELECT` statements.
  A disabled-cache stub must therefore return a cache miss without preventing
  normal parsing and execution.
- DDL and DML paths call query-cache invalidation macros from
  `sql_db.cc`, `sql_table.cc`, `sql_insert.cc`, `sql_truncate.cc`,
  `sql_partition.cc`, `sql_admin.cc`, `handler.cc`, and replication event
  code. A disabled-cache profile must keep those call sites harmless no-ops.
- `vendor/mariadb/server/sql/sys_vars.cc` exposes `query_cache_size`,
  `query_cache_limit`, `query_cache_min_res_unit`, `query_cache_type`,
  `query_cache_wlock_invalidate`, `query_cache_strip_comments`, and
  `have_query_cache`. The size and type update hooks call methods on the
  global `query_cache` object.
- `vendor/mariadb/server/sql/mysqld.cc` owns the process globals
  `query_cache_size`, `query_cache_limit`, `query_cache_min_res_unit`, and
  `Query_cache query_cache`; startup sets min unit, result limit, initializes,
  and resizes the cache. Startup also currently assigns
  `have_query_cache=SHOW_OPTION_YES`.
- `mysqld.cc` exposes `Qcache_*` status variables by reading public fields of
  the global `query_cache` object.
- `vendor/mariadb/server/libmysqld/emb_qcache.cc` is only referenced from
  `sql_cache.cc`; it serializes embedded result packets into query-cache
  blocks through `Querycache_stream`.
- The current linked smoke binary still contains many `Query_cache::*` symbols,
  including `send_result_to_client`, `store_query`, `init_cache`, and the
  global `query_cache` object.

## Proposed design

Add an embedded-library CMake option:

```cmake
MYLITE_DISABLE_QUERY_CACHE
```

When enabled:

- define `MYLITE_DISABLE_QUERY_CACHE` for embedded library sources,
- remove `../sql/sql_cache.cc` and `emb_qcache.cc` from
  `SQL_EMBEDDED_SOURCES`,
- add a MyLite-owned embedded stub source that defines the `Query_cache`
  methods and C helper symbols needed by existing MariaDB call sites,
- make the stub permanently disabled:
  - `send_result_to_client()` returns `0` so execution continues normally,
  - `store_query()`, `insert()`, `end_of_result()`, `abort()`, invalidation,
    `flush()`, `pack()`, and `destroy()` are no-ops,
  - `resize()` returns `0`,
  - public `Qcache_*` counters remain zero,
  - `is_disabled()` remains true through the existing class state,
- set `have_query_cache=SHOW_OPTION_NO` in the disabled profile,
- keep the sysvar names present for compatibility, with attempts to size the
  cache collapsing back to `0` through the existing update hook,
- set `-DMYLITE_DISABLE_QUERY_CACHE=ON` in `tools/build-mariadb-minsize.sh`.

Do not change the full MariaDB server target behavior. The query-cache source
remains imported under `vendor/mariadb/server/` and can still be built by
profiles that do not set the MyLite option.

## Non-goals

- Do not remove MariaDB's subquery expression cache; it is a separate optimizer
  feature implemented by `sql_expression_cache.cc`.
- Do not remove SQL syntax for `SQL_CACHE` or `SQL_NO_CACHE`; with no query
  cache these are parsed but have no cache effect.
- Do not remove the `query_cache_*` sysvars in this slice. Hiding or rejecting
  them completely would be a broader compatibility decision.
- Do not change storage-engine handler APIs that mention query-cache table
  registration; disabled stubs make those paths unreachable.
- Do not add a public `libmylite` API change.

## Affected subsystems

- Embedded library build graph.
- SQL parse and SELECT execution cache hooks.
- DDL/DML invalidation hooks.
- Query-cache sysvars and `Qcache_*` status variables.
- Embedded startup option reporting through `have_query_cache`.
- `libmylite` open/close smoke coverage for unsupported minsize surfaces.

## DDL metadata routing impact

No direct DDL metadata routing change is expected. DDL already invalidates
query-cache entries through inherited MariaDB hooks; in the disabled profile
those hooks become no-ops and do not create, persist, or route metadata.

## Single-file and embedded-lifecycle impact

No `.mylite` file-format, catalog, lock, or recovery change is expected. The
slice removes one server-global in-memory cache from the minsize embedded
startup lifecycle. It should not introduce persistent files or companion files.

## Public API and file-format impact

No public `libmylite` API change. No `.mylite` file-format change.

## Binary-size impact

Expected archive savings are bounded by `sql_cache.cc.o`, `emb_qcache.cc.o`,
and any query-cache code/data retained in linked runtime artifacts. Because
section GC is already enabled, final value must be measured on stripped linked
artifacts rather than inferred from archive member sizes.

## License, trademark, and dependency impact

No new dependency or license impact. The change omits MariaDB-derived
query-cache implementation objects from the default minsize embedded profile
while leaving the imported source available in the tree.

## Test plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

Add open/close smoke assertions that:

- `SHOW VARIABLES LIKE 'have_query_cache'` reports `NO` in the minsize
  profile,
- `SHOW VARIABLES LIKE 'query_cache_size'` reports `0`,
- setting `query_cache_size` to a non-zero value leaves the effective value at
  `0`,
- ordinary `SELECT` execution still works after the query-cache hooks return
  misses/no-ops.

Measure:

```sh
stat -c "%s" build/mariadb-minsize/libmysqld/libmariadbd.a
llvm-ar t build/mariadb-minsize/libmysqld/libmariadbd.a | wc -l
stat -c "%s" build/mariadb-minsize/libmysqld/libmariadbd.a
stat -c "%s" build/mariadb-minsize/mylite/libmylite.a
stat -c "%s" build/mariadb-minsize/storage/mylite/libmylite_embedded.a
stat -c "%s" build/mariadb-minsize/mylite/mylite-open-close-smoke
cp build/mariadb-minsize/mylite/mylite-open-close-smoke \
  build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
llvm-strip --strip-unneeded \
  build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
stat -c "%s" build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
llvm-size build/mariadb-minsize/mylite/mylite-open-close-smoke
```

## Acceptance criteria

- The minsize build succeeds with `MYLITE_DISABLE_QUERY_CACHE=ON`.
- The embedded archive no longer contains `sql_cache.cc.o` or
  `emb_qcache.cc.o`.
- The linked smoke binary no longer contains the upstream
  `Query_cache::send_result_to_client`, `Query_cache::store_query`,
  `Query_cache::init_cache`, or `Querycache_stream` implementation symbols.
- `have_query_cache` reports `NO` in the minsize profile.
- Attempts to set a non-zero `query_cache_size` leave the effective cache size
  at `0`.
- Ordinary scalar query execution still succeeds.
- Build, open/close smoke, and compatibility harness pass.
- Size deltas are recorded in `docs/research/production-size-analysis.md`.

## Risks and unresolved questions

- MariaDB keeps query-cache sysvars even when the cache is unavailable. This
  slice keeps that compatibility shape; a later unsupported-surface slice can
  decide whether MyLite should reject cache sysvar writes more explicitly.
- The disabled stub must keep `Query_cache` object construction compatible with
  the upstream class layout in `sql_cache.h` because status variables read its
  public fields and sysvar update hooks call class methods.
