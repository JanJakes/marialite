# Storage engine compatibility matrix

## Problem Statement

MyLite now supports a broad common application SQL surface, but the project
needs a precise way to answer "what is still missing compared with
InnoDB/MyISAM/Aria?" A single yes/no compatibility claim is too coarse:
application SQL syntax, storage file ownership, crash recovery, concurrency,
maintenance tooling, and performance structures mature at different rates.

This slice creates a maintained compatibility matrix that separates verified
MyLite behavior from explicit storage-engine gaps.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`). The local MyLite snapshot used
for the matrix is `bee7e2e576c26985a3f0e12c47b46bd99a5ddafd`.

- `README.md` states the product goal: MariaDB SQL semantics through an
  embedded `libmylite` runtime with one primary `.mylite` file and documented
  MyLite-owned companions.
- `docs/architecture/engineering-standards.md` marks persistent `.frm`,
  `.ibd`, `.MAI`, `.MAD`, `aria_log.*`, `ib_logfile*`, binlog, relay log, and
  plugin sidecars as incompatible with the final product shape.
- `docs/architecture/single-file-storage.md` documents the current file
  format, catalog publication, row/index pages, transaction/recovery gaps,
  primary-file locking, schemas, temporary data, and system-schema policy.
- `tools/build-mariadb-minsize.sh` disables `PLUGIN_INNOBASE`, `PLUGIN_ARIA`,
  dynamic plugins, Aria-backed temporary tables, partitioning, and most
  optional engines in the default embedded profile.
- `build/mariadb-minsize/mylite-build-report.txt` from the latest local run
  records `PLUGIN_INNOBASE=NO`, `PLUGIN_ARIA=NO`, no dynamic plugin artifacts,
  and built-in MyLite/MyISAM/HEAP/Sequence/type plugins.
- `vendor/mariadb/server/storage/mylite/ha_mylite.h` advertises MyLite handler
  capabilities for nullable keys, BLOB/TEXT indexes, FULLTEXT, GEOMETRY,
  RTREE-key metadata, virtual columns, repair, and exact record stats.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc` implements commit,
  rollback, savepoint rollback, FK hooks, DDL catalog mutations, row/index
  DML, FULLTEXT scoring, SPATIAL metadata-backed predicates, maintenance
  methods, primary-file locking, and MyLite catalog recovery paths.
- `build/mariadb-minsize/mylite-compatibility-harness-report.txt` reports
  passing embedded lifecycle, `libmylite` lifecycle, storage/recovery,
  MariaDB comparison, and sidecar scan groups, with no unexpected inherited
  sidecars.
- MariaDB's InnoDB documentation describes InnoDB as transactional,
  ACID-compliant, supporting online DDL, XA, foreign keys, fulltext, spatial
  indexes, compression, encryption, transaction logging, purge, non-locking
  reads, and row locking.
- MariaDB's MyISAM documentation describes MyISAM as non-transactional, without
  foreign keys, stored in `.frm`/`.MYD`/`.MYI` files, with FULLTEXT, GIS data
  types, table locking, and concurrent inserts.
- MariaDB's Aria documentation describes Aria as a crash-safe MyISAM successor
  with Aria logs and page formats; its `TRANSACTIONAL` option means crash-safe
  statement behavior, not true SQL transaction support.
- MariaDB's foreign-key documentation says current foreign keys are supported
  only by InnoDB in upstream MariaDB and that `SET DEFAULT` is unsupported.
- MariaDB's full-text documentation says FULLTEXT indexes are supported for
  MyISAM, Aria, InnoDB, and Mroonga tables, with engine-specific search rules.

## Scope

This slice:

- creates `docs/compatibility/storage-engine-compatibility-matrix.md`;
- compares MyLite with InnoDB, MyISAM, and Aria across storage files, SQL DDL,
  row types, indexes, constraints, transactions, recovery, locking,
  concurrency, temporary data, maintenance, diagnostics, and performance;
- distinguishes verified MyLite support from partial/deferred behavior;
- records a risk register of future slices.

## Non-Goals

- Do not implement any missing compatibility feature in this slice.
- Do not claim exhaustive MariaDB MTR coverage.
- Do not treat MySQL behavior as normative.
- Do not change the default build profile or engine routing.

## Design

The compatibility matrix is a documentation artifact with these rules:

1. Start with a compact at-a-glance dashboard before the detailed tables.
2. Keep MyLite build-profile and `ENGINE=` routing behavior in MyLite-specific
   tables, not in InnoDB/MyISAM/Aria comparison tables.
3. Use a small, consistent MyLite status vocabulary: `✅ Supported`,
   `🟡 Partial`, `🟡 Needs coverage`, `🚧 Deferred`, `❌ Unsupported`, and
   `⚪ N/A`.
4. Keep MyLite status separate from notes in detailed comparison tables. The
   status cell should contain only the status; caveats belong in the notes
   cell.
5. Reference engine columns use plain descriptive values because they are
   baselines, not MyLite implementation statuses.
6. Use `✅ Supported` only when the current source and smoke/compatibility
   reports directly prove the behavior.
7. Use `🟡 Partial` when the application-facing SQL works but physical
   storage, performance, or recovery semantics differ from the reference
   engine.
8. Use `🟡 Needs coverage` when the behavior is expected through existing
   MyLite/MariaDB paths but needs targeted matrix tests before a support claim.
9. Use `🚧 Deferred` when the feature is compatible with the product goal but
   not implemented.
10. Use `❌ Unsupported` for intentionally omitted or unsupported default-product
   behavior.
11. Keep InnoDB/MyISAM/Aria comparisons at engine-family level and cite MariaDB
   documentation rather than MySQL behavior.
12. Describe concurrent access in embedded terms and avoid implying that
   independent processes can safely open the same reference-engine datadir.

## Affected Subsystems

- Project compatibility documentation.
- Roadmap planning.
- Future slice selection.

No code or build-system subsystem changes are required.

## DDL Metadata Routing Impact

None. This is documentation-only.

## Single-File and Embedded Lifecycle

The matrix reinforces the current product boundary:

- MyLite is the default durable storage path.
- `ENGINE=InnoDB` and common `ENGINE=MyISAM` application clauses route to
  MyLite in MyLite-owned embedded runtimes.
- Real InnoDB/Aria sidecar files remain outside the default single-file product
  shape.

## Public API and File Format Impact

No public API or file-format change.

## Binary Size Impact

No binary-size impact. The matrix records the latest local build report size
only as compatibility context.

## License and Dependency Impact

No dependency change. The matrix links to MariaDB documentation and local
MariaDB-derived source references.

## Test and Verification Plan

Documentation checks:

```sh
git diff --check
```

Source evidence can be refreshed with:

```sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
```

## Acceptance Criteria

- The matrix has explicit rows for InnoDB-level storage guarantees that MyLite
  does not yet implement.
- The matrix has explicit rows for MyISAM/Aria features that MyLite either
  replaces, exceeds, or lacks.
- Supported MyLite features are backed by local source/report references.
- The matrix has a clear at-a-glance status summary for current application
  readiness, partial storage semantics, deferred internals, and out-of-scope
  server/datadir surfaces.
- MyLite build-profile and `ENGINE=` routing behavior are separate from the
  engine-family comparison matrix.
- MyLite status values are visually consistent and separated from explanatory
  notes across the detailed tables.
- Deferred work is summarized as concrete future slice candidates.

## Risks and Unresolved Questions

- Some data-type families are expected to work through the raw MariaDB record
  bridge but need targeted smoke coverage before being marked `✅ Supported`.
- The matrix should be revisited after any recovery, MVCC, physical index,
  native temp-spill, typed row-format, or PHP lifecycle slice.
- Real InnoDB support remains a separate product-mode question, not a default
  MyLite storage-engine requirement.

## Implementation Result

Implemented as documentation only:

- `docs/compatibility/storage-engine-compatibility-matrix.md`
- `docs/specs/storage-engine-compatibility-matrix/specs.md`

Review amendments:

- Added a top-level emoji status legend and at-a-glance dashboard so the
  current position is readable before the detailed matrix.
- Split MyLite build-profile and `ENGINE=` routing rows out of the
  InnoDB/MyISAM/Aria comparison matrix.
- Made MyLite status values consistent across detailed tables and moved caveats
  into notes columns.
- Clarified concurrent access rows to distinguish server-mediated MariaDB
  clients from independent embedded opens of the same durable storage.
- Clarified Aria FULLTEXT support and MyLite's current physical FULLTEXT/R-tree
  performance boundary.
- Changed expected broad type-family support to "needs coverage" until targeted
  smoke coverage exists.
