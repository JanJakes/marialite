# executable-export-size-profile

## Problem

The current linked MyLite smoke binaries export nearly every MariaDB and MyLite
global symbol because `vendor/mariadb/server/mylite/CMakeLists.txt` sets
`ENABLE_EXPORTS TRUE` on the smoke executable targets. On Linux, the current
link line for `mylite-open-close-smoke` includes `-Wl,--export-dynamic`, which
inflates `.dynsym`, `.dynstr`, `.gnu.hash`, and relocation metadata in the
linked proxy used by the production-size analysis.

This slice tests whether MyLite's local embedded smoke executables can stop
exporting their full process symbol table. This is a linked-artifact size
profile only; it is not expected to reduce `libmariadbd.a`.

## Source baseline

- MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Current MyLite size baseline after `gis-function-size-profile`:
  - `libmariadbd.a`: 33,092,908 bytes,
  - stripped `mylite-open-close-smoke`: 15,122,040 bytes,
  - linked `size` total: 15,373,843 bytes.

## Source findings

- `vendor/mariadb/server/mylite/CMakeLists.txt` defines four local smoke
  executable targets:
  - `mylite-embedded-bootstrap-smoke`,
  - `mylite-open-close-smoke`,
  - `mylite-storage-engine-smoke`,
  - `mylite-compatibility-smoke`.
- The same file sets `ENABLE_EXPORTS TRUE` on all four targets under `IF(UNIX)`.
- The observed Linux link line for `mylite-open-close-smoke` includes
  `-Wl,--export-dynamic`.
- The current linked `mylite-open-close-smoke` has 28,646 dynamic symbols from
  `nm -D`.
- The largest dynamic-symbol-related sections in the current linked proxy are:
  - `.rela.dyn`: 4,139,400 bytes,
  - `.dynstr`: 1,186,695 bytes,
  - `.dynsym`: 687,576 bytes,
  - `.gnu.hash`: 211,064 bytes.
- MariaDB's main server and client targets still use `ENABLE_EXPORTS TRUE` in
  their own CMake files. This slice touches only MyLite-owned smoke targets.

## Proposed design

Remove the MyLite-local `ENABLE_EXPORTS TRUE` block for smoke executables.

The MyLite smoke targets statically link embedded MariaDB and MyLite code, run
inside the same process, and do not load dynamic plugins in the minsize profile.
They do not need to be dynamic symbol providers.

## Non-goals

- Do not change `mariadbd`, MariaDB client, libmysqld example, or upstream test
  executable export behavior.
- Do not change first-party `libmylite` API visibility.
- Do not remove public `mylite_*` symbols from static archives.
- Do not change dynamic plugin policy.
- Do not treat this as a static archive size win.

## Affected subsystems

- MyLite smoke executable link flags.
- Production-size proxy measurement.
- Compatibility harness executable links.

## DDL metadata routing impact

None.

## Single-file and embedded-lifecycle impact

No file-format, catalog, runtime lifecycle, lock, or recovery behavior should
change. The compatibility harness must still pass because this should only
change executable dynamic symbol export metadata.

## Public API and file-format impact

No public `libmylite` API change. No `.mylite` file-format change.

## Binary-size impact

Expected savings are only in linked executable artifacts. The archive should
stay effectively unchanged. The likely savings are bounded by dynamic symbol
and relocation sections in the linked open-close proxy.

## License, trademark, and dependency impact

No new dependency or license impact.

## Test plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

Measure:

```sh
stat -c "%s" build/mariadb-minsize/libmysqld/libmariadbd.a
stat -c "%s" build/mariadb-minsize/mylite/mylite-open-close-smoke
cp build/mariadb-minsize/mylite/mylite-open-close-smoke \
  build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
strip --strip-unneeded \
  build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
stat -c "%s" build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
size build/mariadb-minsize/mylite/mylite-open-close-smoke
nm -D build/mariadb-minsize/mylite/mylite-open-close-smoke | wc -l
```

## Acceptance criteria

- `mylite-open-close-smoke` no longer links with `-Wl,--export-dynamic`.
- Dynamic symbol count drops materially from the 28,646-symbol baseline.
- Build, open/close smoke, and compatibility harness pass.
- `libmariadbd.a` does not grow.
- Linked proxy size deltas are recorded in
  `docs/research/production-size-analysis.md`.

## Verification

Validated on 2026-05-12 with:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

Observed evidence:

- `mylite-open-close-smoke` link command no longer contains
  `--export-dynamic`.
- `nm -D build/mariadb-minsize/mylite/mylite-open-close-smoke | wc -l`
  reports 488 dynamic symbols, down from 28,646.
- `libmariadbd.a` remains 33,092,908 bytes.
- `libmylite-open-close-report.txt` reports `status=0` and `phase=complete`.
- `mylite-compatibility-harness-report.txt` reports `status=0` for all groups.

Measured artifacts after this slice:

| Artifact | Bytes | Delta from GIS profile |
| --- | ---: | ---: |
| `libmariadbd.a` | 33,092,908 | 0 |
| archive object count | 488 | 0 |
| `libmylite.a` | 93,752 | 0 |
| `libmylite_embedded.a` | 303,480 | 0 |
| `mylite-open-close-smoke` | 15,261,992 | -2,162,792 |
| stripped `mylite-open-close-smoke` copy | 12,959,352 | -2,162,688 |
| linked `size` total | 13,249,609 | -2,124,234 |

## Risks and unresolved questions

- This reduces test/proxy executable size, not the current static archive.
- If a future MyLite CLI deliberately hosts dynamic plugins, that executable
  may need explicit export policy. The minsize embedded profile currently keeps
  dynamic plugins disabled.
- Production shared-library symbol visibility still needs its own release
  packaging slice.
