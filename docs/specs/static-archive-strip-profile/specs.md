# Static archive strip profile

## Problem

The MyLite minsize build produces a static `libmariadbd.a` archive that still
contains symbols and metadata not needed by the current linked MyLite smoke
consumers. Stripping copied release binaries is already documented as a
packaging win; this slice tests whether the static archive itself can be
stripped in the minsize build without breaking consumers.

## Source findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

Relevant source paths:

- `tools/build-mariadb-minsize.sh` builds the `mysqlserver` target, whose Unix
  output is `build/mariadb-minsize/libmysqld/libmariadbd.a`.
- The same script records archive size and object count in
  `build/mariadb-minsize/mylite-build-report.txt`.
- MyLite smoke binaries link against the static archive through the
  `libmylite` target.

Current Oracle-parser-reduced archive measurements:

- unstripped `libmariadbd.a`: 35,944,110 bytes,
- copied archive after `strip -g`: 34,946,886 bytes,
- copied archive after `strip --strip-unneeded`: 34,606,670 bytes.

An explicit relink test with `strip --strip-unneeded` on the archive passed the
open/close smoke binary link and execution.

## Design

After building the `mysqlserver` target in the minsize script, run
`strip --strip-unneeded` on `libmariadbd.a`, then run `ranlib` on the archive.

This is a minsize build-output transformation. It should not change source
semantics, generated SQL code, public C API behavior, or the non-minsize build.

## Non-goals

- Do not strip source object files in place.
- Do not strip system libraries.
- Do not make claims about third-party static archive consumers beyond the
  MyLite consumer-link smoke coverage.
- Do not add binary compression in this slice.

## Compatibility impact

The risk is static archive consumer compatibility: aggressive stripping can
remove symbols that an unusual consumer or diagnostic workflow expects. The
current MyLite smokes are the acceptance gate for this attempt.

## Single-file and embedded-lifecycle impact

No `.mylite` file format, catalog, storage, locking, recovery, or lifecycle
semantics should change.

## Binary-size impact

The expected current archive saving is 1,337,440 bytes compared with the
Oracle-parser-reduced unstripped archive. Linked binary size is expected to be
unchanged unless a consumer link shape changes.

After implementation, the measured size is:

- stripped `libmariadbd.a`: 34,606,670 bytes,
- archive objects: 494,
- `mylite-open-close-smoke`: 18,264,608 bytes,
- stripped `mylite-open-close-smoke`: 15,850,736 bytes,
- `size` total: 16,111,856 bytes.

Compared with the Oracle-parser-reduced unstripped archive, this saves
1,337,440 bytes from `libmariadbd.a`. Compared with the original
production-size baseline, the combined type-plugin, charset-small,
Oracle-parser, and static-archive-strip profile saves 8,798,762 bytes from
`libmariadbd.a`. The stripped linked open-close proxy is unchanged from the
Oracle-parser profile at 15,850,736 bytes.

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

The artifact section should record the stripped archive size.

## Verification

Run on 2026-05-12:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

All passed. `build/mariadb-minsize/mylite-build-report.txt` records
`strip=strip --strip-unneeded` and `bytes=34606670` for
`libmariadbd.a`.

## Acceptance criteria

- `libmariadbd.a` is stripped by the minsize build script.
- Current MyLite open/close and compatibility smokes link and pass against the
  stripped archive.
- Size deltas are recorded in this spec and in production-size analysis.

## Risks

- `strip --strip-unneeded` is more aggressive than `strip -g`; if downstream
  static consumers fail later, the fallback is to switch the minsize script to
  `strip -g`.
- Re-running lower-level CMake targets directly may regenerate an unstripped
  archive until `tools/build-mariadb-minsize.sh` runs again.
