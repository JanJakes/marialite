# Production size analysis

This document records the current production-oriented size profile for MyLite
and the measured impact of likely size-reduction levers.

## Measurement scope

The baseline is the current `tools/build-mariadb-minsize.sh` profile:

- `CMAKE_BUILD_TYPE=MinSizeRel`
- `BUILD_CONFIG=mysql_release`
- `FEATURE_SET=small`
- `WITH_EMBEDDED_SERVER=ON`
- `DISABLE_SHARED=ON`
- `WITHOUT_DYNAMIC_PLUGINS=ON`
- system `ssl`, `pcre`, `fmt`, and `zlib`
- Aria, InnoDB, partitioning, Performance Schema, RocksDB, Mroonga, Connect,
  Spider, S3, OQGraph, Sphinx, ColumnStore, FederatedX, Blackhole, Archive,
  feedback, and selected authentication plugins disabled
- `MYLITE_DISABLE_ARIA=ON`
- `USE_ARIA_FOR_TMP_TABLES=OFF`

The report used for the baseline was generated at `2026-05-12T00:33:29Z` from
`vendor/mariadb/server` into `build/mariadb-minsize`.

This project does not yet have a final packaged production artifact such as a
shared `libmylite.so` bundle. For now, the most useful size signals are:

- the static embedded MariaDB archive used by MyLite,
- the first-party MyLite wrapper archive,
- the MyLite engine component archive,
- stripped linked smoke binaries as a proxy for final linked footprint, and
- dynamic system libraries only if a distribution bundle chooses to vendor
  them instead of relying on platform packages.

## Current baseline

| Artifact | Bytes | MiB | Notes |
| --- | ---: | ---: | --- |
| `build/mariadb-minsize/libmysqld/libmariadbd.a` | 43,405,432 | 41.39 | Main embedded MariaDB archive, 500 objects |
| `build/mariadb-minsize/mylite/libmylite.a` | 93,752 | 0.09 | First-party public wrapper |
| `build/mariadb-minsize/storage/mylite/libmylite_embedded.a` | 303,480 | 0.29 | MyLite storage-engine component archive |
| `build/mariadb-minsize/mylite/mylite-open-close-smoke` | 22,325,488 | 21.29 | Unstripped linked proxy |
| stripped `mylite-open-close-smoke` copy | 19,331,904 | 18.44 | `strip --strip-unneeded` on copied binary |

The linked proxy has this section profile:

| Section group | Bytes |
| --- | ---: |
| text | 16,917,748 |
| data | 2,393,240 |
| bss | 306,856 |
| total `size` decimal | 19,617,844 |

Largest linked sections in the open-close proxy:

| Section | Bytes | Interpretation |
| --- | ---: | --- |
| `.rodata` | 4,884,713 | Collation tables, parser tables, SQL metadata, constants |
| `.rela.dyn` | 4,808,928 | Dynamic relocations from the current link shape |
| `.text` | 3,850,428 | Executable code |
| `.data.rel.ro` | 1,506,808 | Relocated read-only data |
| `.dynstr` | 1,225,412 | Dynamic string table |
| `.eh_frame` | 883,892 | Unwind metadata |
| `.data` | 848,576 | Writable data |
| `.dynsym` | 711,936 | Dynamic symbol table |

If a Linux distribution bundle vendors the current dynamic dependencies, it
adds about 11,340,944 bytes, or 10.82 MiB, before compression:

| Dependency | Resolved file size |
| --- | ---: |
| `libpcre2-8.so.0.11.2` | 592,328 |
| `libz.so.1.3` | 133,272 |
| `libssl.so.3` | 737,192 |
| `libcrypto.so.3` | 4,597,928 |
| `libcrypt.so.1.1.0` | 198,584 |
| `libstdc++.so.6.0.33` | 2,633,224 |
| `libm.so.6` | 591,800 |
| `libgcc_s.so.1` | 133,696 |
| `libc.so.6` | 1,722,920 |

These libraries are not currently part of `libmariadbd.a`. They matter only for
distribution formats that bundle runtime libraries.

## Where the bytes are

The SQL layer dominates the current static footprint.

| Component archive | Bytes | MiB |
| --- | ---: | ---: |
| `libmysqld/libsql_embedded.a` | 32,826,258 | 31.31 |
| `strings/libstrings.a` | 4,839,668 | 4.62 |
| `plugin/type_inet/libtype_inet_embedded.a` | 2,384,608 | 2.27 |
| `plugin/type_uuid/libtype_uuid_embedded.a` | 1,066,660 | 1.02 |
| `mysys/libmysys.a` | 649,718 | 0.62 |
| `storage/myisam/libmyisam_embedded.a` | 586,252 | 0.56 |
| `storage/mylite/libmylite_embedded.a` | 303,480 | 0.29 |
| `storage/heap/libheap_embedded.a` | 158,668 | 0.15 |
| `storage/myisammrg/libmyisammrg_embedded.a` | 152,748 | 0.15 |
| `storage/csv/libcsv.a` | 112,192 | 0.11 |
| `storage/sequence/libsequence.a` | 99,508 | 0.09 |
| `sql/libsql_sequence_embedded.a` | 95,722 | 0.09 |
| `mylite/libmylite.a` | 93,752 | 0.09 |

The largest individual objects are mostly SQL expression, type, parser,
charset, and optional type support:

| Object | Bytes |
| --- | ---: |
| `item_func.cc.o` | 1,575,832 |
| `sql_type.cc.o` | 1,456,224 |
| `item_strfunc.cc.o` | 1,440,840 |
| `yy_mariadb.cc.o` | 1,386,632 |
| `yy_oracle.cc.o` | 1,372,688 |
| `item.cc.o` | 1,258,392 |
| `item_create.cc.o` | 1,139,664 |
| `item_timefunc.cc.o` | 1,049,896 |
| `item_cmpfunc.cc.o` | 1,027,216 |
| `ctype-uca1400.c.o` | 973,408 |
| `item_geofunc.cc.o` | 908,920 |
| `field.cc.o` | 858,688 |
| `type_uuid/plugin.cc.o` | 858,584 |
| `type_inet/plugin.cc.o` | 794,384 |
| `type_inet/sql_type_inet.cc.o` | 749,712 |
| `ctype-uca.c.o` | 739,744 |

The current built-in plugins are:

- `binlog`
- `csv`
- `heap`
- `mhnsw`
- `myisam`
- `myisammrg`
- `mylite`
- `mysql_password`
- `online_alter_log`
- `sequence`
- `sql_sequence`
- `thread_pool_info`
- `type_geom`
- `type_inet`
- `type_uuid`
- `user_variables`
- `userstat`

## Measured reduction experiments

| Experiment | Archive bytes | Archive delta | Stripped linked proxy | Linked delta | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| Baseline | 43,405,432 | 0 | 19,331,904 | 0 | Passes current smokes |
| Strip archive with `strip -g` | 42,261,216 | -1,144,216 | n/a | n/a | Low-risk packaging step |
| Strip archive with `strip --strip-unneeded` | 41,873,048 | -1,532,384 | n/a | n/a | Higher risk than `strip -g` for static archives |
| `WITH_EXTRA_CHARSETS=none` | 40,820,782 | -2,584,650 | 16,836,664 | -2,495,240 | Segfaults in open-close smoke |
| `WITH_EXTRA_CHARSETS=none`, `DEFAULT_COLLATION=utf8mb4_general_ci` | 40,820,774 | -2,584,658 | 16,836,664 | -2,495,240 | Still segfaults in open-close smoke |
| `WITH_EXTRA_CHARSETS=complex` | 43,325,192 | -80,240 | 19,248,368 | -83,536 | Too small to matter |
| all tested `DISABLE_PSI_*` switches | 43,405,432 | 0 | not retested | n/a | No current size effect |
| plugin flags for type/user/sequence plugins | 43,296,232 | -109,200 | 19,265,896 | -66,008 | Large type plugins remain built in |
| `-ffunction-sections -fdata-sections` plus `--gc-sections` | 48,305,352 | +4,899,920 | 19,331,816 | -88 | Reject |
| CMake LTO | 342,480,510 | +299,075,078 | 18,016,192 | -1,315,712 | Reject for now due archive bloat and ODR warnings |

The two `WITH_EXTRA_CHARSETS=none` builds both completed and linked, but
`mylite-open-close-smoke --mode=exclusive` exited with signal 139. The failure
was not deep-debugged because the immediate decision is already clear: this
cannot be a production default until the crash is fixed and the compatibility
impact is accepted.

Stripping the linked open-close proxy reduced it from 22,325,488 bytes to
19,331,904 bytes, saving 2,993,584 bytes, or 2.85 MiB. That is the lowest-risk
packaging win for any copied executable or shared-library style artifact.

The LTO build reduced the stripped linked proxy by 1.25 MiB, but the static
archive became 326.61 MiB and GCC emitted type/ODR mismatch warnings around
MariaDB parser and server structures, including generated parser types. That is
not a safe release lever today.

The current plugin flags did remove small built-ins such as `sequence`,
`thread_pool_info`, and `user_variables`, but `type_geom`, `type_inet`,
`type_uuid`, and `userstat` remained built in. Source or CMake fork work is
needed before the larger plugin savings are actually available.

## Decision matrix

| Lever | Expected savings | Risk | Worth doing? | Reason |
| --- | ---: | --- | --- | --- |
| Strip copied release binaries | About 2.85 MiB on the linked proxy | Low | Yes | Standard packaging step; does not change source behavior |
| Strip release static archive with `strip -g` | About 1.09 MiB | Low | Yes | Removes debug symbols while preserving normal archive symbol use |
| Strip release static archive with `strip --strip-unneeded` | About 1.46 MiB | Medium | Maybe | More savings, but static archive consumers can be more sensitive to symbol stripping |
| `WITH_EXTRA_CHARSETS=complex` | About 0.08 MiB | Low | No | Savings are too small to justify a compatibility profile |
| `WITH_EXTRA_CHARSETS=none` | About 2.46 MiB archive, 2.38 MiB linked | High today | Not yet | Good size potential, but both measured variants crash |
| Make type plugins profile-gated | Up to roughly 3.29 MiB raw archive inputs before link effects | Medium/high | Research next | `type_inet` and `type_uuid` are large, but current flags do not remove them |
| Remove or profile-gate Oracle SQL parser | Up to roughly 1.31 MiB raw object input before link effects | High | Research later | Requires a compatibility decision on Oracle mode support |
| Remove server-only SQL subsystems | Potentially large | High | Research later | The big bytes are entangled in `libsql_embedded.a`; needs slice-by-slice fork work |
| `DISABLE_PSI_*` switches | 0 in this build | Low | No | No measured effect |
| section garbage collection | 0 useful savings, archive grows | Low | No | Worsens archive size and does not improve stripped linked size |
| LTO | About 1.25 MiB linked, archive much larger | High | No | ODR warnings and huge archives are unacceptable for release |
| Bundle fewer dynamic libraries | Up to 10.82 MiB if currently vendored | Distribution-dependent | Decide per packaging target | Current static archive does not include these libraries |

## Recommendations

Take these now:

1. Add an explicit release-packaging strip step for copied artifacts.
2. Prefer `strip -g` for release static archives unless a consumer-link test
   proves `strip --strip-unneeded` is safe for MyLite's archive distribution.
3. Keep a stripped linked smoke binary size in the build report so regressions
   are visible.

Do not take these now:

1. Do not make `WITH_EXTRA_CHARSETS=none` the default. It currently crashes even
   when the default collation is changed away from `utf8mb4_uca1400_ai_ci`.
2. Do not enable LTO for production. The linked binary gets smaller, but the
   archive becomes much larger and the compiler reports ODR-sensitive MariaDB
   type mismatches.
3. Do not spend time on `WITH_EXTRA_CHARSETS=complex`, PSI switches, or section
   garbage collection as standalone size work.

Research next if size becomes a release blocker:

1. A `charset-small-profile` slice that fixes the `WITH_EXTRA_CHARSETS=none`
   crash, defines the compatibility cost, and runs full MyLite smoke coverage.
2. A `type-plugin-profile-gating` slice that makes `type_inet`, `type_uuid`,
   and possibly `type_geom` removable only if MariaDB semantics remain clear
   and tests document the unsupported surfaces.
3. An `oracle-parser-profile-gating` slice if MyLite decides not to support
   Oracle SQL mode in the embedded profile.
4. Longer-term SQL-layer pruning of server-only surfaces. This is likely where
   meaningful multi-MiB savings exist, but it should be done as compatibility
   slices, not as broad dead-code removal.

The best near-term decision is to implement packaging stripping and size
reporting first, then treat charset, type-plugin, and Oracle-parser reductions
as deliberate compatibility work. The current data does not support broad
compiler/linker tuning as an effective path.
