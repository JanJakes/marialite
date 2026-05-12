# MyISAM Admin Size Profile

## Problem

The default aggressive minsize profile must keep MyISAM available internally
because MariaDB still uses it for disk temporary tables. Removing MyISAM
entirely saves size but breaks schema-table metadata queries such as
`SHOW COLUMNS`.

The retained MyISAM archive still includes admin-only check and repair code
that user tables cannot normally reach in the MyLite profile because
`ENGINE=MyISAM` is hidden from user selection.

Current measured MyISAM admin component:

| Artifact | Bytes |
| --- | ---: |
| `storage/myisam/libmyisam_embedded.a:mi_check.c.o` | 94,088 |
| merged `libmysqld/libmariadbd.a:mi_check.c.o` | 93,072 |

The linked smoke currently retains `mi_repair`, `mi_repair_by_sort`,
`mi_repair_parallel`, `chk_key`, and `chk_data_link` symbols.

## Source Findings

MariaDB source references are from imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `storage/myisam/CMakeLists.txt` includes `mi_check.c` in the mandatory
  MyISAM plugin source list.
- `storage/myisam/ha_myisam.cc` calls `mi_repair`,
  `mi_repair_by_sort`, and `mi_repair_parallel` from MyISAM admin methods:
  `check()`, `analyze()`, `repair()`, `optimize()`,
  `check_and_repair()`, and persistent `enable_indexes()`.
- Normal indexed temp-table reads do not need `mi_check.c`: `_mi_check_index`
  is defined in `mi_search.c`, and `mi_check_index_tuple_real` is defined in
  `mi_key.c`.
- `ha_myisam::start_bulk_insert()` can disable indexes and later recreate them
  through the repair path. In the admin-disabled profile, it must avoid that
  optimization so internal temporary tables never need repair code to finish a
  bulk insert.

## Scope

Add a `MYLITE_DISABLE_MYISAM_ADMIN` minsize option that:

- omits `mi_check.c` from the embedded MyISAM build,
- returns `HA_ADMIN_NOT_IMPLEMENTED` for MyISAM check/analyze/repair/optimize
  admin operations,
- disables MyISAM automatic crash repair in this profile,
- avoids bulk-insert index disabling that would require repair to rebuild
  indexes, and
- preserves normal MyISAM temporary-table create/open/read/write/delete paths.

## Non-Goals

This slice does not remove MyISAM itself, MyISAM full-text code, or the
handlerton needed by MariaDB's inherited disk temporary-table path.

This slice does not change user `ENGINE=MyISAM` behavior; the existing legacy
storage-engine profile already rejects user-created MyISAM tables.

## Binary-Size Impact

The static archive should drop roughly 90 KiB if `mi_check.c.o` is omitted.
The linked runtime impact should be smaller because section GC already keeps
only the live repair/check symbols from that object.

## Test and Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-myisam-admin MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-myisam-admin MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-myisam-admin MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
git diff --check
bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh tools/run-compatibility-test-harness.sh
```

Measure:

- `libmysqld/libmariadbd.a` bytes and object count,
- stripped `mylite-open-close-smoke` bytes,
- absence of `mi_check.c.o` from the merged archive, and
- absence of `mi_repair`, `mi_repair_by_sort`, `mi_repair_parallel`,
  `chk_key`, and `chk_data_link` from the linked smoke.

## Acceptance Criteria

- Default minsize build links without `mi_check.c.o`.
- Open/close smoke and full compatibility harness pass.
- User `ENGINE=MyISAM` remains explicitly unavailable.
- Disk temporary-table execution still works for schema-table metadata and
  compatibility smokes.
- Size deltas are recorded in `docs/research/production-size-analysis.md`.

## Risks

- A hidden temp-table path could still depend on persistent MyISAM index
  rebuild. The implementation must keep indexes active instead of disabling
  them when admin repair code is omitted.
- MariaDB startup could still try automatic MyISAM repair if
  `myisam_recover_options` remains active. The minsize profile should force
  auto-repair off.
