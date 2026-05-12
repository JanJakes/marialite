# General1400 Collation Size Profile

## Problem

The aggressive minsize profile already sets `MYLITE_DISABLE_UCA_COLLATIONS=ON`
and defaults SQL text to `utf8mb4_general_ci`, but the linked runtime still
retains MariaDB's compiled `general1400_as_ci` collations and Unicode casefold
tables. Current `build/mariadb-minsize-geometry-type` evidence:

| Retained symbol/data | Linked bytes |
| --- | ---: |
| `my_u1400_casefold_index` | 34,816 |
| `my_u1400tr_casefold_index` | 34,816 |
| `my_u520_casefold_index` | 34,816 |
| `my_u300_casefold_index` | 2,048 |
| `my_u300tr_casefold_index` | 2,048 |
| `u1400_casefold_page00` | 2,048 |
| `u1400tr_casefold_page00` | 2,048 |
| `u520_casefold_page00` | 2,048 |
| `u300_casefold_page00` | 2,048 |
| `u300tr_casefold_page00` | 2,048 |

`ctype-unidata.c.o` is currently 267,312 archive bytes. Not all of it is
removable because ordinary `general_ci` still uses Unicode weight data, but the
large UCA-style casefold indexes should become dead if the minsize profile no
longer registers or directly references `general1400_as_ci` collations.

## Source Findings

MariaDB source references are from imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `strings/ctype-utf8.c` defines
  `my_charset_utf8mb3_general1400_as_ci`,
  `my_charset_utf8mb4_general1400_as_ci`, and their collation handlers.
- `mysys/charset-def.c:init_compiled_charsets()` registers both
  `general1400_as_ci` collations even when `HAVE_UCA_COLLATIONS` is undefined.
- `strings/ctype-unidata.c` defines `my_casefold_unicode1400`,
  `my_casefold_unicode1400tr`, `my_casefold_unicode520`, and the large
  `my_u*casefold_index` tables retained by the linked smoke.
- `sql/mysqld.cc` initializes `system_charset_info` and `files_charset_info` to
  `my_charset_utf8mb3_general1400_as_ci`.
- `sql/lex_ident.h`, `sql/create_options.h`, `sql/sql_acl.cc`, and
  `sql/structs.h` use `my_charset_utf8mb3_general1400_as_ci` for internal
  case-insensitive identifier, option, role, user, and statistics comparisons.
- `include/m_ctype.h` only declares the `general1400_as_ci` charset objects;
  declarations can remain.

## Scope

Add a `MYLITE_DISABLE_GENERAL1400_COLLATIONS` aggressive minsize switch that:

- requires `MYLITE_DISABLE_UCA_COLLATIONS=ON`,
- skips registration of compiled `utf8mb3_general1400_as_ci` and
  `utf8mb4_general1400_as_ci` collations,
- routes retained internal minsize comparisons that directly reference
  `my_charset_utf8mb3_general1400_as_ci` to `my_charset_utf8mb3_general_ci`,
  and
- keeps ordinary `utf8mb3_general_ci`, `utf8mb4_general_ci`, binary collations,
  and existing UCA-disabled unknown-collation behavior.

## Non-Goals

This slice does not remove `ctype-unidata.c` entirely.

This slice does not remove ordinary Unicode-aware `general_ci` behavior.

This slice does not change the full MariaDB server target or non-minsize
profiles.

This slice does not claim MariaDB 11.8 collation compatibility. It extends the
already aggressive UCA-disabled profile.

## Compatibility Impact

High. Internal case-insensitive comparisons for identifiers, engine option
values, roles, users, hosts, and user-statistics keys will use
`utf8mb3_general_ci` instead of `utf8mb3_general1400_as_ci` in the aggressive
profile. ASCII and common BMP comparisons should behave the same, but newer
Unicode casefold behavior can diverge from MariaDB 11.8.

The public SQL default remains `utf8mb4_general_ci`, matching the existing
UCA-disabled minsize profile.

## Single-File And Embedded-Lifecycle Impact

No file-format, catalog, lock, recovery, or sidecar behavior should change.
The affected lifecycle point is charset/collation registration during embedded
startup.

## Public API Or File-Format Impact

No public `libmylite` API change and no `.mylite` file-format change.

## Binary-Size Impact

Expected linked savings are bounded by the retained casefold tables and
`general1400` handlers, roughly 0.1-0.2 MiB before link-layout effects.
Expected archive savings are smaller than the full `ctype-unidata.c.o` member
because ordinary `general_ci` data must remain, but section GC should remove
unreferenced per-symbol data from linked runtime artifacts.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-general1400-collations MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-general1400-collations MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-general1400-collations MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
git diff --check
bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh tools/run-compatibility-test-harness.sh
```

Measure:

- `libmysqld/libmariadbd.a` bytes and object count,
- stripped `mylite-open-close-smoke` bytes,
- absence of `my_charset_utf8mb[34]_general1400_as_ci` and
  `my_u*casefold_index` symbols from the linked smoke when possible, and
- retained `utf8mb4_general_ci` success and `utf8mb4_uca1400_ai_ci` rejection
  in the open/close smoke.

## Acceptance Criteria

- Default minsize build enables `MYLITE_DISABLE_GENERAL1400_COLLATIONS`.
- Open/close smoke and full compatibility harness pass.
- `@@collation_server` remains `utf8mb4_general_ci`.
- `utf8mb4_uca1400_ai_ci` remains rejected with MariaDB's unknown-collation
  diagnostic.
- Size deltas are recorded in `docs/research/production-size-analysis.md`.

## Risks

- This is a high-compatibility cut because identifier and account comparison
  semantics can change for newer Unicode characters.
- Some retained SQL path may assume `general1400_as_ci` is registered by name.
- If linked symbols remain, the attempted switch may be too entangled to keep.
