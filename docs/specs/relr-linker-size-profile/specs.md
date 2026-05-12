# RELR Linker Size Profile

## Problem Statement

The current MyLite minsize profile has reduced the embedded archive
substantially, but the linked runtime-style artifact still carries a large
dynamic relocation section. On the ARM64 Ubuntu 24.04 build, the linked
`mylite-open-close-smoke` binary has a `.rela.dyn` section of 4,112,040 bytes
and 168,691 `R_AARCH64_RELATIVE` relocations. That relocation table is larger
than the executable `.text` section.

This slice tests and applies compact ELF relative relocations for the minsize
profile. The goal is to reduce executable or shared-library style artifacts
without removing additional SQL semantics.

## Source Findings

MariaDB source/build references:

- `tools/build-mariadb-minsize.sh` owns the reproducible minsize CMake profile
  and strips `libmysqld/libmariadbd.a`.
- `vendor/mariadb/server/mylite/CMakeLists.txt` links the MyLite smoke
  executables against `libmylite` or `mysqlserver`.
- The current build is a PIE executable on ARM64:
  `ELF 64-bit LSB pie executable, ARM aarch64`.

Toolchain references:

- GNU `ld` documents `-z pack-relative-relocs` as producing compact relative
  relocations with `DT_RELR`, `DT_RELRSZ`, and `DT_RELRENT`, and as adding the
  glibc `GLIBC_ABI_DT_RELR` dependency when linked against glibc.
  Source: https://sourceware.org/binutils/docs/ld.pdf
- The GNU `ld` in the current ARM64 container reports
  `-z pack-relative-relocs ignored`, so it cannot deliver this reduction for
  this build profile.
- LLVM `lld` has AArch64 RELR test coverage through
  `--pack-dyn-relocs=relr`.
  Source:
  https://llvm.googlesource.com/llvm-project/+/refs/tags/llvmorg-14.0.0-rc2/lld/test/ELF/pack-dyn-relocs-relr-loop.s
- LLVM `lld` requires `-z pack-relative-relocs`, not only
  `--pack-dyn-relocs=relr`, for glibc-linked binaries because glibc rejects
  `DT_RELR` objects that lack the `GLIBC_ABI_DT_RELR` version dependency.
  Source:
  https://llvm.googlesource.com/llvm-project/+/refs/tags/llvmorg-16.0.3/lld/test/ELF/pack-dyn-relocs-glibc.s

Local experiment:

- Installing `lld` in the minsize container and configuring with
  `LDFLAGS="-fuse-ld=lld -Wl,-z,pack-relative-relocs -Wl,--pack-dyn-relocs=relr"`
  produces a runnable smoke binary with `.relr.dyn` and
  `GLIBC_ABI_DT_RELR`.
- Passing only `--pack-dyn-relocs=relr` produced a binary that failed during
  configure-time `try_run()` with:
  `DT_RELR without GLIBC_ABI_DT_RELR dependency`.

## Proposed Design

Install `lld` in the minsize build image and set the minsize CMake linker flags
to:

```text
-fuse-ld=lld -Wl,-z,pack-relative-relocs -Wl,--pack-dyn-relocs=relr
```

Apply the same flags to executable, shared-library, and module linker flags so
the profile covers the current smoke executable and a future shared
`libmylite.so` or PHP-extension style module.

Record the linker flags in `mylite-build-report.txt` so size measurements can
be traced back to the link mode.

## Affected Subsystems

- Build tooling: minsize Docker image and CMake configure arguments.
- Packaging/runtime ABI: linked ELF artifacts built by this profile depend on
  a loader/libc that supports `GLIBC_ABI_DT_RELR`.
- SQL execution and storage semantics: no intended behavior change.

## Single-File and Embedded-Lifecycle Impact

This is a link-format change only. It does not affect database file layout,
companion files, locks, recovery, open/close behavior, or the MyLite C API.

## Public API and File-Format Impact

No public C API or database file-format change.

The runtime ABI impact is external: Linux/glibc artifacts linked with this
profile require glibc support for `DT_RELR`. The current Ubuntu 24.04
container uses glibc 2.39, which supports this. Older deployment baselines must
be evaluated before keeping this profile for broad binary distribution.

## Binary-Size Impact

Measured against the current `procedure-analyse-size-profile` baseline:

| Artifact | GNU ld profile | lld RELR profile | Delta |
| --- | ---: | ---: | ---: |
| `libmariadbd.a` | 32,359,184 | 32,359,184 | 0 |
| `mylite-open-close-smoke` | 15,173,312 | 11,119,792 | -4,053,520 |
| stripped `mylite-open-close-smoke` copy | 12,892,376 | 8,820,296 | -4,072,080 |

The lld RELR smoke binary section profile:

| Section group | Bytes |
| --- | ---: |
| text | 6,721,032 |
| data | 2,095,896 |
| bss | 299,057 |
| total `size` decimal | 9,115,985 |

Relevant linked sections:

| Section | Bytes |
| --- | ---: |
| `.rela.dyn` | 63,456 |
| `.relr.dyn` | 24,304 |
| `.text` | 3,417,692 |
| `.rodata` | 2,154,551 |
| `.data.rel.ro` | 1,295,592 |
| `.eh_frame` | 792,444 |

## License, Trademark, and Dependency Impact

The minsize build image gains Ubuntu's `lld` package. LLVM components are
Apache-2.0-with-LLVM-exception licensed; this affects the build toolchain only
and does not add a runtime dependency to `libmylite`.

## Test and Verification Plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

Verify:

- `mylite-build-report.txt` records the lld RELR linker flags.
- `readelf -d build/mariadb-minsize/mylite/mylite-open-close-smoke` contains
  `RELR`, `RELRSZ`, and `RELRENT`.
- `readelf -V build/mariadb-minsize/mylite/mylite-open-close-smoke` contains
  `GLIBC_ABI_DT_RELR`.
- The stripped smoke binary is materially smaller than the GNU ld profile.

## Acceptance Criteria

- The minsize Docker image includes `lld`.
- The minsize build profile uses lld RELR linker flags for executable, shared,
  and module artifacts.
- The open/close smoke and compatibility harness pass.
- The production size analysis records the new lowest linked runtime size and
  the portability risk.

## Risks and Unresolved Questions

- RELR is not suitable for older Linux/glibc deployment baselines. This is a
  strong size attempt, not automatically the final broad-distribution default.
- The current measurement is ARM64. x86-64 should be measured separately before
  claiming identical savings across architectures.
- This does not reduce `libmariadbd.a`; archive-size work must continue through
  SQL subsystem slicing.
