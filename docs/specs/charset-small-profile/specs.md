# Charset small profile

## Problem

`WITH_EXTRA_CHARSETS=none` is the largest remaining measured size reduction
that does not require removing whole SQL subsystems. Earlier measurements showed
roughly 2.5 MiB of additional savings, but the open-close smoke crashed during
embedded startup.

This slice diagnoses and attempts to make the profile viable for MyLite's
aggressive minsize build.

## Source findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

Relevant source paths:

- `vendor/mariadb/server/cmake/character_sets.cmake` always includes
  `latin1`, `utf8mb3`, and `utf8mb4` with the default charset, and adds more
  charsets only when `WITH_EXTRA_CHARSETS` is `complex` or `all`.
- The same CMake file always sets `HAVE_UCA_COLLATIONS=1`.
- `vendor/mariadb/server/mysys/charset-def.c:init_compiled_charsets()` adds
  compiled collations for enabled charsets, then calls
  `my_uca1400_collation_definitions_add()`,
  `mysql_uca0900_utf8mb4_collation_definitions_add()`, and
  `mysql_utf8mb4_0900_bin_add()`.
- `vendor/mariadb/server/strings/ctype-uca1400.c` generates UCA 1400
  collations by iterating every `MY_CS_ENCODING_*` value, including encodings
  for charsets disabled by `WITH_EXTRA_CHARSETS=none`.
- `my_uca1400_collation_definition_add()` calls
  `my_uca0520_builtin_collation_by_id(param.cs_id, param.nopad_flags)` and
  immediately dereferences the result through `src->cs_name`.

The failing backtrace from the current type-plugin-reduced tree was:

```text
my_uca1400_collation_build_name()
my_uca1400_collation_definitions_add()
init_compiled_charsets()
init_available_charsets()
my_charset_get_by_name()
get_charset_by_csname()
Charset_collation_map_st::insert_or_replace(...)
Charset_collation_map_st::from_text(...)
init_common_variables()
init_embedded_server()
mylite_open_v2()
main()
```

The inferred cause is that `my_uca0520_builtin_collation_by_id()` returns
`NULL` for a charset omitted from the small profile, and UCA 1400 registration
does not skip that absent base collation.

## Design

Make UCA 1400 registration skip collation IDs whose base compiled collation is
not present in the current build profile.

Then set MyLite's minsize profile to `WITH_EXTRA_CHARSETS=none`. Keep MariaDB's
default `utf8mb4_uca1400_ai_ci` collation if the patched profile passes,
because retaining the default collation is a smaller compatibility tradeoff
than switching to `utf8mb4_general_ci`.

## Non-goals

- Do not remove UCA 1400 support for `utf8mb3` or `utf8mb4`.
- Do not change the default charset or collation unless the default UCA 1400
  collation remains impossible after the null-base fix.
- Do not remove parser support for explicit charset names in this slice.
- Do not claim full MariaDB charset compatibility for the aggressive size
  profile.

## Compatibility impact

The size profile omits non-default compiled charsets such as `big5`,
`cp932`, `euckr`, `gbk`, `sjis`, `tis620`, `ucs2`, `utf16`, and `utf32`.
Statements that depend on those charsets should fail explicitly under inherited
MariaDB charset lookup behavior.

Keeping `utf8mb4_uca1400_ai_ci` as the default preserves MariaDB 11.8's default
collation for the main MyLite profile.

## Single-file and embedded-lifecycle impact

This slice should not affect `.mylite` file format, catalog layout, storage
pages, locking, recovery, or open/close ownership. The affected lifecycle point
is embedded runtime startup during charset initialization.

## Binary-size impact

The type-plugin-reduced charset-none experiment measured:

- `libmariadbd.a`: 37,356,940 bytes,
- `mylite-open-close-smoke`: 19,167,984 bytes,
- stripped `mylite-open-close-smoke`: 16,440,560 bytes.

Those numbers were from the crashing build. After the runtime fix, the measured
size is:

- `libmariadbd.a`: 37,356,932 bytes,
- archive objects: 494,
- `mylite-open-close-smoke`: 19,168,184 bytes,
- stripped `mylite-open-close-smoke`: 16,440,560 bytes,
- `size` total: 16,727,851 bytes.

Compared with the type-plugin-reduced profile, this saves 2,584,666 bytes from
`libmariadbd.a` and 2,495,240 bytes from the stripped linked open-close proxy.
Compared with the original production-size baseline, the combined type-plugin
and charset-small profile saves 6,048,500 bytes from `libmariadbd.a` and
2,891,344 bytes from the stripped linked open-close proxy.

## Test plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

Also inspect:

```sh
build/mariadb-minsize/mylite-build-report.txt
```

The report should show `WITH_EXTRA_CHARSETS=none`, and current smokes should
pass without changing the default collation away from MariaDB's UCA 1400
default.

## Verification

Run on 2026-05-12:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

All passed. `build/mariadb-minsize/include/config.h` still defines
`MYSQL_DEFAULT_COLLATION_NAME` as `utf8mb4_uca1400_ai_ci`.

## Acceptance criteria

- The charset-none minsize profile builds and links.
- The open-close smoke no longer segfaults during charset initialization.
- Current MyLite compatibility harness smokes pass.
- Size deltas are recorded in this spec and in production-size analysis.
- The default collation remains `utf8mb4_uca1400_ai_ci` unless explicitly
  documented otherwise.

## Risks

- UCA 0900 alias registration may have similar assumptions, though current
  evidence points at UCA 1400.
- Some MariaDB SQL paths may expect omitted charsets to exist even if startup
  succeeds.
- This is an aggressive size profile; omitted charset compatibility must be
  treated as a deliberate product decision later.
