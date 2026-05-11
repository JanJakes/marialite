# Single-file storage design

This document describes the storage architecture that makes MyLite distinct
from embedded MariaDB.

## Definition

"Single file" means one primary database file, not "no other file is ever
created while the database is open." SQLite creates journals, WAL files, shared
memory files, and temporary files in some modes; MyLite can do the same when it
is the right durability or concurrency tradeoff.

- one primary user-visible database file, such as `app.mylite`,
- no persistent `.frm`, `.ibd`, `.MAI`, `.MAD`, `aria_log.*`, `ib_logfile*`, or
  binlog sidecars as independent MariaDB schema, table, log, or engine state,
- documented MyLite-owned companion files may be used for rollback journals,
  WAL, shared memory, locks, and temporary spill,
- an unclean shutdown may leave a journal or WAL file that is required for
  recovery; that file is part of the MyLite lifecycle, not a separate
  user-managed database asset,
- companion files need deterministic names, recovery rules, cleanup rules, and
  tests.

This is stricter than "one directory" or "one bundle," but less strict than
"never create a temporary or recovery companion file."

## Candidate designs

### 1. Virtual datadir container

Store a normal MariaDB datadir inside one container file and intercept file I/O.

Pros:

- fastest path to reuse existing engines,
- preserves more MariaDB behavior at first,
- can run existing Aria/InnoDB file formats inside the container.

Cons:

- requires a filesystem-like layer: directories, rename, stat, fsync, locks,
  sparse files, file growth, crash recovery, and path normalization,
- MariaDB uses both `mysys` wrappers and engine-specific OS file abstractions,
- keeps InnoDB/Aria complexity and size,
- produces "MariaDB in a box" rather than a purpose-built library,
- hard to reason about crash safety because there are nested recovery systems.

Use this only as a compatibility experiment, not as the primary design.

### 2. MyLite storage engine

Implement a new static storage engine that stores persistent catalog and table
state in `.mylite` and owns any recovery companion files it needs.

Pros:

- matches the product goal directly,
- avoids InnoDB and Aria durable sidecars,
- uses MariaDB's existing handler and table-discovery APIs,
- can be much smaller than carrying all default engines,
- keeps MariaDB SQL semantics above the handler layer.

Cons:

- requires a real transactional storage engine,
- requires DDL/catalog integration,
- requires careful mapping of MariaDB record formats, indexes, generated
  columns, constraints, and autoincrement,
- requires single-file recovery design.

This is the recommended architecture.

### 3. MariaDB SQL layer over SQLite

Use SQLite as the durable engine beneath MariaDB's handler API.

Pros:

- SQLite already solves single-file paging, locking, and recovery,
- binary size could stay lower than InnoDB,
- implementation can start by mapping MariaDB rows/indexes to SQLite tables.

Cons:

- MariaDB's SQL layer expects a storage engine, not another SQL engine,
- SQLite type, collation, transaction, and DDL semantics do not match MariaDB,
- using SQLite SQL execution would fight MariaDB's parser and optimizer,
- some handler operations need low-level ordered index access rather than SQL.

SQLite is worth evaluating as a pager/B-tree component, not as the SQL layer.

## Recommended file format

The `.mylite` file should have explicit regions:

```text
header
catalog pages
table/index pages
undo/redo or append log pages, unless these live in companion files
free space map
integrity/checkpoint metadata
```

The header should include:

- magic bytes,
- file-format version,
- MyLite library compatibility version,
- page size,
- endian marker,
- checksum mode,
- catalog root page,
- recovery/checkpoint pointers,
- flags for durability mode and feature gates.

The catalog should include:

- schemas,
- table definitions,
- table-definition binary images needed by MariaDB discovery,
- indexes,
- columns,
- constraints,
- triggers/views/procedures when supported,
- collation and character-set metadata,
- autoincrement state,
- engine-private table roots.

## Table definitions

MariaDB's SQL layer can open table definitions from a binary `.frm` image.
MyLite should use that rather than inventing a parallel definition system at
first. MariaDB's table-discovery API also supports initializing a `TABLE_SHARE`
from a SQL `CREATE TABLE` string, but storing the generated binary image is the
lower-risk first bridge because it preserves the exact definition MariaDB
produced.

1. On `CREATE TABLE`, let MariaDB produce the table definition image.
2. Store that image in the `.mylite` catalog.
3. Do not write a durable `.frm` file.
4. Implement `discover_table()` to initialize `TABLE_SHARE` from the stored
   image.
5. Implement `discover_table_names()` and `discover_table_existence()` so
   `SHOW TABLES`, `DROP DATABASE`, and information schema do not depend on
   directories.

Discovery covers reopening and listing metadata after it exists. The DDL write
path is separate: `CREATE`, `ALTER`, `DROP`, and `RENAME` must be traced so
MyLite either routes generated table-definition images directly into the catalog
or carries a narrow SQL-layer fork that suppresses durable `.frm` writes. This
needs a focused `ddl-metadata-routing` slice before single-file DDL is treated
as solved.

The current bridge now proves more than empty DDL routing for copy ALTER:
MariaDB-driven `ALTER TABLE ... ALGORITHM=COPY` can copy supported MyLite rows
into the altered table, materialize added-column defaults, rebuild supported
primary and secondary indexes, preserve nullable index entries and BLOB/TEXT
payloads, continue autoincrement from copied rows, and survive fresh-process
reopen. It still does not provide crash recovery for a process exit during the
DDL swap.
Standalone `CREATE INDEX` and `DROP INDEX` route through MariaDB's
`mysql_alter_table()` machinery as copy-ALTER-backed operations for MyLite.
The storage smoke verifies that path preserves rows, updates persisted table
definitions, publishes the final durable index-root state across reopen, and
does not leave `.frm` sidecars when the in-place ALTER probe falls back to copy
ALTER.
`TRUNCATE TABLE` uses MariaDB's handler truncate path for MyLite rather than
table recreation. The storage engine clears row payload roots, clears durable
index roots, resets table-local autoincrement state, and keeps the existing
table definition image in the primary `.mylite` file.

The catalog must also store or derive the table definition version used to
detect stale cached definitions. MariaDB's discovery documentation describes
`HA_ERR_TABLE_DEF_CHANGED` and `tabledef_version` as the mechanism for telling
the server a table definition changed unexpectedly.

Later, table definitions can be normalized into a MyLite catalog format or
stored as SQL text, but storing the exact MariaDB image is the safer first step.

The first implemented catalog file format uses two fixed 4096-byte header slots
at offsets 0 and 4096, with append-only catalog payload blobs starting at offset
8192. Each header names a catalog payload by offset, length, generation, and
checksum. Loading chooses the newest valid header whose payload checksum
matches; writing appends the payload first, flushes it, then publishes the
inactive header. This protects table-definition catalog publication from a
corrupted latest payload, but it is not yet a row-storage pager or full
transaction recovery system.

The first row-storage proof stores simple non-BLOB row images with hidden
64-bit row ids. It now writes those row images into typed per-table row payload
page chains addressed by catalog `ROWPAGE` roots, rather than as `ROW` records
inside the logical catalog payload. New writes use page-local binary row slot
directories and packed record bytes inside row payload pages. This validates
MariaDB handler read/write/update/delete integration, fresh-process
persistence, and row-page recovery fallback, but it is still a temporary
raw-record bridge. Page reuse, typed column encoding, transaction recovery, and
durable B-tree index pages are still needed.

The current bridge layer accepts supported BTREE/undefined keys and
autoincrement columns. It stores leading-key autoincrement counters in catalog
payload records and derives non-leading composite-key autoincrement values
through MariaDB's inherited per-prefix handler lookup over the current durable
key-image stream. MyLite now stores durable primary and secondary key-entry
streams in typed index payload pages addressed by catalog `INDEXPAGE` roots.
This proves MariaDB's indexed handler path, nullable key-image ordering,
per-prefix autoincrement generation, and uniqueness enforcement, but it is
still not the final B-tree storage architecture.
For duplicate-key DML, MyLite reports both the duplicate key ordinal and the
offending row id through MariaDB's `lookup_errkey`/`dup_ref` handler path.
That lets inherited SQL execution perform `INSERT IGNORE`, `REPLACE`, and
`INSERT ... ON DUPLICATE KEY UPDATE` against MyLite rows while preserving the
same durable row and index publication machinery used by ordinary DML.
MyLite-owned embedded runtimes set MyLite as the default, temporary, and
enforced storage engine. Ordinary application DDL with no `ENGINE=` clause,
explicit `ENGINE=InnoDB`, explicit `ENGINE=MyISAM`, `CREATE TABLE ... LIKE`,
`CREATE TABLE ... SELECT`, `ALTER TABLE ... ENGINE=InnoDB`, and user-created
temporary tables route to the MyLite handler instead of creating external
durable engine sidecars. The MyLite-only enforcement path intentionally
overrides MariaDB's default `NO_ENGINE_SUBSTITUTION` rejection for these
application engine clauses; non-MyLite reference runtimes keep inherited
MariaDB behavior.
The grouped MariaDB-reference comparison now exercises common application query
composition over MyLite rows and indexes: joins, left joins, grouped HAVING
queries, derived-table ORDER/LIMIT queries, `IN` and `EXISTS` subqueries,
`UNION ALL`, temporary `CREATE TABLE ... SELECT`, `INSERT ... SELECT`, and
multi-table `UPDATE`/`DELETE`. Those statements remain MariaDB SQL-layer
features above the handler API, but the comparison proves MyLite's row scan,
index, temporary-table, and row-mutation hooks match the embedded MyISAM
reference for the covered observable results.
WordPress-shaped core and plugin tables are covered by the storage smoke. The
current storage bridge preserves utf8mb4 charset/collation table options,
display-width integer metadata, long text family columns, zero datetime
defaults, prefix and multi-column indexes, explicit InnoDB/MyISAM engine
clauses routed to MyLite, and SQL-dump-style `AUTO_INCREMENT=N` across
fresh-process reopen. MyLite advertises MariaDB's 3072-byte key and key-part
limit for the current profile, so common utf8mb4 `varchar(191)` WordPress
indexes are stored as ordinary key images. Genuinely overlong unique keys use
MariaDB's `HA_KEY_ALG_LONG_HASH` hidden generated hash key; MyLite stores and
searches that generated key image while MariaDB's SQL/handler layer performs
the long-unique duplicate check against the original user columns.
Application migration DDL is now covered for common populated-table ALTER
forms. The storage smoke verifies `ALTER TABLE` adding primary, unique, and
secondary keys, renaming indexes, dropping indexes, adding and dropping CHECK
constraints, and adding and dropping foreign keys. The write phase proves
unique, CHECK, and FK violations are rejected while those constraints exist;
the final altered metadata and row payloads survive fresh-process reopen and
recovery read after the constraints are dropped.

The current primary file format stores catalog payload generations, allocator
metadata, row payloads, and index payloads in typed 4096-byte page chains. The
two fixed header slots now publish v3 catalog generations with both a logical
catalog payload root and a dedicated allocator payload root. The catalog
generation points to table row and index roots. The page store has catalog,
row, index, and allocator page types. New v3 catalog payloads no longer contain
`FREEPAGE` records; free ranges are serialized in allocator payload pages with
magic `MYLITE FREE LIST 1`. Row, index, and catalog page-chain writers can
reuse complete obsolete ranges from accepted prior generations, while the
allocator payload itself remains append-only for now to avoid self-reference.
Loading still accepts pre-release v2 catalog generations with catalog-embedded
`FREEPAGE` records, then rewrites through the v3 path on the next durable
write. Loading an accepted generation also scans complete pages and merges
pages not protected by the accepted catalog, allocator payload, row roots,
index roots, or existing free ranges into the in-memory free list. Those orphan
pages, including pages left by a rejected newer generation, are published in
the allocator payload on the next successful write. Transaction/recovery pages
still need dedicated formats before the raw-record bridge can be retired.

Supported MyLite row DML now participates in MariaDB transactions. The engine
registers statement and write-transaction participation through
`external_lock()` and DML mutation paths, captures in-memory catalog and
allocator snapshots before the first supported row mutation, defers durable
`.mylite` header publication until commit, and restores snapshots on rollback.
The storage smoke verifies that rolled-back DML returns to the baseline row
state with no warning `1196`, and that committed DML survives fresh-process
reopen. It also verifies that failed multi-row duplicate-key inserts restore
the pre-statement snapshot in autocommit and explicit transaction modes without
leaking partial rows.

MyLite savepoints reuse the same in-memory transaction context. MariaDB
savepoint storage holds a small MyLite savepoint ID, while the actual catalog
and allocator snapshots stay in the THD-owned MyLite transaction context.
Savepoints are captured even when MyLite is only a clean read participant so
far, so a later MyLite write can roll back to a savepoint established after a
read. The storage smoke verifies rollback to savepoint, release savepoint, and
fresh-process reopen after committing post-savepoint state.

This is still a bridge over the current whole-generation, in-memory row/index
storage model. It gives atomic commit, rollback, and savepoint rollback for the
supported DML subset without adding a journal companion file yet. It is not the
final pager design: page-level undo/redo, XA, transactional DDL, MVCC, and
useful concurrent writer behavior still need dedicated formats and tests before
the bridge can be retired.

Configured primary files are currently single-process owned. MyLite opens the
primary `.mylite` file with a retained descriptor, takes a nonblocking
exclusive advisory lock through MariaDB's `my_lock()` with `MY_FORCE_LOCK`, and
keeps that descriptor for the storage-engine lifetime. Catalog load and write
I/O use the retained descriptor so POSIX record locks are not lost by closing a
second descriptor for the same file. A second process or external
advisory-lock holder fails explicitly until the lock is released. This adds no
lock sidecar and does not claim cross-process reader/writer concurrency.
Catalog lock/open/write failures are mapped to existing MariaDB handler
diagnostics where possible; advisory-lock conflicts now surface as lock
timeouts instead of generic index corruption, while invalid on-disk catalog
contents remain corruption failures.

Supported fixed MariaDB record images larger than one row slot page now split
across `MYLITEROWOVF3` segment payloads inside row page type `2`. This lifts the
one-page row-size limit for non-BLOB rows while preserving the raw fixed-record
bridge. Non-key BLOB/TEXT columns now use the same row and overflow page
storage: MyLite stores a fixed record prefix with native BLOB pointer bytes
cleared, appends BLOB/TEXT payload bytes in MariaDB `TABLE_SHARE::blob_field`
order, and reconstructs `Field_blob` pointers into handler-owned read buffers
on table scan, position read, and index read paths. BLOB/TEXT prefix key parts
and nullable key parts are supported through MariaDB key-image bytes in
existing `INDEXPAGE` payloads. MyLite builds key images from incoming MariaDB
records directly, but decodes stored MyLite rows into temporary MariaDB record
buffers first so key generation never reads cleared native BLOB pointer bytes.
Nullable unique keys allow multiple rows when any user key part is NULL and
still reject duplicate all-non-NULL key tuples. Descending BTREE key parts are
supported by sorting durable key-image streams with MariaDB's
`HA_REVERSE_SORT` metadata. `USING HASH` secondary indexes are accepted and
maintained through the same durable key-image stream; this preserves SQL
results but is not yet a physical hash-table layout. FULLTEXT indexes are
accepted and persisted in the MariaDB table-definition image, but they are not
written as ordered `INDEXPAGE` roots. MyLite currently evaluates common
`MATCH ... AGAINST` natural and boolean term searches through a handler-owned
full scan over stored rows; a durable inverted fulltext page format remains a
future performance and compatibility slice. GEOMETRY columns are accepted as
MariaDB blob-backed fields and persisted through the same row and overflow
payload path used by BLOB/TEXT columns. SPATIAL indexes are accepted as
metadata-backed RTREE definitions for valid non-null one-part geometry keys,
but they are not written as ordered `INDEXPAGE` roots and MyLite does not yet
advertise ordered/range reads for them. SQL-layer spatial predicates still
evaluate over stored rows; a physical R-tree or handler-level MBR scan remains
a future performance and compatibility slice.
Foreign keys are supported for MyLite tables by storing key-definition and FK
metadata in the catalog, comparing durable key-image prefixes for referential
checks, exposing FK metadata through MariaDB handler hooks and information
schema (`KEY_COLUMN_USAGE`, `TABLE_CONSTRAINTS`, and
`REFERENTIAL_CONSTRAINTS`), and honoring `foreign_key_checks=0` for row
checks. Nullable child key parts use MariaDB's match-simple behavior: rows with
NULL FK parts are accepted, while non-NULL nullable key images are compared
without the nullable marker byte and with MariaDB `key_cmp()` equality.
`CASCADE` and `SET NULL` parent update/delete actions mutate prelocked child
tables inside the same in-memory catalog generation, refresh every touched
child index, recurse through further child references with a depth limit of 15,
and roll back through the existing statement/transaction snapshots on failure.
`SET DEFAULT` remains unsupported because MariaDB/InnoDB still leave that
action unimplemented.
Generated columns are supported when MariaDB's SQL and handler layers can own
the expression lifecycle above MyLite's raw-record bridge. MyLite advertises
`HA_CAN_VIRTUAL_COLUMNS`, persists generated-column metadata inside the stored
MariaDB table-definition image, lets MariaDB compute non-indexed virtual
generated values after row reads, and stores stored generated-column values in
normal row payloads. Stored generated-column indexes use existing durable
`INDEXPAGE` key-image streams. Supported BTREE/HASH indexes on non-stored
virtual generated columns use the same durable key-image stream after MyLite
temporarily materializes the virtual key-part values through MariaDB's
generated-expression evaluator during index maintenance and rebuilds. MyLite
also accepts non-indexed non-stored virtual BLOB/TEXT and GEOMETRY generated
row values by skipping those non-durable virtual fields in the row
BLOB/GEOMETRY encode/decode bridge, clearing their transient fixed record
bytes before storage, and letting MariaDB materialize them after handler reads.
Virtual generated BLOB/TEXT prefix indexes, unique prefix constraints, HASH
metadata keys, and FULLTEXT definitions are supported when MariaDB accepts the
generated expression and key shape. Ordinary key parts reuse the existing
durable `INDEXPAGE` key-image stream, and FULLTEXT definitions remain
metadata-backed while MyLite materializes virtual generated text before
full-scan scoring. Virtual generated GEOMETRY SPATIAL keys remain constrained
by MariaDB SQL because virtual generated columns cannot be declared `NOT NULL`,
while SPATIAL keys require non-null geometry fields.
CHECK constraints are accepted for supported MyLite tables through MariaDB's
table-definition metadata and SQL-layer expression evaluation. MyLite persists
the generated table-definition image in the catalog and rediscovery restores
CHECK metadata after fresh-process reopen; the current storage smoke proves
invalid INSERT and UPDATE statements fail without mutating valid rows, and
that active CHECK metadata is visible through `TABLE_CONSTRAINTS` and
`CHECK_CONSTRAINTS` while dropped CHECK metadata is absent after reopen and
recovery. MyLite does not own a separate CHECK expression evaluator.

Common healthy-table maintenance statements are supported through MyLite
handler admin methods. `CHECK TABLE` validates that the opened catalog
definition exists, live row images match the MariaDB table share, and physical
BTREE/HASH index roots match opened key metadata. `ANALYZE TABLE` refreshes
volatile exact row-count stats, while `OPTIMIZE TABLE` and `REPAIR TABLE`
currently run the same validation and return OK for healthy opened tables.
Physical page compaction and offline salvage of unreadable primary files remain
future durability/recovery work. `CHECKSUM TABLE` uses MariaDB's inherited
row-scan checksum path, and `SHOW INDEX`, `DESCRIBE`, and `EXPLAIN` continue to
flow through MariaDB's table metadata, optimizer, `info()`, indexed access, and
range-count hooks.

Persistent views are supported for MyLite schemas without durable `.frm`
sidecars. MyLite serializes MariaDB's canonical text view definition into the
primary catalog, discovers it with a memory-backed file parser, and leaves
MariaDB's view expansion, `SHOW CREATE VIEW`, and information-schema behavior
in place. The storage smoke verifies `CREATE VIEW`, `CREATE OR REPLACE VIEW`,
`ALTER VIEW`, `DROP VIEW`, fresh-process reopen, and recovery read.

Persistent triggers are supported for MyLite schemas without durable `.TRG` or
`.TRN` sidecars. MyLite stores the serialized per-table `TYPE=TRIGGERS`
definition and schema-wide trigger-name mapping in the primary catalog, then
loads MariaDB's `Table_triggers_list` through the memory-backed file parser.
The storage smoke verifies `CREATE TRIGGER`, `CREATE OR REPLACE TRIGGER`,
`CREATE TRIGGER IF NOT EXISTS`, `DROP TRIGGER`, `SHOW CREATE TRIGGER`,
information-schema visibility, trigger firing for INSERT/UPDATE/DELETE,
table rename, fresh-process reopen, recovery read, and table-drop cleanup.

Persistent standalone stored procedures and stored functions are supported for
MyLite schemas without durable `mysql.proc` sidecars. MyLite stores the
routine metadata MariaDB normally persists in `mysql.proc` as schema-level
catalog records keyed by schema, routine type, and routine name. MariaDB still
owns routine parsing, body compilation, cache invalidation, `CALL`, stored
function invocation, characteristic rendering, and `SHOW CREATE`; MyLite
replaces only the persistence and enumeration bridge for MyLite schemas. The
storage smoke verifies `CREATE PROCEDURE`, `CREATE FUNCTION`,
`CREATE OR REPLACE FUNCTION`, `IF NOT EXISTS`, `ALTER`, `DROP`,
`SHOW CREATE`, `CALL`, stored function invocation,
`information_schema.ROUTINES`, `information_schema.PARAMETERS`,
fresh-process reopen, recovery read, and dropped-function metadata absence.
Packages, package bodies, and events remain unsupported persistent schema
objects because their metadata and scheduler semantics still depend on separate
`mysql.*` system-table designs.

User-created temporary MyLite tables are supported as session-scoped in-memory
definitions that reuse the current row and index storage structures without
publishing durable catalog, row, index, or free-page records. MariaDB's
temporary-table lifecycle owns open-table reuse and cleanup, while MyLite marks
the handler instances that were opened against temporary definitions so
internal copy-ALTER replacement tables continue to use the durable catalog
path. The storage smoke verifies temporary table DDL/DML, indexed reads,
autoincrement, truncate, shadowing of a durable table name, drop cleanup, and
fresh-process absence. Public read-only handles may also create, mutate, query,
truncate, and drop session temporary MyLite tables; temporary-only changes keep
transaction snapshots but do not flush the primary `.mylite` file. Temporary
MyLite foreign keys are supported as session-local metadata: same-session
temporary parent references are enforced, visible through `SHOW CREATE TABLE`,
and can cascade into temporary child rows without durable catalog publication.
Disk-backed MariaDB internal temporary spill is currently supported through
the inherited MyISAM internal temporary-table path because the MyLite profile
disables Aria and `USE_ARIA_FOR_TMP_TABLES`. Those files are transient
statement/runtime scratch under the configured temporary runtime directory, not
durable table sidecars: MariaDB's internal temporary-table cleanup drops them
before shutdown, and the compatibility sidecar scan fails if any `.MYD`,
`.MYI`, Aria, InnoDB, binlog, or catalog-temp file remains. The compatibility
harness forces this path with `tmp_memory_table_size=0` and `big_tables=1`
over MyLite source rows and verifies `Created_tmp_disk_tables` increments. A
native MyLite spill-file format remains future work.

Current row payload pages are variable-sized slot and overflow pages. Runtime
free-page accounting tracks their actual page-chain length in memory instead
of deriving it from logical payload bytes; this keeps accepted catalog
generations recoverable after a later generation is rejected or corrupted.

## Schemas

MariaDB's `database.table` model now maps MyLite schemas to namespaces inside
the catalog:

```text
schema_id -> schema name
table_id -> schema_id + table name
index_id -> table_id + index name
```

No persistent directory is created for a MyLite schema. The current catalog
payload stores explicit `SCHEMA` records, while the loader still accepts older
pre-release catalog generations by seeding `mylite` and deriving schema names
from table definitions.

In the embedded MyLite namespace, `CREATE DATABASE`, `DROP DATABASE`, `USE`,
`SHOW DATABASES`, `SHOW TABLES`, and the relevant `information_schema.SCHEMATA`
and `information_schema.TABLES` list paths use catalog helpers instead of
datadir directory scans. Empty schemas persist because their names are catalog
records, not inferred from table directories.

The built-in `mylite` schema remains the default namespace, but new databases
no longer expose the old hard-coded `mylite.probe` seed table. Dropping or
replacing the default schema is still unsupported. The inherited
table-definition bridge still has narrow transient `.frm` compatibility paths
for copy ALTER, standalone index DDL, and table discovery. MyLite skips those
transient `.frm` writes or renames only for catalog schemas that do not have
directories; the final normalized metadata catalog remains a later design.

## System schema

MariaDB expects a `mysql` system schema for grants, plugins, time zones, events,
and related tables. MyLite should not blindly create the normal system
schema as Aria tables in a datadir.

Initial policy:

- no network users,
- no password authentication,
- no dynamic plugin installation,
- no replication metadata,
- no event scheduler in the first profile,
- time zone support starts with `SYSTEM` and named time zones can be added later.

Current MyLite schema namespace policy reserves `mysql`,
`performance_schema`, and `sys` so they cannot become ordinary catalog schemas
through `CREATE DATABASE` or `DROP DATABASE`. `information_schema` remains
virtual through MariaDB's existing schema-table machinery. Replacement
`mysql.*` tables, performance schema tables, and any `sys` helper views still
need separate designs before those names can expose MyLite-owned system
surfaces.

Embedded startup initializes MariaDB's foreign-server cache through the
existing no-table path, so MyLite no longer probes the absent `mysql.servers`
table during startup. Foreign-server SQL remains explicitly unsupported until
MyLite has a catalog or virtual-table design for that metadata.

Implementation options:

- store minimal system tables in the MyLite engine,
- replace some system tables with read-only virtual tables,
- compile out or hard-disable unsupported subsystems,
- keep `information_schema` virtual, because it is already a server-generated
  surface.

## Transactions and recovery

MyLite must decide early whether to use:

- rollback journal inside the main file,
- append-only redo log pages inside the main file,
- external rollback journal or WAL companion files,
- shadow paging,
- SQLite pager integration,
- a small embedded KV/pager library with a compatible license.

An external rollback journal or WAL is acceptable if it is MyLite-owned,
recoverable after a crash, and checkpointed or cleaned up according to a
documented lifecycle. It must not become a generic MariaDB datadir or a set of
unbounded durable engine files.

Minimum v1 guarantees:

- atomic commit for a single connection,
- crash recovery after process or OS crash, including any journal or WAL
  companions,
- no torn catalog updates,
- checksum or equivalent corruption detection for critical pages,
- file lock that prevents unsafe concurrent writers.

Stretch guarantees:

- multiple read connections in one process,
- concurrent write transactions in one process where the storage design can
  preserve them safely,
- multiple read processes,
- one writer across processes,
- online backup snapshots,
- incremental vacuum/compaction.

## Locking

The first implementation should prefer correctness while avoiding design choices
that permanently rule out useful write concurrency:

- one process may open the database for write,
- multiple handles in that process are coordinated by a shared runtime object,
- a read-only process opens the primary file read-only, holds a shared advisory
  lock, and rejects MyLite catalog or row mutations,
- v1 may serialize writes if the first pager or recovery design requires it,
- a second process gets `MYLITE_BUSY` or read-only access until cross-process
  locking is implemented and tested.

MyLite should preserve MariaDB-style in-process write concurrency where it can
be implemented safely above the selected storage design. Cross-process writes
need explicit locking and recovery design before they are promised.

## Temporary data

Temporary tables, query spill files, and recovery companion files are policy
decisions, not violations of the primary `.mylite` database-file model.

Recommended v1:

- MyLite-owned rollback journal, WAL, shared-memory, and lock files are allowed
  when a slice justifies them,
- memory temporary tables first,
- temp directory spill allowed for large sorts/internal temp tables,
- query temp files are non-durable and not part of the database file,
- document that "single file" means one primary database file, not "never
  creates a companion file while running."

Strict no-temp-file mode can be added as a configuration option with lower
query limits. A stricter no-companion-files mode is separate and may be
incompatible with some durability or concurrency modes.

## Durability modes

Expose a small set of modes:

- `MYLITE_DURABILITY_FULL`: fsync at transaction boundaries.
- `MYLITE_DURABILITY_NORMAL`: safe against process crashes, weaker against OS
  crashes depending on platform.
- `MYLITE_DURABILITY_OFF`: test/development only.

These should be MyLite API options, not raw MariaDB server options.

## Migration

MyLite should not try to open an arbitrary MariaDB datadir as a `.mylite`
file. Migration should be logical:

- import SQL dump,
- optional direct import tool for a stopped MariaDB datadir later,
- export SQL dump compatible with MariaDB where possible.

## Open questions

- Should v1 use a custom pager or integrate SQLite's pager/B-tree code?
- Should rollback journal or WAL state live inside the `.mylite` file or in
  documented companion files?
- What write concurrency should v1 preserve in one process?
- Which ALTER TABLE operations need native or in-place implementations beyond
  MariaDB's copy-rebuild path?
- Which MariaDB tests can be made storage-engine-agnostic and run first?
- Which hidden storage-engine dependencies remain after the default MyLite
  profile omits Aria?
- What minimum collation set is acceptable for a small default build?
- How should physical fulltext indexes and physical R-tree spatial indexes be
  phased into the MyLite engine?
