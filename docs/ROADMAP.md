# MyLite roadmap

This roadmap orders the first engineering slices and tracks implementation
progress. It is intentionally higher-level than the slice specs in
`docs/specs/`: each slice still needs its own source-linked design before code
lands.

## Status key

- `Done`: accepted and represented in the repository.
- `Planned`: expected work, but not started.
- `In progress`: active implementation or research.
- `Blocked`: waiting on a named prerequisite or decision.

## Current state

MyLite currently has project documentation, source analysis, architecture
direction, API sketches, workflow guidance, and a mechanical MariaDB Server
11.8.6 source import under `vendor/mariadb/server/`. It also has a
reproducible Docker-based minimal embedded MariaDB build profile that produces
`build/mariadb-minsize/libmysqld/libmariadbd.a`, plus an embedded bootstrap
smoke target that starts the runtime in-process, runs `SELECT 1` under
controlled temporary paths, and verifies the first explicit embedded rejections
for dynamic plugin, UDF creation, and foreign-server metadata commands. The
first static `libmylite` wrapper now exposes open/close and handle-owned
diagnostics for one initialized database path per process, plus the first
public `mylite_exec()` convenience API so callers can execute supported SQL
through that handle without reaching for `MYSQL *` internals. The API also
exposes handle-local affected rows, generated insert ids, warning counts, and
structured warning lookup for the last executed statement, plus the first
prepared statement lifecycle with binary-safe column bytes and parameter
binding for NULL, numeric, text, and BLOB values. `mylite_open_v2()` now
supports local `file:` URI filenames through `MYLITE_OPEN_URI`, including
`mode=ro`, `mode=rw`, and `mode=rwc`. The first static `MYLITE`
storage-engine skeleton is registered in the embedded profile.
The engine can discover user-created catalog-backed table definitions, run a
bounded `CREATE`, copy `ALTER`, `RENAME`, and `DROP` lifecycle without leaving
durable `.frm` table-definition files, persist frm-backed table definitions in
the primary `.mylite` file across fresh embedded processes, prove populated
copy `ALTER` preserves supported rows, BLOB/TEXT payloads, rebuilt indexes,
nullable keys, and autoincrement state, and recover the previous valid catalog
generation when the latest append-only catalog payload is corrupted.
It can store simple non-BLOB rows, enforce supported primary and unique keys,
serve basic ordered index access, and persist table-local autoincrement state
in the `.mylite` payload. A grouped compatibility harness now runs the embedded
lifecycle, `libmylite` lifecycle, storage/recovery smokes, a MariaDB-reference
comparison for the supported subset, and a MyLite runtime sidecar scan. The
MariaDB-reference comparison now covers ordinary application query composition
over MyLite tables, including joins, left joins, grouped HAVING queries,
derived-table ORDER/LIMIT queries, `IN` and `EXISTS` subqueries, `UNION ALL`,
temporary `CREATE TABLE ... SELECT`, `INSERT ... SELECT`, and multi-table
`UPDATE`/`DELETE` statements. The
primary file format now stores catalog payload generations in typed 4096-byte
page chains under a v3 two-header publication format with a dedicated
allocator payload root, and simple row images now live in typed per-table row
payload page chains addressed through catalog `ROWPAGE` roots. New row payload
writes use binary page-local row slot directories instead of text row streams,
with oversized fixed row images rejected explicitly until overflow pages are
designed. Supported primary and secondary key indexes now publish durable
`INDEXPAGE` roots to typed index payload pages and handler index reads use those
loaded roots when they match open MariaDB key metadata.
Large non-BLOB fixed MariaDB record images now split across row overflow segment
pages inside the primary file, and non-key BLOB/TEXT row payloads now use the
same row and overflow page storage without persisting MariaDB row-buffer
pointers. Non-null BLOB/TEXT prefix keys and nullable key parts now use MariaDB
key-image bytes in durable `INDEXPAGE` payloads, with stored rows decoded
before key-image generation so cleared native BLOB pointer bytes are never
read. Nullable unique keys follow MariaDB's multiple-NULL behavior while still
rejecting duplicate all-non-NULL key tuples.
Foreign keys are now supported for MyLite tables. MyLite persists
key-definition and FK metadata in the primary file, enforces child
parent-existence checks and restrict/no-action parent restrictions from durable
key-image prefixes, exposes FK metadata through `SHOW CREATE TABLE` and
information schema including `KEY_COLUMN_USAGE`, `TABLE_CONSTRAINTS`, and
`REFERENTIAL_CONSTRAINTS`, preserves FK metadata across fresh-process reopen
and recovery fallback, honors `foreign_key_checks=0` for row checks, and
applies `CASCADE`/`SET NULL` parent update/delete actions through prelocked
child-table catalog mutations. `SET DEFAULT` remains an explicit rejection
because MariaDB/InnoDB still leave that action unimplemented.
Generated columns are supported for the subset that fits the current
raw-record bridge: non-indexed virtual generated columns are computed by
MariaDB after handler reads, including non-indexed virtual generated BLOB/TEXT
and GEOMETRY values that stay out of durable row payloads. Stored generated
columns are persisted in normal row payloads, stored generated-column indexes
use the durable key-image stream, copy `ALTER` computes stored generated values
for copied rows, and supported BTREE/HASH indexes on non-stored virtual
generated columns materialize generated key values through MariaDB's expression
evaluator before writing normal durable key-image streams. Virtual generated
BLOB/TEXT prefix indexes, unique prefix constraints, HASH metadata keys, and
FULLTEXT definitions are supported when MariaDB accepts the generated
expression and key shape; ordinary key parts reuse durable key-image streams,
and FULLTEXT materializes virtual text before metadata-backed scoring. Virtual
generated GEOMETRY SPATIAL keys remain constrained by MariaDB SQL because
virtual generated columns cannot be declared `NOT NULL`, while SPATIAL keys
require non-null geometry fields. CHECK constraints are supported for MyLite
tables through inherited MariaDB SQL-layer semantics; the current smoke proves
invalid INSERT and UPDATE statements are rejected, valid rows remain unchanged,
and CHECK metadata survives fresh-process reopen through the persisted
table-definition image and is visible through
`TABLE_CONSTRAINTS` and `CHECK_CONSTRAINTS`.
Persistent views, triggers, stored procedures, and stored functions are now
supported for MyLite schemas. MyLite stores MariaDB's canonical text view and
trigger definitions, trigger-name mappings, and standalone routine metadata in
the primary catalog, then reloads those objects through MariaDB's parser,
compiler, cache, and execution paths. The storage smoke verifies create,
replace, alter, drop, show, information-schema visibility including routine
and parameter metadata,
fresh-process reopen, recovery read, trigger firing after reopen, `CALL`
execution, stored function invocation, and dropped-routine rejection after
reopen. Persistent schema-object DDL for packages, package bodies, and events
remains rejected explicitly until those object definitions can live in the
MyLite catalog without durable MariaDB datadir sidecars or hidden `mysql.*`
system table writes.
Descending BTREE key parts are supported by applying MariaDB's
`HA_REVERSE_SORT` metadata to durable key-image ordering, and `USING HASH`
indexes are accepted through the current key-image stream. FULLTEXT indexes
are now accepted as metadata-backed search definitions and evaluated through a
MyLite full-scan `FT_INFO` adapter for common `MATCH ... AGAINST` natural and
boolean term searches; they deliberately do not create ordinary ordered
`INDEXPAGE` roots until a later inverted-index format exists. GEOMETRY columns
now persist through the existing BLOB/TEXT row and overflow bridge, and SPATIAL
indexes are accepted as metadata-backed RTREE definitions for valid non-null
one-part geometry keys. SPATIAL keys are intentionally not written as ordered
`INDEXPAGE` roots or exposed as ordered index read paths until a physical
R-tree or handler-level MBR scan is designed.
Unsupported index additions through copy ALTER are covered separately so
failed replacement-table definitions do not mutate valid MyLite rows or
catalog state.
Temporary MyLite tables are supported as session-scoped in-memory table
definitions that reuse the current row and index structures without publishing
durable catalog or payload records. The storage smoke verifies temporary
CREATE/INSERT/SELECT/UPDATE/DELETE/TRUNCATE/DROP, indexed reads,
autoincrement, durable-name shadowing, cleanup, and fresh-process absence.
Read-only `libmylite` handles can also create, mutate, query, truncate, and
drop session temporary MyLite tables without mutating the primary `.mylite`
file. Temporary MyLite foreign keys are now session-local in-memory metadata:
same-session temporary parent references are enforced, visible through
`SHOW CREATE TABLE`, and can cascade into temporary child rows without
publishing durable records. Disk-backed MariaDB internal temporary spill is
covered for MyLite source tables through the inherited MyISAM internal
temporary-table path under the configured temporary runtime directory; the
compatibility harness now forces that path, verifies
`Created_tmp_disk_tables` increments, and proves no persistent sidecars remain
after shutdown. A native MyLite spill-file format remains future work.
Composite-key `AUTO_INCREMENT` columns are supported when the generated column
is a non-leading part of a key. MyLite advertises MariaDB's
`HA_AUTO_PART_KEY` capability and routes non-leading generation through the
inherited handler prefix-max lookup over durable key-image indexes, while
keeping the existing table-local counter for leading autoincrement keys.
Duplicate-key DML now supports the common application conflict clauses
`INSERT IGNORE`, `REPLACE`, and `INSERT ... ON DUPLICATE KEY UPDATE` by
publishing the offending MyLite row id through MariaDB's handler duplicate-row
position path. The storage smoke verifies primary and secondary unique-key
conflicts, secondary index lookup after mutation, and fresh-process reopen.
Application-style engine clauses now route to MyLite in MyLite-owned embedded
runtimes: omitted engines, explicit `ENGINE=InnoDB`, explicit `ENGINE=MyISAM`,
`CREATE TABLE ... LIKE`, `CREATE TABLE ... SELECT`, `ALTER TABLE ...
ENGINE=InnoDB`, and user-created temporary tables use the MyLite handler
instead of external durable engines, including under MariaDB's default
`NO_ENGINE_SUBSTITUTION` mode.
Common table maintenance and diagnostic statements now work for healthy MyLite
tables. `CHECK TABLE`, `ANALYZE TABLE`, `OPTIMIZE TABLE`, and `REPAIR TABLE`
return MariaDB admin `status:OK` rows through MyLite handler overrides;
`CHECKSUM TABLE` uses MariaDB's scan-based checksum path; and `SHOW INDEX`,
`DESCRIBE`, and `EXPLAIN` are covered by the storage smoke for MyLite table,
key, column, and plan metadata. `OPTIMIZE TABLE` is currently a validation
no-op rather than physical page compaction, and `REPAIR TABLE` is scoped to
healthy opened tables rather than offline salvage of unreadable primary files.
WordPress-shaped schema storage is now covered in the catalog persistence,
fresh-process read, and recovery smokes. MyLite creates and reopens
WordPress-style `wp_options` and `wp_posts` tables, plugin tables with explicit
InnoDB/MyISAM engine clauses routed to MyLite, utf8mb4 and utf8 table
collations, prefix and multi-column indexes, long text columns, zero datetime
defaults, `AUTO_INCREMENT=N`, duplicate-key updates, and indexed lookups.
MyLite now advertises a 3072-byte key-part limit for the embedded profile and
accepts MariaDB `HA_KEY_ALG_LONG_HASH` keys so genuinely overlong unique keys
can use MariaDB's hidden generated hash key while preserving SQL-layer
long-unique duplicate checks.
Application migration ALTER coverage now proves populated-table `ALTER TABLE`
forms for adding primary, unique, and secondary keys, renaming and dropping
indexes, adding and dropping CHECK constraints, and adding and dropping foreign
keys. The smoke verifies active unique/CHECK/FK violations are rejected, then
verifies the final dropped-constraint state survives fresh-process reopen and
recovery read.
Standalone supported index DDL now proves `CREATE INDEX` and `DROP INDEX`
preserve rows, persist the final table definition, and avoid durable `.frm`
sidecars after MariaDB's in-place ALTER probe falls back to copy ALTER.
`TRUNCATE TABLE` now uses MariaDB's handler truncate path for supported MyLite
tables, clearing rows and durable index roots inside the primary `.mylite`
file, resetting autoincrement state, preserving the table definition, and
surviving fresh-process reopen.
Persistent free-page ranges now let later row, index, and catalog page-chain
rewrites reuse complete obsolete ranges from accepted prior generations instead
of always allocating at EOF. Allocator metadata now lives in dedicated type-4
page chains instead of logical catalog `FREEPAGE` records. Accepted-generation
load now also reclaims complete unreferenced pages left by unpublished or
rejected generations and publishes them through the allocator payload on the
next successful write. Row payload free-range accounting tracks actual
variable-sized row page-chain lengths so recovery fallback generations remain
loadable after later rejected writes.
Supported MyLite row DML now registers with MariaDB's
transaction manager, defers durable `.mylite` generation publication until
commit, restores in-memory catalog and allocator snapshots on rollback, and
persists committed state across fresh-process reopen. MyLite savepoints are
now backed by the same THD-owned in-memory catalog and allocator snapshots for
the supported row-DML subset, including rollback to a savepoint set after a
MyLite read but before the first MyLite write. XA, transactional DDL,
page-level undo/redo, MVCC, and useful concurrent writer behavior remain
deferred. The storage smoke now also proves statement-level rollback on
multi-row duplicate-key insert errors in both autocommit and explicit
transaction modes, so partial statement changes do not leak through the current
transaction bridge. Configured primary files are now single-process owned with
an exclusive advisory lock held on the
`.mylite` file for the MyLite storage-engine lifetime; another process or
external advisory-lock holder causes an explicit catalog operation failure
until that lock is released, and the SQL-facing handler diagnostic now reports
a lock timeout instead of misleading index corruption. Public read-only opens
now start the process-scoped runtime in read-only mode, take a shared advisory
lock on the primary file, allow reads of existing MyLite rows, and reject
MyLite DDL/DML mutations with public `MYLITE_READONLY` diagnostics.
`MYLITE_OPEN_EXCLUSIVE` now supports create-or-fail primary-file opens for the
documented read-write create combination. MyLite schema names now persist as
catalog records, and embedded `CREATE DATABASE`, `DROP DATABASE`, `USE`,
`SHOW DATABASES`, `SHOW TABLES`, and relevant information-schema list paths
operate on the catalog namespace without requiring durable schema directories.
Server-owned schema names `mysql`, `performance_schema`, and `sys` are now
reserved so they cannot become ordinary MyLite catalog schemas before
replacement system surfaces are designed. Embedded startup now initializes the
foreign-server cache without probing `mysql.servers`, removing the inherited
missing-table diagnostic from MyLite smoke reports while keeping foreign-server
SQL explicitly unsupported. The default MyLite embedded profile now omits the
Aria storage engine, disables Aria-backed temporary tables, uses MyISAM as the
isolated MariaDB comparison reference, and treats any Aria log/control file in
MyLite runtime directories as an unexpected sidecar. Current bootstrap smokes
record no observed runtime files, and the grouped sidecar scan reports no known
inherited sidecars. New MyLite databases expose the default `mylite` schema
without injecting the old hard-coded `mylite.probe` seed table.

## Implementation plan

| Order | Slice | Status | Purpose |
| --- | --- | --- | --- |
| 0 | Project foundation | Done | Define the product goal, GPL baseline, architecture direction, workflow, and initial research. |
| 1 | `upstream-11-8-import` | Done | Import a pinned MariaDB 11.8 LTS source tag mechanically and record upstream refs. |
| 2 | `build-profile-minsize` | Done | Produce a reproducible embedded build, record artifact size, and document which server-only or rare optional components are omitted by default. |
| 3 | `embedded-bootstrap` | Done | Start an in-process MariaDB-derived runtime under MyLite-owned defaults without exposing daemon administration as the library model. |
| 4 | `unsupported-server-surface` | Done | Make daemon-only and unsupported features fail explicitly instead of leaking partial server behavior. |
| 5 | `libmylite-open-close` | Done | Add the first public C API for opening and closing a `.mylite` file with handle-owned diagnostics. |
| 6 | `storage-engine-skeleton` | Done | Add a static MyLite storage engine with enough handler shape for controlled smoke tests. |
| 7 | `mylite-engine-discovery` | Done | Reopen table definitions from the MyLite catalog through MariaDB table-discovery APIs. |
| 8 | `ddl-metadata-routing` | Done | Prove `CREATE`, `ALTER`, `DROP`, and `RENAME` do not leave durable `.frm` table-definition sidecars. |
| 9 | `single-file-catalog` | Done | Store initial frm-backed table definitions inside the `.mylite` file. |
| 10 | `file-format-recovery` | Done | Define and implement the first durable file header, page layout, catalog update protocol, and initial catalog recovery guarantees. |
| 11 | `row-index-storage` | Done | Implement the first durable heap row storage and core table-scan read/write/update/delete handler methods. |
| 12 | `index-autoincrement-storage` | Done | Add enforced key metadata, basic index access, and durable autoincrement state. |
| 13 | `compatibility-test-harness` | Done | Run embedded lifecycle, unexpected-sidecar detection, crash/reopen, and MariaDB comparison tests in repeatable groups. |
| 14 | `pager-page-store` | Done | Add the first reusable MyLite page-store layer for catalog payloads, row pages, future index pages, and free-space tracking. |
| 15 | `row-page-storage` | Done | Move simple row images from the logical catalog payload into typed row pages addressed through table catalog roots. |
| 16 | `row-slot-storage` | Done | Replace table-sized row payload streams with page-local row records and slot directories. |
| 17 | `index-page-storage` | Done | Add durable primary/secondary index page roots for supported keys instead of rebuilding all index cursors from rows. |
| 18 | `row-overflow-storage` | Done | Add overflow row payload segments so large non-BLOB fixed row images can span row pages. |
| 19 | `free-list-page-reuse` | Done | Persist and validate free page ranges, then reuse complete obsolete row and index page chains from accepted prior generations. |
| 20 | `orphan-page-reclaim` | Done | Reclaim complete unreferenced pages left by rejected or unpublished generations after recovery accepts a safe catalog generation. |
| 21 | `allocator-page-store` | Done | Store free-page ranges in a dedicated allocator page chain so catalog payload chains can reuse accepted free ranges. |
| 22 | `transaction-boundary-semantics` | Done | Make MyLite's current non-transactional rollback boundary explicit in engine flags, tests, and docs before real journal/WAL work. |
| 23 | `primary-file-locking` | Done | Hold an exclusive advisory lock on the primary `.mylite` file so concurrent processes fail explicitly before cross-process concurrency exists. |
| 24 | `catalog-error-diagnostics` | Done | Return accurate MariaDB handler diagnostics for MyLite catalog lock, open, load, and write failures instead of misleading generic corruption errors. |
| 25 | `deferred-transaction-publication` | Done | Register MyLite as a MariaDB transaction participant for supported DML and defer `.mylite` generation publication until commit, restoring in-memory snapshots on rollback. |
| 26 | `transaction-savepoint-snapshots` | Done | Add MyLite savepoint hooks backed by transaction-context snapshots for the supported row-DML subset. |
| 27 | `statement-error-rollback` | Done | Prove failed multi-row DML statements restore MyLite's pre-statement snapshot in autocommit and explicit transaction modes. |
| 28 | `blob-text-row-storage` | Done | Store non-key BLOB/TEXT row payloads inside existing row and overflow pages without persisting native row-buffer pointers. |
| 29 | `libmylite-exec` | Done | Add the first public SQL execution convenience API over the opened embedded MyLite handle. |
| 30 | `libmylite-statement-effects` | Done | Expose affected rows, generated insert ids, and warning counts through the public `libmylite` handle. |
| 31 | `libmylite-prepared-statements` | Done | Add the first public no-parameter prepared statement lifecycle and column accessors. |
| 32 | `libmylite-parameter-binding` | Done | Add the first public prepared-statement parameter binding API for NULL, numeric, text, and BLOB values. |
| 33 | `libmylite-warning-enumeration` | Done | Add structured warning, note, and error-condition retrieval through the public `libmylite` handle. |
| 34 | `libmylite-readonly-open` | Done | Enforce `MYLITE_OPEN_READONLY` through runtime startup, storage-engine locking, and public read-only diagnostics. |
| 35 | `libmylite-exclusive-open` | Done | Support `MYLITE_OPEN_EXCLUSIVE` for create-or-fail primary-file opens. |
| 36 | `blob-text-key-storage` | Done | Support non-null BLOB/TEXT prefix key parts in the current row and index storage bridge. |
| 37 | `nullable-key-storage` | Done | Support nullable key parts, including MariaDB-style unique-key NULL semantics, in the current row and index storage bridge. |
| 38 | `copy-alter-row-preservation` | Done | Prove populated copy ALTER preserves supported MyLite rows, indexes, BLOB/TEXT payloads, nullable keys, and autoincrement state. |
| 39 | `foreign-key-rejection` | Done | Reject MyLite foreign-key DDL explicitly until FK catalog, enforcement, locking, and cascade semantics are designed. |
| 40 | `generated-column-rejection` | Done | Historical conservative rejection slice, superseded by `generated-column-storage`. |
| 41 | `check-constraint-enforcement` | Done | Prove CHECK constraints are enforced and persisted for supported MyLite tables through inherited MariaDB semantics. |
| 42 | `schema-object-ddl-rejection` | Done | Reject persistent schema-object DDL that still depends on view, trigger, routine, package, or event metadata outside the MyLite catalog. |
| 43 | `unsupported-index-ddl-rejection` | Done | Historical rejection boundary for specialized index DDL; later slices superseded descending, HASH, FULLTEXT, and SPATIAL. |
| 44 | `unsupported-index-alter-rejection` | Done | Historical copy-ALTER rejection boundary; later slices superseded descending, HASH, FULLTEXT, and SPATIAL ALTER support. |
| 45 | `temporary-table-rejection` | Done | Prove `CREATE TEMPORARY TABLE ... ENGINE=MYLITE` fails without creating durable MyLite catalog entries. |
| 46 | `standalone-index-ddl-lifecycle` | Done | Prove standalone `CREATE INDEX` and `DROP INDEX` preserve MyLite rows, catalog metadata, and durable index roots. |
| 47 | `truncate-table-lifecycle` | Done | Implement handler-based `TRUNCATE TABLE` for supported MyLite tables, clearing rows and indexes while resetting autoincrement state. |
| 48 | `schema-namespace-catalog` | Done | Persist MyLite schema names in the catalog and route schema namespace operations without datadir directories. |
| 49 | `system-schema-namespace-policy` | Done | Keep server-owned schema names from becoming ordinary MyLite catalog schemas before replacement system surfaces are designed. |
| 50 | `foreign-server-cache-startup` | Done | Initialize the embedded foreign-server cache without probing missing `mysql.servers` system tables at startup. |
| 51 | `aria-startup-sidecars` | Done | Omit Aria from the default MyLite embedded profile and remove the remaining inherited Aria log/control sidecar exception. |
| 52 | `seed-probe-removal` | Done | Remove the hard-coded `mylite.probe` seed table now that user-created catalog tables cover discovery. |
| 53 | `libmylite-uri-open` | Done | Support local `file:` URI filenames through the existing public `MYLITE_OPEN_URI` flag. |
| 54 | `descending-hash-index-storage` | Done | Support descending BTREE key parts and HASH key algorithms in MyLite's durable key-image index stream. |
| 55 | `foreign-key-restrict-constraints` | Done | Persist and enforce MyLite restrict/no-action foreign keys through catalog metadata, key-image checks, and MariaDB FK metadata hooks. |
| 56 | `generated-column-storage` | Done | Support virtual generated columns, stored generated columns, stored generated-column indexes, and generated-column copy ALTER for supported MyLite rows. |
| 57 | `fulltext-index-storage` | Done | Support FULLTEXT DDL and common `MATCH ... AGAINST` searches through persisted metadata and a full-scan handler adapter. |
| 58 | `geometry-spatial-storage` | Done | Support GEOMETRY row storage, spatial functions over stored rows, and metadata-backed SPATIAL keys without physical R-tree roots. |
| 59 | `foreign-key-actions` | Done | Support FK `CASCADE` and `SET NULL` parent update/delete actions through recursive catalog-level child mutations. |
| 60 | `virtual-generated-index-storage` | Done | Support BTREE/HASH indexes and unique constraints over supported non-stored virtual generated columns. |
| 61 | `temporary-table-storage` | Done | Support user temporary MyLite tables through session-scoped in-memory definitions that do not persist to the primary `.mylite` file. |
| 62 | `composite-autoincrement-storage` | Done | Support non-leading `AUTO_INCREMENT` key parts through MariaDB's per-prefix handler lookup over MyLite key-image indexes. |
| 63 | `duplicate-key-dml-storage` | Done | Support `INSERT IGNORE`, `REPLACE`, and `INSERT ... ON DUPLICATE KEY UPDATE` through duplicate row-position reporting. |
| 64 | `application-engine-routing` | Done | Route omitted and common application `ENGINE=` clauses to MyLite in MyLite-owned embedded runtimes without creating external engine sidecars. |
| 65 | `wordpress-schema-storage` | Done | Prove WordPress-shaped core and plugin schema patterns, including utf8mb4 prefix keys, long unique keys, explicit InnoDB/MyISAM clauses, and `AUTO_INCREMENT=N`, through MyLite storage and reopen. |
| 66 | `application-alter-constraint-storage` | Done | Prove common application migration ALTER forms for keys, CHECK constraints, and foreign keys on populated MyLite tables through write, reopen, and recovery. |
| 67 | `persistent-view-storage` | Done | Store persistent MyLite views in the primary catalog and prove create, replace, alter, drop, show, reopen, and recovery behavior without durable `.frm` sidecars. |
| 68 | `persistent-trigger-storage` | Done | Store persistent MyLite triggers in the primary catalog and prove create, replace, drop, rename, show, execution, reopen, and recovery behavior without durable `.TRG` or `.TRN` sidecars. |
| 69 | `persistent-routine-storage` | Done | Store persistent MyLite procedures and functions in the primary catalog and prove create, replace, alter, drop, show, execution, reopen, and recovery behavior without durable `mysql.proc` sidecars. |
| 70 | `routine-information-schema` | Done | Expose catalog-backed MyLite procedures and functions through `information_schema.ROUTINES` and `information_schema.PARAMETERS` without durable `mysql.proc` sidecars. |
| 71 | `constraint-information-schema` | Done | Prove MyLite primary, unique, CHECK, and foreign-key metadata through `TABLE_CONSTRAINTS`, `REFERENTIAL_CONSTRAINTS`, and `CHECK_CONSTRAINTS` across write, reopen, and recovery. |
| 72 | `table-maintenance-statements` | Done | Support common healthy-table maintenance and diagnostic SQL: `CHECK`, `ANALYZE`, `OPTIMIZE`, `REPAIR`, `CHECKSUM`, `SHOW INDEX`, `DESCRIBE`, and `EXPLAIN`. |
| 73 | `readonly-temporary-tables` | Done | Allow session temporary MyLite tables under public read-only opens while keeping durable DDL/DML rejected and the primary file unchanged. |
| 74 | `temporary-foreign-key-metadata` | Done | Support session-local temporary MyLite foreign-key metadata, missing-parent checks, and cascades without durable catalog publication. |
| 75 | `application-query-compatibility` | Done | Compare common application query SQL against MariaDB/MyISAM, including joins, grouping, subqueries, unions, temporary CTAS, and query-driven DML. |
| 76 | `internal-temporary-spill-lifecycle` | Done | Force and compare disk-backed internal temporary spill over MyLite source rows, while proving cleanup leaves no persistent sidecars. |
| 77 | `virtual-generated-lob-geometry-storage` | Done | Support non-indexed virtual generated BLOB/TEXT and GEOMETRY row values without persisting virtual payload bytes. |
| 78 | `virtual-generated-lob-index-storage` | Done | Support BTREE/HASH prefix indexes, unique prefix constraints, and FULLTEXT keys over supported virtual generated BLOB/TEXT values. |
| 79 | `storage-engine-compatibility-matrix` | Done | Document where MyLite stands against InnoDB, MyISAM, and Aria across SQL surface, storage files, durability, recovery, concurrency, and performance. |

## Size and profile direction

MyLite should be smaller than a full MariaDB server distribution, but size is a
measured engineering constraint, not a slogan. The default embedded profile
should omit running-server-specific services and low-value optional components
that do not fit a local file-owned library, including:

- network listener and server account administration,
- replication, binlog, relay log, and Galera/wsrep,
- dynamic plugin loading and external durable storage engines,
- Aria startup logs and Aria-backed temporary tables in the MyLite runtime,
- performance schema and server audit plugins,
- rarely used optional engines or plugins unless a slice justifies them.

The `build-profile-minsize` slice should establish the first baseline size and
the exact component list. Later slices should record meaningful size changes
when they add or remove runtime surface.

## Research still needed

The existing research is enough to start implementation, but several decisions
need focused slice-level research before code is written:

- reproducible Linux build environment and macOS secondary build notes,
- DDL metadata routing through `CREATE`, `ALTER`, `DROP`, and `RENAME`,
- pager, B-tree, transaction, and crash recovery design,
- journal or WAL placement, companion-file lifecycle, and write concurrency
  target,
- minimal system schema policy and replacement for server-only tables,
- first compatibility test subset and unexpected-sidecar detector.

These should be answered inside the relevant slice specs, not as detached
general research.
