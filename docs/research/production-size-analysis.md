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
- `WITH_EXTRA_CHARSETS=none`
- `MYLITE_DISABLE_ORACLE_PARSER=ON`
- Aria, InnoDB, partitioning, Performance Schema, RocksDB, Mroonga, Connect,
  Spider, S3, OQGraph, Sphinx, ColumnStore, FederatedX, Blackhole, Archive,
  feedback, and selected authentication plugins disabled
- `MYLITE_DISABLE_ARIA=ON`
- `USE_ARIA_FOR_TMP_TABLES=OFF`

The original comparison baseline was generated at `2026-05-12T00:33:29Z` from
`vendor/mariadb/server` into `build/mariadb-minsize`. Current measurements
include the `type-plugin-size-profile`, `charset-small-profile`, and
`oracle-parser-size-profile`, `static-archive-strip-profile`, and
`small-builtin-plugin-profile`, `xml-function-size-profile`, and
`gis-function-size-profile`, and `executable-export-size-profile` attempts,
which
remove the built-in `type_geom`, `type_inet`, `type_uuid`, `sequence`,
`thread_pool_info`, `user_variables`, and `userstat` plugins, set
`WITH_EXTRA_CHARSETS=none`, omit the Oracle SQL-mode parser, omit XML SQL
functions, omit GIS SQL functions, remove full-symbol exports from MyLite
smoke executables, and strip the static archive in the MyLite minsize profile.

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
| `build/mariadb-minsize/libmysqld/libmariadbd.a` | 33,092,908 | 31.56 | Main embedded MariaDB archive, 488 objects, stripped |
| `build/mariadb-minsize/mylite/libmylite.a` | 93,752 | 0.09 | First-party public wrapper |
| `build/mariadb-minsize/storage/mylite/libmylite_embedded.a` | 303,480 | 0.29 | MyLite storage-engine component archive |
| `build/mariadb-minsize/mylite/mylite-open-close-smoke` | 15,261,992 | 14.55 | Unstripped linked proxy |
| stripped `mylite-open-close-smoke` copy | 12,959,352 | 12.36 | `strip --strip-unneeded` on copied binary |

The linked proxy has this section profile:

| Section group | Bytes |
| --- | ---: |
| text | 10,844,913 |
| data | 2,106,200 |
| bss | 298,496 |
| total `size` decimal | 13,249,609 |

Largest linked sections in the open-close proxy:

| Section | Bytes | Interpretation |
| --- | ---: | --- |
| `.rela.dyn` | 4,139,400 | Dynamic relocations from the current link shape |
| `.text` | 3,486,572 | Executable code |
| `.rodata` | 2,144,937 | Collation tables, parser tables, SQL metadata, constants |
| `.data.rel.ro` | 1,303,984 | Relocated read-only data |
| `.eh_frame` | 802,004 | Unwind metadata |
| `.data` | 767,480 | Writable data |
| `.bss` | 298,464 | Zero-initialized writable data |
| `.eh_frame_hdr` | 182,516 | Unwind table index |

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

The SQL layer dominates the current static footprint. The component table below
was captured before the type-plugin, charset-small, Oracle-parser, static
archive strip, and small-plugin size profiles were applied and explains why
those attempts were worth testing.

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
- `sql_sequence`

## Measured reduction experiments

| Experiment | Archive bytes | Archive delta | Stripped linked proxy | Linked delta | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| Baseline | 43,405,432 | 0 | 19,331,904 | 0 | Passes current smokes |
| `type-plugin-size-profile` | 39,941,598 | -3,463,834 | 18,935,800 | -396,104 | Passes current smokes |
| `charset-small-profile` after type plugins | 37,356,932 | -6,048,500 | 16,440,560 | -2,891,344 | Passes current smokes |
| `oracle-parser-size-profile` after charset | 35,944,110 | -7,461,322 | 15,850,736 | -3,481,168 | Passes current smokes |
| `static-archive-strip-profile` after Oracle parser | 34,606,670 | -8,798,762 | 15,850,736 | -3,481,168 | Passes current smokes |
| `small-builtin-plugin-profile` after archive strip | 34,474,690 | -8,930,742 | 15,849,720 | -3,482,184 | Passes current smokes |
| `xml-function-size-profile` after small built-ins | 33,957,690 | -9,447,742 | 15,585,480 | -3,746,424 | Passes current smokes |
| `gis-function-size-profile` after XML functions | 33,092,908 | -10,312,524 | 15,122,040 | -4,209,864 | Passes current smokes |
| `executable-export-size-profile` after GIS functions | 33,092,908 | -10,312,524 | 12,959,352 | -6,372,552 | Passes current smokes |
| Strip archive with `strip -g` | 42,261,216 | -1,144,216 | n/a | n/a | Low-risk packaging step |
| Strip archive with `strip --strip-unneeded` | 41,873,048 | -1,532,384 | n/a | n/a | Higher risk than `strip -g` for static archives |
| `WITH_EXTRA_CHARSETS=none` before UCA fix | 40,820,782 | -2,584,650 | 16,836,664 | -2,495,240 | Segfaulted in open-close smoke |
| `WITH_EXTRA_CHARSETS=none`, `DEFAULT_COLLATION=utf8mb4_general_ci`, before UCA fix | 40,820,774 | -2,584,658 | 16,836,664 | -2,495,240 | Still segfaulted in open-close smoke |
| `WITH_EXTRA_CHARSETS=complex` | 43,325,192 | -80,240 | 19,248,368 | -83,536 | Too small to matter |
| all tested `DISABLE_PSI_*` switches | 43,405,432 | 0 | not retested | n/a | No current size effect |
| plugin flags for type/user/sequence plugins | 43,296,232 | -109,200 | 19,265,896 | -66,008 | Large type plugins remain built in |
| `-fno-asynchronous-unwind-tables` | 34,474,690 | 0 from current | 15,849,720 | 0 from current | Reject; current smokes pass but no artifact-size reduction |
| `-ffunction-sections -fdata-sections` plus `--gc-sections` | 48,305,352 | +4,899,920 | 19,331,816 | -88 | Reject |
| CMake LTO | 342,480,510 | +299,075,078 | 18,016,192 | -1,315,712 | Reject for now due archive bloat and ODR warnings |

The two original `WITH_EXTRA_CHARSETS=none` builds both completed and linked,
but `mylite-open-close-smoke --mode=exclusive` exited with signal 139. The
follow-up `charset-small-profile` slice fixed the UCA 1400 startup assumption
by skipping generated collations whose base compiled charset is absent. The
profile now passes current smokes while retaining the compiled default
`utf8mb4_uca1400_ai_ci`.

Stripping the current linked open-close proxy reduces it from 15,261,992 bytes
to 12,959,352 bytes, saving 2,302,640 bytes, or 2.20 MiB. That remains the
lowest-risk packaging win for any copied executable or shared-library style
artifact.

The LTO build reduced the stripped linked proxy by 1.25 MiB, but the static
archive became 326.61 MiB and GCC emitted type/ODR mismatch warnings around
MariaDB parser and server structures, including generated parser types. That is
not a safe release lever today.

The earlier plugin flags experiment did remove small built-ins such as `sequence`,
`thread_pool_info`, and `user_variables`, but `type_geom`, `type_inet`,
`type_uuid`, and `userstat` remained built in. The later type-plugin and
small-plugin profiles supersede that partial result.

The `type-plugin-size-profile` attempt then made `type_geom`, `type_inet`, and
`type_uuid` non-mandatory and disabled them in the MyLite minsize profile. That
reduced the static archive by 3,463,834 bytes and the stripped linked proxy by
396,104 bytes while current MyLite smokes still passed.

The `charset-small-profile` attempt then set `WITH_EXTRA_CHARSETS=none` and
fixed UCA 1400 registration for omitted base charsets. On top of the type
plugin profile, it reduced the static archive by another 2,584,666 bytes and
the stripped linked proxy by another 2,495,240 bytes while current MyLite smokes
still passed.

The `oracle-parser-size-profile` attempt then removed `yy_oracle.cc.o` from the
embedded archive and linked a 1,664-byte unsupported-mode stub instead. On top
of the charset-small profile, it reduced the static archive by another
1,412,822 bytes and the stripped linked proxy by another 589,824 bytes while
current MyLite smokes still passed. The open/close smoke now verifies that a
statement parsed after `SET sql_mode=ORACLE` fails with `ER_NOT_SUPPORTED_YET`.

The `static-archive-strip-profile` attempt then ran `strip --strip-unneeded`
and `ranlib` on `libmariadbd.a` after building the `mysqlserver` target. On top
of the Oracle-parser profile, it reduced the static archive by another
1,337,440 bytes. The stripped linked proxy was unchanged, but relinking current
smokes against the stripped archive passed.

The `small-builtin-plugin-profile` attempt then disabled optional `sequence`,
`thread_pool_info`, `user_variables`, and `userstat` built-ins while leaving
mandatory `sql_sequence` support intact. On top of the static archive strip, it
reduced the static archive by another 131,980 bytes and the stripped linked
proxy by another 1,016 bytes.

The `xml-function-size-profile` attempt then omitted `item_xmlfunc.cc` from the
embedded source list and removed the `EXTRACTVALUE` and `UPDATEXML` native
function builders in the minsize profile. On top of the small built-in plugin
profile, it reduced the static archive by another 517,000 bytes and the
stripped linked proxy by another 264,240 bytes. The linked open-close proxy no
longer contains `Item_func_xml_extractvalue`, `Item_func_xml_update`, or
`my_xpath_function` symbols, and the open/close smoke verifies both XML
functions now fail through MariaDB's unknown-function path.

The `gis-function-size-profile` attempt then omitted `item_geofunc.cc`,
`gcalc_tools.cc`, and `gcalc_slicescan.cc` from the embedded source list and
linked a small empty GIS registry/type-constructor shim instead. On top of the
XML function profile, it reduced the static archive by another 864,782 bytes
and the stripped linked proxy by another 463,440 bytes. The linked open-close
proxy no longer contains `Item_func_geometry_from_text`,
`Create_func_geometry_from_text`, or `Gcalc_function` symbols, and the
open/close smoke verifies `ST_ASTEXT()` now fails through MariaDB's
unknown-function path. The compatibility harness still verifies MyLite rejects
GEOMETRY columns and SPATIAL keys.

The `executable-export-size-profile` attempt then removed `ENABLE_EXPORTS TRUE`
from MyLite-owned smoke executables. On top of the GIS function profile, it left
the static archive unchanged and reduced the stripped linked proxy by another
2,162,688 bytes. The dynamic symbol count in `mylite-open-close-smoke` dropped
from 28,646 to 488, and the link command no longer includes
`-Wl,--export-dynamic`. This is a linked-artifact and size-proxy win, not a
static archive reduction.

## Decision matrix

| Lever | Expected savings | Risk | Worth doing? | Reason |
| --- | ---: | --- | --- | --- |
| Strip copied release binaries | About 2.30 MiB on the current linked proxy | Low | Yes | Standard packaging step; does not change source behavior |
| Strip release static archive with `strip --strip-unneeded` | 1.28 MiB beyond Oracle-parser profile | Medium | Applied as size attempt | Current smokes relink and pass; downstream static consumers may still need coverage |
| Strip release static archive with `strip -g` | About 0.95 MiB on the current archive | Low | Fallback | Less aggressive alternative if `--strip-unneeded` breaks a consumer |
| `WITH_EXTRA_CHARSETS=complex` | About 0.08 MiB | Low | No | Savings are too small to justify a compatibility profile |
| `WITH_EXTRA_CHARSETS=none` / `charset-small-profile` | 2.46 MiB archive and 2.38 MiB stripped linked beyond type-plugin profile | High compatibility | Applied as size attempt | Current smokes pass after the UCA 1400 null-base fix, but non-default charsets are omitted |
| Make type plugins profile-gated | 3.30 MiB archive, 0.38 MiB stripped linked | Medium/high | Applied as size attempt | Current smokes pass, but `INET`, `UUID`, and spatial plugin surfaces are compatibility tradeoffs |
| Remove small optional built-ins | 0.13 MiB archive, 0.001 MiB stripped linked | Medium/low | Applied as size attempt | Current smokes pass, but plugin-provided information-schema surfaces are omitted |
| Remove or profile-gate Oracle SQL parser | 1.35 MiB archive and 0.56 MiB stripped linked beyond charset-small profile | High compatibility | Applied as size attempt | Current smokes pass, but `sql_mode=ORACLE` now fails explicitly in the minsize profile |
| Remove XML SQL functions | 0.49 MiB archive and 0.25 MiB stripped linked beyond small-builtin profile | Medium compatibility | Applied as size attempt | Current smokes pass, but `EXTRACTVALUE()` and `UPDATEXML()` now fail as unknown functions |
| Remove GIS SQL functions | 0.82 MiB archive and 0.44 MiB stripped linked beyond XML profile | High compatibility | Applied as size attempt | Current smokes pass, but native GIS functions now fail as unknown functions in the minsize profile |
| Remove unnecessary executable symbol exports | 0 archive, 2.06 MiB stripped linked beyond GIS profile | Low/medium | Applied as size attempt | Current smokes pass; this only applies to linked executables that are not dynamic-plugin hosts |
| Remove server-only SQL subsystems | Potentially large | High | Research later | The big bytes are entangled in `libsql_embedded.a`; needs slice-by-slice fork work |
| `DISABLE_PSI_*` switches | 0 in this build | Low | No | No measured effect |
| `-fno-asynchronous-unwind-tables` | 0 in this build | Low | No | Full rebuild produced identical archive and stripped linked sizes |
| section garbage collection | 0 useful savings, archive grows | Low | No | Worsens archive size and does not improve stripped linked size |
| LTO | About 1.25 MiB linked, archive much larger | High | No | ODR warnings and huge archives are unacceptable for release |
| Bundle fewer dynamic libraries | Up to 10.82 MiB if currently vendored | Distribution-dependent | Decide per packaging target | Current static archive does not include these libraries |

## Recommendations

Take these now:

1. Add an explicit release-packaging strip step for copied artifacts.
2. Keep `strip --strip-unneeded` for the minsize static archive while current
   consumer-link smokes pass.
3. Keep a stripped linked smoke binary size in the build report so regressions
   are visible.

Do not take these now:

1. Do not enable LTO for production. The linked binary gets smaller, but the
   archive becomes much larger and the compiler reports ODR-sensitive MariaDB
   type mismatches.
2. Do not spend time on `WITH_EXTRA_CHARSETS=complex`, PSI switches, or section
   garbage collection as standalone size work.

Research next if size becomes a release blocker:

1. Longer-term SQL-layer pruning of server-only surfaces. This is likely where
   meaningful multi-MiB savings exist, but it should be done as compatibility
   slices, not as broad dead-code removal.

The best next decisions are deeper SQL-layer reductions as deliberate
compatibility work. The current data does not support broad compiler/linker
tuning as an effective path.
