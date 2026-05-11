# aria-startup-sidecars

## Problem Statement

Every MyLite runtime smoke still creates inherited Aria startup files under
the controlled datadir:

```text
aria_log.00000001
aria_log_control
```

The compatibility harness currently classifies these files as known inherited
sidecars, not MyLite-owned companions. That is useful for tracking debt, but it
is incompatible with MyLite's final single-file invariant: persistent
`aria_log.*` files are independent MariaDB engine state, not part of the
`.mylite` file lifecycle. The next slice should remove those files from MyLite
runtime smokes by omitting Aria from the default embedded profile if the
remaining supported MyLite subset can run without it.

## Scope

- Make Aria optional for MyLite's embedded minsize build profile.
- Disable Aria in `tools/build-mariadb-minsize.sh`.
- Keep non-MyLite upstream-style builds defaulting to MariaDB's existing Aria
  behavior unless they opt into the MyLite profile flag.
- Disable Aria-backed internal temporary tables in the MyLite minsize profile
  so the SQL layer falls back to the already-built MyISAM path.
- Change the compatibility reference smoke from `ENGINE=Aria` to
  `ENGINE=MyISAM`, because Aria is no longer part of the default profile.
- Remove the MyLite runtime sidecar-scan exception for `aria_log.*` and
  `aria_log_control`.
- Make bootstrap and compatibility reports prove that MyLite runtime
  directories no longer contain Aria log files.
- Update roadmap, architecture, and prior specs that documented the Aria
  startup sidecar debt.

## Non-Goals

- Do not remove MyISAM, HEAP, CSV, Sequence, or other mandatory MariaDB
  engines in this slice.
- Do not run a full production bundle-size optimization pass; record artifact
  sizes only as normal slice evidence.
- Do not support user-created Aria tables in MyLite.
- Do not emulate `aria_log.*` inside `.mylite`.
- Do not change MyLite row, index, catalog, transaction, lock, or public API
  formats.
- Do not import the MariaDB Test Run suite.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Official MariaDB Aria documentation says Aria is compiled in by default, is
  required to be in use when MariaDB starts, and is used for internal on-disk
  tables in MariaDB's normal server profile:
  <https://mariadb.com/docs/server/server-usage/storage-engines/aria/aria-storage-engine>.
- Official Aria FAQ documentation describes `aria_log_control` and
  `aria_log.*` files as Aria log/control files:
  <https://mariadb.com/docs/server/server-usage/storage-engines/aria/aria-faq>.
- Official Aria system-variable documentation describes
  `aria_used_for_temp_tables` as a read-only variable and notes that when it is
  off, MariaDB uses MyISAM for on-disk temporary tables:
  <https://mariadb.com/docs/server/server-usage/storage-engines/aria/aria-system-variables>.
- `storage/maria/CMakeLists.txt` currently calls
  `MYSQL_ADD_PLUGIN(aria ... STORAGE_ENGINE MANDATORY LINK_LIBRARIES myisam
  mysys mysys_ssl RECOMPILE_FOR_EMBEDDED)`. MariaDB's
  `cmake/plugin.cmake` ignores `-DPLUGIN_ARIA=NO` for mandatory plugins by
  clearing the cache entry and setting `PLUGIN_ARIA` to `YES`.
- `storage/maria/CMakeLists.txt` also defines `USE_ARIA_FOR_TMP_TABLES` on by
  default. `sql/sql_class.h` includes `<maria.h>` and sets the internal
  temporary table engine to Aria only when both `WITH_ARIA_STORAGE_ENGINE` and
  `USE_ARIA_FOR_TMP_TABLES` are present; otherwise it uses MyISAM.
- `storage/maria/ha_maria.cc:ha_maria_init()` calls `maria_init()`,
  `ma_control_file_open()`, `translog_init()`, recovery, and checkpoint
  initialization during plugin startup. That path is responsible for opening
  the Aria control file and transaction logs.
- `storage/maria/ma_loghandler.c:translog_filename_by_fileno()` constructs
  `aria_log.0000000N` names from `log_descriptor.directory`.
- `storage/maria/ha_maria.cc` exposes `aria_log_dir_path`, but redirecting the
  files would still leave independent Aria log state outside `.mylite`.
- `sql/mysqld.cc` initializes `default_storage_engine` to `MyISAM` when InnoDB
  is not compiled. The current MyLite minsize profile already disables InnoDB,
  so Aria is not needed as the default user-table engine.
- `tools/run-compatibility-test-harness.sh` currently uses `ENGINE=Aria` for
  the MariaDB reference comparison and scans MyLite runtime directories with a
  temporary exception for Aria startup logs.
- Current measured evidence before this slice:
  `mylite-embedded-bootstrap-report.txt` records
  `datadir/aria_log.00000001` at 16,384 bytes and
  `datadir/aria_log_control` at 52 bytes, and the compatibility harness report
  records those files across every MyLite runtime group as known inherited
  sidecars.

## Proposed Design

Add a MyLite-owned CMake option in `storage/maria/CMakeLists.txt`:

```cmake
OPTION(MYLITE_DISABLE_ARIA "Disable Aria in the MyLite embedded profile" OFF)
```

When the option is off, keep MariaDB's current Aria plugin definition,
utilities, and `USE_ARIA_FOR_TMP_TABLES` default. When the option is on:

- set `PLUGIN_ARIA=NO` for report/cache clarity,
- set `PLUGIN_S3=NO`, because the adjacent S3 copy utility links the omitted
  Aria target and S3 is outside the MyLite embedded profile,
- force `USE_ARIA_FOR_TMP_TABLES=OFF`,
- do not define the `aria` storage-engine target,
- do not define Aria utility executable targets that link against the omitted
  target.

This keeps the fork delta narrow and explicit: upstream-derived defaults remain
unchanged unless the MyLite minsize build asks for the embedded-only omission.

Update `tools/build-mariadb-minsize.sh` to pass:

```sh
-DMYLITE_DISABLE_ARIA=ON
-DUSE_ARIA_FOR_TMP_TABLES=OFF
-DPLUGIN_ARIA=NO
-DPLUGIN_S3=NO
```

`PLUGIN_S3=NO` prevents S3 helper targets from depending on Aria in the MyLite
profile. S3 is not part of the current embedded built-in plugin set and is
outside the MyLite single-file storage target.

Change the reference comparison engine in
`tools/run-compatibility-test-harness.sh` to `MyISAM`. MyISAM remains mandatory
in this MariaDB source tree and is already initialized first by
`sql/sql_plugin.cc`. Its `.MYD`/`.MYI` files stay confined to the isolated
reference runtime and are not treated as MyLite companions.

After Aria is removed from the default MyLite profile, remove
`is_known_inherited_sidecar()`'s Aria exception. Aria log/control files should
be classified as unexpected sidecars if they appear in any MyLite runtime
directory.

## Affected Subsystems

- MariaDB CMake plugin configuration for `storage/maria`.
- MyLite minsize build script and build report cache evidence.
- Compatibility harness reference engine and sidecar classifier.
- Embedded bootstrap and compatibility reports.
- Roadmap, single-file storage design, and older specs that named Aria logs as
  inherited runtime debt.

## DDL Metadata Routing Impact

None for MyLite tables. This slice does not change `.frm` interception,
catalog-backed table discovery, schema catalog records, or table definition
storage. Reference MyISAM runs may create `.frm`/`.MYD`/`.MYI` files in their
isolated reference runtime, which is outside MyLite runtime sidecar scanning.

## Single-File And Embedded-Lifecycle Implications

Removing Aria from MyLite runtime startup removes the last currently known
MariaDB engine log sidecars from MyLite runtime directories. The primary file
remains the `.mylite` file, and no new MyLite-owned companion file is added.

This also reduces startup work by avoiding Aria control-file, translog,
recovery, and checkpoint initialization. If future SQL features need durable
temporary spill, they should use a MyLite-owned temporary or pager design rather
than reintroducing Aria logs.

## Public API Or File-Format Impact

No public `libmylite` API change. No `.mylite` file-format change.

## Binary-Size Impact

The expected direction is a smaller `libmariadbd.a`, because the Aria storage
engine target and its embedded plugin registration will be omitted from the
default MyLite profile. The slice must record measured post-implementation
artifact sizes, but the full bundle-size decision analysis remains deferred
until implementation work reaches the user's requested later phase.

## License, Trademark, And Dependency Impact

No new dependency. The changes remain GPL-2.0-only within MariaDB-derived build
files and first-party MyLite scripts/docs.

## Test And Verification Plan

- Run `git diff --check`.
- Run `bash -n tools/build-mariadb-minsize.sh
  tools/run-embedded-bootstrap-smoke.sh tools/run-compatibility-test-harness.sh`.
- Run `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`.
- Run `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`.
- Verify `mylite-build-report.txt` records `WITH_ARIA_STORAGE_ENGINE=OFF` and
  no longer lists `builtin_maria_aria_plugin`.
- Verify `mylite-embedded-bootstrap-report.txt` no longer lists
  `aria_log.00000001` or `aria_log_control`.
- Verify `mylite-compatibility-harness-report.txt` reports
  `unexpected_sidecars=none` and `known_inherited_sidecars=none` for MyLite
  runtime directories.
- Verify the `mariadb_comparison` group still reports `status=0` using the
  MyISAM reference engine.
- Record artifact sizes after the passing build.

## Acceptance Criteria

- Aria is omitted from the default MyLite minsize embedded build profile.
- MyLite embedded startup and compatibility smokes create no Aria log/control
  files in MyLite runtime directories.
- The sidecar scan no longer has an Aria exception.
- The grouped compatibility harness still passes.
- Non-MyLite builds keep MariaDB's default Aria behavior unless they explicitly
  enable the MyLite disable option.
- Documentation no longer describes Aria startup logs as current MyLite runtime
  debt after implementation.

## Risks And Unresolved Questions

- Some MariaDB SQL paths may assume Aria for internal on-disk temporary tables.
  Source review shows MyISAM fallback exists when `USE_ARIA_FOR_TMP_TABLES` is
  unset, but the compatibility harness must verify the supported subset.
- MyISAM remains in the build and still brings external table-file behavior for
  reference runs. That is acceptable only while MyISAM is not exposed as a
  MyLite-owned durable user-table engine.
- Removing Aria can expose latent assumptions in startup plugins or system
  variables. The first implementation should keep the CMake option scoped to
  the MyLite profile so fallback is clear if verification finds such an
  assumption.

## Implementation Result

The default MyLite minsize profile now passes `MYLITE_DISABLE_ARIA=ON`,
`PLUGIN_ARIA=NO`, `USE_ARIA_FOR_TMP_TABLES=OFF`, and `PLUGIN_S3=NO`.
`storage/maria/CMakeLists.txt` keeps MariaDB's default Aria behavior unless
that MyLite option is enabled; when enabled, it skips the Aria engine and Aria
utility targets and clears the Aria and S3 cache definitions.

The top-level MariaDB CMake file now clears stale `EMBEDDED_PLUGIN_LIBS` on
reconfigure, matching the existing static plugin cache reset. It also adds
Aria utility dependencies to `minbuild` only when the utility targets exist.

Disabling Aria exposed two upstream fallback assumptions:

- The MyISAM internal temporary-table branch in `sql/sql_select.cc` referenced
  newer Aria-only `TABLE_SHARE` fields and an Aria-scoped `keyinfo` variable.
  That branch now uses the passed key metadata directly and tracks MyISAM
  unique constraints locally.
- `sql/handler.h` and `sql/log.cc` referenced `maria_hton` without
  `WITH_ARIA_STORAGE_ENGINE` guards. They now keep Aria-specific behavior under
  the existing storage-engine define and use the generic transaction-manager
  path otherwise.

The compatibility harness now uses `ENGINE=MyISAM` as its isolated MariaDB
reference engine. MyLite runtime sidecar scanning now treats
`aria_log.*` and `aria_log_control` as unexpected sidecars and reports no known
inherited sidecars.

Verification completed:

- `bash -n tools/build-mariadb-minsize.sh
  tools/run-embedded-bootstrap-smoke.sh tools/run-compatibility-test-harness.sh`.
- `git diff --check`.
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`.
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`.

Report evidence:

- `mylite-build-report.txt`: `MYLITE_DISABLE_ARIA=ON`,
  `PLUGIN_ARIA=NO`, `USE_ARIA_FOR_TMP_TABLES=OFF`, and
  `WITH_ARIA_STORAGE_ENGINE=OFF`; no `builtin_maria_aria_plugin` entry.
- `mylite-embedded-bootstrap-report.txt`: `status=0`,
  `mysql_servers_startup=absent`, `aria_startup_sidecars=absent`,
  `Observed Runtime Files=none`, and `Dynamic Plugin Artifacts=none`.
- `mylite-compatibility-reference-report.txt`: `engine=MyISAM`.
- `mylite-compatibility-harness-report.txt`: all groups report `status=0`;
  `sidecar_scan` reports `unexpected_sidecars=none` and
  `known_inherited_sidecars=none`.

Measured artifacts after implementation:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 43,407,882 bytes.
- `build/mariadb-minsize/mylite/libmylite.a`: 87,206 bytes.
- `build/mariadb-minsize/storage/mylite/libmylite_embedded.a`: 305,932 bytes.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,247,744
  bytes.
- `build/mariadb-minsize/mylite/mylite-compatibility-smoke`: 22,248,816
  bytes.
- `build/mariadb-minsize/mylite/mylite-storage-engine-smoke`: 22,314,848
  bytes.
