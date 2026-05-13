# Hidden Visibility Size Profile

## Problem

The aggressive minsize profile still builds most MariaDB and MyLite objects with
default ELF symbol visibility. MyLite's public ABI is the `libmylite` C API, and
the project standards already require first-party library symbols to be hidden by
default with explicit exports for public functions. The minsize build should use
hidden default visibility so ordinary internal symbols do not carry public
export metadata into runtime-style linked artifacts.

## Source Base

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Local baseline before this slice:
  `006106647ad558397eb9bd71932635ae3731d450`.

## Source Findings

- [tools/build-mariadb-minsize.sh](../../../tools/build-mariadb-minsize.sh)
  sets `-Oz`, lld RELR, section GC, ICF, `DISABLE_SHARED=ON`, and
  `WITHOUT_DYNAMIC_PLUGINS=ON`, but it does not set CMake's C or C++ visibility
  presets.
- [docs/architecture/engineering-standards.md](../../architecture/engineering-standards.md)
  states that first-party symbols are hidden by default and public ABI functions
  use an explicit API macro.
- [vendor/mariadb/server/mylite/include/mylite.h](../../../vendor/mariadb/server/mylite/include/mylite.h)
  defines `MYLITE_API` as `__attribute__((visibility("default")))` for GCC and
  annotates the public `mylite_*` declarations with that macro.
- [vendor/mariadb/server/CMakeLists.txt](../../../vendor/mariadb/server/CMakeLists.txt)
  makes `DISABLE_SHARED` imply `WITHOUT_DYNAMIC_PLUGINS`, so the default minsize
  profile is already static-plugin-only and does not rely on runtime plugin
  symbol lookup.
- MariaDB's `MYSQL_PLUGIN_IMPORT` macro is blank on non-Windows platforms in
  [vendor/mariadb/server/include/my_global.h](../../../vendor/mariadb/server/include/my_global.h).
  That means hidden default visibility does not preserve MariaDB internal
  globals as public ELF exports unless they are explicitly annotated elsewhere.

## Design

- Add these CMake cache entries to `tools/build-mariadb-minsize.sh`:
  - `CMAKE_C_VISIBILITY_PRESET=hidden`
  - `CMAKE_CXX_VISIBILITY_PRESET=hidden`
  - `CMAKE_VISIBILITY_INLINES_HIDDEN=ON`
- Keep `MYLITE_API` as the public export boundary for the MyLite C API.
- Do not add source-level visibility annotations to MariaDB internals in this
  slice.
- Record the visibility cache entries in the generated minsize build report.

## Non-Goals

- Do not define a final shared `libmylite.so` export map in this slice.
- Do not expose MariaDB's C API or server-internal symbols as a public ABI.
- Do not change SQL feature support, storage behavior, catalog behavior, or
  file-format behavior.

## Embedded and Single-File Impact

This slice has no direct storage, file-format, temporary-file, catalog, or
embedded lifecycle impact. It affects object visibility and linked artifact
metadata only.

## Binary-Size Impact

The official verification build on top of `time-zone-table-size-profile`
showed this reduction:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 29,147,460 | 29,117,602 | -29,858 |
| `mylite/mylite-open-close-smoke` | 7,748,552 | 7,705,056 | -43,496 |
| stripped `mylite-open-close-smoke` | 5,564,416 | 5,532,056 | -32,360 |
| stripped `mylite-compatibility-smoke` | 5,455,224 | 5,422,808 | -32,416 |

The generated build report records `CMAKE_C_VISIBILITY_PRESET=hidden`,
`CMAKE_CXX_VISIBILITY_PRESET=hidden`, and
`CMAKE_VISIBILITY_INLINES_HIDDEN=ON`.

## Compatibility and Packaging Impact

The current public ABI remains `mylite_*` through `MYLITE_API`. Future packaging
that wants a shared library exporting MariaDB C API symbols, server globals, or
dynamic plugin service symbols must add an explicit export policy rather than
relying on default visibility.

## Test Plan

- Build the minsize profile through `tools/build-mariadb-minsize.sh`.
- Run `tools/run-embedded-bootstrap-smoke.sh`.
- Run `tools/run-libmylite-open-close-smoke.sh`.
- Run `tools/run-compatibility-test-harness.sh`.
- Run shell syntax and whitespace checks:
  - `bash -n tools/build-mariadb-minsize.sh`
  - `git diff --check`

## Acceptance Criteria

- The normal minsize script configures hidden default C and C++ visibility.
- The generated build report records the visibility cache entries.
- Public MyLite C API declarations remain explicitly default-visible.
- The embedded, open/close, and compatibility smokes pass.
- Production size analysis records the measured size impact and remaining risk.

## Risks

- Hidden visibility is correct for the current static embedded profile, but it
  is not a substitute for a final shared-library export map.
- If a future dynamic plugin profile is reintroduced, plugin-visible MariaDB
  service symbols need a separate audit.
