# Legacy MySQL 5.0 Collation Size Profile

## Problem Statement

The aggressive MyLite minsize profile retains MariaDB's legacy
`utf8mb3_general_mysql500_ci` collation even though the profile already omits
extra character sets, UCA collations, and `general1400` collations. The linked
open-close smoke roots the MySQL 5.0 collation's weight table, casefold
descriptor, collation handler, and charset descriptor.

This is a compatibility-only collation for older MySQL behavior. It is not the
default MyLite collation and is not required for ordinary `utf8mb4_general_ci`
execution.

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/mysys/charset-def.c` registers
  `my_charset_utf8mb3_general_mysql500_ci` when `HAVE_CHARSET_utf8mb3` is set.
  It also registers `my_charset_ucs2_general_mysql500_ci` when `ucs2` is
  compiled, although the current minsize profile does not retain `ucs2`.
- `vendor/mariadb/server/strings/ctype-utf8.c` defines the
  `utf8mb3_general_mysql500_ci` weight helpers, `strcoll.inl` instantiations,
  collation handler, and charset descriptor.
- `vendor/mariadb/server/strings/ctype-unidata.c` includes
  `ctype-unicode300-general_mysql500_ci.h` and defines
  `my_casefold_mysql500`, which points at `weight_general_mysql500_ci_index`.
- `vendor/mariadb/server/strings/ctype-unidata.h` exposes the MySQL 5.0
  weight table and helper used by the retained implementation.
- `vendor/mariadb/server/sql/sql_string.h` has a narrow MDEV-30746 path for
  `ucs2_general_mysql500_ci` collation id 159. It is unaffected when `ucs2`
  and the MySQL 5.0 collations are absent from the aggressive profile.
- The current linked smoke keeps:
  - `weight_general_mysql500_ci_index` at 2,048 bytes,
  - `weight_general_mysql500_ci_page00` at 512 bytes,
  - `my_charset_utf8mb3_general_mysql500_ci` at 200 bytes, and
  - `my_casefold_mysql500` at 24 bytes.

## Official Documentation References

- [MariaDB Supported Character Sets and Collations](https://mariadb.com/docs/server/reference/data-types/string-data-types/character-sets/supported-character-sets-and-collations)
  lists `utf8mb3_general_mysql500_ci` as a supported `utf8mb3` collation with
  id 223.
- [MariaDB Setting Character Sets and Collations](https://mariadb.com/docs/server/reference/data-types/string-data-types/character-sets/setting-character-sets-and-collations)
  documents explicit charset and collation selection at server, database,
  table, column, and connection scope.

## Scope

This slice may:

- add `MYLITE_DISABLE_MYSQL500_COLLATIONS`,
- enable it in the aggressive minsize build,
- stop registering the MySQL 5.0 collations when the profile is enabled,
- omit the `utf8mb3_general_mysql500_ci` implementation and MySQL 5.0 weight
  table in that profile, and
- add smoke coverage that the collation is absent and fails explicitly.

## Non-Goals

This slice does not:

- remove `utf8mb3_general_ci`, `utf8mb3_bin`, or utf8mb3 no-pad collations,
- change the default minsize collation,
- remove retained utf8mb4 general/bin collations,
- alter ordinary `general_ci` sort semantics, or
- change the public `libmylite` API or `.mylite` file format.

## Proposed Design

Add a top-level `MYLITE_DISABLE_MYSQL500_COLLATIONS` CMake option. Require
`MYLITE_DISABLE_UCA_COLLATIONS=ON` when it is enabled, keeping this as a narrow
aggressive-profile compatibility tradeoff rather than a general MariaDB build
mode.

When enabled:

- `charset-def.c` skips `my_charset_utf8mb3_general_mysql500_ci` and
  `my_charset_ucs2_general_mysql500_ci` registration.
- `ctype-utf8.c` omits the MySQL 5.0 utf8mb3 weight helpers, collation
  handler, and charset descriptor.
- `ctype-unidata.c` omits the MySQL 5.0 weight table include and
  `my_casefold_mysql500`.
- `ctype-unidata.h` and `m_ctype.h` expose those declarations only when the
  profile is disabled.

The open-close smoke extends the collation profile with:

```sql
SELECT COUNT(*) FROM information_schema.COLLATIONS
WHERE COLLATION_NAME='utf8mb3_general_mysql500_ci';
SELECT _utf8mb3'a' COLLATE utf8mb3_general_mysql500_ci;
```

The count must be `0`, and direct use must fail with `ER_UNKNOWN_COLLATION`.

## Affected Subsystems

- Top-level MariaDB CMake configuration.
- Aggressive minsize build script.
- MySys compiled collation registration.
- Strings Unicode and utf8mb3 collation implementation.
- Open-close collation profile smoke.
- Production size analysis.

## Single-File And Embedded-Lifecycle Impact

No file ownership, recovery, locking, catalog, sidecar, or embedded lifecycle
behavior changes.

## Public API Or File-Format Impact

No public `libmylite` API change and no `.mylite` file-format change.

SQL compatibility impact: the aggressive profile no longer accepts explicit
`utf8mb3_general_mysql500_ci` or `ucs2_general_mysql500_ci` metadata. This is a
deliberate compatibility tradeoff for the smallest profile.

## Binary-Size Impact

Expected direct linked-symbol removals are small, roughly 2.8 KiB of visible
table/descriptor data plus associated collation helper code. The actual archive
and stripped linked deltas must be measured because `strcoll.inl` helper code,
ICF, and section GC can change the result.

Implemented measurements against the preceding `sql-digest-size-profile`
baseline:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 25,523,714 | 25,515,034 | -8,680 |
| unstripped `mylite-open-close-smoke` | 6,620,632 | 6,616,792 | -3,840 |
| stripped `mylite-open-close-smoke` | 4,657,704 | 4,654,432 | -3,272 |

`llvm-size` total for the linked open-close smoke changed from 4,880,125 to
4,877,861 bytes (-2,264). Visible MySQL 5.0 collation symbols are absent from
the linked smoke.

## License, Trademark, And Dependency Impact

No new dependency or license impact. This is a GPL-2.0-only MariaDB-derived
build-profile change.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-mysql500-collation \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-mysql500-collation \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-mysql500-collation \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-mysql500-collation \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
llvm-nm --demangle --defined-only \
  build/mariadb-minsize-no-mysql500-collation/mylite/mylite-open-close-smoke | \
  rg 'mysql500|weight_general_mysql500|my_casefold_mysql500'
```

Measure:

- archive bytes,
- unstripped and stripped linked open-close smoke bytes,
- `llvm-size` section changes, and
- absence of MySQL 5.0 collation symbols in the linked smoke.

## Acceptance Criteria

- Passed: the minsize build succeeds with
  `MYLITE_DISABLE_MYSQL500_COLLATIONS=ON`.
- Passed: the open/close, storage-engine, and compatibility smokes pass.
- Passed: `INFORMATION_SCHEMA.COLLATIONS` no longer reports
  `utf8mb3_general_mysql500_ci`.
- Passed: direct `utf8mb3_general_mysql500_ci` use fails with
  `ER_UNKNOWN_COLLATION`.
- Passed: the linked smoke no longer contains `weight_general_mysql500_ci_index`,
  `weight_general_mysql500_ci_page00`,
  `my_charset_utf8mb3_general_mysql500_ci`, or `my_casefold_mysql500`.
- Passed: size results are recorded here and in
  `docs/research/production-size-analysis.md`.

## Verification

Passed:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-mysql500-collation \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-mysql500-collation \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-mysql500-collation \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-mysql500-collation \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

The open-close smoke report includes:

```text
exec_mysql500_collation_rows=0
exec_mysql500_collation_message=Unknown collation: 'utf8mb3_general_mysql500_ci'
```

## Risks And Unresolved Questions

- Importing external MariaDB/MySQL metadata that explicitly names the MySQL 5.0
  collations will fail in the aggressive profile.
- The savings are expected to be small. This slice should be kept only if the
  measured bundle-size delta is real and the compatibility tradeoff remains
  acceptable for the most aggressive profile.
