# Dynamic Plugin Loading Size Profile

## Problem Statement

The current MyLite minsize profile sets `WITHOUT_DYNAMIC_PLUGINS=ON` and the
embedded SQL path already rejects `CREATE FUNCTION ... SONAME`,
`INSTALL PLUGIN`, and `UNINSTALL PLUGIN`. Even so, MariaDB still compiles the
runtime `dlopen()` plugin loader and the dynamic plugin service table into
`sql_plugin.cc`.

That service table roots callback structures for services that are not useful
in MyLite's embedded profile, including crypto, wsrep, provider compression,
logger, JSON, and SQL client service callbacks. This is a poor fit for an
embedded build that has no dynamic plugin artifacts and no supported dynamic
extension surface.

## Source Findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

Relevant source paths:

- `tools/build-mariadb-minsize.sh` passes `-DWITHOUT_DYNAMIC_PLUGINS=ON`.
- `vendor/mariadb/server/sql/sql_parse.cc` rejects dynamic extension SQL in
  embedded builds through the earlier `unsupported-server-surface` slice.
- `vendor/mariadb/server/sql/sql_plugin.cc` still compiles `plugin_dl_add()`,
  `read_mysql_plugin_info()`, `read_maria_plugin_info()`, and
  `plugin_dl_foreach()` whenever the host platform has `HAVE_DLOPEN`.
- `vendor/mariadb/server/sql/sql_plugin_services.inl` defines
  `list_of_services[]` for dynamic plugin symbol patching. On the current
  zlib-disabled baseline, `sql_plugin.cc.o` still has undefined references to
  crypto service helpers such as `my_aes_crypt`, `my_random_bytes`, `my_md5`,
  and `my_sha*`, plus many wsrep and provider service helpers.
- `vendor/mariadb/server/sql/mysqld.cc` reports `have_dlopen=YES` solely from
  `HAVE_DLOPEN`, even though MyLite's embedded profile does not support
  dynamic plugin loading.

Current OpenSSL-root archive objects from
`build/mariadb-minsize-zlib/libmysqld/libmariadbd.a` include:

| Object | Bytes | Relevant roots |
| --- | ---: | --- |
| `item_strfunc.cc.o` | 1,352,576 | SQL KDF direct OpenSSL calls |
| `my_crypt.cc.o` | 22,576 | AES and random bytes |
| `vio.c.o` | 6,760 | OpenSSL error cleanup |
| `my_md5.cc.o` | 4,800 | OpenSSL MD5 wrapper |
| `des_key_file.cc.o` | 4,168 | DES key-file loader |
| `my_sha512.cc.o` | 3,864 | OpenSSL SHA-512 wrapper |
| `my_sha384.cc.o` | 3,864 | OpenSSL SHA-384 wrapper |
| `openssl.c.o` | 3,824 | OpenSSL compatibility sizing |
| `my_sha256.cc.o` | 3,408 | OpenSSL SHA-256 wrapper |
| `my_sha224.cc.o` | 3,408 | OpenSSL SHA-224 wrapper |
| `my_sha1.cc.o` | 3,376 | OpenSSL SHA-1 wrapper |

The dynamic plugin service table is not the only OpenSSL root, but it is one of
the reasons `sql_plugin.cc.o` keeps the AES, MD5, SHA, and random-byte helpers
live even when no dynamic plugins can be loaded.

## Design

Add a MyLite-only CMake option:

```cmake
MYLITE_DISABLE_DYNAMIC_PLUGIN_LOADING
```

When enabled:

- Treat the dynamic plugin loader as unavailable even if the platform has
  `HAVE_DLOPEN`.
- Compile `sql_plugin.cc` dynamic loading code through the existing no-dlopen
  fallback paths.
- Keep builtin static plugin registration intact.
- Replace the full dynamic-plugin service list with the minimum entries needed
  by existing startup code. The current startup code updates
  `list_of_services[1]` for `debug_sync_service`, so the minimal list must keep
  a placeholder entry at index 0 and `debug_sync_service` at index 1 unless the
  startup code is also narrowed.
- Report `have_dlopen=NO` for the MyLite minsize profile.
- Enable the option in `tools/build-mariadb-minsize.sh`.

## Non-Goals

- Do not remove static builtin plugin registration.
- Do not remove the plugin registry or storage-engine plugin infrastructure.
- Do not change non-MyLite builds.
- Do not remove SQL parser support for dynamic plugin statements. They already
  fail explicitly in embedded execution.
- Do not claim this removes all OpenSSL usage. SQL crypto functions,
  authentication helpers, DES key-file support, and some startup helpers may
  still root `libcrypto`.

## Compatibility Impact

Dynamic plugins are already outside MyLite's embedded product surface. The
change should make that unsupported status reflected at build/link time rather
than only at SQL execution time.

The compatibility risk is command-line or internal startup paths that still
expect to load dynamic plugins despite the minsize profile. The minsize profile
does not support such extensions, so the correct behavior is an explicit
disabled-feature failure rather than library loading.

## Single-File And Embedded-Lifecycle Impact

This should not affect the `.mylite` file format, catalog layout, lock policy,
or MyLite-owned companion files. It reduces inherited server extension
machinery that could otherwise attempt filesystem access outside the primary
file lifecycle.

## Binary-Size Impact

Expected effects:

- reduce linked runtime size by letting section GC discard the dynamic loader
  and service handlers,
- reduce archive size if stripped section/object contents become smaller,
- possibly reduce OpenSSL-root references, but not remove `libcrypto.so.3`
  until the remaining SQL/auth crypto roots are handled.

The implementation must record:

- `libmariadbd.a` size and object count,
- unstripped and stripped `mylite-open-close-smoke` sizes,
- dynamic dependencies,
- whether `sql_plugin.cc.o` still roots crypto service helpers.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-dynamic-plugin-loading \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-dynamic-plugin-loading \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-dynamic-plugin-loading \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

Also inspect:

```sh
ldd build/mariadb-minsize-dynamic-plugin-loading/mylite/mylite-open-close-smoke
nm -A --undefined-only \
  build/mariadb-minsize-dynamic-plugin-loading/libmysqld/libmariadbd.a \
  | grep ':sql_plugin.cc.o:'
```

## Acceptance Criteria

- The minsize build compiles with dynamic plugin loading disabled.
- Static builtin plugins still initialize, including `MYLITE`, `HEAP`,
  hidden internal `MyISAM`, and `mysql_password` if still built.
- Dynamic extension SQL remains explicitly unsupported in embedded mode.
- The open/close smoke and compatibility harness pass.
- Production size analysis records measured deltas.

## Risks And Unresolved Questions

- `sql_plugin.cc` still has broad plugin-registry responsibilities, so this
  slice should avoid aggressive rewrites.
- The minimal service list relies on the current `debug_sync_service` index
  assumption. A future cleanup can replace the indexed mutation with a named
  helper.
- `libcrypto.so.3` is expected to remain after this slice because SQL crypto,
  authentication, DES key-file, and startup helpers still reference OpenSSL.

## Implementation Result

Implemented behind `MYLITE_DISABLE_DYNAMIC_PLUGIN_LOADING` and enabled in
`tools/build-mariadb-minsize.sh`.

The implementation:

- compiles `sql_plugin.cc` dynamic `dlopen()` loading paths through the
  existing disabled-feature fallback when the MyLite option is enabled,
- replaces the full dynamic plugin service table with a two-entry minimal list
  that preserves the existing `debug_sync_service` startup update,
- leaves static builtin plugin registration intact,
- reports `have_dynamic_loading=NO`, and
- extends the open/close smoke to verify that runtime variable.

Measured from
`MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-dynamic-plugin-loading`:

| Artifact | Bytes | Delta From Zlib Profile |
| --- | ---: | ---: |
| `libmysqld/libmariadbd.a` | 32,122,742 | -49,278 |
| `mylite/mylite-open-close-smoke` | 8,415,504 | -40,488 |
| stripped `mylite-open-close-smoke` copy | 6,041,152 | -26,192 |

The linked dependency set is unchanged and still includes `libcrypto.so.3`.
`sql_plugin.cc.o` no longer roots dynamic-service references to crypto, wsrep,
logger, JSON, or SQL client callbacks, but other retained objects still root
OpenSSL.

Verification passed:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-dynamic-plugin-loading \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-dynamic-plugin-loading \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-dynamic-plugin-loading \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```
