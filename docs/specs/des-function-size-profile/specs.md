# DES function size profile

## Problem

The aggressive MyLite minsize profile still carries MariaDB's legacy
`DES_ENCRYPT()` / `DES_DECRYPT()` SQL-function builders and DES key-file
startup plumbing. In the embedded build those functions are already disabled
at execution time by MariaDB's `!EMBEDDED_LIBRARY` guard, but the linked smoke
still contains `des_keyschedule`, the `--des-key-file` option storage, mutex
registration, and the `des_key_file.cc` archive member.

That is low-value embedded surface. MyLite has no server administration model,
no grant-based `DES_DECRYPT()` privilege flow, and no reason to load a process
global DES key file for a file-owned embedded database.

## Source Findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

Relevant source paths:

- `vendor/mariadb/server/sql/item_create.cc` registers native builders for
  `DES_DECRYPT` and `DES_ENCRYPT` in `func_array`.
- `vendor/mariadb/server/sql/item_create.cc` defines
  `Create_func_des_decrypt` and `Create_func_des_encrypt`.
- `vendor/mariadb/server/sql/item_strfunc.cc` defines
  `Item_func_des_encrypt` and `Item_func_des_decrypt`; their OpenSSL DES
  implementation is already guarded by
  `defined(HAVE_des) && !defined(EMBEDDED_LIBRARY)`.
- `vendor/mariadb/server/sql/des_key_file.cc` loads a plaintext DES key file
  into the process-global `des_keyschedule` array.
- `vendor/mariadb/server/sql/mysqld.cc` declares DES key-file globals,
  initializes and destroys `LOCK_des_key_file`, exposes `--des-key-file`,
  and registers DES file and mutex instrumentation whenever `HAVE_des` is set.
- `vendor/mariadb/server/sql/sql_reload.cc` handles `REFRESH_DES_KEY_FILE`
  when `HAVE_des` is set.
- `vendor/mariadb/server/libmysqld/CMakeLists.txt` includes
  `../sql/des_key_file.cc` in the embedded source list.

The current linked `mylite-open-close-smoke` still contains a live
`des_keyschedule` symbol even though embedded `DES_*` execution returns the
MariaDB feature-disabled path.

## Design

Add a MyLite-only `MYLITE_DISABLE_DES_FUNCTIONS` build option for the embedded
minsize profile.

When enabled:

- omit `../sql/des_key_file.cc` from the embedded SQL archive,
- do not register `DES_ENCRYPT()` or `DES_DECRYPT()` native builders,
- do not compile the DES item method definitions in `item_strfunc.cc`,
- do not expose the DES key-file option, globals, mutex, file instrumentation,
  startup load, or refresh path.

The disabled functions should fail through MariaDB's existing unknown-function
diagnostic, matching other aggressive minsize SQL-function omissions.

## Non-Goals

- Do not change non-embedded MariaDB server behavior.
- Do not remove AES, SHA, MD5, `RANDOM_BYTES()`, `KDF()`, or authentication
  digest helpers in this slice.
- Do not claim this removes OpenSSL or `libcrypto.so.3`; other retained crypto
  roots still require it.

## Compatibility Impact

`DES_ENCRYPT()` and `DES_DECRYPT()` become unknown functions in the aggressive
embedded minsize profile. This is acceptable for the size experiment because
the embedded profile already cannot execute the DES implementation and DES
key-file administration is server-shaped process-global state.

## Single-File And Embedded-Lifecycle Impact

This slice removes a process-global server key-file hook. It does not change
the `.mylite` file format, catalog layout, storage engine behavior, or allowed
MyLite companion-file lifecycle.

## Binary-Size Impact

Expected savings are small but clean:

- remove `des_key_file.cc.o` from `libmariadbd.a`,
- remove `des_keyschedule` and DES option/mutex instrumentation from linked
  runtime artifacts,
- reduce `item_create.cc.o` and `item_strfunc.cc.o` by the DES builders and
  disabled item bodies.

This is not expected to remove `libcrypto.so.3`.

## Test Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-des-function \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-des-function \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-des-function \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Also inspect the linked smoke for absence of `des_keyschedule` and compare the
current archive and stripped linked-smoke sizes against
`build/mariadb-minsize-dynamic-plugin-loading`.

## Acceptance Criteria

- The minsize build completes with `MYLITE_DISABLE_DES_FUNCTIONS=ON`.
- The open/close smoke verifies `DES_ENCRYPT()` and `DES_DECRYPT()` fail as
  unknown functions.
- The compatibility harness still passes.
- `des_key_file.cc.o` is absent from the embedded archive.
- The linked smoke no longer contains `des_keyschedule`.
- Production size analysis records the measured delta.

## Risks

- A retained refresh or option path could still reference `des_key_file` or
  `load_des_key_file()`. The build and symbol checks should catch this.
- DES function removal is SQL-visible. It should remain limited to the
  aggressive embedded minsize profile.

## Attempt Result

Implemented in `build/mariadb-minsize-des-function` with
`MYLITE_DISABLE_DES_FUNCTIONS=ON`.

Measured against `build/mariadb-minsize-dynamic-plugin-loading`:

| Artifact | Previous bytes | New bytes | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 32,122,742 | 32,081,652 | -41,090 |
| `mylite/mylite-open-close-smoke` | 8,415,504 | 8,405,008 | -10,496 |
| stripped `mylite-open-close-smoke` copy | 6,041,152 | 6,034,528 | -6,624 |

Verification:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-des-function
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh` passed.
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-des-function
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh` passed.
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-des-function
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh` passed.
- `bash -n tools/build-mariadb-minsize.sh` passed.
- `git diff --check` passed.

The resulting embedded archive has 435 objects and no `des_key_file.cc.o`
member. The linked open/close smoke has no `des_keyschedule`,
`Item_func_des*`, or `Create_func_des*` symbols. The open/close smoke verifies
`DES_ENCRYPT()` and `DES_DECRYPT()` now fail through MariaDB's unknown-function
diagnostic.

`libcrypto.so.3` remains in the linked runtime dependencies because retained
AES, SHA, MD5, random-byte, password/auth, SQL digest, and startup compatibility
helpers still use OpenSSL-backed `mysys_ssl` symbols.
