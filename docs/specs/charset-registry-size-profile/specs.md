# Charset Registry Size Profile

## Problem Statement

The aggressive MyLite minsize profile compiles only a small retained charset
and collation set, but still allocates MariaDB's default 4096-entry
`all_charsets` pointer registry. The linked open-close smoke currently keeps
`all_charsets` as a 32,768-byte `.bss` symbol even though the retained compiled
collations are latin1, utf8mb3, utf8mb4, binary, and filename variants with
bounded numeric collation ids. Retained no-pad collations use
`MY_NOPAD_ID(x)`, so the practical lower bound is above 1024 rather than 256.

This is a fixed process-global registry cost, not a storage or SQL execution
feature.

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/include/my_sys.h` defines
  `MY_ALL_CHARSETS_SIZE` as 4096 and declares
  `all_charsets[MY_ALL_CHARSETS_SIZE]`.
- `vendor/mariadb/server/mysys/charset.c` defines the global
  `CHARSET_INFO *all_charsets[MY_ALL_CHARSETS_SIZE]` and indexes it by
  collation id in `add_compiled_collation()`,
  `add_compiled_extra_collation()`, `get_internal_charset()`,
  `my_collation_is_known_id()`, and related lookup helpers.
- `vendor/mariadb/server/cmake/character_sets.cmake` already constrains the
  aggressive profile through `WITH_EXTRA_CHARSETS=none`,
  `MYLITE_DISABLE_UCA_COLLATIONS=ON`, and
  `DEFAULT_COLLATION=utf8mb4_general_ci`.
- `vendor/mariadb/server/mysys/charset-def.c` registers the retained built-in
  binary, filename, latin1, utf8mb3, and utf8mb4 general/bin/no-pad collations
  in this profile. The higher UCA and `general1400` registrations are
  profile-gated out.
- `vendor/mariadb/server/sql/sql_show.cc` exposes retained charset and
  collation metadata through `SHOW CHARACTER SET`, `SHOW COLLATION`, and
  `INFORMATION_SCHEMA.COLLATIONS`.
- The current linked smoke binary defines `all_charsets` as 32,768 bytes and
  does not retain the optional `my_collation_statistics` array.

## Official Documentation References

- [MariaDB Information Schema COLLATIONS Table](https://mariadb.com/docs/server/reference/system-tables/information-schema/information-schema-tables/information-schema-collations-table)
  documents the supported-collation metadata surface and notes its relationship
  to `SHOW COLLATION`.
- [MariaDB Setting Character Sets and Collations](https://mariadb.com/docs/server/reference/data-types/string-data-types/character-sets/setting-character-sets-and-collations)
  documents charset and collation selection at server, database, table, and
  column levels.

## Scope

This slice may:

- add a top-level `MYLITE_CHARSET_REGISTRY_SIZE` CMake cache value,
- define that value for the aggressive minsize profile,
- make `MY_ALL_CHARSETS_SIZE` derive from that value,
- validate that reduced registry sizes are used only with the UCA-disabled
  profile, and
- add smoke coverage that retained collations all fit inside the reduced
  registry.

## Non-Goals

This slice does not:

- remove additional charset or collation implementations,
- change the default collation beyond the existing
  `utf8mb4_general_ci` aggressive profile,
- change string comparison semantics for retained collations,
- change the public `libmylite` API, or
- change `.mylite` file format.

## Proposed Design

Add `MYLITE_CHARSET_REGISTRY_SIZE` as a numeric top-level CMake cache value
with default `4096`. When it differs from 4096, CMake validates that:

- the value is numeric,
- the value is at least 1152, and
- `MYLITE_DISABLE_UCA_COLLATIONS=ON` is also set.

The aggressive minsize script defaults to the upstream registry size, but
accepts `MYLITE_CHARSET_REGISTRY_SIZE=1152` as an opt-in measured profile.

`include/my_sys.h` then derives `MY_ALL_CHARSETS_SIZE` from
`MYLITE_CHARSET_REGISTRY_SIZE` when provided. This keeps ordinary MariaDB-size
builds unchanged while allowing the opt-in profile to allocate only a
1152-entry pointer registry. Reduced builds also define
`MYLITE_REDUCED_CHARSET_REGISTRY`, so tests can distinguish the opt-in profile
from the default 4096-entry fallback. The profile is not enabled by default
because it reduces loaded `.bss` size rather than stripped bundle bytes.

The minsize smoke wrappers forward `MYLITE_CHARSET_REGISTRY_SIZE` into their
Docker runs. Without that, verification commands can silently reconfigure the
build directory back to the default registry size before testing.

The open-close smoke extends the existing collation profile with:

```sql
SELECT COUNT(*) FROM information_schema.COLLATIONS WHERE ID >= 1152;
```

The count must be `0` under the aggressive profile. Retained UCA and
`general1400` rejection checks remain unchanged.

## Affected Subsystems

- Top-level MariaDB CMake configuration.
- MySys charset registry globals.
- Aggressive minsize build script.
- Open-close collation profile smoke.
- Production size analysis.

## Single-File And Embedded-Lifecycle Impact

No file, recovery, locking, catalog, or sidecar behavior changes. This reduces
process-global charset registry capacity in the aggressive embedded profile.

## Public API Or File-Format Impact

No public `libmylite` API change and no `.mylite` file-format change.

SQL compatibility impact is limited to collation ids outside the reduced
registry. Those collations are already outside the current aggressive profile
because UCA and extra charset sets are omitted.

## Binary-Size Impact

Expected linked `.bss` savings from `all_charsets` are:

| Registry size | `all_charsets` bytes | Expected direct saving |
| ---: | ---: | ---: |
| 4096 | 32,768 | 0 |
| 1152 | 9,216 | -23,552 |

Implemented measurements against the preceding `sql-digest-size-profile`
baseline:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 25,523,714 | 25,523,738 | +24 |
| unstripped `mylite-open-close-smoke` | 6,620,632 | 6,621,664 | +1,032 |
| stripped `mylite-open-close-smoke` | 4,657,704 | 4,658,664 | +960 |

`llvm-size` total for the linked open-close smoke changed from 4,880,125 to
4,832,945 bytes (-47,180). The linked `.bss` section dropped from 223,625 to
176,521 bytes (-47,104), and the `all_charsets` symbol dropped from 32,768 to
9,216 bytes. This is a loaded-memory win, not a stripped bundle-size win.

## License, Trademark, And Dependency Impact

No new dependency or license impact. This is a GPL-2.0-only MariaDB-derived
build-profile change.

## Test And Verification Plan

Run:

```sh
MYLITE_CHARSET_REGISTRY_SIZE=1152 \
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-small-charset-registry \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_CHARSET_REGISTRY_SIZE=1152 \
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-small-charset-registry \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_CHARSET_REGISTRY_SIZE=1152 \
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-small-charset-registry \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_CHARSET_REGISTRY_SIZE=1152 \
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-small-charset-registry \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh \
  tools/run-libmylite-open-close-smoke.sh \
  tools/run-storage-engine-smoke.sh \
  tools/run-compatibility-test-harness.sh
git diff --check
llvm-nm --demangle --print-size --size-sort --reverse-sort \
  build/mariadb-minsize-small-charset-registry/mylite/mylite-open-close-smoke | \
  rg 'all_charsets'
```

Measure:

- archive bytes,
- unstripped and stripped linked open-close smoke bytes,
- `llvm-size` section changes, and
- `all_charsets` symbol size.

## Acceptance Criteria

- Passed: the opt-in minsize build succeeds with
  `MYLITE_CHARSET_REGISTRY_SIZE=1152`.
- Passed: the open/close, storage-engine, and compatibility smokes pass.
- Passed: `SHOW COLLATION` / `INFORMATION_SCHEMA.COLLATIONS` retained rows have ids
  below the reduced registry size.
- Passed: `all_charsets` drops from 32,768 bytes to 9,216 bytes in the linked
  smoke.
- Passed: size results are recorded here and in
  `docs/research/production-size-analysis.md`.

## Verification

Passed:

```sh
MYLITE_CHARSET_REGISTRY_SIZE=1152 \
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-small-charset-registry \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_CHARSET_REGISTRY_SIZE=1152 \
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-small-charset-registry \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_CHARSET_REGISTRY_SIZE=1152 \
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-small-charset-registry \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_CHARSET_REGISTRY_SIZE=1152 \
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-small-charset-registry \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

The open-close smoke report includes:

```text
exec_charset_registry_rows=registry_size=1152,collations_ge_size=0
```

## Risks And Unresolved Questions

- A reduced registry size is only safe for the aggressive UCA-disabled profile.
  Broad MariaDB-compatible builds need the upstream 4096-entry registry.
- Imported table metadata that references high omitted collation ids will fail
  as unknown collation metadata, which is coherent with the current
  UCA-disabled aggressive profile but should not be used as a general import
  compatibility promise.
