# Legacy Storage Engine Size Profile

## Problem Statement

The MyLite minsize profile still exposes MariaDB's `CSV`, `MyISAM`, and
`MRG_MyISAM` storage engines as user-selectable table engines. These engines
create engine-specific table files outside MyLite's primary `.mylite` file
model and are not part of the target embedded single-file product shape.

This slice gates those durable legacy table engines out of the user-selectable
MyLite minsize profile and verifies that explicit attempts to create such
tables fail through MariaDB's unknown-storage-engine path.

The slice intentionally does not remove the `HEAP`/`MEMORY` engine. `HEAP` is
in-memory, does not create durable table sidecars, and is referenced by SQL
optimizer/temp-table code paths. It can be evaluated separately only after temp
table behavior is covered more deeply.

## Source Findings

MariaDB source references:

- `vendor/mariadb/server/cmake/plugin.cmake` treats `MYSQL_ADD_PLUGIN(... 
  MANDATORY)` as unconditional by clearing `PLUGIN_<NAME>` and setting it to
  `YES`. Normal `PLUGIN_MYISAM=NO`-style cache flags cannot disable mandatory
  engines.
- `vendor/mariadb/server/storage/myisam/CMakeLists.txt` registers `myisam` with
  `MYSQL_ADD_PLUGIN(myisam ... STORAGE_ENGINE MANDATORY RECOMPILE_FOR_EMBEDDED)`.
- `vendor/mariadb/server/storage/myisammrg/CMakeLists.txt` registers
  `myisammrg` with `MYSQL_ADD_PLUGIN(myisammrg ... STORAGE_ENGINE MANDATORY
  RECOMPILE_FOR_EMBEDDED)`.
- `vendor/mariadb/server/storage/csv/CMakeLists.txt` registers `csv` with
  `MYSQL_ADD_PLUGIN(csv ... STORAGE_ENGINE MANDATORY)`.
- Before this slice, `build/mariadb-minsize/sql/sql_builtin.cc` included
  `builtin_maria_csv_plugin`, `builtin_maria_myisam_plugin`, and
  `builtin_maria_myisammrg_plugin` in `mysql_mandatory_plugins`.
- `vendor/mariadb/server/sql/handler.cc` and `vendor/mariadb/server/sql/table.cc`
  report `ER_UNKNOWN_STORAGE_ENGINE` when a named engine cannot be resolved.
- `vendor/mariadb/server/sql/sql_class.h` defines the inherited disk temporary
  table engine as `myisam_hton` when Aria temporary tables are disabled.
- `vendor/mariadb/server/sql/sql_select.cc` creates inherited disk temporary
  tables through MyISAM helper APIs in that profile.
- `vendor/mariadb/server/sql/handler.cc` makes storage engines with
  `HTON_NOT_USER_SELECTABLE` unavailable through name resolution.

MariaDB documentation references:

- MariaDB documents MyISAM as a storage engine and documents MERGE as a logical
  table over identical MyISAM tables:
  https://mariadb.com/docs/server/server-usage/storage-engines/myisam-storage-engine
  and https://mariadb.com/docs/server/server-usage/storage-engines/merge
- MariaDB documents CSV as storing table data in text files:
  https://mariadb.com/docs/server/server-usage/storage-engines/csv/csv-overview

Measured current archive members:

```text
legacy_storage_objects=77 bytes=751,336
```

Largest members in that group include:

| Object | Bytes |
| --- | ---: |
| `ha_myisam.cc.o` | 124,408 |
| `ha_tina.cc.o` | 94,496 |
| `ha_myisammrg.cc.o` | 89,712 |
| `mi_check.c.o` | 79,544 |
| `mi_open.c.o` | 25,552 |
| `ft_static.c.o` | 23,992 |

## Proposed Design

Add a MyLite-owned CMake option:

```text
MYLITE_DISABLE_LEGACY_STORAGE_ENGINES=ON
```

When enabled, the `csv` and `myisammrg` storage-engine CMake files return
before registering their mandatory plugins. Add the option before
`CONFIGURE_PLUGINS()` so all storage-engine subdirectories see a single cache
entry.

Keep MyISAM compiled and initialized because MariaDB still uses `myisam_hton`
for inherited internal disk temporary tables when Aria temporary tables are
disabled. In the MyLite minsize profile, mark the MyISAM handlerton with
`HTON_NOT_USER_SELECTABLE` so `ENGINE=MyISAM` resolves like an unavailable
storage engine for user DDL. Skip MyISAM command-line utilities in this profile.

Set MyLite's embedded runtime default storage engine to `MYLITE` and its
user-created temporary-table default to `MEMORY`.

Enable the option in `tools/build-mariadb-minsize.sh`.

Add an open/close smoke check that attempts:

- `CREATE TABLE ... ENGINE=CSV`
- `CREATE TABLE ... ENGINE=MyISAM`
- `CREATE TABLE ... ENGINE=MRG_MyISAM`

and expects `ER_UNKNOWN_STORAGE_ENGINE` with SQLSTATE `42000`.

## Affected MariaDB Subsystems

- Build/plugin registration: `cmake/plugin.cmake` generated built-in plugin
  lists should no longer include CSV or MRG_MyISAM in the MyLite minsize
  profile.
- MyISAM registration: MyISAM remains built in for inherited internal disk
  temporary tables but is not user-selectable.
- SQL DDL engine resolution: explicit legacy engine names should fail before
  table creation.
- Storage engines: durable CSV and MERGE table handlers are omitted; durable
  user-created MyISAM tables are blocked by name resolution.

## DDL Metadata Routing Impact

DDL that specifies these omitted engines must not route to MyLite catalog
metadata. It should fail during storage-engine resolution. No new table should
be created and no `.mylite` catalog entry should be written for the rejected
DDL.

## Single-File and Embedded-Lifecycle Impact

This moves the minsize profile closer to the MyLite product model because CSV,
MyISAM, and MERGE table files are no longer available as accidental
user-created durable sidecar storage under the embedded runtime.

Inherited MyISAM temporary spill remains available under MariaDB's temporary
directory lifecycle. Replacing that path with a MyLite-owned temporary spill
engine is separate storage-engine work.

Opening, closing, recovery, and the primary `.mylite` file format are otherwise
unchanged.

## Public API and File-Format Impact

No public `libmylite` C API or MyLite file-format change.

The SQL compatibility surface changes for the minsize profile: explicit
`ENGINE=CSV`, `ENGINE=MyISAM`, and `ENGINE=MRG_MyISAM` table creation becomes
unsupported.

## Binary-Size Impact

Expected archive reduction is limited to CSV, MRG_MyISAM, and MyISAM utilities
because MyISAM itself must stay linked for internal disk temporary tables.
Measured clean-build reduction after the RELR profile:

```text
libmariadbd.a: -252,074 bytes
stripped linked smoke: -33,440 bytes
```

## License, Trademark, and Dependency Impact

No new dependencies. The removed code is already MariaDB-derived GPL-2.0-only
code in the vendor tree; this is a build-profile gate, not a source import or
license change.

## Test and Verification Plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

Verify:

- `mylite-build-report.txt` records
  `MYLITE_DISABLE_LEGACY_STORAGE_ENGINES:BOOL=ON`.
- `libmariadbd.a` no longer defines `builtin_maria_csv_plugin` or
  `builtin_maria_myisammrg_plugin`.
- `libmariadbd.a` still defines `builtin_maria_heap_plugin`,
  `builtin_maria_myisam_plugin`, and `builtin_maria_mylite_plugin`.
- The open/close smoke report records unknown-engine diagnostics for CSV,
  MyISAM, and MRG_MyISAM.
- The compatibility harness still passes all groups and reports no unexpected
  sidecars.

## Acceptance Criteria

- The minsize profile omits CSV and MRG_MyISAM built-ins.
- MyISAM remains built in only for inherited internal temporary table paths and
  is not user-selectable.
- MyLite DDL attempts for the omitted engines fail explicitly and leave no
  MyLite table behind.
- Existing lifecycle, storage, and MariaDB comparison smokes pass.
- Production size analysis records the measured archive and linked-runtime
  impact.

## Risks and Unresolved Questions

- Some inherited MariaDB code still assumes MyISAM is available for internal
  temporary tables, maintenance, repair, or server-oriented metadata paths.
  This slice preserves the internal temporary-table path and blocks the user
  DDL path.
- `HEAP`/`MEMORY` remains built in. Removing it would be a separate higher-risk
  temp-table slice.
- This is a minsize compatibility tradeoff. A broader compatibility profile may
  choose to keep these engines available.
