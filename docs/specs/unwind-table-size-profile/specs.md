# Unwind Table Size Profile

## Problem Statement

The current minsize linked smoke binary still carries large unwind metadata:
`.eh_frame` is 597,352 bytes and `.eh_frame_hdr` is 125,516 bytes in
`build/mariadb-minsize-kdf-function/mylite/mylite-open-close-smoke`. That is
roughly 0.69 MiB in the stripped runtime-style artifact.

This slice tests whether the embedded minsize profile can compile without
nonessential unwind tables while preserving C++ exception support. The goal is
to reduce runtime-style executable or shared-library size without removing SQL
semantics.

## Source Findings

- `tools/build-mariadb-minsize.sh` owns the reproducible aggressive minsize
  build profile.
- `vendor/mariadb/server/CMakeLists.txt` already owns MyLite-specific minsize
  compile/link options such as section GC and lld ICF.
- The current source still uses C++ exceptions in first-party MyLite code and
  in MariaDB SQL code, including `std::bad_alloc` handling and `fmt` format
  errors. This slice must not use `-fno-exceptions`.
- The current stripped linked smoke section profile includes:
  - `.eh_frame`: 597,352 bytes
  - `.eh_frame_hdr`: 125,516 bytes
  - `.gcc_except_table`: 42,876 bytes

## Proposed Design

Add `MYLITE_DISABLE_UNWIND_TABLES` as an off-by-default MariaDB CMake option.
When enabled, it adds:

```text
-fno-asynchronous-unwind-tables -fno-unwind-tables
```

to compile options.

Enable the option in `tools/build-mariadb-minsize.sh`. Do not add
`-fno-exceptions`; exception handling remains compiled so existing MyLite and
MariaDB error paths are preserved.

## Affected Subsystems

- Build tooling and compiler flags for the minsize profile.
- Debuggability/backtraces in runtime-style minsize artifacts.
- No intended SQL, storage, public API, or file-format behavior change.

## Single-File And Embedded-Lifecycle Impact

None. This changes generated machine-code metadata only.

## Public API Or File-Format Impact

No public C API or `.mylite` file-format change.

## Binary-Size Impact

Measured against `build/mariadb-minsize-kdf-function`:

| Artifact | KDF profile | No-unwind profile | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 32,052,836 | 31,864,556 | -188,280 |
| `mylite-open-close-smoke` | 8,397,032 | 8,324,928 | -72,104 |
| stripped `mylite-open-close-smoke` copy | 6,029,392 | 5,962,256 | -67,136 |

The linked smoke section deltas were:

| Section | KDF profile | No-unwind profile | Delta |
| --- | ---: | ---: | ---: |
| `.eh_frame` | 597,352 | 541,644 | -55,708 |
| `.eh_frame_hdr` | 125,516 | 114,076 | -11,440 |
| `.gcc_except_table` | 42,876 | 42,876 | 0 |

The compiler retained mandatory C++ exception metadata but removed enough
nonessential unwind data to save 67,136 bytes in the stripped runtime-style
artifact.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-unwind \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-unwind \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-unwind \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Also compare:

- `libmysqld/libmariadbd.a`
- unstripped `mylite/mylite-open-close-smoke`
- stripped `mylite/mylite-open-close-smoke`
- `.eh_frame`, `.eh_frame_hdr`, and `.gcc_except_table` sections

## Acceptance Criteria

- The minsize build completes.
- The open/close smoke passes.
- The compatibility harness passes.
- Linked artifact sizes and unwind section changes are recorded in this spec
  and in `docs/research/production-size-analysis.md`.
- Exception support is not disabled.

## Risks And Unresolved Questions

- Removing unwind tables can reduce native stack-unwind quality in crash
  diagnostics and profilers.
- The compiler may still emit mandatory exception unwind metadata for C++ code
  that needs it, so the theoretical 0.69 MiB section total is an upper bound,
  not a guaranteed saving.

## Implementation Result

Implemented as `MYLITE_DISABLE_UNWIND_TABLES`. The aggressive minsize script
enables it by default, while the upstream-derived CMake option remains
off-by-default outside that profile.

Verification run:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-unwind MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-unwind MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-unwind MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh`
