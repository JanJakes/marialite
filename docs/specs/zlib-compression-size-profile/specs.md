# zlib compression size profile

## Problem statement

The aggressive MyLite minsize profile still links `libz.so.1` into the linked
runtime artifact. In the current embedded product shape, zlib-backed SQL
compression functions, compressed client/server protocol packets, compressed
binlog events, and MariaDB compressed columns are not needed by the default
single-file runtime.

The dependency is small compared with `libcrypto.so.3`, but it is still a
remaining runtime library and it roots scattered compression code that does not
serve the embedded API.

## MariaDB base and source references

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Current dependency evidence from `build/mariadb-minsize-libcrypt`:
  - `ldd mylite-open-close-smoke` lists `libz.so.1`;
  - `/lib/aarch64-linux-gnu/libz.so.1.3` is 133,272 bytes in the Ubuntu 24.04
    minsize container;
  - `nm -u mylite-open-close-smoke` shows live zlib symbols:
    `compress`, `compressBound`, `crc32`, `deflate`, `deflateEnd`,
    `deflateInit2_`, `deflateInit_`, `inflate`, `inflateEnd`,
    `inflateInit2_`, and `uncompress`.
- Current archive roots:
  - `vendor/mariadb/server/mysys/crc32ieee.cc` includes `<zlib.h>` and uses
    zlib `crc32()` as the non-accelerated `my_checksum()` fallback.
  - `vendor/mariadb/server/mysys/my_compress.c` implements packet compression
    helpers with zlib `deflate()` and `uncompress()`.
  - `vendor/mariadb/server/sql/item_create.cc`,
    `vendor/mariadb/server/sql/item_strfunc.h`, and
    `vendor/mariadb/server/sql/item_strfunc.cc` register and implement
    `COMPRESS()`, `UNCOMPRESS()`, and `UNCOMPRESSED_LENGTH()`.
  - `vendor/mariadb/server/sql/field_comp.cc`,
    `vendor/mariadb/server/sql/field.cc`, and
    `vendor/mariadb/server/sql/sql_type.cc` implement zlib compressed-column
    metadata and field conversion.
  - `vendor/mariadb/server/sql/log_event.cc` and
    `vendor/mariadb/server/sql/log_event_server.cc` implement compressed
    binlog event helpers.
  - `vendor/mariadb/server/sql/net_serv.cc`,
    `vendor/mariadb/server/sql-common/client.c`, and
    `vendor/mariadb/server/include/mysql_com.h` expose compressed protocol
    behavior under `HAVE_COMPRESS`.
  - `vendor/mariadb/server/mysys/CMakeLists.txt` and
    `vendor/mariadb/server/libmysqld/CMakeLists.txt` link `${ZLIB_LIBRARIES}`.

## Scope

Add a MyLite-only aggressive minsize option:

```text
MYLITE_DISABLE_ZLIB_COMPRESSION=ON
```

When enabled, the profile will:

- remove zlib from `mysys` and `libmysqld` link dependencies;
- keep build-time zlib detection intact for non-MyLite builds;
- replace the zlib-backed `my_checksum()` fallback with a small
  dependency-free CRC32 fallback while retaining hardware CRC dispatch when it
  is available;
- remove SQL builders and item implementations for `COMPRESS()`,
  `UNCOMPRESS()`, and `UNCOMPRESSED_LENGTH()`;
- make MariaDB compressed-column syntax fail explicitly in the minsize profile;
- compile compressed binlog helpers and packet compression helpers as disabled
  paths that do not reference zlib;
- report `have_compress=NO` in the embedded minsize runtime.

## Non-goals

- Do not remove `CRC32()` or `CRC32C()` SQL functions.
- Do not remove `my_checksum()` or weaken retained MyISAM temporary-table and
  checksum code that still needs CRC32.
- Do not remove ordinary string, JSON, password, or crypto functions.
- Do not remove parser tokens such as `COMPRESSED`; the syntax should fail
  through a deliberate unsupported/unknown-compression path.
- Do not change public `libmylite` API or the `.mylite` file format.
- Do not remove zlib from the build container in this slice; the target is the
  linked runtime dependency.

## Proposed design

1. Add `MYLITE_DISABLE_ZLIB_COMPRESSION` to the aggressive minsize CMake
   profile and pass it from `tools/build-mariadb-minsize.sh`.
2. In `mysys`, define the same macro, avoid linking `${ZLIB_LIBRARIES}`, and
   compile `my_compress.c` into deterministic disabled stubs when the profile is
   enabled.
3. In `mysys/crc32ieee.cc`, guard the zlib include and `crc32()` fallback. Use a
   small software CRC32 fallback only when no architecture-specific accelerated
   implementation is selected.
4. In `libmysqld`, avoid adding `${ZLIB_LIBRARIES}` to `LIBS` when the profile
   is enabled.
5. Guard the SQL native-function registry, item class declarations, and method
   bodies for `COMPRESS()`, `UNCOMPRESS()`, and `UNCOMPRESSED_LENGTH()`.
6. Guard `field_comp.cc` so `compression_methods` contains no zlib compressor
   implementation in the minsize profile.
7. Change `Column_definition::set_compressed()` so compressed-column syntax
   reports `ER_UNKNOWN_COMPRESSION_METHOD` instead of creating a compressed
   field with a null compressor.
8. Compile compressed binlog helper functions as disabled paths returning
   failure without zlib references.
9. Adjust compressed protocol capability and `have_compress` reporting so the
   embedded minsize runtime does not advertise compression support.
10. Extend the open/close smoke with checks for:
    - `have_compress=NO`;
    - removed SQL compression functions fail as unknown functions;
    - `CRC32()` still succeeds;
    - compressed-column DDL fails through the expected diagnostic.

## Affected MariaDB subsystems

- Minsize build and final link dependency set.
- `mysys` checksum and packet compression helpers.
- Native SQL function registry and string-function item classes.
- Compressed-column metadata path.
- Binlog event helper code.
- Client/server compressed protocol capability reporting.
- Minsize smoke coverage.

## DDL metadata routing impact

This slice touches DDL syntax only for MariaDB compressed columns. In the
aggressive profile, `COMPRESSED` column attributes must fail before a MyLite
table definition is persisted. The slice must not create `.frm` sidecars,
catalog rows, or partial MyLite table state for rejected compressed-column DDL.

## Single-file and embedded-lifecycle impact

No intended file-format or lifecycle impact. The removed features are
network/server/binary-log compression surfaces or optional SQL/column
compression features. Open/close, ordinary MyLite table DDL/DML, and sidecar
scans must keep passing.

## Public API or file-format impact

No public C API or `.mylite` file-format change.

SQL compatibility impact: the aggressive minsize profile no longer supports
MariaDB's zlib-backed `COMPRESS()`, `UNCOMPRESS()`,
`UNCOMPRESSED_LENGTH()`, or compressed-column storage.

## Binary-size and dependency impact

Expected savings:

- remove `libz.so.1` from linked runtime dependencies;
- avoid 133,272 bytes in a Linux ARM64 bundle that vendors runtime libraries;
- modest linked/static reductions from removed SQL compression, binlog
  compression, compressed-column, and packet-compression code;
- retain CRC32 behavior without a zlib runtime dependency.

This is much smaller than the potential `libcrypto.so.3` dependency win, but it
is lower risk because the embedded profile has no need to expose zlib
compression surfaces.

## License, trademark, and dependency impact

No new dependency. This reduces the runtime dependency set. MariaDB-derived
code remains GPL-2.0-only under the existing project license.

## Test and verification plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-zlib \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-zlib \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-zlib \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Inspect:

```sh
ldd build/mariadb-minsize-zlib/mylite/mylite-open-close-smoke |
  grep -E 'libz|libcrypto|libstdc\\+\\+' || true
nm -u build/mariadb-minsize-zlib/mylite/mylite-open-close-smoke |
  grep -E 'compress|uncompress|deflate|inflate|zlib|crc32' || true
nm -C build/mariadb-minsize-zlib/mylite/mylite-open-close-smoke |
  grep -E 'Item_func_(compress|uncompress|uncompressed_length)|Create_func_(compress|uncompress|uncompressed_length)' || true
```

Expected inspection result:

- no `libz.so.1` in `ldd`;
- no undefined zlib symbols;
- no removed SQL compression function classes/builders in the linked smoke;
- retained CRC32 symbols are allowed if they are MyLite/MariaDB symbols rather
  than undefined zlib imports.

## Acceptance criteria

- `MYLITE_DISABLE_ZLIB_COMPRESSION=ON` is enabled by the aggressive minsize
  build script.
- Build, open/close smoke, and compatibility harness pass.
- `have_compress` reports `NO` in the open/close smoke.
- `COMPRESS()`, `UNCOMPRESS()`, and `UNCOMPRESSED_LENGTH()` fail through
  MariaDB's unknown-function path.
- `CRC32()` still executes successfully.
- Compressed-column DDL fails without persisting table metadata.
- `libz.so.1` is absent from linked smoke runtime dependencies.
- Production-size analysis records size and dependency deltas.

## Risks and unresolved questions

- `my_checksum()` has non-binlog users, including retained MyISAM temporary
  table code, so zlib cannot be removed by deleting checksum support.
- Compressed-column type handlers remain in the general SQL type graph. The
  first implementation should prevent new compressed columns; a deeper future
  slice can remove more of the retained type-handler surface if measurements
  show it is worthwhile.
- If retained client or binlog code still sets compression flags internally, the
  disabled helper paths must fail safely rather than corrupting packets or
  events.
