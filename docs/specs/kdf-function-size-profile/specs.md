# KDF function size profile

## Problem

The aggressive MyLite minsize profile still registers MariaDB's `KDF()` SQL
function. `KDF()` directly calls OpenSSL HKDF and PBKDF2 APIs from
`item_strfunc.cc`, so it keeps uncommon SQL crypto code live in the linked
embedded runtime.

This slice tests whether omitting `KDF()` is a worthwhile incremental size
reduction after DES key-file support has already been removed.

## Source Findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

Relevant source paths:

- `vendor/mariadb/server/sql/item_create.cc` defines `Create_func_kdf` and
  registers `KDF` in `func_array`.
- `vendor/mariadb/server/sql/item_strfunc.h` declares `Item_func_kdf`.
- `vendor/mariadb/server/sql/item_strfunc.cc` defines
  `Item_func_kdf::fix_length_and_dec()` and `Item_func_kdf::val_str()`.
- `Item_func_kdf::val_str()` calls OpenSSL HKDF APIs when `HAVE_hkdf` is set:
  `EVP_PKEY_CTX_new_id()`, `EVP_PKEY_derive_init()`,
  `EVP_PKEY_CTX_set_hkdf_md()`, `EVP_PKEY_CTX_set1_hkdf_key()`,
  `EVP_PKEY_CTX_set1_hkdf_salt()`, `EVP_PKEY_CTX_add1_hkdf_info()`,
  `EVP_PKEY_derive()`, and `EVP_PKEY_CTX_free()`.
- The PBKDF2 path calls `PKCS5_PBKDF2_HMAC()` and `EVP_sha512()`.
- The current OpenSSL dependency is still rooted by many other surfaces, so
  this slice is not expected to remove `libcrypto.so.3`.

## Design

Add a MyLite-only `MYLITE_DISABLE_KDF_FUNCTION` build option for the embedded
minsize profile.

When enabled:

- do not compile `Create_func_kdf`,
- do not register `KDF` in `func_array`,
- do not compile `Item_func_kdf` method bodies in `item_strfunc.cc`,
- verify `KDF()` fails through MariaDB's existing unknown-function diagnostic.

This follows the existing minsize pattern for XML, regex, Oracle aliases,
`ENCRYPT()`, and DES.

## Non-Goals

- Do not remove AES, MD5, SHA/SHA2, `RANDOM_BYTES()`, `PASSWORD()`,
  `ENCODE()`, `DECODE()`, or authentication digest helpers.
- Do not attempt to remove `mysys_ssl` or `libcrypto.so.3` in this slice.
- Do not change non-embedded MariaDB server behavior.

## Compatibility Impact

`KDF()` becomes unavailable in the aggressive embedded minsize profile. This is
a SQL-visible compatibility loss, but it is narrow and limited to a specialized
crypto helper with low value in the embedded default.

## Single-File And Embedded-Lifecycle Impact

This slice only removes SQL expression code. It does not change the `.mylite`
file format, catalog, storage engine, locking, recovery, or allowed companion
files.

## Binary-Size Impact

Expected savings are small:

- less `item_create.cc.o` code/data for the native function builder and
  registry entry,
- less `item_strfunc.cc.o` code for HKDF/PBKDF2 execution,
- fewer direct OpenSSL symbol references from `item_strfunc.cc.o`.

`libcrypto.so.3` should remain because retained SQL/auth crypto and internal
digest helpers still root OpenSSL-backed code.

## Test Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-kdf-function \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-kdf-function \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-kdf-function \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Also compare current archive and stripped linked-smoke sizes against
`build/mariadb-minsize-des-function`.

## Acceptance Criteria

- The minsize build completes with `MYLITE_DISABLE_KDF_FUNCTION=ON`.
- The open/close smoke verifies `KDF()` fails as an unknown function.
- The compatibility harness still passes.
- The linked smoke no longer contains `Item_func_kdf` or `Create_func_kdf`
  symbols.
- Production size analysis records the measured delta.

## Risks

- This is a real SQL compatibility reduction, unlike DES where embedded
  execution was already disabled.
- The linked-runtime savings may be too small to justify losing `KDF()` outside
  the most aggressive size profile.

## Attempt Result

Implemented in `build/mariadb-minsize-kdf-function` with
`MYLITE_DISABLE_KDF_FUNCTION=ON`.

Measured against `build/mariadb-minsize-des-function`:

| Artifact | Previous bytes | New bytes | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 32,081,652 | 32,052,836 | -28,816 |
| `mylite/mylite-open-close-smoke` | 8,405,008 | 8,397,032 | -7,976 |
| stripped `mylite-open-close-smoke` copy | 6,034,528 | 6,029,392 | -5,136 |

Verification:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-kdf-function
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh` passed.
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-kdf-function
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh` passed.
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-kdf-function
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh` passed.
- `bash -n tools/build-mariadb-minsize.sh` passed.
- `git diff --check` passed.

The linked open/close smoke has no `Item_func_kdf` or `Create_func_kdf`
symbols. The embedded archive no longer has direct undefined references to
`EVP_PKEY_CTX*` or `PKCS5_PBKDF2_HMAC()` from `KDF()` code. The open/close
smoke verifies `KDF()` fails through MariaDB's unknown-function diagnostic.

`libcrypto.so.3` remains in the linked runtime dependencies because retained
AES, SHA, MD5, random-byte, password/auth, SQL digest, and startup compatibility
helpers still use OpenSSL-backed `mysys_ssl` symbols.
