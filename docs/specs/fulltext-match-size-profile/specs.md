# Fulltext Match Size Profile

## Problem Statement

The aggressive MyLite minsize profile already removes MyISAM full-text storage
code and MyLite table DDL rejects `FULLTEXT` indexes. The SQL layer still keeps
`MATCH ... AGAINST` item construction, full-text item methods, and optimizer
hooks even though no retained MyLite storage path can execute a full-text
search.

This slice removes the SQL-visible full-text search expression from the
aggressive embedded profile and measures whether section GC can drop the
remaining `Item_func_match` runtime.

## Source Findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

Relevant local source paths:

- `docs/specs/myisam-fulltext-size-profile/specs.md` records that MyISAM
  full-text objects are already omitted from the minsize profile, while SQL
  `MATCH ... AGAINST` was intentionally left for a later slice.
- `docs/specs/unsupported-index-ddl-rejection/specs.md` records that MyLite
  rejects `FULLTEXT` indexes and does not implement `MATCH/AGAINST` behavior.
- `vendor/mariadb/server/sql/sql_yacc.yy:10257` parses
  `MATCH ident_list_arg AGAINST (...)` and constructs `Item_func_match`.
- `vendor/mariadb/server/sql/item_func.h:3840` declares `Item_func_match`.
- `vendor/mariadb/server/sql/item_func.cc:6238` implements full-text search
  setup, index selection, execution, equality, and printing.
- `vendor/mariadb/server/sql/sql_select.cc:7532` keeps full-text key optimizer
  detection for `Item_func_match` predicates.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:2304` rejects
  unsupported MyLite key flags including `HA_FULLTEXT_legacy`.

Current linked symbol evidence from
`build/mariadb-minsize-no-eh-frame-header`:

| Symbol group | Visible linked bytes |
| --- | ---: |
| `Item_func_match`, `FT_SELECT`, and full-text stage/helper symbols | about 19 KiB |
| `select_export` / `select_dump` host-file export symbols | about 4 KiB |
| SQL `HANDLER` command symbols | about 5 KiB |

Full-text search is the largest of these small embedded-only candidates and is
already unsupported by MyLite storage.

## Design

Add a minsize option named `MYLITE_DISABLE_FULLTEXT_MATCH`.

When enabled:

- `sql_yacc.yy` rejects `MATCH ... AGAINST` during parsing with
  `ER_NOT_SUPPORTED_YET` and a MyLite minsize-profile message;
- `item_func.cc` does not compile the `Item_func_match` method bodies;
- the `Item_func_match` declaration remains in `item_func.h`, because retained
  optimizer code checks the `FT_FUNC` item kind and casts existing `Item *`
  pointers in generic code;
- MyISAM full-text storage stays governed by
  `MYLITE_DISABLE_MYISAM_FULLTEXT`;
- ordinary `LIKE`, equality/range lookup, BTREE indexes, and MyLite table
  metadata remain unchanged.

The profile should not remove the `MATCH` and `AGAINST` tokens from the lexer
or grammar. Keeping the grammar shape but rejecting construction keeps parse
maintenance smaller and gives users an explicit unsupported-feature diagnostic.

## Non-Goals

- Do not remove MyISAM itself or disk temporary-table spill.
- Do not remove ordinary string search such as `LIKE`.
- Do not change MyLite `FULLTEXT` key DDL rejection.
- Do not change non-minsize MariaDB embedded builds.
- Do not remove optimizer code that is still shared with ordinary key planning
  unless a later source audit proves it is full-text-only.

## Affected Subsystems

- Minsize CMake options and build script.
- Generated MariaDB parser input (`sql_yacc.yy`).
- SQL item full-text implementation in `item_func.cc`.
- MyLite open/close unsupported-profile smoke coverage.

## DDL Metadata Routing Impact

No table-definition routing change. MyLite already rejects `FULLTEXT` key DDL
before persisting a table definition. This slice removes only SQL full-text
search expression construction.

## Single-File And Embedded-Lifecycle Impact

No file-format, lock, recovery, startup, or sidecar behavior changes. The
removed feature is an engine-dependent search expression with no MyLite storage
implementation.

## Public API Or File-Format Impact

No public `libmylite` C API change and no `.mylite` file-format change.

SQL compatibility impact: the aggressive minsize profile no longer supports
`MATCH ... AGAINST`, and now fails explicitly before optimizer or storage
execution.

## Binary-Size Impact

Savings are modest. On top of `eh-frame-header-size-profile`, this slice
reduced:

- `libmysqld/libmariadbd.a` from 26,484,414 bytes to 26,454,822 bytes,
  saving 29,592 bytes;
- unstripped linked `mylite-open-close-smoke` from 6,868,688 bytes to
  6,860,560 bytes, saving 8,128 bytes;
- stripped linked `mylite-open-close-smoke` from 4,842,168 bytes to
  4,836,264 bytes, saving 5,904 bytes.

The linked smoke no longer defines `Item_func_match` method bodies. Small
generic `FT_SELECT` optimizer symbols remain linked by ordinary range-planning
code and were left in place for this bounded slice.

## License, Trademark, And Dependency Impact

No new dependency or license change.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-fulltext-match \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-fulltext-match \
  tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-fulltext-match \
  tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-fulltext-match \
  tools/run-embedded-bootstrap-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-fulltext-match \
  tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Measure:

```sh
llvm-nm --demangle --size-sort --print-size --radix=d \
  build/mariadb-minsize-no-fulltext-match/mylite/mylite-open-close-smoke |
  rg 'Item_func_match|FT_SELECT|fulltext'
```

Verified:

- `tools/build-mariadb-minsize.sh`
- `tools/run-libmylite-open-close-smoke.sh`
- `tools/run-storage-engine-smoke.sh`
- `tools/run-embedded-bootstrap-smoke.sh`
- `tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh tools/run-storage-engine-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/run-compatibility-test-harness.sh`
- `git diff --check`

## Acceptance Criteria

- `MATCH ... AGAINST` reports `ER_NOT_SUPPORTED_YET` in the minsize profile.
- Existing MyLite `FULLTEXT` index DDL rejection still passes.
- Open/close, storage-engine, embedded-bootstrap, and compatibility smokes
  pass.
- Linked smoke no longer defines the `Item_func_match` method bodies.
- Size deltas are recorded in this spec and production size analysis.

## Risks And Unresolved Questions

- Some generic optimizer code still contains `FT_FUNC` branches. Leaving those
  branches in place is intentional for this bounded slice; removing them needs
  a broader optimizer audit.
- This is SQL-visible. It belongs only in the aggressive size profile unless
  product compatibility accepts losing MariaDB full-text search syntax.
