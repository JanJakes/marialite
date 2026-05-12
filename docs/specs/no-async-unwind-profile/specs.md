# No async unwind profile

## Problem

The current stripped linked open-close proxy still contains large unwind
metadata sections:

- `.eh_frame`: 830,300 bytes,
- `.eh_frame_hdr`: 189,396 bytes.

This slice tests whether the MyLite minsize build can compile with reduced
asynchronous unwind metadata while preserving current behavior.

## Source findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

Relevant build facts:

- `tools/build-mariadb-minsize.sh` uses `CMAKE_BUILD_TYPE=MinSizeRel`.
- The current CMake cache uses `CMAKE_C_FLAGS_MINSIZEREL=-Os -DNDEBUG` and
  `CMAKE_CXX_FLAGS_MINSIZEREL=-Os -DNDEBUG`.
- The linked smoke binary's largest metadata sections after source trimming
  include `.eh_frame` and `.eh_frame_hdr`.

## Design

Add `-fno-asynchronous-unwind-tables` to the MyLite minsize C and C++ compiler
flags. This should remove asynchronous unwind tables used mainly by profilers,
debuggers, and some stack-walking tools while preserving ordinary C++ exception
support if the compiler still needs synchronous unwind data.

Do not add `-fno-exceptions` or `-fno-rtti` in this slice.

## Non-goals

- Do not change MariaDB source semantics.
- Do not disable C++ exceptions.
- Do not change non-minsize builds.
- Do not claim compatibility with external profilers or crash unwinders that
  require asynchronous unwind tables.

## Compatibility impact

Runtime SQL behavior should not change. The main risk is degraded native stack
unwinding in stripped release binaries.

## Single-file and embedded-lifecycle impact

No `.mylite` file format, catalog, storage, locking, recovery, or lifecycle
semantics should change.

## Binary-size impact

The theoretical linked savings are bounded by the current `.eh_frame` and
`.eh_frame_hdr` sections, about 1.02 MiB before any compiler-retained
synchronous unwind data.

## Test plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

Measure section sizes with:

```sh
size -A build/mariadb-minsize/mylite/mylite-open-close-smoke
```

## Acceptance criteria

- The minsize profile builds and links.
- Current MyLite open/close and compatibility smokes pass.
- Linked section-size deltas are recorded in this spec and in production-size
  analysis.
- If savings are negligible, reject the attempt and document why.

## Risks

- Some platforms or future dependencies may rely on asynchronous unwind tables
  for diagnostics.
- The compiler may retain most unwind metadata for C++ exception handling,
  yielding little or no size reduction.
