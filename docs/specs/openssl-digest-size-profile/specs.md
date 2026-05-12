# OpenSSL Digest Size Profile

## Problem Statement

The current aggressive MyLite embedded-size profile has removed VIO TLS,
SQL-visible OpenSSL crypto functions, and inherited server encryption hooks, but
the linked runtime artifact still depends on `libcrypto.so.3`. That dependency
is now rooted by small internal digest and startup compatibility helpers rather
than by user-facing TLS or encryption features.

This slice removes OpenSSL from the minsize profile by replacing the retained
MD5 and SHA-1 wrappers with MyLite-local implementations and by stubbing the
OpenSSL startup compatibility check when no OpenSSL consumer remains.

## Source Findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`), imported under
`vendor/mariadb/server/`.

- `vendor/mariadb/server/mysys_ssl/CMakeLists.txt` always builds
  `my_sha1.cc`, `my_sha224.cc`, `my_sha256.cc`, `my_sha384.cc`,
  `my_sha512.cc`, `my_md5.cc`, `openssl.c`, and `my_crypt.cc`, and links
  OpenSSL crypto when `MYLITE_DISABLE_VIO_SSL` is enabled.
- `vendor/mariadb/server/mysys_ssl/my_sha.inl` implements all SHA wrappers only
  through WolfSSL or OpenSSL APIs.
- `vendor/mariadb/server/mysys_ssl/my_md5.cc` implements MD5 only through
  WolfSSL or OpenSSL APIs.
- `vendor/mariadb/server/mysys_ssl/openssl.c` implements
  `check_openssl_compatibility()` by allocating OpenSSL cipher and digest
  contexts when OpenSSL 1.1+ compatibility macros are active.
- `vendor/mariadb/server/vio/vio.c` calls OpenSSL cleanup macros from
  `vio_end()` whenever `HAVE_OPENSSL` is set. In the minsize profile, this must
  be guarded by `MYLITE_DISABLE_VIO_SSL` because VIO TLS has already been
  compiled out.
- The current linked smoke artifact still has OpenSSL undefined references only
  from `my_sha1.cc.o`, `my_md5.cc.o`, and `openssl.c.o`; `my_crypt.cc.o` remains
  in the archive but is no longer linked into the smoke after server encryption
  was removed.
- Retained embedded SQL objects still reference:
  - `my_sha1` from `lib_sql.cc.o`, `client.c.o`, and `password.c.o`;
  - `my_sha1_multi` from `password.c.o`;
  - `my_md5` from `sql_digest.cc.o` and `table.cc.o`;
  - `check_openssl_compatibility` from `lib_sql.cc.o`.
- `vendor/mariadb/server/include/mysql/service_sha1.h` and
  `vendor/mariadb/server/include/mysql/service_md5.h` define the public wrapper
  ABI that static and plugin code expect.

## Proposed Design

Add `MYLITE_DISABLE_OPENSSL_DIGESTS` for the minsize profile.

When enabled:

- compile a MyLite-owned `mysys_ssl/mylite_digest.cc` instead of the OpenSSL
  MD5, SHA, AES/random, and startup compatibility objects;
- guard VIO OpenSSL cleanup under the existing `MYLITE_DISABLE_VIO_SSL` option;
- provide the retained static wrapper symbols:
  - `my_sha1`, `my_sha1_multi`, `my_sha1_context_size`, `my_sha1_init`,
    `my_sha1_input`, and `my_sha1_result`;
  - `my_md5`, `my_md5_multi`, `my_md5_context_size`, `my_md5_init`,
    `my_md5_input`, and `my_md5_result`;
  - `check_openssl_compatibility`;
- make `check_openssl_compatibility()` return success because the minsize
  profile no longer uses OpenSSL context allocation;
- do not provide SHA-2 or AES/random wrappers in this option, because current
  minsize source references no longer require them.

The digest implementations must compute standard MD5 and SHA-1 output bytes.
They are retained only for inherited internal metadata, digest, and password
helper code; SQL-visible MD5/SHA/AES/password functions remain disabled by the
earlier `MYLITE_DISABLE_SQL_CRYPTO_FUNCTIONS` slice.

## Affected Subsystems

- `mysys_ssl` build composition and link dependencies.
- `libmysqld` static library link dependency list.
- MyLite minsize build script and report evidence.
- MyLite smoke tests for digest vector coverage.

## DDL Metadata Routing Impact

This slice does not change DDL routing or table metadata storage. It preserves
the existing internal MD5 entry points used by view/table digest code.

## Single-File and Embedded-Lifecycle Implications

No new persistent files, sidecars, sockets, or runtime services are introduced.
Startup no longer runs an OpenSSL allocation compatibility check in the minsize
profile, which is appropriate only because OpenSSL is no longer linked.

## Public API and File-Format Impact

No `libmylite` public API changes and no file-format changes.

## Binary-Size Impact

Expected direct effects:

- remove `libcrypto.so.3` from the linked runtime dependencies;
- remove OpenSSL wrapper objects and unused SHA-2/AES/random objects from the
  `libmariadbd.a` archive in the minsize profile;
- add a small standalone MD5/SHA-1 implementation.

If a package vendors runtime libraries next to the linked extension or
executable, the main saving is the removed `libcrypto.so.3` dependency. On the
current Ubuntu 24.04 ARM64 container that library is 4,597,928 bytes.

## License and Dependency Impact

No new dependency is introduced. The new digest implementation is first-party
GPL-2.0-only MyLite code in the MariaDB-derived tree. This reduces runtime
OpenSSL dependency obligations for the aggressive embedded-size profile.

## Test and Verification Plan

- Build the minsize profile with `MYLITE_DISABLE_OPENSSL_DIGESTS=ON`.
- Add and run a focused `mylite-digest-smoke` executable that checks:
  - MD5 empty string and `abc` vectors;
  - SHA-1 empty string and `abc` vectors;
  - multi-block MD5 and SHA-1 vectors;
  - multi-input SHA-1 and MD5 concatenation behavior;
  - context init/input/result behavior.
- Run the existing `libmylite` open/close smoke.
- Run the grouped compatibility harness.
- Confirm `ldd` for `mylite-open-close-smoke` no longer lists
  `libcrypto.so.3`.
- Confirm `llvm-nm --undefined-only` no longer reports OpenSSL `CRYPTO_`,
  `EVP_`, or `SHA1_` symbols in the linked smoke.
- Record archive and stripped linked runtime sizes in
  `docs/research/production-size-analysis.md`.

## Acceptance Criteria

- The minsize profile builds successfully.
- Digest vector smoke passes.
- Existing open/close and compatibility harness checks pass.
- The linked smoke runtime has no `libcrypto.so.3` dependency.
- Production-size analysis records the measured delta and dependency status.

## Risks and Unresolved Questions

- MD5 and SHA-1 are legacy algorithms. This slice keeps them only because
  inherited MariaDB internals still require exact digest compatibility.
- A future profile that re-enables TLS, AES, SQL crypto, SHA-2 SQL functions, or
  OpenSSL-backed authentication plugins must not enable this option without
  either restoring OpenSSL wrappers or adding separately reviewed replacements.
