# MyISAM RTREE Size Profile

## Problem

The aggressive MyLite minsize profile still keeps MyISAM internally because
MariaDB uses it for inherited disk temporary tables. User-created
`ENGINE=MyISAM` tables are already hidden, and MyLite tables already reject
SPATIAL indexes. The retained MyISAM build still includes RTREE and spatial-key
implementation code that ordinary MyISAM temporary-table spill should not need.

Current measured MyISAM RTREE/spatial component object sizes from
`build/mariadb-minsize-myisam-fulltext/storage/myisam/CMakeFiles/myisam_embedded.dir`:

| Object | Bytes |
| --- | ---: |
| `rt_mbr.c.o` | 15,432 |
| `rt_index.c.o` | 14,784 |
| `sp_key.c.o` | 4,888 |
| `rt_split.c.o` | 4,800 |
| `rt_key.c.o` | 2,920 |
| Total | 42,824 |

The linked open/close smoke still contains live symbols such as
`rtree_find_first`, `rtree_get_next`, `rtree_insert`, `rtree_delete`,
`rtree_estimate`, `rtree_key_cmp`, `rtree_split_page`, and `sp_make_key`.

## Source Findings

MariaDB source references are from imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `storage/myisam/CMakeLists.txt` includes `rt_index.c`, `rt_key.c`,
  `rt_mbr.c`, `rt_split.c`, and `sp_key.c` in the mandatory MyISAM plugin
  source list.
- `storage/myisam/ha_myisam.cc` advertises `HA_CAN_RTREEKEYS`, maps table
  key metadata with `HA_SPATIAL_legacy`, gives RTREE indexes special
  `index_flags()`, and avoids disabling RTREE indexes during bulk insert.
- `storage/myisam/mi_create.c` rewrites RTREE key definitions into MyISAM's
  expanded spatial key-segment layout and persists those generated key
  segments.
- `storage/myisam/mi_open.c` detects RTREE keys from existing MyISAM index
  files, allocates `rtree_recursion_state`, rewrites RTREE key segments after
  reading metadata, and installs `rtree_insert`/`rtree_delete` callbacks.
- `storage/myisam/mi_key.c` routes RTREE key creation through `sp_make_key()`
  and expands RTREE key-part maps.
- `storage/myisam/mi_rkey.c`, `mi_rnext.c`, `mi_rnext_same.c`, and
  `mi_range.c` call RTREE search and estimate helpers from generic MyISAM read
  paths.
- `sql/mysqld.cc` initializes the global `have_rtree_keys` capability to
  `YES`, and `sql/sys_vars.cc` exposes that capability through
  `@@have_rtree_keys`.
- Existing MyLite DDL coverage rejects SPATIAL indexes and GEOMETRY storage for
  MyLite tables, and the legacy-storage-engine profile already rejects
  explicit user `ENGINE=MyISAM`.

## Scope

Add a `MYLITE_DISABLE_MYISAM_RTREE` minsize option that:

- omits MyISAM RTREE/spatial-key implementation sources from the embedded
  MyISAM build,
- stops advertising RTREE support from the MyISAM handlerton,
- rejects accidental MyISAM RTREE/SPATIAL key creation in the disabled profile,
- rejects pre-existing MyISAM RTREE keys if such a table is opened in this
  disabled profile,
- compiles generic MyISAM read/write/index paths so they do not reference
  removed RTREE helpers, and
- reports `@@have_rtree_keys=NO` in the disabled profile.

## Non-Goals

This slice does not remove MyISAM itself or ordinary MyISAM BTREE support.
MyISAM remains linked for MariaDB's inherited disk temporary-table path.

This slice does not remove SQL grammar for `SPATIAL` or `USING RTREE`, and it
does not change MyLite's existing GEOMETRY/SPATIAL rejection behavior.

This slice does not remove SQL-layer geometry parser or function code. GIS SQL
functions were handled separately by `gis-function-size-profile`.

## Binary-Size Impact

The static archive should drop at least the removed RTREE/spatial object code,
roughly 40 to 45 KiB before archive metadata effects. Linked runtime impact may
be lower because only paths reachable from the open/close smoke are counted, but
the current linked smoke has live RTREE symbols that should disappear.

## DDL Metadata Routing Impact

No MyLite catalog-format change. User SPATIAL DDL for MyLite tables is already
rejected before table-definition persistence. If a hidden MyISAM path somehow
requests an RTREE key in this profile, it must fail clearly rather than creating
`.MYD`/`.MYI` files that depend on omitted RTREE implementation code.

## Single-File And Embedded-Lifecycle Implications

Removing MyISAM RTREE support reduces inherited non-MyLite table behavior
inside the embedded runtime. It does not change the current temporary spill
allowance: MyISAM remains present only for inherited temporary tables under
MyLite-controlled runtime directories.

Reporting `have_rtree_keys=NO` makes the embedded runtime capability surface
match the compiled feature set.

## Public API Or File-Format Impact

No public `libmylite` API change and no `.mylite` file-format change.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-myisam-rtree MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-myisam-rtree MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-myisam-rtree MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
git diff --check
bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh tools/run-compatibility-test-harness.sh
```

Measure:

- `libmysqld/libmariadbd.a` bytes and object count,
- `storage/myisam/libmyisam_embedded.a` bytes and object count,
- stripped `mylite-open-close-smoke` bytes,
- absence of `rt_*.c.o` and `sp_key.c.o` members from the merged archive, and
- absence of live `rtree_*` and `sp_make_key` symbols from the linked smoke.

## Acceptance Criteria

- Default minsize build links without MyISAM RTREE/spatial source objects.
- Open/close smoke and full compatibility harness pass.
- User `ENGINE=MyISAM` remains explicitly unavailable.
- MyLite SPATIAL index DDL remains rejected.
- Disk temporary-table execution still works for schema-table metadata and
  compatibility smokes.
- Size deltas are recorded in `docs/research/production-size-analysis.md`.

## Risks

- A hidden MyISAM temporary-table path should not request RTREE indexes. If it
  does, the disabled profile must fail clearly rather than linking RTREE code
  back in.
- SQL-layer RTREE and SPATIAL syntax remains compiled. That is acceptable for
  this bounded slice, but future geometry support will need a separate index
  design rather than depending on hidden MyISAM RTREE code.
