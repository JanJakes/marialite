# System Versioning Size Profile

## Problem Statement

MariaDB's temporal table support includes system-versioned tables,
application-time periods, and bitemporal tables. MyLite does not yet have
catalog metadata, row history storage, update/delete semantics, or recovery
rules for those table shapes. The aggressive minsize profile still compiles
`item_vers.cc`, which implements query-time helper items for system-versioned
table predicates and transaction-id lookup.

MyLite should reject temporal table metadata explicitly and omit the retained
system-versioning item runtime from the aggressive embedded profile.

Current baseline after `diagnostics-statement-size-profile`:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 27,978,354 |
| `item_vers.cc.o` object | 178,432 |
| `item_vers.cc.o` `llvm-size` total | 26,791 |
| stripped `mylite-open-close-smoke` | 5,345,504 |

## Source Findings

- Imported MariaDB base: `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- MariaDB documents `WITH SYSTEM VERSIONING` as table history support with
  hidden row-start and row-end metadata:
  `https://mariadb.com/kb/en/system-versioned-tables/`.
- MariaDB documents application-time periods as table metadata declared with
  `PERIOD FOR name(start, end)`:
  `https://mariadb.com/kb/en/application-time-periods/`.
- `vendor/mariadb/server/sql/item_vers.cc` implements
  `Item_func_history`, `Item_func_trt_ts`, `Item_func_trt_id`, and
  `Item_func_trt_trx_sees`, which are used when MariaDB builds predicates for
  versioned table reads.
- `vendor/mariadb/server/sql/sql_select.cc:1030` constructs
  `Item_func_history`, `Item_func_trt_id`, and `Item_func_trt_trx_sees*` in
  `period_get_condition()`.
- `vendor/mariadb/server/sql/handler.h:2173` defines `Vers_parse_info`, and
  `handler.h:2333` stores both `vers_info` and `period_info` in table DDL
  metadata.
- `vendor/mariadb/server/sql/table.h:1904` exposes `TABLE::versioned()`, and
  `table.h:1942` exposes application-period field access.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc` currently rejects
  generated columns and unsupported index shapes, but it has no temporal table
  metadata rejection.

## Scope

Add a minsize option that:

- defines `MYLITE_DISABLE_SYSTEM_VERSIONING`;
- removes `../sql/item_vers.cc` from the embedded SQL source list;
- adds embedded unsupported method bodies for retained `Item_func_*` methods
  referenced by `sql_select.cc`;
- rejects MyLite table definitions with system-versioned metadata;
- rejects MyLite table definitions with application-time period metadata;
- rejects copy `ALTER` attempts that add those temporal metadata surfaces; and
- records measured archive and linked-size impact.

## Non-Goals

- Do not remove parser syntax for temporal table clauses.
- Do not remove the generic temporal metadata structs from `handler.h` or
  `table.h`.
- Do not implement MyLite history rows, period constraints, `FOR SYSTEM_TIME`
  query semantics, or `FOR PORTION OF` DML.
- Do not change full MariaDB server target behavior.
- Do not change ordinary `TIMESTAMP`, `DATETIME`, or date/time SQL behavior.

## Proposed Design

Add `MYLITE_DISABLE_SYSTEM_VERSIONING` to
`vendor/mariadb/server/libmysqld/CMakeLists.txt` and enable it in
`tools/build-mariadb-minsize.sh`.

When enabled, remove `item_vers.cc` and compile small unsupported method bodies
inside `sql_select.cc` for the class methods that file can still reference.
Those methods report `ER_NOT_SUPPORTED_YET` if a versioned-table predicate path
is reached. Keeping them in an already-linked object avoids a separate C++
archive member that would duplicate weak `Item` hierarchy support sections.

Add a MyLite table-shape helper:

```c++
static bool mylite_table_has_temporal_metadata(const TABLE *table);
```

The helper rejects `TABLE::versioned()` and `TABLE_SHARE::period.name` before
`ha_mylite::create()` stores a table definition. This keeps the table-storage
boundary honest: MyLite will not persist a table definition whose row history,
period constraints, temporal DML, and recovery semantics are not implemented.

Extend storage-engine smoke coverage near the generated-column rejection tests:

- `CREATE TABLE ... WITH SYSTEM VERSIONING` fails and leaves no table;
- `CREATE TABLE ... PERIOD FOR ...` fails and leaves no table;
- `ALTER TABLE ... ADD SYSTEM VERSIONING, ALGORITHM=COPY` fails and leaves the
  base table readable; and
- `ALTER TABLE ... ADD PERIOD FOR ..., ALGORITHM=COPY` fails and leaves the base
  table readable.

## Affected Subsystems

- Embedded minsize SQL source list.
- MyLite handler table-shape validation.
- Storage-engine smoke unsupported-surface coverage.
- Binary-size analysis documentation.

## DDL Metadata Routing Impact

Temporal table metadata is rejected before any MyLite catalog mutation. Failed
`CREATE` statements must not leave visible tables. Failed copy `ALTER`
statements must preserve the prior table definition and row payload.

## Single-File And Embedded-Lifecycle Impact

No file-format change. The rejection avoids claiming single-file ownership for
history rows and temporal metadata that MyLite cannot yet recover or query.

## Public API Or File-Format Impact

No public `libmylite` API change and no `.mylite` file-format version bump.

## Binary-Size Impact

Expected savings are bounded by `item_vers.cc.o`, minus the replacement stub.
Because `item_vers.cc.o` contributes 26,791 bytes of text/data/bss but 178,432
bytes of archive member bytes, the archive win should be larger than the linked
runtime win.

Measured after implementation:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 27,978,354 | 27,863,442 | -114,912 |
| unstripped `mylite-open-close-smoke` | 7,439,896 | 7,436,656 | -3,240 |
| stripped `mylite-open-close-smoke` | 5,345,504 | 5,342,968 | -2,536 |
| stripped `mylite-compatibility-smoke` | 5,232,424 | 5,229,936 | -2,488 |

The archive no longer contains `item_vers.cc.o`. A separate stub object was
tested, but it saved only 3,652 archive bytes because it repeated weak C++
support sections from the `Item` hierarchy. The final implementation hosts the
small unsupported method bodies in already-linked `sql_select.cc` under
`MYLITE_DISABLE_SYSTEM_VERSIONING`, reducing the archive by 114,912 bytes.

## License, Trademark, And Dependency Impact

No new dependency or licensing change. MariaDB-derived sources remain in the
tree and are only omitted from the aggressive embedded profile.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-system-versioning \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-system-versioning \
  tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-system-versioning \
  tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-system-versioning \
  tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-system-versioning \
  tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Measure:

- archive bytes;
- unstripped and stripped linked smoke bytes;
- absence of `item_vers.cc.o` from `libmariadbd.a`;
- `sql_select.cc.o` growth compared with the removed object; and
- storage-smoke report evidence for temporal DDL rejection.

## Acceptance Criteria

- The minsize build succeeds with `MYLITE_DISABLE_SYSTEM_VERSIONING=ON`.
- Storage, open/close, bootstrap, and compatibility smokes pass.
- System-versioned and application-period MyLite `CREATE` statements fail
  without visible tables.
- Copy `ALTER` attempts to add system versioning or application periods fail
  while preserving existing rows.
- `libmariadbd.a` no longer contains `item_vers.cc.o`.
- Size results are recorded here and in
  `docs/research/production-size-analysis.md`.

## Risks And Unresolved Questions

- This removes SQL-standard temporal table support from the aggressive profile.
  That is acceptable only because MyLite cannot safely store or recover those
  semantics today.
- Parser support remains, so non-MyLite tables in other embedded experiments
  may still reach the unsupported runtime methods if temporal metadata exists.
- Future support needs MyLite catalog metadata, row history storage,
  transaction-time semantics, period constraints, temporal DML, ALTER behavior,
  and crash recovery.

## Verification

Validated on 2026-05-13 with:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-system-versioning \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-system-versioning \
  tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-system-versioning \
  tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-system-versioning \
  tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-system-versioning \
  tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

The storage-engine report includes:

```text
unsupported_system_versioned_create=rejected
unsupported_period_create=rejected
unsupported_system_versioned_alter=rejected
unsupported_period_alter=rejected
temporal_base_count=1
```
