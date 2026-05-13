# Auth Protocol Size Profile

## Problem Statement

The aggressive MyLite minsize profile still links MariaDB's network login
protocol helpers even though the product API opens an in-process `.mylite` file
and does not expose a daemon socket, remote client, or password handshake.

This is distinct from SQL authorization semantics. The profile already builds
with `NO_EMBEDDED_ACCESS_CHECKS`, and MyLite's local embedded connection path
sets a full-rights `Security_context` directly. The remaining target is the
client/server authentication protocol shell that is retained for inherited
`libmysqld` compatibility.

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/include/my_global.h` defines
  `NO_EMBEDDED_ACCESS_CHECKS` for `EMBEDDED_LIBRARY` builds.
- `vendor/mariadb/server/libmysqld/lib_sql.cc` has a
  `NO_EMBEDDED_ACCESS_CHECKS` `check_embedded_connection()` implementation
  that does not call `acl_authenticate()`. It initializes charset state, fills
  the local `Security_context`, grants `GLOBAL_ACLS`, and optionally changes
  database.
- The non-`NO_EMBEDDED_ACCESS_CHECKS` fallback in the same file emulates a
  `COM_CHANGE_USER` packet and calls `acl_authenticate()`, but that path is not
  compiled for MyLite's embedded minsize build.
- `vendor/mariadb/server/sql/sql_parse.cc` still retains `COM_CHANGE_USER`
  dispatch inside the general command dispatcher. That path calls
  `acl_authenticate()` and carries save/restore logic for a network user change.
- `vendor/mariadb/server/sql/sql_acl.cc` implements `acl_authenticate()`, the
  native and old password authentication plugins, plugin VIO handshake helpers,
  and the `builtin_maria_mysql_password_plugin` declaration.
- `vendor/mariadb/server/sql/sql_builtin.cc.in` hard-codes
  `builtin_maria_mysql_password_plugin` into mandatory built-ins.

MariaDB documentation context:

- The Embedded MariaDB Interface documentation describes `libmysqld` as using
  the same C API shape as the normal client library and linking with
  `libmysqld` instead of `libmysqlclient`:
  https://mariadb.com/kb/en/embedded-mariadb-interface/
- The Connector/C `mysql_real_connect()` documentation describes host, user,
  password, port, socket, and authentication-oriented connection parameters:
  https://mariadb.com/kb/en/mysql_real_connect/

## Scope

This slice may:

- add `MYLITE_DISABLE_AUTH_PROTOCOL`,
- enable it only in `tools/build-mariadb-minsize.sh`,
- omit `builtin_maria_mysql_password_plugin` from mandatory embedded built-ins,
- compile `acl_authenticate()` to a tiny unsupported stub in the embedded
  minsize profile,
- reject `COM_CHANGE_USER` directly in the embedded minsize dispatcher, and
- assert that network authentication protocol symbols are absent from the
  linked open-close smoke.

## Non-Goals

This slice does not:

- remove SQL parsing for user names, definers, roles, or grants,
- remove `CURRENT_USER()`, `USER()`, `CURRENT_ROLE()`, or definer handling,
- remove SQL password/hash functions,
- remove account-management grammar,
- change MyLite public API or `.mylite` file format,
- change non-minsize embedded builds, or
- make broader account-management compatibility decisions.

## Proposed Design

Add `MYLITE_DISABLE_AUTH_PROTOCOL` in
`vendor/mariadb/server/libmysqld/CMakeLists.txt` and forward it as a compile
definition for the embedded target.

When enabled:

- `sql_builtin.cc.in` keeps the extern declaration valid for ordinary builds
  but does not add `builtin_maria_mysql_password_plugin` to
  `mysql_mandatory_plugins` when access checks are already disabled.
- `sql_acl.cc` defines `MYLITE_AUTH_PROTOCOL_OMITTED` only when the new macro,
  `EMBEDDED_LIBRARY`, and `NO_EMBEDDED_ACCESS_CHECKS` are all true. Under that
  combined guard, `acl_authenticate()` reports `ER_UNKNOWN_COM_ERROR` and
  returns failure. The native/old password plugin descriptors and handshake
  implementation are not compiled.
- `sql_parse.cc` handles `COM_CHANGE_USER` with a direct unsupported-command
  error under the same minsize macro, avoiding the network user-change
  save/restore path.

## Affected Subsystems

- Embedded minsize build configuration.
- Built-in plugin list for the embedded target.
- Network authentication protocol code in `sql_acl.cc`.
- `COM_CHANGE_USER` dispatch in `sql_parse.cc`.
- Open-close smoke symbol checks.

## Single-File And Embedded-Lifecycle Impact

No file ownership, storage, lock, recovery, or catalog behavior changes. The
slice removes code for a daemon-style login protocol that MyLite does not
expose.

## Public API Or File-Format Impact

No public `libmylite` API change and no `.mylite` file-format change.

Compatibility impact is limited to the aggressive minsize build: inherited
embedded C API callers cannot use `COM_CHANGE_USER` or built-in server password
plugin negotiation. MyLite's local open path is unchanged.

## Binary-Size Impact

The linked savings are small because the minsize profile already avoids server
account-table loading and because SQL password functions are separate from the
network authentication plugin.

Implemented measurements against the preceding
`embedded-default-files-size-profile` baseline:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 25,391,134 | 25,371,140 | -19,994 |
| unstripped `mylite-open-close-smoke` | 6,479,480 | 6,468,904 | -10,576 |
| stripped `mylite-open-close-smoke` | 4,546,760 | 4,538,104 | -8,656 |

`llvm-size` total for the linked open-close smoke changed from 4,769,232 to
4,760,574 bytes (-8,658). The linked smoke no longer contains
`builtin_maria_mysql_password_plugin`, native/old auth handlers,
`do_auth_once`, server handshake packet helpers, or `server_mpvio_*` helpers.

## License, Trademark, And Dependency Impact

No new dependency or license impact. This is a GPL-2.0-only MariaDB-derived
build-profile change.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-auth-protocol \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-auth-protocol \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-auth-protocol \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-auth-protocol \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-auth-protocol \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh
git diff --check
```

Measure:

- archive bytes,
- unstripped and stripped linked open-close smoke bytes,
- section profile changes, and
- absence or retention of the targeted authentication protocol symbols.

## Acceptance Criteria

- Passed: the minsize build succeeds with `MYLITE_DISABLE_AUTH_PROTOCOL=ON`.
- Passed: embedded bootstrap, open/close, storage-engine, and compatibility smokes pass.
- Passed: local `libmylite` open/close behavior is unchanged.
- Passed: the linked open-close smoke no longer contains password-auth plugin
  descriptors or plugin VIO handshake helpers.
- Passed: size results are recorded here and in
  `docs/research/production-size-analysis.md`.

## Verification

Passed:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-auth-protocol \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-auth-protocol \
  MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-auth-protocol \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-auth-protocol \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-auth-protocol \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh \
  tools/run-embedded-bootstrap-smoke.sh tools/run-storage-engine-smoke.sh \
  tools/run-compatibility-test-harness.sh
git diff --check
```

## Risks And Unresolved Questions

- This should remain an aggressive-profile change until MyLite has a final
  compatibility policy for the inherited embedded C API.
- The likely linked size win is small. The value is mostly removing a
  server-only compatibility root before larger direct-dispatch work.
