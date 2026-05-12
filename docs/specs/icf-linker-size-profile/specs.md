# ICF Linker Size Profile

## Problem

The current minsize profile still carries duplicate linked code in runtime-style
artifacts after section garbage collection and RELR relocation packing. LLVM
`ld.lld` supports identical code folding (ICF), which can merge identical
function bodies at link time.

This slice tests whether ICF can reduce MyLite's stripped linked runtime
artifact without changing the embedded archive or removing more SQL behavior.

## Source Findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

Relevant local source paths:

- `tools/build-mariadb-minsize.sh` owns the reproducible minsize CMake profile
  and currently sets lld RELR and section-GC linker flags.
- `vendor/mariadb/server/CMakeLists.txt` defines
  `MYLITE_ENABLE_SECTION_GC` and applies `-ffunction-sections` and
  `-fdata-sections`, making linked code eligible for fine-grained GC and ICF.
- `vendor/mariadb/server/mylite/CMakeLists.txt` links the local MyLite smoke
  executables that act as current runtime-size proxies.

Toolchain evidence from the Ubuntu 24.04 minsize container:

```text
ld.lld --help
  --icf=all               Enable identical code folding
  --icf=none              Disable identical code folding (default)
  --icf=safe              Enable safe identical code folding
```

Measured local experiments after `rpl-filter-size-profile`:

| Link mode | Stripped `mylite-open-close-smoke` | Delta |
| --- | ---: | ---: |
| Current profile | 6,257,608 | 0 |
| `--icf=safe` | 6,257,608 | 0 |
| `--icf=all` | 6,094,568 | -163,040 |

The stripped embedded archive remains 32,283,380 bytes because ICF is a final
link step, not a static-archive compaction step.

## Design

Add a MyLite minsize linker option named `MYLITE_ENABLE_ICF`.

Supported values:

- `OFF`, `none`, `0`, `NO`, `FALSE`: do not pass an ICF flag.
- `safe`: pass `-Wl,--icf=safe`.
- `all`, `ON`, `YES`, `TRUE`: pass `-Wl,--icf=all`.

The default minsize profile should use `all` because the current user goal is
to drive linked runtime size as low as possible and commit size attempts for
later keep/drop review.

Record `MYLITE_ENABLE_ICF` in CMake cache metadata and in
`mylite-build-report.txt` through the existing cache-entry report.

## Non-Goals

- Do not remove more SQL functions, storage engines, or parser behavior in this
  slice.
- Do not claim archive-size savings.
- Do not claim this is automatically the safest broad-distribution default.
- Do not change public `libmylite` API or storage file format.

## Affected Subsystems

- Minsize build tooling.
- Linked executable/shared/module artifacts produced by the minsize profile.
- Production-size analysis documentation.

## DDL Metadata Routing Impact

None. This is a linker-profile change only.

## Single-File and Embedded-Lifecycle Impact

No intended impact. ICF does not change MyLite's catalog, file layout, sidecar
policy, open/close ownership, locking, or recovery behavior.

## Public API and File-Format Impact

No public API or `.mylite` file-format change.

There is a binary-behavior risk: `--icf=all` may fold functions even when code
compares function addresses. The current smokes must pass, and this risk must
remain documented for release packaging decisions.

## Binary-Size Impact

Expected current impact:

- stripped linked runtime: about 159 KiB saved,
- static embedded archive: no change,
- dynamic library dependencies: no change.

The size win is small compared with RELR, charset, collation, or SQL subsystem
removals, but it is link-only and composes with the existing profile.

## License, Trademark, and Dependency Impact

No new runtime dependency. The minsize container already installs LLVM `lld`
from Ubuntu packages for the RELR profile.

## Test Plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

Measure:

```sh
stat -c "%s" build/mariadb-minsize/libmysqld/libmariadbd.a
cp build/mariadb-minsize/mylite/mylite-open-close-smoke \
  build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
strip --strip-unneeded \
  build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
stat -c "%s" build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
grep '^MYLITE_ENABLE_ICF:' build/mariadb-minsize/CMakeCache.txt
```

## Acceptance Criteria

- The minsize build profile links with `-Wl,--icf=all` by default.
- `MYLITE_ENABLE_ICF=OFF` can disable ICF for comparison builds.
- Build, open/close smoke, and compatibility harness pass.
- `libmariadbd.a` remains unchanged after archive stripping.
- The stripped linked runtime size is lower than the current 6,257,608-byte
  baseline.
- Production-size analysis records the size delta and risk.

## Risks and Unresolved Questions

- `--icf=all` is more aggressive than `--icf=safe`; it can merge functions
  whose distinct addresses might be observable. Current MyLite code should not
  depend on inherited function address uniqueness, but full MariaDB has a large
  surface and this remains a release risk.
- The current measurement is ARM64/Linux/glibc. x86-64 and future shared
  `libmylite.so` packaging should be measured separately.
- This does not address the larger remaining embedded-only opportunities:
  VIO TLS, SQL crypto functions, authentication, thread pool, and deeper
  binlog/GTID roots.
