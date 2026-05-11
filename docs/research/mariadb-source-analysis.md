# MariaDB source analysis

This document records the initial source-level research for MyLite.

## Snapshot

- Upstream repository: <https://github.com/MariaDB/server>
- Branch inspected: `11.8`
- Commit inspected: `04e09010773caf0b302b2933fff3fe95381a5e13`
- Candidate import tag: `mariadb-11.8.6`
- Candidate import commit: `9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`
- Local clone location during research: temporary `/tmp` checkout of the `11.8`
  branch
- Date: 2026-05-11

MariaDB 11.8 is the current stable LTS line. MariaDB 12.2 is the highest stable
rolling line according to the public release listing, while 12.3 was listed as
RC and 13.0 as preview at the time of this review. MariaDB 11.8 is the better
initial base for a fork because it has longer maintenance expectations.

The inspected `11.8` branch was a floating branch head. A real source import
should pin an exact tag or commit, such as `mariadb-11.8.6`, rather than relying
on the branch name alone.

On 2026-05-11, `git ls-remote` reported `mariadb-11.8.6` as tag object
`2eeb8795e593db09241c1c5210fee34b3569ca47` with peeled commit
`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`. Unless a newer 11.8 patch tag is
selected immediately before import, use that tag for the first upstream import.

Release reference:

- <https://mariadb.org/mariadb/all-releases/>

## Existing embedded support

MariaDB already has embedded server support:

- `CMakeLists.txt` defines `WITH_EMBEDDED_SERVER` and adds the `libmysqld`
  source directory when it is enabled.
- `libmysqld/CMakeLists.txt` builds the `mysqlserver` target with Unix output
  name `mariadbd`, producing `libmariadbd.a` and a `libmysqld.a` compatibility
  symlink in the inspected build graph.
- MariaDB's docs describe `libmysqld` as using the same interface as the C
  client library, with applications calling `mysql_library_init()` and
  `mysql_library_end()`.
- `include/mysql.h` aliases `mysql_library_init` to `mysql_server_init` and
  `mysql_library_end` to `mysql_server_end`.

Source references:

- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/CMakeLists.txt#L502-L510>
- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/libmysqld/CMakeLists.txt#L176-L210>
- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/include/mysql.h#L376-L394>
- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/libmysqld/libmysql.c#L126-L185>

Conclusion: in-process execution is not the hard part. The hard part is making
the embedded runtime behave like a file-owned library instead of a server core.

## What embedded initialization still does

`libmysqld/lib_sql.cc:init_embedded_server()` is not a small library bootstrap.
It initializes a server-shaped runtime:

- thread and early server variables,
- default option loading,
- logger base,
- system variables,
- common server variables,
- datadir state,
- tmpdir state,
- SSL,
- server components,
- ACL/grants unless compiled/configured out,
- time zones,
- UDFs,
- replication filters,
- init-file execution,
- DDL recovery.

Source reference:

- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/libmysqld/lib_sql.cc#L532-L669>

Implication for MyLite: `libmylite` should not simply expose
`mysql_library_init()`. It needs a controlled bootstrap path that sets embedded
defaults, refuses unsupported server options, and owns all per-file state.

## Datadir and table metadata assumptions

MariaDB 11.8 still has visible `.frm` handling. Table creation accepts an `.frm`
image and normally writes a `.frm` file unless the storage engine supports
discovery. Table deletion comments still distinguish engine data from the
`.frm` file.

Source references:

- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/sql/handler.cc#L6477-L6525>
- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/sql/handler.cc#L3348-L3368>

Implication for MyLite: persistent `.frm` files are incompatible with the
primary `.mylite` database-file model. The storage engine must use MariaDB's
discovery API or the SQL layer must be forked to read definitions from a
single-file catalog.

## DDL metadata routing gap

The discovery API is necessary, but it does not by itself prove that the create
and alter paths can avoid durable `.frm` writes. Before MyLite claims native
single-file DDL, a focused `ddl-metadata-routing` slice should trace
`CREATE`, `ALTER`, `DROP`, and `RENAME` through the SQL layer and handler calls,
then either route generated table-definition images directly into the MyLite
catalog or document the minimum SQL-layer fork needed.

The acceptance test for that slice should run DDL around a `.mylite` file and
fail on unexpected durable `.frm`, engine, MariaDB log, or schema-directory
sidecars.

## Storage engine escape hatch

MariaDB's `handlerton` has a table discovery API:

- `discover_table()` fills a `TABLE_SHARE` from engine metadata.
- `discover_table_names()` lists tables known by an engine.
- `discover_table_existence()` checks existence without a full discovery.
- Discovery exists specifically so tables can be found without a user-issued
  `CREATE TABLE`.
- MariaDB's table-discovery documentation explicitly calls out
  `TABLE_SHARE::init_from_binary_frm_image()` for engines that store a generated
  `.frm` image internally, plus `TABLE_SHARE::init_from_sql_statement_string()`
  as another discovery path.

Source references:

- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/sql/handler.h#L1621-L1707>
- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/sql/handler.cc#L6820-L6931>
- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/sql/handler.cc#L7111-L7180>
- <https://mariadb.com/docs/server/reference/product-development/plugin-development/storage-engines-storage-engine-development/table-discovery>

This is the most important positive finding. A MyLite storage engine can
store table definitions inside `.mylite` and return them to the MariaDB SQL
layer on demand.

## Handler surface

A real MyLite engine must implement core handler methods:

- open/create/drop/rename,
- table scans,
- index scans,
- row insert/update/delete,
- truncate,
- autoincrement,
- locks,
- transaction hooks,
- discovery hooks,
- metadata/version checks,
- enough ALTER behavior to satisfy MariaDB DDL.

Source references:

- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/sql/handler.h#L1480-L1515>
- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/sql/handler.h#L5230-L5268>
- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/sql/handler.h#L5321-L5349>
- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/sql/handler.h#L5400-L5448>

This is significant work, but it is bounded work. It is preferable to
intercepting every file operation from every existing storage engine.

## Existing engines do not match the single-file goal

Aria and InnoDB are useful references but not final answers:

- MariaDB documentation describes Aria's `aria_log.*` write-ahead log files.
- MariaDB system-table SQL creates many `mysql.*` tables using Aria.
- InnoDB uses table spaces and redo/undo files in normal operation.
- The inspected source contains `ib_logfile0` naming and undo tablespace
  directory state.

Source references:

- <https://mariadb.com/docs/server/server-usage/storage-engines/aria/aria-storage-engine>
- <https://mariadb.com/docs/server/server-usage/storage-engines/innodb/innodb-tablespaces/innodb-file-per-table-tablespaces>
- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/storage/innobase/include/log0log.h#L38-L44>
- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/storage/innobase/include/srv0srv.h#L198-L205>
- <https://github.com/MariaDB/server/blob/04e09010773caf0b302b2933fff3fe95381a5e13/scripts/mariadb_system_tables.sql#L38-L149>

Implication: keeping InnoDB or Aria as the main durable engine pushes MyLite
toward a virtual datadir container. That may be useful for a compatibility
experiment, but it is not the cleanest product architecture.

## Build probe

A local CMake probe was run with:

- `CMAKE_BUILD_TYPE=MinSizeRel`
- `BUILD_CONFIG=mysql_release`
- `FEATURE_SET=small`
- `WITH_EMBEDDED_SERVER=ON`
- InnoDB, partition, archive, blackhole, federatedx, feedback, performance
  schema, RocksDB, Mroonga, CONNECT, Spider, OQGraph, and Sphinx disabled

Findings:

- CMake configuration succeeded after explicitly using Homebrew Bison 3.8.2.
- The configuration still enabled static Aria, MyISAM, MyISAMMRG, MEMORY/HEAP,
  CSV, Sequence, auth socket, type plugins, user variables, userstat, thread
  pool info, WSREP provider, and other components.
- `ninja -t commands libmariadbd.a | wc -l` reported 676 build commands in
  this environment.
- Compilation failed before producing a library because the current macOS SDK
  include paths caused libc++ header lookup failures.

This is not a MyLite blocker. It does mean binary-size claims need a
dedicated reproducible build environment before they become numbers.

## Licensing

MariaDB Server is GPL-2.0-only. MariaDB's GitHub README explicitly says this is
"only under version 2" without an "or later" clause, and the MariaDB licensing
FAQ says MariaDB is distributed under GPL version 2.

References:

- <https://github.com/MariaDB/server#licensing>
- <https://mariadb.com/docs/general-resources/community/community/faq/licensing-questions/licensing-faq>
- <https://mariadb.com/trademarks/>
- <https://www.mysql.com/about/legal/>

MyLite should therefore be GPL-2.0-only unless all MariaDB-derived code is
removed, which is not the premise of this project.

SQLite-like lifecycle semantics do not imply SQLite-like licensing. A
distributed application that links or embeds MyLite must account for GPL
obligations. Trademark obligations are separate from GPL obligations; public
packaging should review MariaDB and MySQL trademark guidance and avoid implying
affiliation.

## Recommendation

Do not spend the main effort on wrapping `libmariadbd` or packaging a datadir.
Use `libmariadbd` to get the first in-process smoke tests running, then invest
in:

1. a `libmylite` facade,
2. a single-file storage engine,
3. table discovery from the single-file catalog,
4. embedded bootstrap cleanup,
5. binary-size trimming once the runtime shape is stable.
