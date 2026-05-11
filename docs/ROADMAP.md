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
binding for NULL, numeric, text, and BLOB values. The first static
`MYLITE` storage-engine skeleton is registered in the embedded profile.
The engine can discover the seed table `mylite.probe`, run a bounded `CREATE`,
copy `ALTER`, `RENAME`, and `DROP` lifecycle without leaving durable `.frm`
table-definition files, persist frm-backed table definitions in the primary
`.mylite` file across fresh embedded processes, prove populated copy `ALTER`
preserves supported rows, BLOB/TEXT payloads, rebuilt indexes, nullable keys,
and autoincrement state, and recover the previous valid catalog generation
when the latest append-only catalog payload is corrupted.
It can store simple non-BLOB rows, enforce supported primary and unique keys,
serve basic ordered index access, and persist table-local autoincrement state
in the `.mylite` payload. A grouped compatibility harness now runs the embedded
lifecycle, `libmylite` lifecycle, storage/recovery smokes, a MariaDB-reference
comparison for the supported subset, and a MyLite runtime sidecar scan. The
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
Foreign-key DDL is now rejected explicitly for MyLite tables until FK catalog,
referential checks, locking, and cascade semantics are designed.
Generated-column DDL is also rejected explicitly until expression metadata,
materialization, generated indexes, and ALTER recomputation are designed.
CHECK constraints are supported for MyLite tables through inherited MariaDB
SQL-layer semantics; the current smoke proves invalid INSERT and UPDATE
statements are rejected, valid rows remain unchanged, and CHECK metadata
survives fresh-process reopen through the persisted table-definition image.
Persistent schema-object DDL for views, triggers, stored routines, packages,
and events is now rejected explicitly until those object definitions can live
in the MyLite catalog without durable MariaDB datadir sidecars or hidden
`mysql.*` system table writes.
FULLTEXT, SPATIAL, HASH, and descending indexes are covered as explicit DDL
rejections until their search, geometry, hash, ordering, and storage semantics
are designed for MyLite's durable index format.
Unsupported index additions through copy ALTER are being covered separately so
failed replacement-table definitions do not mutate valid MyLite rows or
catalog state.
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
documented read-write create combination.

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
| 40 | `generated-column-rejection` | Done | Reject MyLite generated-column DDL explicitly until expression storage, materialization, indexes, and ALTER recomputation are designed. |
| 41 | `check-constraint-enforcement` | Done | Prove CHECK constraints are enforced and persisted for supported MyLite tables through inherited MariaDB semantics. |
| 42 | `schema-object-ddl-rejection` | Done | Reject persistent schema-object DDL that still depends on view, trigger, routine, package, or event metadata outside the MyLite catalog. |
| 43 | `unsupported-index-ddl-rejection` | Done | Prove FULLTEXT, SPATIAL, HASH, and descending indexes fail explicitly without storing unsupported MyLite table definitions. |
| 44 | `unsupported-index-alter-rejection` | In progress | Prove failed unsupported-index copy ALTER statements leave existing MyLite rows and table definitions intact. |

## Size and profile direction

MyLite should be smaller than a full MariaDB server distribution, but size is a
measured engineering constraint, not a slogan. The default embedded profile
should omit running-server-specific services and low-value optional components
that do not fit a local file-owned library, including:

- network listener and server account administration,
- replication, binlog, relay log, and Galera/wsrep,
- dynamic plugin loading and external durable storage engines,
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
