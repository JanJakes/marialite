# fulltext-index-storage

> Superseded note: this slice kept SPATIAL/GEOMETRY rejection in place while
> adding FULLTEXT. `docs/specs/geometry-spatial-storage/specs.md` later added
> GEOMETRY storage and metadata-backed SPATIAL key support.

## Problem Statement

Before this slice, MyLite rejected `FULLTEXT` indexes. That blocked a common
application SQL surface used by search-oriented WordPress plugins and other
MariaDB/MySQL applications. The current durable `INDEXPAGE` stream is an
ordered key-image structure, not an inverted fulltext index, so MyLite should
not route FULLTEXT keys through normal index pages. It can still support the
API surface by persisting the table metadata and evaluating
`MATCH ... AGAINST` through a handler-owned full scan until a later slice
designs a physical inverted index.

## MariaDB Base And Source References

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/handler.h:188` defines `HA_CAN_FULLTEXT`.
- `vendor/mariadb/server/sql/handler.h:4355` through `handler.h:4362`
  define the handler fulltext virtual methods: `ft_init()`, `ft_init_ext()`,
  and `ft_read()`.
- `vendor/mariadb/server/include/ft_global.h:37` defines the `FT_INFO`
  callback table used by SQL-layer `MATCH` evaluation.
- `vendor/mariadb/server/sql/sql_table.cc:3627` through
  `sql_table.cc:3637` maps `FULLTEXT` DDL to `HA_FULLTEXT_legacy` and
  `HA_KEY_ALG_FULLTEXT`.
- `vendor/mariadb/server/sql/sql_table.cc:3683` through
  `sql_table.cc:3690` rejects FULLTEXT DDL unless the engine advertises
  `HA_CAN_FULLTEXT`.
- `vendor/mariadb/server/sql/item_func.cc:6297` initializes fulltext search
  through `handler::ft_init_ext()`.
- `vendor/mariadb/server/sql/item_func.cc:6503` through
  `item_func.cc:6528` computes `MATCH` values through `FT_INFO` relevance
  callbacks.
- `vendor/mariadb/server/sql/sql_select.cc:15496` and
  `sql_select.cc:25613` route fulltext access through `handler::ha_ft_read()`.
- `vendor/mariadb/server/storage/myisam/ha_myisam.h:79` and
  `ha_myisam.cc:2425` show the existing MyISAM handler shape for fulltext
  init/read hooks.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:3312` currently rejects
  FULLTEXT key flags in `mylite_key_supports_storage()`.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:3416` currently rebuilds
  durable `INDEXPAGE` roots for every accepted key.

## Scope

This slice will:

- advertise `HA_CAN_FULLTEXT`,
- accept `FULLTEXT` key metadata for MyLite table definitions,
- keep FULLTEXT keys out of ordered durable `INDEXPAGE` roots,
- implement `ft_init_ext()`, `ft_init()`, `ft_read()`, and `FT_INFO`
  callbacks with a full-scan evaluator over stored MyLite rows,
- support natural-language and simple boolean `MATCH ... AGAINST` searches for
  ordinary word terms, including required `+term` and prohibited `-term`
  boolean tokens,
- verify `SHOW CREATE TABLE` and fresh-process reopen preserve FULLTEXT
  metadata,
- keep SPATIAL indexes and GEOMETRY columns rejected.

## Non-Goals

- Do not implement a physical inverted fulltext index.
- Do not implement MyISAM's full boolean grammar, query expansion, stopword
  files, parser plugins, phrase proximity, or exact MyISAM relevance scoring.
- Do not advertise `HA_CAN_FULLTEXT_EXT`.
- Do not implement FULLTEXT over unsupported column shapes.
- Do not implement SPATIAL indexes or GEOMETRY storage.

## Proposed Design

Treat FULLTEXT keys as metadata-backed search definitions rather than ordered
key-image indexes.

1. Handler flags: add `HA_CAN_FULLTEXT` so MariaDB accepts FULLTEXT DDL and
   `MATCH` expressions for MyLite tables.
2. Key validation: accept `HA_KEY_ALG_FULLTEXT` with `HA_FULLTEXT_legacy` when
   all key parts are otherwise supported fields. Continue rejecting SPATIAL and
   unsupported algorithms.
3. Durable index roots: skip FULLTEXT keys when refreshing normal `INDEXPAGE`
   roots. The table-definition image still stores the FULLTEXT key metadata,
   but no ordered key-image root is published for it.
4. Fulltext search: implement a MyLite `FT_INFO` object that stores the query,
   key number, match mode, matching row IDs, and relevance scores. At
   `ft_init_ext()` time, scan MyLite rows, decode them into MariaDB record
   buffers, extract text from the fulltext key fields, score matches, and store
   matching row IDs. `ft_read()` reads the next matching row by row ID.
5. Relevance callbacks: for `MATCH` value calculation on current rows, extract
   the same key-field text from the provided record and compute the same
   simple score.

This deliberately favors correctness of the common SQL surface over claiming
physical fulltext index performance. A later slice can replace the full scan
with a durable inverted payload without changing user-visible DDL.

## Affected Subsystems

- MyLite handler capability flags.
- MyLite key-shape validation.
- Durable index-root refresh.
- Handler fulltext API implementation.
- Storage smoke DDL, DML, metadata, and persistence coverage.
- Roadmap and single-file architecture docs.

## DDL Metadata Routing Impact

FULLTEXT table definitions must persist only in the primary `.mylite` file
through the existing MariaDB table-definition image. MyLite must not create
`.MYI`, `.MYD`, parser-plugin, or auxiliary fulltext sidecars. Failed
unsupported SPATIAL/GEOMETRY DDL must remain absent from discovery.

## Single-File And Embedded-Lifecycle Implications

No companion files are introduced. There is no file-format version bump for
this first full-scan implementation because FULLTEXT metadata already lives in
the table-definition image and no new fulltext payload page type is added.

## Public API Or File-Format Impact

No public `libmylite` API change. The SQL surface expands to include FULLTEXT
DDL and `MATCH ... AGAINST` over MyLite tables. The primary file format remains
compatible with current pre-release v3 catalog files.

## Binary-Size Impact

Expected binary growth is moderate: tokenizer/scorer helpers, an `FT_INFO`
adapter, and smoke coverage. No new dependency is added.

## License, Trademark, And Dependency Impact

No new dependency or licensing change. The evaluator is MyLite-owned code built
inside the existing GPL-2.0-only MariaDB-derived tree.

## Test And Verification Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc`:
  - create a table with `FULLTEXT KEY`,
  - verify `SHOW CREATE TABLE` exposes the FULLTEXT key,
  - insert rows and query natural-language `MATCH ... AGAINST`,
  - evaluate `MATCH ... AGAINST` as a scalar relevance expression,
  - query simple boolean `MATCH ... AGAINST (... IN BOOLEAN MODE)` with
    required and prohibited terms,
  - update and delete rows and verify fulltext results change,
  - add a FULLTEXT key through copy `ALTER TABLE`,
  - persist a FULLTEXT table and verify metadata/results after fresh-process
    reopen,
  - keep SPATIAL/GEOMETRY rejection checks passing.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n tools/run-storage-engine-smoke.sh tools/run-compatibility-test-harness.sh tools/run-libmylite-open-close-smoke.sh tools/run-embedded-bootstrap-smoke.sh tools/build-mariadb-minsize.sh`
  - `git diff --check`

## Acceptance Criteria

- `CREATE TABLE ... FULLTEXT KEY ... ENGINE=MYLITE` succeeds.
- FULLTEXT metadata survives `SHOW CREATE TABLE` and fresh-process reopen.
- Natural-language `MATCH ... AGAINST` returns rows containing searched terms.
- Simple boolean required/prohibited terms work over supported FULLTEXT fields.
- FULLTEXT results reflect row updates and deletes.
- FULLTEXT keys are not stored as ordinary ordered `INDEXPAGE` roots.
- Existing BLOB/TEXT, nullable key, descending/HASH, generated-column, FK,
  CHECK, transaction, recovery, lifecycle, and sidecar checks keep passing.
- SPATIAL/GEOMETRY remain explicit rejections.

## Risks And Unresolved Questions

- This is not a full MyISAM-compatible fulltext implementation. Stopword,
  parser-plugin, phrase, query-expansion, and exact relevance semantics remain
  future work.
- Full-scan search is SQL-correct for the covered common cases but not
  scalable. A durable inverted fulltext page format should replace it.
- Tokenization initially targets ordinary word terms and ASCII case folding;
  broader collation-sensitive tokenization needs a dedicated compatibility
  slice.

## Implementation Result

Implemented as metadata-backed FULLTEXT support in the MyLite handler.
`ha_mylite` now advertises `HA_CAN_FULLTEXT`, accepts FULLTEXT key definitions
when their key parts fit the current row bridge, skips FULLTEXT keys when
publishing ordered `INDEXPAGE` roots, and implements MariaDB's `FT_INFO`
handler callbacks with a full scan over stored MyLite rows.

The storage smoke now covers:

- `CREATE TABLE ... FULLTEXT KEY ... ENGINE=MYLITE`,
- `SHOW CREATE TABLE` exposure of FULLTEXT metadata,
- natural-language `MATCH ... AGAINST`,
- scalar `MATCH ... AGAINST` relevance evaluation,
- simple boolean required and prohibited terms,
- fulltext visibility after row update and delete,
- copy `ALTER TABLE ... ADD FULLTEXT KEY`,
- persistence of FULLTEXT metadata and search results after fresh-process
  reopen,
- continued SPATIAL/GEOMETRY rejection.

Report evidence from `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`:

- `build/mariadb-minsize/mylite-storage-engine-report.txt`:
  - `status=0`
  - `message=ok`
  - `fulltext_show_create=present`
  - `fulltext_natural_ids=1,2,3`
  - `fulltext_projection_hits=1:hit,2:miss,3:miss`
  - `fulltext_boolean_ids=1,2`
  - `fulltext_updated_ids=1,2`
  - `fulltext_deleted_ids=2`
  - `fulltext_alter_ids=1`
  - `unsupported_spatial_key=rejected`
- `build/mariadb-minsize/mylite-catalog-write-report.txt`:
  - `status=0`
  - `persisted_fulltext_show_create=present`
  - `persisted_fulltext_ids=1`
- `build/mariadb-minsize/mylite-catalog-read-report.txt`:
  - `status=0`
  - `persisted_fulltext_show_create=present`
  - `persisted_fulltext_ids=1`
  - `index_payloads` includes only the table's ordered key roots, not a
    FULLTEXT root for `persisted_fulltext`.
