# Error Message Size Profile

## Problem

The aggressive minsize profile still links MariaDB's full generated English
server error-message catalog through `sql/derror.cc`. In the current
`build/mariadb-minsize-no-stored-program-runtime` measurement,
`derror.cc.o` contributes about 108 KiB of message text and about 31 KiB of
`english_msgs` metadata before relocation overhead. That is real linked data in
the runtime artifact, not just unused archive payload.

MyLite needs stable diagnostics for common SQL, public API, and deliberately
unsupported embedded features, but a local embedded library does not need the
full server prose catalog for every daemon, replication, and rare SQL error in
the most aggressive size attempt.

## Source Findings

- `vendor/mariadb/server/sql/derror.cc` initializes server error messages in
  `init_errmessage()`. For English it includes the generated
  `mysqld_ername.h` table and copies message pointers into
  `DEFAULT_ERRMSGS`.
- `vendor/mariadb/server/sql/unireg.h` defines `ER_DEFAULT()` and
  `ER_THD()` as direct indexed lookups into the per-range message arrays. A
  compact profile must keep every registered slot non-null because not every
  caller goes through `my_error()`.
- `vendor/mariadb/server/mysys/my_error.c` tolerates missing registered
  messages by reporting `Unknown error %d`, but direct `ER_THD()` callers do
  not get that protection.
- `vendor/mariadb/server/extra/CMakeLists.txt` uses `comp_err` to generate
  `mysqld_error.h`, `mysqld_ername.h`, `sql_state.h`, and build-tree
  `errmsg.sys` files from `sql/share/errmsg-utf8.txt`.
- The generated `mysqld_error.h` and `sql_state.h` define numeric MariaDB
  errors and SQLSTATE mapping. This slice should not change either one.
- The current smokes assert message fragments for common public diagnostics:
  unsupported minsize features, duplicate-key errors, read-only errors,
  missing functions, unknown storage engines, unknown collations/locales/time
  zones, and stored-program runtime rejection.

## Design

Add an aggressive embedded-only CMake option,
`MYLITE_DISABLE_FULL_ERROR_MESSAGES`, enabled by `tools/build-mariadb-minsize.sh`.
When set, `derror.cc` will build a compact server catalog without including the
full `mysqld_ername.h` generated table.

The compact catalog will:

- keep `ER_ERROR_FIRST`, `ER_ERROR_LAST`, range registration, SQLSTATE, and
  numeric MariaDB errno behavior unchanged;
- allocate the same per-range message pointer layout at runtime;
- initialize every slot in every registered range to a static generic
  no-placeholder message, so extra varargs passed to `my_error()` are ignored
  safely and direct `ER_THD()` lookups remain non-null;
- override selected common errors with their original generated MariaDB format
  strings where tests and public diagnostics depend on meaningful text or
  placeholders;
- keep mysys file/OS error messages meaningful by retaining the server messages
  used by `init_myfunc_errs()`;
- continue to call `my_error_register()` and `init_myfunc_errs()` through the
  existing MariaDB path.

The normal non-minsize path remains unchanged and still includes the full
generated message catalog.

## Affected Subsystems

- `sql/derror.cc`: embedded catalog initialization.
- `libmysqld/CMakeLists.txt`: embedded minsize option and compile definition.
- `tools/build-mariadb-minsize.sh`: aggressive profile enablement.
- `mylite/open_close_smoke.cc`: direct regression checks for retained compact
  message behavior.

## Single-File and Embedded Lifecycle

This slice only changes linked diagnostic strings. It does not affect storage,
catalog files, temporary files, recovery, locks, or runtime ownership.

## Public API Impact

`libmylite` errno and SQLSTATE accessors remain unchanged. Error message text
for common errors remains descriptive. Rare server errors in the aggressive
minsize profile may return the generic compact message instead of MariaDB's full
English prose. The normal profile keeps full messages.

## Binary-Size Impact

Expected linked-size savings are bounded by the current `derror.cc.o`
`mysqld_ername.h` payload: roughly 108 KiB of strings plus roughly 31 KiB of
metadata, offset by a small retained compact table. The static archive should
also shrink by the same object payload scale.

## License, Trademark, and Dependencies

No new dependency or license change. The retained messages are still
MariaDB-derived GPL-2.0-only source.

## Tests

- Build the minsize profile in a fresh build directory.
- Run:
  - `tools/run-libmylite-open-close-smoke.sh`
  - `tools/run-storage-engine-smoke.sh`
  - `tools/run-embedded-bootstrap-smoke.sh`
  - `tools/run-compatibility-test-harness.sh`
- Add direct smoke coverage that verifies:
  - duplicate-key text still contains the duplicated value;
  - unsupported-feature diagnostics still include the feature name;
  - unknown engine/collation/locale/time-zone diagnostics still include the
    user-provided name;
  - compact fallback text is available through a rare registered error slot.

## Acceptance Criteria

- The aggressive minsize build passes the existing smokes and compatibility
  harness.
- `libmysqld/libmariadbd.a` and stripped `mylite-open-close-smoke` shrink
  measurably from the stored-program-runtime baseline.
- Numeric error codes and SQLSTATE mappings remain unchanged.
- Common public diagnostics keep actionable message text.
- The normal non-compact error-message path remains unchanged.

## Risks

- Some rarely tested error path may depend on exact message prose for a direct
  string comparison.
- Generic fallback text is less useful for rare server errors. This is only
  acceptable in the most aggressive size profile.
- The retained-message list must grow when new smoke-covered unsupported
  features depend on message fragments.

## Implementation Results

Implemented `MYLITE_DISABLE_FULL_ERROR_MESSAGES` in the embedded library
build. In this profile, `sql/derror.cc` allocates the same registered error
ranges as MariaDB, initializes every slot with a generic no-placeholder
fallback, and restores the original format strings for common MyLite-facing
diagnostics.

Measured against `build/mariadb-minsize-no-stored-program-runtime`:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 26,682,446 | 26,484,414 | -198,032 |
| `sql/derror.cc.o` | 216,392 | 18,480 | -197,912 |
| unstripped `mylite-open-close-smoke` | 7,101,072 | 6,965,448 | -135,624 |
| stripped `mylite-open-close-smoke` | 5,074,696 | 4,938,992 | -135,704 |

The linked smoke section profile changed from `text=4,016,597`,
`data=1,054,928`, `bss=226,265`, `total=5,297,790` to
`text=3,911,269`, `data=1,024,504`, `bss=226,801`, `total=5,162,574`.

Verification commands:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-compact-errors MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-compact-errors tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-compact-errors tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-compact-errors tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-compact-errors tools/run-compatibility-test-harness.sh
```
