# Small built-in plugin profile

## Problem

The current minsize profile still includes a few small built-in plugins that
are server-observability or optional SQL surfaces rather than core MyLite file
ownership behavior. The savings are smaller than the charset or parser slices,
but the changes are isolated enough to test as separate size attempts.

## Source findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

Relevant source paths:

- `vendor/mariadb/server/plugin/userstat/CMakeLists.txt` registers `USERSTAT`
  as `MANDATORY`.
- `vendor/mariadb/server/plugin/user_variables/CMakeLists.txt` registers
  `user_variables` as `DEFAULT RECOMPILE_FOR_EMBEDDED`.
- `vendor/mariadb/server/storage/sequence/CMakeLists.txt` registers the
  `sequence` storage engine as `DEFAULT`.
- `vendor/mariadb/server/sql/CMakeLists.txt` registers `thread_pool_info` as
  `DEFAULT STATIC_ONLY NOT_EMBEDDED`.

Current stripped archive member sizes:

- `userstat.cc.o`: 28,512 bytes,
- `user_variables.cc.o`: 8,400 bytes,
- `sequence.cc.o`: 85,448 bytes.

The generated built-in plugin list still names `userstat`, `user_variables`,
`sequence`, and `thread_pool_info`.

## Design

Make `USERSTAT` non-mandatory, then disable these optional built-ins in the
MyLite minsize CMake profile:

- `PLUGIN_USERSTAT=NO`,
- `PLUGIN_USER_VARIABLES=NO`,
- `PLUGIN_SEQUENCE=NO`,
- `PLUGIN_THREAD_POOL_INFO=NO`.

This slice intentionally does not remove MariaDB's mandatory `sql_sequence`
storage engine or the core SQL sequence source files.

## Non-goals

- Do not remove sequence SQL syntax or mandatory `sql_sequence` support in this
  slice.
- Do not remove `online_alter_log`, `heap`, `csv`, `myisam`, or `myisammrg`.
- Do not make broad information-schema changes beyond omitted plugin surfaces.

## Compatibility impact

The minsize profile will omit the `USER_STATISTICS`, `CLIENT_STATISTICS`,
`INDEX_STATISTICS`, `TABLE_STATISTICS`, and `USER_VARIABLES` plugin-provided
information-schema surfaces if they are otherwise available. The `SEQUENCE`
storage engine plugin is also omitted, but SQL sequence support is left intact
for this attempt.

## Single-file and embedded-lifecycle impact

This slice should not affect `.mylite` file format, catalog layout, storage
pages, locking, recovery, or open/close ownership.

## Binary-size impact

The direct stripped archive opportunity is roughly 122 KiB from the three
present plugin objects. Final linked savings may be smaller.

After implementation, the measured size is:

- `libmariadbd.a`: 34,474,690 bytes,
- archive objects: 491,
- `mylite-open-close-smoke`: 18,259,560 bytes,
- stripped `mylite-open-close-smoke`: 15,849,720 bytes,
- `size` total: 16,084,955 bytes.

Compared with the static-archive-strip profile, this saves 131,980 bytes from
`libmariadbd.a` and 1,016 bytes from the stripped linked open-close proxy.
Compared with the original production-size baseline, the combined type-plugin,
charset-small, Oracle-parser, static-archive-strip, and small-plugin profile
saves 8,930,742 bytes from `libmariadbd.a` and 3,482,184 bytes from the
stripped linked open-close proxy.

## Test plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

Also inspect the generated built-in plugin list in
`build/mariadb-minsize/sql/sql_builtin.cc`.

## Verification

Run on 2026-05-12:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

All passed. The generated built-in plugin list no longer includes `sequence`,
`thread_pool_info`, `user_variables`, or `userstat`. The archive still contains
`ha_sequence.cc.o` and `sql_sequence.cc.o` for mandatory `sql_sequence`
support.

## Acceptance criteria

- The minsize profile builds and links.
- Current MyLite open/close and compatibility smokes pass.
- The built-in plugin list no longer includes the disabled optional plugins.
- Size deltas are recorded in this spec and in production-size analysis.

## Risks

- Some inherited information-schema queries may expect the omitted plugin
  surfaces.
- `PLUGIN_SEQUENCE=NO` may not be enough to remove all sequence-related code
  because SQL sequence support is still compiled into the SQL layer.
