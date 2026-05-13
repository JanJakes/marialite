# Section Header Strip Profile

## Problem Statement

The current production-size measurements use `strip --strip-unneeded` on
copied executable and shared-object artifacts. GNU `strip` can also remove ELF
section headers with `--strip-section-headers`. Linux runtime loading does not
need section headers, but post-link inspection, symbolization, and some
debugging workflows do.

This slice measures whether section-header stripping is a usable absolute-floor
packaging step for MyLite artifacts.

## Source Findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- The current GCC PHP-shaped shared-object probe is 3,886,256 bytes after
  `strip --strip-unneeded`.
- The opt-in Clang/PIC PHP-shaped shared-object probe is 3,787,616 bytes after
  `strip --strip-unneeded`.
- The stripped Clang/PIC shared object still contains an ELF section-header
  table starting near the end of the file.
- The Ubuntu 24.04 `strip` supports `--strip-section-headers`.

## Design

For copied release artifacts only, compare:

```sh
strip --strip-unneeded artifact
strip --strip-unneeded --strip-section-headers artifact
```

Do not apply this to static archives or checked-in build products. Treat it as
a release-packaging option for executable/shared-library artifacts after normal
linking is complete.

## Non-Goals

- Do not change MariaDB or MyLite source semantics.
- Do not change linker flags.
- Do not remove loaded ELF segments.
- Do not claim this is a debug-friendly default.

## Binary-Size Impact

Measured on the GCC current profile:

| Artifact | `strip --strip-unneeded` | Sectionless | Delta |
| --- | ---: | ---: | ---: |
| stripped open-close smoke | 3,995,560 | 3,993,128 | -2,432 |
| stripped PHP-shaped shared-object probe | 3,886,256 | 3,883,896 | -2,360 |

Measured on the opt-in Clang/PIC profile:

| Artifact | `strip --strip-unneeded` | Sectionless | Delta |
| --- | ---: | ---: | ---: |
| stripped open-close smoke | 3,880,384 | 3,877,912 | -2,472 |
| stripped PHP-shaped shared-object probe | 3,787,616 | 3,785,216 | -2,400 |

The lowest measured PHP-shaped shared-object size is therefore 3,785,216 bytes:
Clang/PIC plus `strip --strip-unneeded --strip-section-headers`.

## Test And Verification Plan

Run:

```sh
cp artifact artifact.sectionless
strip --strip-unneeded --strip-section-headers artifact.sectionless
ldd artifact.sectionless
```

For executables, run the sectionless open-close smoke in exclusive mode. For
shared objects, compile a small `dlopen()` probe that resolves
`mylite_php_probe` with `dlsym()` and calls it against a temporary `.mylite`
file.

## Verification

Passed:

- GCC sectionless open-close smoke executable ran in exclusive mode.
- Clang/PIC sectionless open-close smoke executable ran in exclusive mode.
- GCC sectionless PHP-shaped shared object loaded through `dlopen()`, exported
  `mylite_php_probe` through `dlsym()`, and opened/closed a temporary
  `.mylite` file.
- Clang/PIC sectionless PHP-shaped shared object loaded through `dlopen()`,
  exported `mylite_php_probe` through `dlsym()`, and opened/closed a temporary
  `.mylite` file.
- `readelf -h` confirmed zero section headers after stripping.

## Decision

Keep section-header stripping as an absolute-floor packaging candidate, not as
the default development artifact. The savings are real but tiny, and the
tradeoff is worse post-link introspection and debugging.
