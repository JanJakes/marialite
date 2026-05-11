# Storage engine compatibility matrix

Snapshot: 2026-05-14, MyLite `bee7e2e576c26985a3f0e12c47b46bd99a5ddafd`
on MariaDB Server `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

This matrix compares the current MyLite storage engine with the MariaDB
InnoDB, MyISAM, and Aria engines from an application and storage-architecture
point of view. It is intentionally split by capability type because "supports
SQL syntax" and "matches InnoDB's storage guarantees" are different claims.

Status legend:

- `✅ Supported`: supported and covered by current MyLite source and
  smoke/compatibility reports.
- `🟡 Partial`: usable with an explicit MyLite-specific boundary.
- `🟡 Needs coverage`: expected to work through existing MyLite/MariaDB paths,
  but not yet covered by targeted matrix tests.
- `🚧 Deferred`: compatible design target, but not implemented yet.
- `❌ Unsupported`: unsupported or intentionally omitted.
- `⚪ N/A`: not relevant to that engine or product shape.
- Plain `Yes`/`No` in the InnoDB, MyISAM, and Aria columns describes the
  reference engine behavior. Emoji markers are used in MyLite columns.

## At a Glance

| Area | MyLite status | Practical reading |
| --- | --- | --- |
| ✅ Common application SQL | ✅ Supported | Representative schemas, migrations, rows, constraints, indexes, triggers, views, routines, temporary tables, and maintenance statements pass current local coverage. |
| ✅ Single primary `.mylite` file | ✅ Supported | Durable MyLite tables live in the primary file without `.frm`, `.ibd`, `.MYD`, `.MYI`, `.MAI`, `.MAD`, or Aria/InnoDB log sidecars. |
| ✅ Core rows and indexes | ✅ Supported | Primary, unique, secondary, composite, nullable, prefix, generated-column, FULLTEXT, and SPATIAL metadata paths are implemented for the covered SQL surface. |
| ✅ Constraints | ✅ Supported | `NOT NULL`, primary/unique constraints, CHECK constraints, foreign-key metadata, restrict/no-action, cascade, `SET NULL`, and `foreign_key_checks=0` are covered. |
| ✅ Persistent schema objects | ✅ Supported | Catalog-backed databases, tables, views, triggers, stored procedures, and stored functions persist inside `.mylite`. |
| 🟡 Transactions | 🟡 Partial | `COMMIT`, `ROLLBACK`, savepoints, and statement rollback work for supported row DML; full crash-recoverable page transactions remain deferred. |
| 🟡 FULLTEXT/SPATIAL performance | 🟡 Partial | DDL and common queries work, but current execution uses metadata/scans rather than final inverted-index or R-tree storage. |
| 🟡 Data-type breadth | 🟡 Needs coverage | Raw MariaDB record storage should cover many scalar families, but ENUM/SET/JSON/UUID/INET/date/time/decimal need targeted matrix coverage before `✅ Supported`. |
| 🚧 InnoDB-grade internals | 🚧 Deferred | MVCC, read views, row/gap locks, concurrent write transactions, redo/undo/WAL, online DDL, XA, compression, and encryption are not implemented. |
| ❌ Server/datadir surfaces | ❌ Unsupported | Users/auth, replication/binlog, dynamic plugins, external durable engine files, and independent datadir ownership are omitted from the default embedded profile. |

## Source Baseline

- MariaDB documentation describes InnoDB as transactional, ACID-compliant, with
  online DDL, transaction logging, purge, non-locking reads, and row locking:
  <https://mariadb.com/docs/server/server-usage/storage-engines/innodb/innodb-storage-engine-introduction>
- MariaDB documentation describes MyISAM as non-transactional, without foreign
  keys, using `.frm`, `.MYD`, and `.MYI` files, with table locking and
  concurrent inserts:
  <https://mariadb.com/docs/server/server-usage/storage-engines/myisam-storage-engine/myisam-overview>
- MariaDB documentation describes Aria as a crash-safe MyISAM successor with
  Aria logs and page formats; its `TRANSACTIONAL` option means crash-safe, not
  true SQL transactions:
  <https://mariadb.com/docs/server/server-usage/storage-engines/aria/aria-storage-engine>
- MariaDB foreign-key documentation says current foreign keys are supported
  only by InnoDB, with no `SET DEFAULT`, no prefix-index FKs, and other limits:
  <https://mariadb.com/docs/server/ha-and-performance/optimization-and-tuning/optimization-and-indexes/foreign-keys>
- MariaDB full-text documentation says FULLTEXT indexes are supported for
  MyISAM, Aria, InnoDB, and Mroonga tables, with engine-specific search rules:
  <https://mariadb.com/docs/server/ha-and-performance/optimization-and-tuning/optimization-and-indexes/full-text-indexes/full-text-index-overview>
- MyLite source evidence:
  - `tools/build-mariadb-minsize.sh` disables `PLUGIN_INNOBASE`, `PLUGIN_ARIA`,
    `PLUGIN_PARTITION`, dynamic plugins, Aria temp tables, and most optional
    engines in the default embedded profile.
  - `vendor/mariadb/server/storage/mylite/ha_mylite.h` advertises
    `HA_NULL_IN_KEY`, `HA_CAN_INDEX_BLOBS`, `HA_CAN_FULLTEXT`,
    `HA_CAN_GEOMETRY`, `HA_CAN_RTREEKEYS`, `HA_CAN_VIRTUAL_COLUMNS`,
    `HA_CAN_REPAIR`, and exact record stats.
  - `vendor/mariadb/server/storage/mylite/ha_mylite.cc` implements MyLite
    commit/rollback/savepoint hooks, FK metadata/enforcement hooks, row and
    index DML, FULLTEXT scoring, metadata-backed SPATIAL support, maintenance
    methods, primary-file locking, and catalog publication.
  - `build/mariadb-minsize/mylite-compatibility-harness-report.txt` reports
    all grouped compatibility checks passing and no unexpected or known
    inherited sidecars in the latest local run.

## High-Level Position

MyLite now covers the common application SQL surface: schemas, DDL, rows,
indexes, BLOB/TEXT, GEOMETRY, FULLTEXT, generated columns, CHECK constraints,
foreign keys, views, triggers, routines, temporary tables, common query forms,
and common maintenance statements.

MyLite is not yet an InnoDB-equivalent storage engine. The biggest remaining
gaps are storage-engine internals: full crash-recoverable row/page transactions,
MVCC snapshots, row/gap locking, useful concurrent writers, XA, online DDL,
physical fulltext/R-tree formats, page compaction, encryption/compression, and
native temporary spill.

Compared with MyISAM and Aria, MyLite already has a broader application SQL
surface in some areas, especially foreign keys and rollback of supported row
DML. It still lacks MyISAM/Aria-style mature physical repair/salvage tooling,
packed/compressed table formats, and Aria's statement crash-safety log model.

## MyLite Build Profile and Engine Routing

These rows are MyLite-specific. They are kept out of the engine comparison
tables because `ENGINE=InnoDB` routing is a MyLite compatibility behavior, not
something MyISAM or Aria should be evaluated against.

### Default Build Profile

| Component | MyLite default profile | Product meaning |
| --- | --- | --- |
| MyLite engine | ✅ Supported | Default durable application table engine. |
| InnoDB engine | ❌ Unsupported | Avoids InnoDB tablespaces, redo/undo logs, background thread model, and datadir ownership. |
| MyISAM engine | 🟡 Partial | Built in for reference and transient internal temp paths; durable application tables route to MyLite. |
| Aria engine | ❌ Unsupported | Avoids `.MAI`, `.MAD`, `aria_log.*`, and Aria control-file sidecars. |
| Dynamic plugins | ❌ Unsupported | Embedded profile does not load external storage-engine plugins. |
| Partition engine | ❌ Unsupported | Partitioned-table file layout and FK restrictions are outside the default profile. |
| Aria-backed temporary tables | ❌ Unsupported | Internal disk spill must not create persistent Aria state. |
| Built-in type plugins | ✅ Supported | UUID/INET and related type plugins are available, but targeted type coverage is still needed. |

### Application `ENGINE=` Clauses

| Application DDL | MyLite default behavior | Compatibility meaning |
| --- | --- | --- |
| No explicit engine | ✅ Supported | Creates a MyLite table. |
| `ENGINE=InnoDB` | ✅ Supported | Routed to MyLite so common applications can hard-code InnoDB without loading real InnoDB. |
| `ENGINE=MyISAM` | ✅ Supported | Routed to MyLite so legacy schemas do not create durable MyISAM files. |
| `ENGINE=Aria` | ❌ Unsupported | Aria is disabled because its durable sidecars conflict with the default product shape. |
| Other storage engines | ❌ Unsupported | Default MyLite is not a dynamic-plugin compatibility environment. |

## File Ownership and Portability

| Capability | InnoDB | MyISAM | Aria | MyLite | Notes |
| --- | --- | --- | --- | --- | --- |
| Primary portable unit | Tablespace/datadir set | Table file set | Table file set plus Aria logs/control state | ✅ Supported | One primary `.mylite` file. |
| Durable table sidecars | `.ibd` optional per-table plus tablespaces/logs | `.frm`, `.MYD`, `.MYI` | `.frm`, `.MAI`, `.MAD`, `aria_log.*`, control file | ✅ Supported | No durable sidecars for MyLite tables. |
| MyLite-owned journal/WAL/lock companions allowed | N/A | N/A | N/A | 🟡 Partial | Design allows owned companions; current primary lock uses a retained descriptor and no sidecar. |
| Default MariaDB system-table model | Uses server `mysql.*` tables | Server-level | Aria commonly backs system tables | 🚧 Deferred | Selective replacement is in progress; MyLite reserves server-owned schemas. |
| File copy portability | Tablespaces require care | Strong legacy property | Similar MyISAM-style copyability for table files | ✅ Supported | The primary `.mylite` file is the portable asset. |

## SQL Surface and Schema Objects

| Capability | InnoDB | MyISAM | Aria | MyLite | Notes |
| --- | --- | --- | --- | --- | --- |
| `CREATE TABLE`, `DROP TABLE`, `RENAME TABLE` | Yes | Yes | Yes | ✅ Supported | No durable `.frm` sidecars. |
| `ALTER TABLE ... ALGORITHM=COPY` | Yes | Yes | Yes | ✅ Supported | Covered for rows, keys, defaults, and constraints. |
| Standalone `CREATE INDEX` / `DROP INDEX` | Yes | Yes | Yes | ✅ Supported | Uses MariaDB's ALTER path. |
| In-place/instant/online DDL algorithms | Yes, engine-specific | Limited | Limited | 🚧 Deferred | Copy ALTER works; online/instant algorithms are future work. |
| Transactional DDL | Partial in MariaDB/InnoDB depending operation | No | No | 🚧 Deferred | |
| `TRUNCATE TABLE` | Yes | Yes | Yes | ✅ Supported | |
| `CREATE TABLE ... LIKE` | Yes | Yes | Yes | ✅ Supported | |
| `CREATE TABLE ... SELECT` | Yes | Yes | Yes | ✅ Supported | |
| Persistent schemas/databases | Datadir/system metadata | Directory-backed | Directory-backed | ✅ Supported | Stored as catalog namespaces. |
| Persistent views | Server metadata | Server metadata | Server metadata | ✅ Supported | Stored in the `.mylite` catalog. |
| Persistent triggers | Server metadata | Server metadata | Server metadata | ✅ Supported | Stored in the `.mylite` catalog. |
| Stored procedures/functions | Server `mysql.*` metadata | Server metadata | Server metadata | ✅ Supported | Stored in the `.mylite` catalog. |
| Routine information schema | Yes | Yes | Yes | ✅ Supported | |
| Packages/package bodies | MariaDB server feature, not engine-owned | Server-level | Server-level | ❌ Unsupported | Outside the default embedded product. |
| Events/scheduler | Server-level | Server-level | Server-level | ❌ Unsupported | Server scheduler is out of scope. |
| Users/grants/auth tables | Server-level | Server-level | Server-level | ❌ Unsupported | Embedded API owns access. |
| Foreign servers | Server-level | Server-level | Server-level | ❌ Unsupported | Startup avoids the `mysql.servers` probe. |

## Row Storage and Data Types

| Capability | InnoDB | MyISAM | Aria | MyLite | Notes |
| --- | --- | --- | --- | --- | --- |
| Fixed-width scalar row fields | Yes | Yes | Yes | ✅ Supported | |
| Variable-length row fields | Yes | Yes | Yes | ✅ Supported | |
| NULL columns | Yes | Yes | Yes | ✅ Supported | |
| Large rows / row overflow | Yes, engine row formats | Yes, dynamic rows | Yes, page/dynamic formats | ✅ Supported | Uses row overflow pages. |
| BLOB/TEXT row payloads | Yes | Yes | Yes | ✅ Supported | Stored in row/overflow pages. |
| GEOMETRY row payloads | Yes | Yes for GIS data types | Yes | ✅ Supported | |
| JSON alias / long text semantics | MariaDB SQL-layer type | MariaDB SQL-layer type | MariaDB SQL-layer type | 🟡 Needs coverage | Expected through TEXT storage; needs targeted tests. |
| ENUM/SET | Yes | Yes | Yes | 🟡 Needs coverage | Expected through row storage; needs targeted tests. |
| UUID/INET plugin types | Type plugins available in build | Server/type plugin dependent | Server/type plugin dependent | 🟡 Needs coverage | Type plugins are built in; needs targeted tests. |
| Generated stored columns | Yes | Yes where MariaDB allows | Yes where MariaDB allows | ✅ Supported | |
| Generated virtual columns | Yes | Yes where MariaDB allows | Yes where MariaDB allows | ✅ Supported | |
| Virtual generated BLOB/TEXT row values | Yes where MariaDB allows | Yes where MariaDB allows | Yes where MariaDB allows | ✅ Supported | |
| Virtual generated GEOMETRY row values | Yes where MariaDB allows | Yes where MariaDB allows | Yes where MariaDB allows | ✅ Supported | |
| Typed column-native file encoding | Engine-specific | Engine-specific | Engine-specific | 🚧 Deferred | Current storage stores MariaDB record images. |
| Row compression | Yes | Compressed MyISAM format | Aria row/page formats | 🚧 Deferred | |
| Page checksums | Yes | Engine-specific | `PAGE_CHECKSUM` option | 🟡 Partial | Catalog/header checksums exist; row/index page checksum policy is deferred. |
| Data-at-rest encryption | Yes | Server/engine dependent | Server/engine dependent | 🚧 Deferred | |

## Indexes and Access Paths

| Capability | InnoDB | MyISAM | Aria | MyLite | Notes |
| --- | --- | --- | --- | --- | --- |
| Primary key | Yes | Yes | Yes | ✅ Supported | |
| Secondary non-unique indexes | Yes | Yes | Yes | ✅ Supported | |
| Unique indexes | Yes | Yes | Yes | ✅ Supported | |
| Composite indexes | Yes | Yes | Yes | ✅ Supported | |
| Nullable key parts | Yes | Yes | Yes | ✅ Supported | |
| Prefix indexes on BLOB/TEXT | Yes | Yes | Yes | ✅ Supported | |
| Descending indexes | Yes | MariaDB parser/engine dependent | MariaDB parser/engine dependent | ✅ Supported | |
| HASH algorithm metadata | InnoDB mostly BTREE semantics | No physical hash | No physical hash | ✅ Supported | Accepted as metadata; not a physical hash table. |
| FULLTEXT DDL | Yes | Yes | Yes | ✅ Supported | |
| FULLTEXT execution | Physical inverted index | Physical FULLTEXT index | Physical FULLTEXT index | 🟡 Partial | Metadata-backed full scan scorer; physical inverted index is deferred. |
| FULLTEXT boolean/natural common searches | Yes | Yes | Yes | ✅ Supported | |
| FULLTEXT optimizer/performance parity | Yes | Yes | Engine-specific | 🚧 Deferred | |
| SPATIAL DDL | Yes | GIS support | GIS support | ✅ Supported | One-part non-null GEOMETRY keys. |
| SPATIAL execution | Physical spatial indexes | GIS access | GIS access | 🟡 Partial | Metadata-backed predicates over stored rows; no physical R-tree yet. |
| Virtual generated scalar indexes | Yes where MariaDB allows | Yes where MariaDB allows | Yes where MariaDB allows | ✅ Supported | |
| Virtual generated BLOB/TEXT prefix/HASH indexes | Yes where MariaDB allows | Yes where MariaDB allows | Yes where MariaDB allows | ✅ Supported | |
| Virtual generated BLOB/TEXT FULLTEXT | Yes where MariaDB allows | Yes where MariaDB allows | Yes where MariaDB allows | ✅ Supported | |
| Virtual generated GEOMETRY SPATIAL | MariaDB SQL requires non-null key parts; generated virtual columns cannot be declared `NOT NULL` | Same SQL-layer constraint | Same SQL-layer constraint | ❌ Unsupported | Follows the MariaDB SQL-layer constraint. |
| Physical B-tree pages | Yes | Yes | Yes | 🚧 Deferred | Current key-entry streams support behavior, not final B-tree performance. |
| Accurate persistent optimizer statistics | Yes | Yes/basic | Yes/basic | 🟡 Partial | Exact record stats exist; cost/statistics model is limited. |
| `records_in_range()` for optimizer | Yes | Yes | Yes | 🟡 Partial | Basic implementation only. |

## Constraints and Integrity

| Capability | InnoDB | MyISAM | Aria | MyLite | Notes |
| --- | --- | --- | --- | --- | --- |
| `NOT NULL` | Yes | Yes | Yes | ✅ Supported | |
| Primary-key uniqueness | Yes | Yes | Yes | ✅ Supported | |
| Unique constraints | Yes | Yes | Yes | ✅ Supported | |
| CHECK constraints | Yes in MariaDB SQL layer | Yes in MariaDB SQL layer | Yes in MariaDB SQL layer | ✅ Supported | Persisted in the catalog. |
| Foreign-key DDL | Yes | No | No | ✅ Supported | |
| Foreign-key metadata in information schema | Yes | No | No | ✅ Supported | |
| FK child insert/update checks | Yes | No | No | ✅ Supported | |
| FK parent delete/update restrict/no action | Yes | No | No | ✅ Supported | |
| FK `CASCADE` | Yes | No | No | ✅ Supported | |
| FK `SET NULL` | Yes where child columns nullable | No | No | ✅ Supported | Rejects incompatible NOT NULL child columns. |
| FK `SET DEFAULT` | No in MariaDB/InnoDB docs | No | No | ❌ Unsupported | Explicitly rejected, matching MariaDB/InnoDB. |
| `foreign_key_checks=0` | Yes | N/A | N/A | ✅ Supported | |
| FK over prefix indexes | No per MariaDB FK docs | N/A | N/A | ❌ Unsupported | Matches MariaDB/InnoDB. |
| FK over indexed generated columns | Restricted by MariaDB/InnoDB docs | N/A | N/A | 🟡 Needs coverage | Needs targeted compatibility tests before a support claim. |
| Partitioned-table FKs | No per MariaDB FK docs | N/A | N/A | ⚪ N/A | Partitioning is disabled in the default profile. |
| Triggers fired by FK actions | No per MariaDB FK docs | N/A | N/A | ⚪ N/A | MariaDB does not fire triggers for FK actions. |

## Transactions, Recovery, and Durability

| Capability | InnoDB | MyISAM | Aria | MyLite | Notes |
| --- | --- | --- | --- | --- | --- |
| SQL transactions | Yes | No | No, even with `TRANSACTIONAL=1` | 🟡 Partial | Supported for the current row-DML subset, not full storage-engine ACID. |
| `COMMIT` / `ROLLBACK` row DML | Yes | No | No | ✅ Supported | Current supported DML only. |
| Savepoint rollback | Yes | No | No | ✅ Supported | Current supported DML only. |
| Statement rollback on duplicate-key error | Yes | No | Statement crash-safety, not SQL rollback | ✅ Supported | Covered duplicate-key cases. |
| Atomic single-connection commit across row pages | Yes | No | Statement-level crash safety for Aria page format | 🚧 Deferred | Needs full pager/recovery guarantee. |
| Crash recovery after process/OS crash | Yes via redo/undo/doublewrite mechanisms | No, repair needed | Yes for crash-safe Aria tables/logs | 🟡 Partial | Catalog generation recovery and row/index fallback exist; full WAL/redo/undo is deferred. |
| Torn-write protection | Yes | No | Page/log checks | 🚧 Deferred | Needed for full row/index pages. |
| MVCC old row versions | Yes | No | No | 🚧 Deferred | |
| Non-locking consistent reads | Yes | No | Limited read/concurrent insert behavior | 🚧 Deferred | |
| Purge of old versions | Yes | N/A | N/A | ⚪ N/A | Applies after MVCC exists. |
| XA transactions | Yes | No | No | ❌ Unsupported | |
| Binary log / replication durability integration | Server-level with InnoDB support | Server-level | Server-level | ❌ Unsupported | Outside the default embedded profile. |
| Online backup snapshots | Server/engine tools | File copy/lock/repair tools | File copy/lock/repair tools | 🚧 Deferred | |
| Page compaction/vacuum | InnoDB purge/space management | Repair/optimize paths | Repair/optimize paths | 🚧 Deferred | Free-page reuse exists for page ranges. |
| Offline salvage tooling | InnoDB tools | `myisamchk`/repair ecosystem | Aria repair ecosystem | 🚧 Deferred | |

## Locking and Concurrency

| Capability | InnoDB | MyISAM | Aria | MyLite | Notes |
| --- | --- | --- | --- | --- | --- |
| Row-level locking | Yes | No | No for ordinary table locking model | 🚧 Deferred | |
| Gap/next-key locking | Yes | No | No | 🚧 Deferred | |
| Table-level locks | Yes plus intention locks | Yes | Yes | ✅ Supported | |
| Concurrent inserts | Yes through row/MVCC design | Yes | Yes in Aria page/transaction-id model | 🟡 Partial | Not claimed as useful concurrent writer support. |
| Multiple in-process connections | Yes | Yes | Yes | 🟡 Partial | `libmylite` currently has one process-scoped initialized DB path. |
| Multiple read-only processes | Server-mediated clients; independent datadir opens are not the normal model | File-level coordination | File-level coordination | 🟡 Partial | Public read-only opens take shared advisory locks; write runtime is exclusive. |
| Multiple writer processes | Server-mediated clients, not independent embedded opens | Unsafe as independent direct writers | Unsafe as independent direct writers | ❌ Unsupported | A second writer gets an explicit lock conflict. |
| Useful concurrent writers in one process | Yes | No table-level write concurrency | Limited | 🚧 Deferred | |
| Deadlock detection | Yes | Table locks avoid row deadlocks | Table locks | 🚧 Deferred | |
| Background maintenance threads | Yes | No similar subsystem | Aria background/log behavior less extensive | ❌ Unsupported | No MyLite-owned background thread model yet. |
| Cheap per-request PHP open/close | Server process usually persistent | File engine but table open/repair costs | Requires Aria logs/control state | 🚧 Deferred | Current embedded runtime remains process-scoped after first init. |

## Query Execution and Application Surface

| Capability | InnoDB | MyISAM | Aria | MyLite | Notes |
| --- | --- | --- | --- | --- | --- |
| MariaDB parser/analyzer/optimizer | Yes | Yes | Yes | ✅ Supported | Inherited from MariaDB. |
| Joins | Yes | Yes | Yes | ✅ Supported | |
| Grouping/aggregation | Yes | Yes | Yes | ✅ Supported | |
| Subqueries | Yes | Yes | Yes | ✅ Supported | |
| `UNION` / `UNION ALL` | Yes | Yes | Yes | ✅ Supported | |
| `INSERT ... SELECT` | Yes | Yes | Yes | ✅ Supported | |
| Multi-table `UPDATE` / `DELETE` | Yes | Yes | Yes | ✅ Supported | Covered for common cases. |
| `INSERT IGNORE` | Yes | Yes | Yes | ✅ Supported | |
| `REPLACE` | Yes | Yes | Yes | ✅ Supported | |
| `INSERT ... ON DUPLICATE KEY UPDATE` | Yes | Yes | Yes | ✅ Supported | |
| Common application schema patterns | Yes | Yes | Yes | ✅ Supported | Current coverage includes WordPress-shaped schemas as one representative suite. |
| `AUTO_INCREMENT=N` table option | Yes | Yes | Yes | ✅ Supported | |
| Composite/non-leading `AUTO_INCREMENT` | InnoDB has restrictions | MyISAM supports legacy patterns | Aria supports legacy patterns | ✅ Supported | Uses MariaDB per-prefix lookup behavior. |
| Application `ALTER` migrations on populated tables | Yes | Yes | Yes | ✅ Supported | Covered for keys, CHECK, FK, rows, and defaults. |
| Standard client/server protocol | Yes through server | Yes through server | Yes through server | ❌ Unsupported | MyLite uses the embedded `libmylite` API, not a daemon. |

## Temporary Data and Spill

| Capability | InnoDB | MyISAM | Aria | MyLite | Notes |
| --- | --- | --- | --- | --- | --- |
| User temporary tables | Yes | Yes | Yes | ✅ Supported | Session-scoped in-memory MyLite definitions. |
| Temporary tables under read-only durable open | Server/session dependent | Server/session dependent | Server/session dependent | ✅ Supported | Session-local temporary tables remain writable. |
| Temporary foreign keys | InnoDB docs disallow temporary FK tables | N/A | N/A | ✅ Supported | Session-local metadata only. |
| Internal MEMORY temp tables | Server-level | Server-level | Server-level | ✅ Supported | Inherited from MariaDB. |
| Internal disk spill | InnoDB/Aria/MyISAM depending server config | MyISAM possible | Aria default in MariaDB | 🟡 Partial | Uses inherited transient MyISAM internal temp path. |
| Native MyLite spill file format | N/A | N/A | N/A | 🚧 Deferred | |
| Strict no-temp-file mode | N/A | N/A | N/A | 🚧 Deferred | |

## Maintenance, Diagnostics, and Administration

| Capability | InnoDB | MyISAM | Aria | MyLite | Notes |
| --- | --- | --- | --- | --- | --- |
| `CHECK TABLE` | Yes | Yes | Yes | ✅ Supported | |
| `ANALYZE TABLE` | Yes | Yes | Yes | ✅ Supported | |
| `OPTIMIZE TABLE` | Yes | Yes | Yes | ✅ Supported | Healthy no-op path today. |
| `REPAIR TABLE` | Limited/engine-specific | Yes | Yes | 🟡 Partial | Healthy no-op path exists; offline repair is deferred. |
| `CHECKSUM TABLE` | Yes | Yes | Yes | ✅ Supported | |
| `SHOW INDEX` | Yes | Yes | Yes | ✅ Supported | |
| `DESCRIBE` | Yes | Yes | Yes | ✅ Supported | |
| `EXPLAIN` | Yes | Yes | Yes | ✅ Supported | |
| `SHOW ENGINE INNODB STATUS` | Yes | N/A | N/A | ⚪ N/A | InnoDB is not loaded. |
| Engine-specific status variables | Yes | Yes | Yes | 🚧 Deferred | Minimal coverage today. |
| Server users/auth/admin SQL | Server-level | Server-level | Server-level | ❌ Unsupported | Embedded API owns access. |
| Replication/binlog/admin surfaces | Server-level | Server-level | Server-level | ❌ Unsupported | Outside the default embedded profile. |

## Performance and Physical Storage

| Capability | InnoDB | MyISAM | Aria | MyLite | Notes |
| --- | --- | --- | --- | --- | --- |
| Mature page cache/buffer pool | Yes | Key cache | Aria page cache | 🚧 Deferred | Current storage uses limited in-memory catalog structures. |
| Physical clustered primary key | Yes | No | No | ❌ Unsupported | |
| Physical ordered secondary B-trees | Yes | Yes | Yes | 🚧 Deferred | Current key-entry streams support behavior, not final B-tree performance. |
| Physical inverted FULLTEXT index | Yes | Yes | Yes | 🚧 Deferred | |
| Physical R-tree/SPATIAL index | Yes | GIS support | GIS support | 🚧 Deferred | |
| Adaptive/hash acceleration | InnoDB adaptive hash optional | No | No | 🚧 Deferred | |
| Query-cost fidelity for large tables | Mature | Mature | Mature | 🟡 Partial | |
| Large database scale limits | Mature engine limits | Mature engine limits | Mature engine limits | 🟡 Needs coverage | Needs scale tests. |
| Binary size impact | Large | Smaller | Moderate | ⚪ N/A | Latest local `libmariadbd.a` report is about 43.6 MB; smaller embedded builds remain a profile goal. |
| Startup cost | Server/engine initialization | Lower | Aria logs/control initialization | 🚧 Deferred | Cheap PHP-style open/close remains a design target. |

## Explicit Missing or Deferred Work

### Compared with InnoDB

1. Full ACID storage recovery for row/index pages: WAL/redo/undo or equivalent
   crash recovery is still deferred beyond the current catalog generation and
   row/index fallback protections.
2. MVCC: no old-row version chains, read views, purge, non-locking consistent
   reads, row/gap locks, or deadlock detection.
3. Useful concurrent writers: MyLite currently favors safe single-writer file
   ownership over InnoDB-style concurrent write transactions.
4. Online DDL and transactional DDL: copy ALTER works for common app migrations,
   but InnoDB-style online/instant DDL and transactional DDL guarantees are not
   implemented.
5. XA/binlog/replication integration: intentionally outside the default
   embedded product.
6. Physical index formats: MyLite supports index behavior through current
   key-entry streams and metadata-backed scans, but not InnoDB-like B-tree,
   inverted FULLTEXT, or R-tree performance structures.
7. Compression, encryption, buffer pool tuning, persistent optimizer stats,
   online backup, and engine-specific status/admin interfaces.

### Compared with MyISAM

1. MyISAM's mature `.MYD`/`.MYI` file tooling and repair/salvage ecosystem are
   not present.
2. Packed/compressed MyISAM table formats are not implemented as MyLite storage
   formats.
3. MyISAM's physical FULLTEXT and mature ordered-index performance are not
   matched yet.
4. MyISAM's simple file-copy table portability is intentionally replaced by
   one primary `.mylite` file.

MyLite already exceeds MyISAM for several application semantics: supported row
DML rollback/savepoints, CHECK/FK enforcement, single primary file ownership,
and catalog-backed persistent views/triggers/routines without durable
MariaDB sidecars.

### Compared with Aria

1. Aria's crash-safe statement log and automatic log recovery are not matched
   yet for all row/index pages.
2. Aria's page formats, page checksums, page cache, and repair ecosystem are not
   implemented as MyLite physical formats.
3. Aria-backed internal disk temporary tables are intentionally disabled in the
   default MyLite profile; current disk spill uses inherited transient MyISAM.
4. Aria's role as MariaDB system-table storage is intentionally not copied;
   MyLite is replacing selected system surfaces with catalog-backed or
   embedded-API behavior.

MyLite already exceeds Aria for foreign-key enforcement and SQL transaction
rollback on the supported row-DML subset.

## Compatibility Risk Register

| Risk | Current status | Next useful slice |
| --- | --- | --- |
| Full crash recovery for row/index writes | 🚧 Deferred | `row-index-wal-recovery` or equivalent pager slice |
| In-process MVCC/read views | 🚧 Deferred | `mylite-mvcc-read-views` |
| Concurrent write transactions | 🚧 Deferred | `mylite-write-lock-manager` after recovery design |
| Cheap PHP per-request lifecycle | 🟡 Partial | `php-request-lifecycle-smoke` and embedded runtime restart/isolation work |
| Native temp spill | 🚧 Deferred | `mylite-native-temp-spill` |
| Physical B-tree index pages | 🚧 Deferred | `mylite-btree-index-pages` |
| Physical FULLTEXT index | 🚧 Deferred | `mylite-fulltext-inverted-index` |
| Physical SPATIAL/R-tree index | 🚧 Deferred | `mylite-spatial-rtree-index` |
| Page checksums for row/index pages | 🚧 Deferred | `mylite-page-checksum-policy` |
| Page compaction/vacuum | 🚧 Deferred | `mylite-page-compaction` |
| Typed column encoding | 🚧 Deferred | `mylite-typed-row-format` |
| Targeted type matrix for ENUM/SET/JSON/UUID/INET/date/time/decimal | 🟡 Needs coverage | `mylite-type-compatibility-matrix` |
| Persistent optimizer stats | 🟡 Partial | `mylite-persistent-statistics` |
| Online/instant DDL | 🚧 Deferred | `mylite-online-ddl` |
| Packages/events | ❌ Unsupported | `mylite-events-packages-catalog` only if product scope requires it |
| Real InnoDB optional datadir mode | ❌ Unsupported | Separate `innodb-compat-datadir-mode` experiment |

## Bottom Line

For common application SQL, MyLite is now substantially closer to a drop-in
engine than MyISAM or Aria are in some semantic areas. For storage-engine
internals, MyLite is not yet an InnoDB replacement. The product can honestly
claim broad common SQL coverage, but it should not claim InnoDB-equivalent ACID
recovery, MVCC, concurrency, physical indexing performance, or
server-administration behavior until those slices are implemented and covered.
