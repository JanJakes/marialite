# Locale Minsize Profile

## Problem Statement

The aggressive embedded minsize profile still compiles MariaDB's generated
`sql_locale.cc` table with every built-in `lc_time_names` / `lc_messages`
locale. The current archive member is 295,856 bytes and the linked smoke
binary retains the full `my_locales` table plus locale objects for non-default
locales such as `de_DE`, `fr_FR`, `ar_AE`, and `zh_CN`.

MyLite's most aggressive embedded profile can reasonably keep the default
`en_US` locale while rejecting other locale names explicitly. This is
SQL-visible, so it should be a deliberate compatibility tradeoff rather than
accidental dead-code removal.

Current baseline after `select-procedure-runtime-size-profile`:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 29,500,552 |
| `sql_locale.cc.o` object | 295,856 |
| stripped `mylite-open-close-smoke` | 5,640,696 |

## Source Findings

- Imported MariaDB base: `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- MariaDB documents server locale support as controlling date/time names
  through `lc_time_names` and error-message language through `lc_messages`:
  <https://mariadb.com/kb/en/server-locale/>.
- `vendor/mariadb/server/sql/sql_locale.cc` is generated from
  `my_locale.pl`. It defines all compiled `MY_LOCALE` objects, the
  `my_locales[]` lookup table, `my_locale_by_number()`,
  `my_locale_by_name()`, and `cleanup_errmsgs()`.
- `vendor/mariadb/server/sql/sql_locale.h` exports only `my_locale_en_US`,
  `my_locales`, the default locale pointers, and the lookup/cleanup
  functions. Other locale objects are not declared in the public header.
- `vendor/mariadb/server/sql/mysqld.cc` initializes both `lc_messages` and
  `lc_time_names_name` to `en_US` before startup lookup.
- `vendor/mariadb/server/sql/sys_vars.cc` validates `SET lc_messages` and
  `SET lc_time_names` through `my_locale_by_number()` or
  `my_locale_by_name()`.
- `vendor/mariadb/server/sql/item_timefunc.cc` uses
  `THD::variables.lc_time_names` for `DAYNAME()`, `MONTHNAME()`, and
  `DATE_FORMAT()` locale output. Some date/time and string functions also
  call `Item::locale_from_val_str()` for an explicit locale argument; unknown
  names warn and fall back to `my_locale_en_US`.
- Symbol inspection of the linked open/close smoke shows the final binary
  retains 117 `my_locale_*` symbols from the full generated table.

## Scope

Add a minsize option that removes the full generated locale table from the
embedded library and replaces it with an `en_US`-only MyLite locale table.

The option will:

- remove `../sql/sql_locale.cc` from `SQL_EMBEDDED_SOURCES`;
- add a MyLite-owned `mylite_locale_stub.cc`;
- keep `my_locale_en_US`, `my_locales[]`, `my_locale_by_number()`,
  `my_locale_by_name()`, and `cleanup_errmsgs()` ABI-compatible for retained
  MariaDB code;
- allow default `en_US` date/time formatting and explicit `SET lc_time_names`
  / `SET lc_messages` to `en_US`; and
- reject other locale names through MariaDB's existing unknown-locale
  diagnostics.

## Non-Goals

- Do not remove date/time functions.
- Do not remove `lc_time_names` or `lc_messages` system variables.
- Do not change non-embedded MariaDB behavior.
- Do not edit the generated upstream `sql_locale.cc` file.
- Do not remove localizable error-message loading for the retained `en_US`
  locale.

## Proposed Design

Add `MYLITE_DISABLE_EXTRA_LOCALES` as a top-level MariaDB CMake option,
remove `../sql/sql_locale.cc` from
`vendor/mariadb/server/libmysqld/CMakeLists.txt` when it is enabled, and
enable the option in `tools/build-mariadb-minsize.sh`.

Create `vendor/mariadb/server/libmysqld/mylite_locale_stub.cc`. The stub keeps
the copied MariaDB `en_US` month/day tables and `MY_LOCALE` object, a compact
`global_errmsgs[]` with only the `english` entry, and a two-entry
`my_locales[]` array containing `&my_locale_en_US` and `NULL`.

`my_locale_by_number(0)` returns `&my_locale_en_US`; all other locale numbers
return `NULL`. `my_locale_by_name()` returns `&my_locale_en_US` only for
`en_US`. This preserves the default startup path and lets existing sysvar
validation produce `ER_UNKNOWN_LOCALE` for removed locales.

## Affected Subsystems

- Embedded minsize SQL source list.
- Locale lookup for `lc_time_names` and `lc_messages`.
- Date/time display names for `DAYNAME()`, `MONTHNAME()`, and
  `DATE_FORMAT()`.
- Explicit-locale SQL functions that use `Item::locale_from_val_str()`.
- Binary-size documentation and open/close smoke coverage.

## DDL Metadata Routing Impact

None. Locale lookup affects expression evaluation and session/global system
variables, not table metadata.

## Single-File And Embedded-Lifecycle Impact

The slice removes compiled static locale data. It does not add files,
sidecars, locks, or lifecycle state. Error-message file loading for the
retained `english` locale remains inherited MariaDB behavior.

## Public API Or File-Format Impact

No public `libmylite` API or `.mylite` file-format change.

SQL compatibility impact: the aggressive minsize profile only supports
`en_US` for `lc_time_names`, `lc_messages`, and explicit SQL locale arguments.
Non-default locales return MariaDB's unknown-locale diagnostics or the
existing warning-plus-`en_US` fallback path depending on the caller.

## Binary-Size Impact

Expected archive savings are close to the current 295,856-byte
`sql_locale.cc.o` member minus a small `en_US` replacement stub. Linked
runtime savings should be meaningful because the full locale object is live in
the current linked smoke.

## License, Trademark, And Dependency Impact

No new dependency or license change. The replacement stub copies GPL-covered
MariaDB locale data for `en_US` into a MyLite-owned embedded profile source
file under the existing GPL-2.0-only project terms.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-locale-en-us \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-locale-en-us \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-locale-en-us \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-locale-en-us \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Add open/close smoke checks that verify:

- `@@lc_time_names` defaults to `en_US`;
- `DATE_FORMAT()` still returns English month/day names;
- `SET lc_time_names='en_US'` succeeds;
- `SET lc_time_names='de_DE'` returns `ER_UNKNOWN_LOCALE`; and
- explicit `FORMAT(..., 'en_US')` still works while a removed explicit locale
  uses MariaDB's existing warning/fallback behavior.

Measure:

- archive bytes and object count;
- unstripped and stripped linked smoke bytes;
- absence of `sql_locale.cc.o` in `libmariadbd.a`;
- presence and size of the replacement stub; and
- retained locale symbols in the linked smoke.

## Acceptance Criteria

- The minsize build completes.
- Embedded bootstrap, open/close smoke, and compatibility harness pass.
- Default `en_US` locale behavior still works.
- Non-default locale assignment fails explicitly.
- The embedded archive no longer contains `sql_locale.cc.o`.
- Size results are recorded here and in
  `docs/research/production-size-analysis.md`.

## Implementation Results

Implemented in the aggressive minsize profile with
`MYLITE_DISABLE_EXTRA_LOCALES=ON`.

Final measurement from
`MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-locale-en-us`:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 29,210,614 |
| `mylite/libmylite.a` | 122,792 |
| `storage/mylite/libmylite_embedded.a` | 388,440 |
| `mylite/mylite-open-close-smoke` | 7,769,040 |
| stripped `mylite-open-close-smoke` | 5,582,144 |
| `mylite/mylite-compatibility-smoke` | 7,642,208 |
| stripped `mylite-compatibility-smoke` | 5,475,768 |
| `mylite_locale_stub.cc.o` | 8,152 |

Delta from the `select-procedure-runtime-size-profile` baseline:

- `libmariadbd.a`: -289,938 bytes.
- stripped `mylite-open-close-smoke`: -58,552 bytes.
- stripped `mylite-compatibility-smoke`: -59,720 bytes.

The archive no longer contains `sql_locale.cc.o`. The linked open/close smoke
retains only four locale-related symbols from the checked symbol pattern:
`my_locale_by_name()`, `my_locale_by_number()`,
`my_default_lc_time_names`, and `my_locale_en_US`.

Open/close smoke coverage records:

```text
exec_locale_profile_rows=en_US:January:Tuesday:1,234.50
exec_locale_removed_message=Unknown locale: 'de_DE'
```

Verification passed:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-locale-en-us \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-locale-en-us \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-locale-en-us \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-locale-en-us \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh \
  tools/run-embedded-bootstrap-smoke.sh \
  tools/run-libmylite-open-close-smoke.sh \
  tools/run-compatibility-test-harness.sh
git diff --check
```

## Risks And Unresolved Questions

- This is a high SQL-compatibility tradeoff for applications that rely on
  localized date/time names or localized error-message files.
- `Item::locale_from_val_str()` currently warns and falls back to `en_US` for
  unknown explicit locale arguments. This slice keeps that inherited behavior
  instead of turning explicit-locale SQL functions into hard errors.
- The upstream locale file is generated. Replacing it in the embedded source
  list is lower-maintenance than editing generated upstream data, but future
  MariaDB locale-lookup signature changes must be caught by the minsize build.
