# WordPress schema storage

## Problem

MyLite now routes common application engine clauses to the MyLite handler, but
the storage smoke still does not exercise WordPress-shaped schema DDL. WordPress
core and many plugins use DDL patterns that are more realistic than the current
small synthetic tables:

- `$charset_collate` table options such as `DEFAULT CHARACTER SET utf8mb4`
  and `COLLATE utf8mb4_unicode_520_ci`,
- integer display widths with `unsigned`,
- long text family columns,
- zero datetime defaults,
- varchar and text prefix indexes,
- multi-column secondary indexes,
- `CREATE TABLE IF NOT EXISTS`,
- plugin activation tables with `ENGINE=InnoDB` or `ENGINE=MyISAM`.

This slice adds focused storage coverage for those patterns and implements any
missing MyLite support discovered by the tests.

## Source Findings

The selected MariaDB base is `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- WordPress `trunk` `src/wp-admin/includes/schema.php` builds core table DDL in
  `wp_get_db_schema()`. The current source uses `$charset_collate`, caps utf8mb4
  index prefixes at 191 characters, and defines `wp_options`, `wp_posts`,
  metadata tables, users, multisite tables, and indexes with integer display
  widths, `unsigned`, `longtext`, `tinytext`, `mediumtext`, `datetime`, unique
  keys, and prefix keys:
  <https://raw.githubusercontent.com/WordPress/wordpress-develop/trunk/src/wp-admin/includes/schema.php>
- `vendor/mariadb/server/sql/sql_yacc.yy:5677` parses table options through
  `opt_create_table_options` and `create_table_option`; nearby rules handle
  `ENGINE`, `COMMENT`, `AUTO_INCREMENT`, row and charset/collation options.
- `vendor/mariadb/server/sql/handler.h:2244` stores parsed table-definition
  options in `Table_scope_and_contents_source_pod_st` / `HA_CREATE_INFO`,
  including charset conversion, comments, `auto_increment_value`,
  `table_options`, `key_block_size`, `row_type`, and engine option lists.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:2123` stores the exact
  MariaDB-generated table-definition image in the MyLite catalog. Table options
  that survive into the frm image should persist through MyLite discovery.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:4405` accepts the current
  row shapes when generated columns and BLOB/TEXT storage are supported. The
  WordPress schema patterns do not require unsupported non-stored virtual
  BLOB/TEXT or GEOMETRY columns.
- `vendor/mariadb/server/storage/mylite/ha_mylite.cc:4566` initializes table
  autoincrement state from `HA_CREATE_INFO::auto_increment_value`, so SQL dumps
  with `AUTO_INCREMENT=N` should be covered.
- `vendor/mariadb/server/sql/handler.h:4587` defaults storage engines to a
  255-byte maximum key-part length unless they override it. MyLite already
  advertised a 3072-byte total key length, but not a matching key-part length,
  which caused utf8mb4 WordPress `varchar(191)` keys to be treated as overlong.
- `vendor/mariadb/server/sql/sql_table.cc:3096` derives key length limits from
  the handler. When a unique key exceeds those limits, `init_key_info()` marks
  it as `HA_KEY_ALG_LONG_HASH`.
- `vendor/mariadb/server/sql/table.cc:1297` initializes the hidden long-unique
  hash virtual column, and `vendor/mariadb/server/sql/table.cc:1343` documents
  that the storage engine sees only the generated hash key-part for that
  unique key.
- `vendor/mariadb/server/mylite/storage_engine_smoke.cc` already tests
  BLOB/TEXT row payloads, prefix keys, nullable keys, composite indexes,
  autoincrement, duplicate-key DML, and fresh-process reopen separately, but it
  does not combine those features into WordPress-like DDL.

## Scope

Add storage smoke coverage for:

- a WordPress core `wp_options`-style table using `CREATE TABLE IF NOT EXISTS`,
  utf8mb4 charset/collation options, `bigint(20) unsigned`, `longtext`,
  primary key, unique key, secondary key, and `AUTO_INCREMENT=N`;
- a WordPress core `wp_posts`-style table using `datetime` zero defaults,
  `longtext`, `text`, several varchar columns, and multi-column/prefix
  indexes;
- a plugin-style table using `mediumint(8) unsigned`, `datetime`, `longtext
  DEFAULT NULL`, `varchar(191)`, and explicit `ENGINE=InnoDB`;
- a legacy plugin-style table using explicit `ENGINE=MyISAM` and
  `DEFAULT CHARSET=utf8`;
- a plugin-style long unique key that exercises MariaDB's
  `HA_KEY_ALG_LONG_HASH` hidden hash-key path;
- representative inserts, duplicate-key behavior, indexed lookups,
  `SHOW CREATE TABLE`, and fresh-process reopen.

## Non-Goals

- Do not implement a complete WordPress installer or PHP runtime.
- Do not import all WordPress core table definitions into the smoke yet.
- Do not add support for views, triggers, stored routines, events, or server
  administration surfaces.
- Do not add physical optimizer special cases for WordPress queries.

## Design

Keep the implementation in the MyLite storage smoke unless tests expose an
engine gap. The smoke should use MyLite-owned runtime defaults from
`application-engine-routing`, so the WordPress and plugin DDL can omit
`ENGINE=` or specify InnoDB/MyISAM naturally.

Persist the tables in the catalog-backed write phase and verify them in the
fresh-process read phase. This proves the table-definition image, row payloads,
key metadata, key-image entries, autoincrement state, and collation metadata
survive reopen.

## Affected Subsystems

- MyLite storage-engine smoke.
- Storage architecture docs and roadmap if coverage lands or if code changes
  are required.
- MyLite storage engine only if the smoke exposes a missing DDL or row/index
  behavior.

## DDL Metadata Routing Impact

The slice relies on existing MyLite table-definition image persistence. It
should verify `SHOW CREATE TABLE` exposes the expected charset/collation
options and indexes after fresh-process rediscovery. It should not introduce
new durable `.frm` sidecars.

## Single-File and Embedded Lifecycle

The WordPress-shaped tables must persist inside the primary `.mylite` catalog
and payload pages. MyLite runtime directories must remain free of `.frm`,
MyISAM, Aria, and InnoDB sidecars.

## Public API and File Format Impact

No public C API or file-format change is expected. If the smoke exposes a
storage failure, the fix should reuse the current row, overflow, and index
payload formats unless a source-level reason requires a new format.

## Binary Size Impact

Expected binary-size impact is mostly test code. Engine support changes are
limited to key capability metadata, key algorithm acceptance, and
autoincrement initialization.

## License and Dependency Impact

No new dependency. WordPress schema strings used in tests are compatibility
fixtures derived from GPL-compatible WordPress source references; keep them
small and focused.

## Test Plan

- Extend `vendor/mariadb/server/mylite/storage_engine_smoke.cc` with
  WordPress-shaped DDL and data checks in the catalog write/read phases.
- Verify:
  - `SHOW CREATE TABLE` includes `ENGINE=MYLITE`,
  - expected indexes are present,
  - inserts and duplicate-key DML work,
  - long unique-key duplicate updates and forced lookups work,
  - indexed lookups return expected ids,
  - `AUTO_INCREMENT=N` initializes and persists correctly,
  - fresh-process read sees all expected rows,
  - sidecar scan remains clean.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` on the smoke scripts
  - `git diff --check`

## Acceptance Criteria

- WordPress-shaped core and plugin tables can be created under MyLite-owned
  runtime defaults without specifying `ENGINE=MYLITE`.
- Explicit `ENGINE=InnoDB` and `ENGINE=MyISAM` plugin-style tables route to
  MyLite.
- Charset/collation table options, prefix indexes, multi-column indexes,
  long unique indexes, long text family columns, zero datetime defaults, and
  SQL dump-style `AUTO_INCREMENT=N` survive fresh-process reopen.
- The grouped compatibility harness still passes and reports no unexpected
  MyLite runtime sidecars.

## Implementation Result

The slice is implemented.

- MyLite now advertises a 3072-byte key-part limit to match its 3072-byte total
  key limit, so utf8mb4 WordPress `varchar(191)` keys keep their expected key
  length instead of being downgraded through MariaDB's default 255-byte handler
  key-part limit.
- MyLite accepts `HA_KEY_ALG_LONG_HASH` indexes in the ordinary durable
  key-image path. MariaDB still owns the hidden generated hash field and the
  higher-level long-unique duplicate check, while MyLite stores and searches the
  generated hash key image.
- MyLite initializes table-local autoincrement state from the table definition
  at create time even when the transient `TABLE` does not have an active DML
  `next_number_field`.
- The storage smoke now persists and reopens WordPress-shaped `wp_options` and
  `wp_posts` tables, an explicit `ENGINE=InnoDB` plugin table, an explicit
  `ENGINE=MyISAM` legacy plugin table, and a long-unique plugin fixture. The
  smoke verifies charset/collation metadata, routed `ENGINE=MYLITE`, indexed
  lookups, duplicate-key updates, `AUTO_INCREMENT=42`, fresh-process read, and
  recovery read.

## Risks and Unresolved Questions

- This slice is representative, not a replacement for a later full WordPress
  import/install smoke.
- WordPress `trunk` can change. Keep the fixture focused on stable schema
  patterns rather than copying every table verbatim.
- Some plugins use unusual MySQL-specific SQL. Those should become additional
  focused slices when discovered.
