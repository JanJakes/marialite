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

The current bridge layer accepts supported non-null BTREE/undefined keys and
autoincrement columns. It stores autoincrement counters in catalog payload
records and now stores durable primary and secondary key-entry streams in typed
index payload pages addressed by catalog `INDEXPAGE` roots. This proves
MariaDB's indexed handler path and uniqueness enforcement, but it is still not
the final B-tree storage architecture.

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
reopen.

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
bridge. BLOB/TEXT tables remain unsupported because MariaDB blob fields carry
pointer-backed row-buffer state that needs a separate durable representation.

## Schemas

MariaDB's `database.table` model should map to namespaces inside the catalog:

```text
schema_id -> schema name
table_id -> schema_id + table name
index_id -> table_id + index name
```

No persistent directory should be created for a schema. `CREATE DATABASE`,
`DROP DATABASE`, `USE`, and table-name resolution become catalog operations.

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
- How much ALTER TABLE should be native before relying on table rebuilds?
- Which MariaDB tests can be made storage-engine-agnostic and run first?
- Can all persistent system tables move to the MyLite engine without hidden
  Aria dependencies?
- What minimum collation set is acceptable for a small default build?
- How should foreign keys, generated columns, fulltext indexes, and spatial
  indexes be phased into the MyLite engine?
