# Production size analysis

This document records the current production-oriented size profile for MyLite
and the measured impact of likely size-reduction levers.

## Measurement scope

The baseline is the current `tools/build-mariadb-minsize.sh` profile:

- `CMAKE_BUILD_TYPE=MinSizeRel`
- `CMAKE_C_FLAGS_MINSIZEREL=-Oz -DNDEBUG`
- `CMAKE_CXX_FLAGS_MINSIZEREL=-Oz -DNDEBUG`
- `CMAKE_C_VISIBILITY_PRESET=hidden`
- `CMAKE_CXX_VISIBILITY_PRESET=hidden`
- `CMAKE_VISIBILITY_INLINES_HIDDEN=ON`
- `CMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -Wl,-z,pack-relative-relocs -Wl,--pack-dyn-relocs=relr -Wl,--gc-sections -Wl,--icf=all`
- `CMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld -Wl,-z,pack-relative-relocs -Wl,--pack-dyn-relocs=relr -Wl,--gc-sections -Wl,--icf=all`
- `CMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld -Wl,-z,pack-relative-relocs -Wl,--pack-dyn-relocs=relr -Wl,--gc-sections -Wl,--icf=all`
- `BUILD_CONFIG=mysql_release`
- `FEATURE_SET=small`
- `WITH_EMBEDDED_SERVER=ON`
- `DISABLE_SHARED=ON`
- `WITHOUT_DYNAMIC_PLUGINS=ON`
- system `ssl`, `pcre`, `fmt`, and build-time `zlib` detection
- `WITH_DYNCOL=OFF`
- `WITH_EXTRA_CHARSETS=none`
- `DEFAULT_COLLATION=utf8mb4_general_ci`
- `MYLITE_DISABLE_ORACLE_PARSER=ON`
- `MYLITE_DISABLE_ORACLE_FUNCTIONS=ON`
- `MYLITE_DISABLE_JSON_SCHEMA_VALID=ON`
- `MYLITE_DISABLE_QUERY_CACHE=ON`
- `MYLITE_DISABLE_BINLOG_CORE=ON`
- `MYLITE_DISABLE_BINLOG_REPLICATION=ON`
- `MYLITE_DISABLE_RPL_GTID_STATE=ON`
- `MYLITE_DISABLE_RPL_UTILITY_SERVER=ON`
- `MYLITE_DISABLE_TC_LOG_MMAP=ON`
- `MYLITE_DISABLE_RPL_FILTER=ON`
- `MYLITE_DISABLE_CRYPT_FUNCTION=ON`
- `MYLITE_DISABLE_DES_FUNCTIONS=ON`
- `MYLITE_DISABLE_DYNAMIC_COLUMNS=ON`
- `MYLITE_DISABLE_KDF_FUNCTION=ON`
- `MYLITE_DISABLE_SQL_CRYPTO_FUNCTIONS=ON`
- `MYLITE_DISABLE_SERVER_ENCRYPTION=ON`
- `MYLITE_DISABLE_OPENSSL_DIGESTS=ON`
- `MYLITE_DISABLE_OPTIMIZER_TRACE=ON`
- `MYLITE_DISABLE_ZLIB_COMPRESSION=ON`
- `MYLITE_DISABLE_REGEX_FUNCTIONS=ON`
- `MYLITE_DISABLE_ROUTINE_INFORMATION_SCHEMA=ON`
- `MYLITE_DISABLE_SERVER_UTILITY_FUNCTIONS=ON`
- `MYLITE_DISABLE_VIO_SSL=ON`
- `MYLITE_DISABLE_JSON_FUNCTIONS=ON`
- `MYLITE_DISABLE_JSON_TABLE=ON`
- `MYLITE_DISABLE_SQL_DIAGNOSTICS_STATEMENTS=ON`
- `MYLITE_DISABLE_SYSTEM_VERSIONING=ON`
- `MYLITE_DISABLE_FOREIGN_SERVER_CACHE=ON`
- `MYLITE_DISABLE_PROXY_PROTOCOL=ON`
- `MYLITE_DISABLE_EXPLAIN_RUNTIME=ON`
- `MYLITE_DISABLE_EVENT_PARSE_DATA=ON`
- `MYLITE_DISABLE_TRIGGER_RUNTIME=ON`
- `MYLITE_DISABLE_VIEW_RUNTIME=ON`
- `MYLITE_DISABLE_TABLE_ADMIN=ON`
- `MYLITE_DISABLE_PERSISTENT_STATISTICS=ON`
- `MYLITE_DISABLE_SELECT_PROCEDURE_RUNTIME=ON`
- `MYLITE_DISABLE_EXTRA_LOCALES=ON`
- `MYLITE_DISABLE_LOAD_DATA=ON`
- `MYLITE_DISABLE_TIME_ZONE_TABLES=ON`
- `MYLITE_DISABLE_VECTOR_TYPE=ON`
- `MYLITE_DISABLE_XA_TRANSACTIONS=ON`
- `MYLITE_DISABLE_GEOMETRY_TYPE=ON`
- `MYLITE_DISABLE_GENERAL1400_COLLATIONS=ON`
- `MYLITE_DISABLE_SQL_SEQUENCE=ON`
- `MYLITE_DISABLE_UNWIND_TABLES=ON`
- `MYLITE_DISABLE_UDF_RUNTIME=ON`
- `MYLITE_DISABLE_WINDOW_FUNCTIONS=ON`
- `MYLITE_DISABLE_UCA_COLLATIONS=ON`
- `MYLITE_DISABLE_LEGACY_STORAGE_ENGINES=ON`
- `MYLITE_DISABLE_MYISAM_ADMIN=ON`
- `MYLITE_DISABLE_MYISAM_FULLTEXT=ON`
- `MYLITE_DISABLE_MYISAM_RTREE=ON`
- `MYLITE_DISABLE_MYISAM_TEMP_SPILL=OFF`
- `MYLITE_DISABLE_SPATIAL_CORE=ON`
- Aria, InnoDB, partitioning, Performance Schema, RocksDB, Mroonga, Connect,
  Spider, S3, OQGraph, Sphinx, ColumnStore, FederatedX, Blackhole, Archive,
  feedback, and selected authentication plugins disabled
- `MYLITE_DISABLE_ARIA=ON`
- `MYLITE_DISABLE_BACKUP_STAGE=ON`
- `MYLITE_ENABLE_SECTION_GC=ON`
- `MYLITE_ENABLE_ICF=all`
- `USE_ARIA_FOR_TMP_TABLES=OFF`

The original comparison baseline was generated at `2026-05-12T00:33:29Z` from
`vendor/mariadb/server` into `build/mariadb-minsize`. Current measurements
include the `type-plugin-size-profile`, `charset-small-profile`, and
`oracle-parser-size-profile`, `static-archive-strip-profile`, and
`small-builtin-plugin-profile`, `xml-function-size-profile`, and
`gis-function-size-profile`, `executable-export-size-profile`,
`vector-function-size-profile`, `profiling-size-profile`, and
`help-command-size-profile`, `procedure-analyse-size-profile`, and
`relr-linker-size-profile`, `legacy-storage-engine-size-profile`,
`section-gc-size-profile`, `json-schema-valid-size-profile`,
`query-cache-size-profile`, `oracle-function-size-profile`,
`server-utility-function-size-profile`, `uca-collation-size-profile`,
`regex-function-size-profile`, `binlog-replication-size-profile`, and
`no-binlog-core-size-profile`, `myisam-admin-size-profile`,
`myisam-fulltext-size-profile`, `myisam-rtree-size-profile`, and
`spatial-core-size-profile`, `sql-sequence-size-profile`,
`geometry-type-size-profile`, `general1400-collation-size-profile`,
`rpl-filter-size-profile`, `icf-linker-size-profile`, and
`vio-tls-size-profile`, `libcrypt-encrypt-size-profile`,
`zlib-compression-size-profile`,
`dynamic-plugin-loading-size-profile`, `des-function-size-profile`, and
`kdf-function-size-profile`, `unwind-table-size-profile`,
`udf-runtime-size-profile`, `window-function-size-profile`, and
`sql-crypto-function-size-profile`, `server-encryption-size-profile`,
`openssl-digest-size-profile`, and the deeper
`no-binlog-core-size-profile` follow-up attempts, and
`tc-log-mmap-size-profile`, `append-query-string-size-profile`,
`rpl-gtid-state-size-profile`, `optimizer-trace-size-profile`,
`backup-stage-size-profile`, `json-table-size-profile`, and
`foreign-server-cache-size-profile`, `proxy-protocol-size-profile`,
`event-parse-data-size-profile`, `xa-transaction-size-profile`,
`trigger-runtime-size-profile`, `view-runtime-size-profile`,
`table-admin-size-profile`, `persistent-statistics-size-profile`,
`select-procedure-runtime-size-profile`, `locale-minsize-profile`, and
`load-data-size-profile`, `oz-compiler-size-profile`,
`time-zone-table-size-profile`, `hidden-visibility-size-profile`,
`explain-runtime-size-profile`, `vector-type-size-profile`,
`json-function-size-profile`, `diagnostics-statement-size-profile`,
`system-versioning-size-profile`, `rpl-utility-server-size-profile`, and
`dynamic-column-size-profile`, and `routine-information-schema-size-profile`.
Together these remove the built-in
`type_geom`, `type_inet`, `type_uuid`, `sequence`, `thread_pool_info`,
`user_variables`, `userstat`, `mhnsw`, `csv`, and `myisammrg` plugins, set
Connector/C's dynamic-column API switch to `OFF`, set
`WITH_EXTRA_CHARSETS=none`, omit the Oracle SQL-mode parser, omit XML, GIS,
vector SQL functions, ordinary JSON SQL functions, and the retained vector type
handler, disable MariaDB statement profiling, omit the SQL `HELP`
command implementation, omit the `PROCEDURE ANALYSE()` implementation, remove
full-symbol exports from MyLite smoke executables, link runtime-style artifacts
with lld and compact `DT_RELR` relative relocations, make the inherited MyISAM
engine non-user-selectable while retaining it for internal disk temporary
tables, compile minsize objects into per-function/per-data sections and link
runtime-style artifacts with `--gc-sections`, omit the `JSON_SCHEMA_VALID()`
validator while retaining ordinary JSON functions, omit MariaDB's query cache
while reporting `have_query_cache=NO`, omit Oracle compatibility function
aliases and Oracle schema routing, omit server-utility SQL functions such as
`BENCHMARK()`, `GET_LOCK()`, `LOAD_FILE()`, replication wait helpers,
`SLEEP()`, and `UUID_SHORT()`, omit UCA 1400 and UCA 0900 collation support
while switching the aggressive minsize default collation to
`utf8mb4_general_ci`, omit regular expression SQL execution surfaces while
removing the PCRE2 linked runtime dependency, omit command-level binlog replay
and replication sources that are unused or already blocked in embedded mode,
compile remaining embedded binlog transaction, row-event, GTID-state, and
event-write entry points to no-ops while omitting the now-unreferenced
`rpl_record.cc` object, omit MyISAM check/repair admin code while retaining
MyISAM for inherited disk temporary tables, omit MyISAM full-text indexing
implementation code, omit MyISAM RTREE/spatial-key implementation code while
reporting `have_rtree_keys=NO`, omit the retained spatial WKB/WKT
implementation core while keeping GEOMETRY type parsing and MyLite rejection
paths, omit the SQL sequence engine implementation while retaining parser
syntax and explicit unsupported/missing-sequence diagnostics, omit retained
GEOMETRY type implementation code while keeping minimal generic type metadata
symbols, omit compiled `general1400_as_ci` collations and unused extended
Unicode casefold tables while retaining ordinary `general_ci`, replace
MariaDB's remaining replication filter implementation with a minimal
permissive minsize stub, fold identical linked code with lld `--icf=all`, omit
VIO TLS transport and the `libssl.so.3` runtime dependency, omit the legacy
`ENCRYPT()` SQL function and the
`libcrypt.so.1` runtime dependency, omit zlib-backed SQL compression,
compressed-column, compressed-protocol, and compressed-binlog surfaces while
retaining CRC32 support, remove the `libz.so.1` runtime dependency, compile out
the dynamic plugin loader and full dynamic plugin service bridge while
reporting `have_dynamic_loading=NO`, and strip the static archive in the
MyLite minsize profile, and omit legacy `DES_ENCRYPT()` / `DES_DECRYPT()`
SQL functions plus DES key-file server administration plumbing, and omit the
OpenSSL-backed `KDF()` SQL function, and disable nonessential compiler unwind
tables while retaining C++ exception support, and omit UDF runtime lookup,
execution, and `mysql.func` dynamic-library loading from the aggressive
embedded profile, and omit SQL window-function item/execution code from the
aggressive embedded profile, and omit OpenSSL-backed SQL crypto/password
functions from the aggressive embedded profile while reporting the SSL library
as disabled when VIO TLS is disabled, omit inherited server-side encryption
hooks for binlogs, relay logs, and encrypted temporary IO caches, replace the
remaining OpenSSL-backed internal MD5/SHA-1 wrappers in the aggressive embedded
profile so `libcrypto.so.3` is no longer a linked runtime dependency, and
deepen the no-binlog core profile by no-oping embedded binlog open/recovery and
annotated-row helpers while omitting `gtid_index.cc`, `log_event.cc`,
`rpl_injector.cc`, and `rpl_record.cc`, and omit the inherited mmap-backed
transaction coordinator `tc.log` implementation while keeping inert status
variables, and move the remaining SQL string-rendering helper out of
`log_event_server.cc` so the full event-server object can be omitted, and
replace the remaining GTID binlog-state lifecycle shell with a tiny no-binlog
stub so the full `rpl_gtid.cc` object can be omitted, and replace optimizer
trace diagnostics with an inert embedded stub while preserving shared JSON
writer helpers, and replace external backup stage, backup lock, and backup DDL
logging with embedded stubs so the full `backup.cc` object can be omitted, and
replace `JSON_TABLE` table-function execution with an unsupported embedded stub,
replace ordinary JSON scalar function registration and JSON aggregate runtime
with an aggressive-profile JSON-function stub while retaining internal JSON
type validation helpers, replace SQL `GET DIAGNOSTICS`, `SIGNAL`, and
`RESIGNAL` statement runtime with unsupported embedded stubs while retaining
the internal diagnostics area and MyLite C API diagnostics, omit
system-versioned table predicate item runtime while rejecting MyLite temporal
table metadata, replace row-replication type-conversion utilities with
fail-closed embedded stubs, omit MariaDB dynamic-column SQL item execution and
the dynamic-column BLOB helper implementation, make stored routine Information
Schema tables empty without scanning `mysql.proc`, and replace the foreign-server
metadata cache with no-op embedded stubs so the `mysql.servers` cache
implementation is omitted, and replace proxy protocol network-listener support
with embedded disabled stubs, replace event parser data validation with a
minimal parser-allocation stub while event DDL remains rejected, replace the
full XA transaction implementation with embedded-disabled XA command stubs, and
replace the full trigger sidecar loader and runtime with inert embedded
no-trigger stubs while trigger DDL remains rejected, and replace the full view
sidecar loader and runtime with embedded-disabled view stubs while preserving
derived-table and CTE column-name helpers, replace inherited table-admin
maintenance, key-cache assignment, and index-preload execution with
unsupported embedded stubs while preserving prepared admin result metadata,
and replace inherited persistent `mysql.table_stats`, `mysql.column_stats`,
`mysql.index_stats`, and JSON histogram storage with embedded no-statistics
stubs while preserving handler row estimates for ordinary planning.
They also remove the remaining generic `SELECT ... PROCEDURE` runtime after
`PROCEDURE ANALYSE()` is already unsupported, leaving a small unsupported
procedure-clause setup stub, replace the generated full locale table with an
`en_US`-only embedded profile stub, and omit `LOAD DATA` / `LOAD XML`
server-file import execution, build the aggressive minsize profile with
GCC/G++ `-Oz`, and omit `mysql.time_zone*` table loading while retaining
`SYSTEM` and numeric-offset time zones, and build aggressive minsize artifacts
with hidden default C/C++ symbol visibility while retaining explicit
`MYLITE_API` exports for the public MyLite C API, and replace full EXPLAIN,
ANALYZE, and SHOW EXPLAIN plan-output runtime with an embedded unsupported
stub while retaining no-op optimizer plan bookkeeping needed by ordinary SQL,
and omit the retained `VECTOR` type handler, ordinary JSON SQL function
runtime, and dynamic-column execution/runtime helpers from the aggressive
embedded profile.

`no-myisam-temp-spill-size-profile` was measured separately as an opt-in
`MYLITE_DISABLE_MYISAM_TEMP_SPILL=ON` experiment. It is not part of the current
default baseline because the compatibility harness shows ordinary schema-table
metadata queries still require MariaDB's inherited disk temporary-table path.

This project does not yet have a final packaged production artifact such as a
shared `libmylite.so` bundle. For now, the most useful size signals are:

- the static embedded MariaDB archive used by MyLite,
- the first-party MyLite wrapper archive,
- the MyLite engine component archive,
- stripped linked smoke binaries as an estimate of final linked footprint, and
- dynamic system libraries only if a distribution bundle chooses to vendor
  them instead of relying on platform packages.

## Current baseline

The current values were measured from
`MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-routine-is`.
Paths below use the default build directory names for readability.

| Artifact | Bytes | MiB | Notes |
| --- | ---: | ---: | --- |
| `build/mariadb-minsize/libmysqld/libmariadbd.a` | 27,644,466 | 26.36 | Main embedded MariaDB archive, stripped; section metadata grows the archive |
| `build/mariadb-minsize/mylite/libmylite.a` | 122,792 | 0.12 | First-party public wrapper with explicit `MYLITE_API` exports |
| `build/mariadb-minsize/storage/mylite/libmylite_embedded.a` | 388,456 | 0.37 | MyLite storage-engine component archive |
| `build/mariadb-minsize/mylite/mylite-open-close-smoke` | 7,365,424 | 7.02 | Unstripped linked smoke binary, hidden default visibility, lld RELR, section GC, ICF, GCC/G++ `-Oz`, reduced unwind tables, no OpenSSL runtime dependency, no retained binlog event reader, GTID-index writer, full GTID binlog-state code, full optimizer trace implementation, external backup stage implementation, full `JSON_TABLE` table-function implementation, ordinary JSON SQL function implementation, SQL diagnostics statement runtime, system-versioned table predicate item runtime, row-replication type-conversion implementation, dynamic-column execution, stored routine Information Schema scan path, full foreign-server metadata cache implementation, proxy protocol network-listener support, full EXPLAIN/ANALYZE plan-output runtime, vector type handler, event parser data validation, XA transaction implementation, trigger sidecar runtime, view sidecar runtime, table-admin maintenance implementation, key-cache assignment, index preload, inherited persistent statistics tables, JSON histograms, generic `SELECT ... PROCEDURE` runtime, non-`en_US` locale table, `LOAD DATA` / `LOAD XML` execution, or `mysql.time_zone*` table loading, no `log_event_server.cc.o`, no real mmap `tc.log` transaction coordinator, no server encryption hooks, no window functions, no UDF runtime, no SQL crypto/password functions, no VIO TLS transport, no `ENCRYPT()`, no legacy DES, no `KDF()`, no zlib compression, and no dynamic plugin loading |
| stripped `mylite-open-close-smoke` copy | 5,288,280 | 5.04 | `strip --strip-unneeded` on copied binary |

The linked smoke binary has this section profile:

| Section group | Bytes |
| --- | ---: |
| text | 4,195,453 |
| data | 1,089,464 |
| bss | 226,809 |
| total `size` decimal | 5,511,726 |

Largest linked sections in the open-close smoke binary:

| Section | Bytes | Interpretation |
| --- | ---: | --- |
| `.text` | 2,549,900 | Executable code |
| `.data.rel.ro` | 924,856 | Relocated read-only data |
| `.rodata` | 946,411 | Parser tables, SQL metadata, constants, retained Unicode data |
| `.eh_frame` | 475,916 | Unwind metadata |
| `.data` | 151,272 | Writable data |
| `.bss` | 224,889 | Zero-initialized writable data |
| `.eh_frame_hdr` | 100,996 | Unwind table index |
| `.rela.dyn` | 42,720 | Remaining unpacked dynamic relocations |
| `.gcc_except_table` | 36,208 | Exception metadata |
| `.relr.dyn` | 16,416 | Packed relative relocations |

If a Linux distribution bundle vendors the current dynamic dependencies, it
adds about 5,081,640 bytes, or 4.85 MiB, before compression:

| Dependency | Resolved file size |
| --- | ---: |
| `libstdc++.so.6.0.33` | 2,633,224 |
| `libm.so.6` | 591,800 |
| `libgcc_s.so.1` | 133,696 |
| `libc.so.6` | 1,722,920 |

These libraries are not currently part of `libmariadbd.a`. They matter only for
distribution formats that bundle runtime libraries.

## Where the bytes are

The SQL layer dominates the current static footprint. The component table below
was captured before the type-plugin, charset-small, Oracle-parser, static
archive strip, and small-plugin size profiles were applied and explains why
those attempts were worth testing.

| Component archive | Bytes | MiB |
| --- | ---: | ---: |
| `libmysqld/libsql_embedded.a` | 32,826,258 | 31.31 |
| `strings/libstrings.a` | 4,839,668 | 4.62 |
| `plugin/type_inet/libtype_inet_embedded.a` | 2,384,608 | 2.27 |
| `plugin/type_uuid/libtype_uuid_embedded.a` | 1,066,660 | 1.02 |
| `mysys/libmysys.a` | 649,718 | 0.62 |
| `storage/myisam/libmyisam_embedded.a` | 586,252 | 0.56 |
| `storage/mylite/libmylite_embedded.a` | 303,480 | 0.29 |
| `storage/heap/libheap_embedded.a` | 158,668 | 0.15 |
| `storage/myisammrg/libmyisammrg_embedded.a` | 152,748 | 0.15 |
| `storage/csv/libcsv.a` | 112,192 | 0.11 |
| `storage/sequence/libsequence.a` | 99,508 | 0.09 |
| `sql/libsql_sequence_embedded.a` | 95,722 | 0.09 |
| `mylite/libmylite.a` | 93,752 | 0.09 |

The largest individual objects are mostly SQL expression, type, parser,
charset, and optional type support:

| Object | Bytes |
| --- | ---: |
| `item_func.cc.o` | 1,575,832 |
| `sql_type.cc.o` | 1,456,224 |
| `item_strfunc.cc.o` | 1,440,840 |
| `yy_mariadb.cc.o` | 1,386,632 |
| `yy_oracle.cc.o` | 1,372,688 |
| `item.cc.o` | 1,258,392 |
| `item_create.cc.o` | 1,139,664 |
| `item_timefunc.cc.o` | 1,049,896 |
| `item_cmpfunc.cc.o` | 1,027,216 |
| `ctype-uca1400.c.o` | 973,408 |
| `item_geofunc.cc.o` | 908,920 |
| `field.cc.o` | 858,688 |
| `type_uuid/plugin.cc.o` | 858,584 |
| `type_inet/plugin.cc.o` | 794,384 |
| `type_inet/sql_type_inet.cc.o` | 749,712 |
| `ctype-uca.c.o` | 739,744 |

The current built-in plugins are:

- `binlog`
- `heap`
- `myisam` (hidden from user engine selection in the MyLite minsize profile)
- `mylite`
- `mysql_password`

## Measured reduction experiments

| Experiment | Archive bytes | Archive delta | Stripped linked smoke | Linked delta | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| Baseline | 43,405,432 | 0 | 19,331,904 | 0 | Passes current smokes |
| `type-plugin-size-profile` | 39,941,598 | -3,463,834 | 18,935,800 | -396,104 | Passes current smokes |
| `charset-small-profile` after type plugins | 37,356,932 | -6,048,500 | 16,440,560 | -2,891,344 | Passes current smokes |
| `oracle-parser-size-profile` after charset | 35,944,110 | -7,461,322 | 15,850,736 | -3,481,168 | Passes current smokes |
| `static-archive-strip-profile` after Oracle parser | 34,606,670 | -8,798,762 | 15,850,736 | -3,481,168 | Passes current smokes |
| `small-builtin-plugin-profile` after archive strip | 34,474,690 | -8,930,742 | 15,849,720 | -3,482,184 | Passes current smokes |
| `xml-function-size-profile` after small built-ins | 33,957,690 | -9,447,742 | 15,585,480 | -3,746,424 | Passes current smokes |
| `gis-function-size-profile` after XML functions | 33,092,908 | -10,312,524 | 15,122,040 | -4,209,864 | Passes current smokes |
| `executable-export-size-profile` after GIS functions | 33,092,908 | -10,312,524 | 12,959,352 | -6,372,552 | Passes current smokes |
| `vector-function-size-profile` after executable exports | 32,862,726 | -10,542,706 | 12,958,200 | -6,373,704 | Passes current smokes |
| `profiling-size-profile` after vector functions | 32,696,392 | -10,709,040 | 12,958,200 | -6,373,704 | Passes current smokes |
| `help-command-size-profile` after profiling | 32,513,192 | -10,892,240 | 12,892,376 | -6,439,528 | Passes current smokes |
| `procedure-analyse-size-profile` after HELP command | 32,359,184 | -11,046,248 | 12,892,376 | -6,439,528 | Passes current smokes |
| `relr-linker-size-profile` after PROCEDURE ANALYSE | 32,359,184 | -11,046,248 | 8,820,296 | -10,511,608 | Passes current smokes; requires modern glibc loader |
| `legacy-storage-engine-size-profile` after RELR | 32,107,110 | -11,298,322 | 8,786,856 | -10,545,048 | Passes current smokes and harness; CSV/MRG omitted, MyISAM hidden for internal temp tables |
| `section-gc-size-profile` after legacy engines | 36,520,566 | -6,884,866 | 8,458,680 | -10,873,224 | Passes current smokes and harness; linked runtime smaller, static archive larger |
| `json-schema-valid-size-profile` after section GC | 36,174,834 | -7,230,598 | 8,413,768 | -10,918,136 | Passes current smokes and harness; ordinary JSON functions retained |
| `query-cache-size-profile` after JSON schema | 36,101,680 | -7,303,752 | 8,390,256 | -10,941,648 | Passes current smokes and harness; query cache reports unavailable |
| `oracle-function-size-profile` after query cache | 35,783,646 | -7,621,786 | 8,355,880 | -10,976,024 | Passes current smokes and harness; Oracle compatibility aliases omitted |
| `server-utility-function-size-profile` after Oracle aliases | 35,555,602 | -7,849,830 | 8,318,304 | -11,013,600 | Passes current smokes and harness; server utility functions omitted |
| `uca-collation-size-profile` after server utilities | 33,777,694 | -9,627,738 | 6,765,440 | -12,566,464 | Passes current smokes and harness; UCA collations omitted and default collation is `utf8mb4_general_ci` |
| `regex-function-size-profile` after UCA collations | 33,699,880 | -9,705,552 | 6,749,888 | -12,582,016 | Passes current smokes and harness; regex surfaces omitted and PCRE2 runtime dependency removed |
| `binlog-replication-size-profile` after regex functions | 33,676,708 | -9,728,724 | 6,750,400 | -12,581,504 | Passes current smokes and harness; command-level replay/replication sources omitted, linked-runtime delta is smoke-test noise |
| `no-binlog-core-size-profile` after binlog replication | 33,532,138 | -9,873,294 | 6,684,088 | -12,647,816 | Passes current smokes and harness; no-ops core binlog entry points and removes `rpl_record.cc` |
| `myisam-admin-size-profile` after no-binlog-core | 33,415,532 | -9,989,900 | 6,619,904 | -12,712,000 | Passes current smokes and harness; omits MyISAM check/repair admin code while retaining disk temp tables |
| `myisam-fulltext-size-profile` after MyISAM admin | 33,328,744 | -10,076,688 | 6,589,968 | -12,741,936 | Passes current smokes and harness; omits MyISAM full-text implementation while retaining disk temp tables |
| `myisam-rtree-size-profile` after MyISAM full-text | 33,284,948 | -10,120,484 | 6,568,840 | -12,763,064 | Passes current smokes and harness; omits MyISAM RTREE/spatial-key implementation while retaining disk temp tables |
| `spatial-core-size-profile` after MyISAM RTREE | 33,144,206 | -10,261,226 | 6,532,968 | -12,798,936 | Passes current smokes and harness; omits spatial WKB/WKT core while retaining GEOMETRY parse and rejection paths |
| `sql-sequence-size-profile` after spatial core | 32,926,698 | -10,478,734 | 6,518,592 | -12,813,312 | Passes current smokes and harness; omits SQL sequence engine implementation while retaining parser syntax |
| `geometry-type-size-profile` after SQL sequence | 32,556,980 | -10,848,452 | 6,473,832 | -12,858,072 | Passes current smokes and harness; omits GEOMETRY type implementation code while retaining minimal generic metadata symbols |
| `general1400-collation-size-profile` after GEOMETRY type | 32,318,588 | -11,086,844 | 6,258,424 | -13,073,480 | Passes current smokes and harness; omits compiled general1400 collations and unused extended Unicode casefold tables |
| `rpl-filter-size-profile` after general1400 collations | 32,283,380 | -11,122,052 | 6,257,608 | -13,074,296 | Passes current smokes and harness; marginal runtime win, mostly archive cleanup |
| `icf-linker-size-profile` after RPL filter | 32,283,380 | -11,122,052 | 6,094,568 | -13,237,336 | Passes current smokes and harness; link-only `--icf=all` runtime win with address-identity risk |
| `vio-tls-size-profile` after ICF | 32,261,482 | -11,143,950 | 6,083,040 | -13,248,864 | Passes current smokes and harness; removes VIO TLS transport and `libssl.so.3` from the linked runtime dependency set |
| `libcrypt-encrypt-size-profile` after VIO TLS | 32,243,074 | -11,162,358 | 6,080,176 | -13,251,728 | Passes current smokes and harness; omits legacy `ENCRYPT()` and removes `libcrypt.so.1` from the linked runtime dependency set |
| `zlib-compression-size-profile` after libcrypt | 32,172,020 | -11,233,412 | 6,067,344 | -13,264,560 | Passes current smokes and harness; omits zlib compression surfaces and removes `libz.so.1` from the linked runtime dependency set |
| `dynamic-plugin-loading-size-profile` after zlib compression | 32,122,742 | -11,282,690 | 6,041,152 | -13,290,752 | Passes current smokes and harness; compiles out dynamic plugin loading and the full plugin service bridge, reports `have_dynamic_loading=NO`, and leaves the `libcrypto.so.3` dependency in place |
| `des-function-size-profile` after dynamic plugin loading | 32,081,652 | -11,323,780 | 6,034,528 | -13,297,376 | Passes current smokes and harness; omits legacy DES SQL functions and DES key-file administration plumbing, and leaves the `libcrypto.so.3` dependency in place |
| `kdf-function-size-profile` after DES functions | 32,052,836 | -11,352,596 | 6,029,392 | -13,302,512 | Passes current smokes and harness; omits the OpenSSL-backed `KDF()` SQL function and leaves the `libcrypto.so.3` dependency in place |
| `unwind-table-size-profile` after KDF | 31,864,556 | -11,540,876 | 5,962,256 | -13,369,648 | Passes current smokes and harness; removes nonessential unwind tables while retaining C++ exception support |
| `udf-runtime-size-profile` after unwind tables | 31,748,626 | -11,656,806 | 5,926,048 | -13,405,856 | Passes current smokes and harness; omits UDF lookup/execution and removes `sql_udf.cc.o` from the embedded archive |
| `window-function-size-profile` after UDF runtime | 31,138,612 | -12,266,820 | 5,849,432 | -13,482,472 | Passes current smokes and harness; omits dedicated window-function item/execution objects, with small retained stubs |
| `sql-crypto-function-size-profile` after window functions | 31,000,638 | -12,404,794 | 5,823,416 | -13,508,488 | Passes current smokes and harness; omits OpenSSL-backed SQL crypto/password functions, but `libcrypto.so.3` remains rooted by auth, digest, table, and binlog/io-cache encryption helpers |
| `server-encryption-size-profile` after SQL crypto | 30,983,136 | -12,422,296 | 5,815,856 | -13,516,048 | Passes current smokes and harness; omits inherited server-side encryption hooks for binlogs and temporary IO caches, but `libcrypto.so.3` remains rooted by auth, digest, table, and startup compatibility helpers |
| `openssl-digest-size-profile` after server encryption | 30,943,248 | -12,462,184 | 5,816,536 | -13,515,368 | Passes current smokes and harness; replaces retained OpenSSL-backed MD5/SHA-1 wrappers, omits the startup compatibility check, and removes the `libcrypto.so.3` runtime dependency; stripped linked smoke grows by 680 bytes while vendored dependency size drops 4,597,928 bytes |
| `no-binlog-core-size-profile` follow-up after OpenSSL digest | 30,703,028 | -12,702,404 | 5,757,656 | -13,574,248 | Passes current smokes and harness; no-ops embedded binlog open/recovery and annotated-row helpers, removes `gtid_index.cc`, `log_event.cc`, and `rpl_injector.cc`, and keeps a tiny `str_to_hex()` replacement for SQL string rendering |
| `tc-log-mmap-size-profile` after no-binlog core follow-up | 30,685,528 | -12,719,904 | 5,751,536 | -13,580,368 | Passes current smokes and harness; omits the inherited mmap-backed `tc.log` transaction coordinator implementation and leaves inert status variables |
| `append-query-string-size-profile` after TC log mmap | 30,385,682 | -13,019,750 | 5,751,112 | -13,580,792 | Passes current smokes and harness; moves the retained SQL string-rendering helper to the minsize stub and omits `log_event_server.cc.o` |
| `rpl-gtid-state-size-profile` after append-query-string | 30,257,244 | -13,148,188 | 5,750,512 | -13,581,392 | Passes current smokes and harness; replaces retained no-binlog GTID state lifecycle methods with a tiny stub and omits `rpl_gtid.cc.o` |
| `optimizer-trace-size-profile` after RPL GTID state | 30,229,492 | -13,175,940 | 5,743,824 | -13,588,080 | Passes current smokes and harness; replaces optimizer trace diagnostics with an inert embedded stub while preserving shared JSON writer helpers |
| `backup-stage-size-profile` after optimizer trace | 30,218,906 | -13,186,526 | 5,740,424 | -13,591,480 | Passes current smokes and harness; replaces external backup stage, backup lock, and backup DDL logging with embedded stubs and omits `backup.cc.o` |
| `json-table-size-profile` after backup stage | 30,103,286 | -13,302,146 | 5,730,200 | -13,601,704 | Passes current smokes and harness; replaces `JSON_TABLE` table-function execution with an embedded unsupported stub while retaining ordinary JSON scalar functions |
| `foreign-server-cache-size-profile` after JSON_TABLE | 30,072,536 | -13,332,896 | 5,726,568 | -13,605,336 | Passes current smokes and harness; replaces foreign-server metadata cache with no-op embedded stubs and omits `sql_servers.cc.o` |
| `proxy-protocol-size-profile` after foreign-server cache | 30,064,524 | -13,340,908 | 5,726,488 | -13,605,416 | Passes current smokes and harness; replaces proxy protocol network-listener support with disabled embedded stubs and omits `proxy_protocol.cc.o` |
| `event-parse-data-size-profile` after proxy protocol | 30,052,668 | -13,352,764 | 5,726,400 | -13,605,504 | Passes current smokes and harness; replaces full event parser data validation with a minimal parser-allocation stub while event DDL remains rejected |
| `xa-transaction-size-profile` after event parse data | 29,920,080 | -13,485,352 | 5,719,056 | -13,612,848 | Passes current smokes and harness; replaces full XA transaction implementation with embedded-disabled stubs |
| `trigger-runtime-size-profile` after XA transactions | 29,848,586 | -13,556,846 | 5,705,280 | -13,626,624 | Passes current smokes and harness; replaces full trigger sidecar loading and execution with inert embedded no-trigger stubs while trigger DDL remains rejected |
| `view-runtime-size-profile` after trigger runtime | 29,810,458 | -13,594,974 | 5,691,984 | -13,639,920 | Passes current smokes and harness; replaces full view sidecar loading and execution with embedded-disabled stubs while retaining shared derived-table/CTE column-name helpers |
| `table-admin-size-profile` after view runtime | 29,778,266 | -13,627,166 | 5,674,104 | -13,657,800 | Passes current smokes and harness; replaces full table maintenance, key-cache assignment, and index-preload execution with unsupported embedded stubs while retaining prepared admin metadata |
| `persistent-statistics-size-profile` after table admin | 29,621,550 | -13,783,882 | 5,641,032 | -13,690,872 | Passes current smokes and harness; replaces persistent `mysql.*` statistics and JSON histogram storage with no-statistics embedded stubs while preserving handler row estimates |
| `select-procedure-runtime-size-profile` after persistent statistics | 29,500,552 | -13,904,880 | 5,640,696 | -13,691,208 | Passes current smokes and harness; replaces generic `SELECT ... PROCEDURE` dispatch with an unsupported embedded stub |
| `locale-minsize-profile` after SELECT procedure runtime | 29,210,614 | -14,194,818 | 5,582,144 | -13,749,760 | Passes current smokes and harness; replaces the generated full locale table with an `en_US`-only embedded profile stub |
| `load-data-size-profile` after locale minsize | 29,169,370 | -14,236,062 | 5,570,344 | -13,761,560 | Passes current smokes and harness; omits `LOAD DATA` / `LOAD XML` execution while retaining ordinary inserts |
| `oz-compiler-size-profile` after LOAD DATA | 29,169,370 | -14,236,062 | 5,570,216 | -13,761,688 | Passes current smokes and harness; switches aggressive minsize compile flags to `-Oz`, a marginal linked-runtime win |
| `time-zone-table-size-profile` after `-Oz` | 29,147,460 | -14,257,972 | 5,564,416 | -13,767,488 | Passes current smokes and harness; omits `mysql.time_zone*` table loading while retaining `SYSTEM` and numeric offsets |
| `hidden-visibility-size-profile` after time-zone tables | 29,117,602 | -14,287,830 | 5,532,056 | -13,799,848 | Passes current smokes and harness; hides internal symbols by default while keeping explicit MyLite C API exports |
| `explain-runtime-size-profile` after hidden visibility | 28,896,338 | -14,509,094 | 5,496,920 | -13,834,984 | Passes current smokes and harness; omits full EXPLAIN/ANALYZE plan-output runtime while retaining ordinary optimizer bookkeeping |
| `vector-type-size-profile` after EXPLAIN runtime | 28,752,966 | -14,652,466 | 5,489,760 | -13,842,144 | Passes current smokes and harness; omits retained VECTOR type handler after vector functions and indexes are already unsupported |
| `json-function-size-profile` after VECTOR type | 28,094,108 | -15,311,324 | 5,352,064 | -13,979,840 | Passes current smokes and harness; omits ordinary JSON SQL functions and parser-backed JSON aggregate runtime while retaining internal JSON type validation helpers |
| `diagnostics-statement-size-profile` after JSON functions | 27,978,354 | -15,427,078 | 5,345,504 | -13,986,400 | Passes current smokes and harness; omits SQL `GET DIAGNOSTICS`, `SIGNAL`, and `RESIGNAL` statement runtime while retaining internal diagnostics and public MyLite diagnostics |
| `system-versioning-size-profile` after diagnostics statements | 27,863,442 | -15,541,990 | 5,342,968 | -13,988,936 | Passes current smokes and harness; omits system-versioned table predicate item runtime and rejects MyLite temporal table metadata |
| `rpl-utility-server-size-profile` after system versioning | 27,845,446 | -15,559,986 | 5,337,144 | -13,994,760 | Passes current smokes and harness; replaces row-replication conversion utilities with fail-closed embedded stubs |
| `dynamic-column-size-profile` after RPL utility server | 27,669,358 | -15,736,074 | 5,294,680 | -14,037,224 | Passes current smokes and harness; omits dynamic-column SQL item execution and replaces `ma_dyncol.c` with fail-closed C API stubs |
| `routine-information-schema-size-profile` after dynamic columns | 27,644,466 | -15,760,966 | 5,288,280 | -14,043,624 | Passes current smokes and harness; makes `INFORMATION_SCHEMA.ROUTINES` and `PARAMETERS` empty without scanning `mysql.proc` |
| `no-myisam-temp-spill-size-profile` after no-binlog-core | 32,836,602 | -10,568,830 | 6,437,408 | -12,894,496 | Opt-in experiment only; open/close smoke passes, but storage/catalog harness fails because schema-table queries need disk temp tables |
| Strip archive with `strip -g` | 42,261,216 | -1,144,216 | n/a | n/a | Low-risk packaging step |
| Strip archive with `strip --strip-unneeded` | 41,873,048 | -1,532,384 | n/a | n/a | Higher risk than `strip -g` for static archives |
| `WITH_EXTRA_CHARSETS=none` before UCA fix | 40,820,782 | -2,584,650 | 16,836,664 | -2,495,240 | Segfaulted in open-close smoke |
| `WITH_EXTRA_CHARSETS=none`, `DEFAULT_COLLATION=utf8mb4_general_ci`, before UCA fix | 40,820,774 | -2,584,658 | 16,836,664 | -2,495,240 | Still segfaulted in open-close smoke |
| `WITH_EXTRA_CHARSETS=complex` | 43,325,192 | -80,240 | 19,248,368 | -83,536 | Too small to matter |
| all tested `DISABLE_PSI_*` switches | 43,405,432 | 0 | not retested | n/a | No current size effect |
| plugin flags for type/user/sequence plugins | 43,296,232 | -109,200 | 19,265,896 | -66,008 | Large type plugins remain built in |
| `-fno-asynchronous-unwind-tables` | 34,474,690 | 0 from current | 15,849,720 | 0 from current | Reject; current smokes pass but no artifact-size reduction |
| `-fno-rtti` | n/a | n/a | n/a | n/a | Reject; retained SQL headers use `dynamic_cast`, so the build fails |
| `CXXFLAGS=-fno-exceptions` | n/a | n/a | n/a | n/a | Reject; MariaDB thread-pool code uses `catch (std::system_error&)`, so the build fails before SQL sources |
| `SECURITY_HARDENED=OFF` after RPL filter | 32,762,840 | +479,460 | 6,359,576 | +101,968 | Reject; open/close smoke and harness pass, but both archive and linked runtime grow |
| early `-ffunction-sections -fdata-sections` plus `--gc-sections` before export removal | 48,305,352 | +4,899,920 | 19,331,816 | -88 | Superseded by `section-gc-size-profile` after executable exports were removed |
| CMake LTO | 342,480,510 | +299,075,078 | 18,016,192 | -1,315,712 | Reject for now due archive bloat and ODR warnings |

The two original `WITH_EXTRA_CHARSETS=none` builds both completed and linked,
but `mylite-open-close-smoke --mode=exclusive` exited with signal 139. The
follow-up `charset-small-profile` slice fixed the UCA 1400 startup assumption
by skipping generated collations whose base compiled charset is absent. The
profile now passes current smokes while retaining the compiled default
`utf8mb4_uca1400_ai_ci`.

Stripping the current linked open-close smoke binary reduces it from
7,365,424 bytes to 5,288,280 bytes, saving 2,077,144 bytes, or 1.98 MiB. That
remains the lowest-risk packaging win for any copied executable or
shared-library style artifact.

The `icf-linker-size-profile` attempt then enabled lld identical code folding
with `--icf=all`. `--icf=safe` produced no size change in this profile, while
`--icf=all` reduced the stripped linked smoke by 163,040 bytes. The archive is
unchanged because ICF runs only at final link time. Current smokes and harness
pass, but this remains an aggressive linker setting because code that compares
function addresses could observe folded functions.

The `vio-tls-size-profile` attempt then removed the VIO TLS transport from the
aggressive embedded profile while keeping OpenSSL's crypto library for retained
SQL/auth helpers. On top of the ICF profile, it reduced the static archive by
21,898 bytes and the stripped linked smoke by 11,528 bytes. It also removes the
737,192-byte Ubuntu 24.04 ARM64 `libssl.so.3` dependency from packages that
vendor runtime libraries. `libcrypto.so.3` remains because SQL/auth crypto
helpers still root it.

The `libcrypt-encrypt-size-profile` attempt then removed MariaDB's legacy
`ENCRYPT()` SQL function from the aggressive embedded profile and stopped
linking `${LIBCRYPT}` when that function is disabled. On top of the VIO TLS
profile, it reduced the static archive by 18,408 bytes and the stripped linked
smoke by 2,864 bytes. It also removes the 198,584-byte Ubuntu 24.04 ARM64
`libcrypt.so.1.1.0` dependency from packages that vendor runtime libraries.
`have_crypt` still reflects platform detection, but `ENCRYPT()` itself now
fails through the unknown-function path.

The `zlib-compression-size-profile` attempt then removed zlib-backed SQL
compression functions, compressed-column creation, compressed protocol
capability reporting, and compressed binlog helper references from the
aggressive embedded profile while retaining `CRC32()` and MariaDB's
`my_checksum()` API. On top of the libcrypt profile, it reduced the static
archive by 71,054 bytes and the stripped linked smoke by 12,832 bytes. It also
removes the 133,272-byte Ubuntu 24.04 ARM64 `libz.so.1.3` dependency from
packages that vendor runtime libraries. `have_compress` now reports `NO`,
`COMPRESS()`, `UNCOMPRESS()`, and `UNCOMPRESSED_LENGTH()` fail through the
unknown-function path, and compressed-column DDL fails with
`ER_UNKNOWN_COMPRESSION_METHOD`.

The `dynamic-plugin-loading-size-profile` attempt then compiled out the
runtime `dlopen()` plugin loader and replaced the full dynamic plugin service
bridge with the minimal startup-visible `debug_sync_service` placeholder in
the aggressive embedded profile. On top of the zlib profile, it reduced the
static archive by 49,278 bytes and the stripped linked smoke by 26,192 bytes.
The open/close smoke now verifies `have_dynamic_loading=NO`. The
`sql_plugin.cc.o` undefined-symbol count dropped from 309 to 141, including
removing service-table references to crypto, wsrep, logger, JSON, and SQL
client callbacks. This did not remove `libcrypto.so.3`, because SQL crypto,
password/auth, DES key-file, and startup compatibility helpers still rooted
OpenSSL at that point.

The `des-function-size-profile` attempt then removed legacy
`DES_ENCRYPT()` / `DES_DECRYPT()` registration and DES key-file administration
from the aggressive embedded profile. On top of the dynamic-plugin-loading
profile, it reduced the static archive by 41,090 bytes and the stripped linked
smoke by 6,624 bytes. The archive no longer contains `des_key_file.cc.o`, and
the linked smoke no longer contains `des_keyschedule`, `Item_func_des*`, or
`Create_func_des*` symbols. `libcrypto.so.3` remains because retained AES,
SHA, MD5, random-byte, password/auth, SQL digest, and startup compatibility
helpers still root OpenSSL-backed code.

The `kdf-function-size-profile` attempt then removed the OpenSSL-backed
`KDF()` SQL function from the aggressive embedded profile. On top of the DES
profile, it reduced the static archive by 28,816 bytes and the stripped linked
smoke by 5,136 bytes. The linked smoke no longer contains `Item_func_kdf` or
`Create_func_kdf` symbols, and the archive no longer has direct undefined
references to `EVP_PKEY_CTX*` or `PKCS5_PBKDF2_HMAC()` from `KDF()` code.
`libcrypto.so.3` remains because retained AES, SHA, MD5, random-byte,
password/auth, SQL digest, and startup compatibility helpers still root
OpenSSL-backed code.

The `unwind-table-size-profile` attempt then added
`-fno-asynchronous-unwind-tables -fno-unwind-tables` to the aggressive minsize
profile without disabling C++ exceptions. On top of the KDF profile, it reduced
the static archive by 188,280 bytes and the stripped linked smoke by 67,136
bytes. `.eh_frame` dropped from 597,352 to 541,644 bytes and `.eh_frame_hdr`
dropped from 125,516 to 114,076 bytes; `.gcc_except_table` stayed unchanged
because exception support remains enabled. The tradeoff is poorer native
stack-unwind/debugger/profiler metadata in the aggressive size profile.

The `udf-runtime-size-profile` attempt then removed UDF lookup and execution
from the aggressive embedded profile. On top of the unwind-table profile, it
reduced the static archive by 115,930 bytes and the stripped linked smoke by
36,208 bytes. The archive no longer contains `sql_udf.cc.o`, and the linked
smoke no longer contains `Create_udf_func`, `Item_func_udf*`,
`Item_sum_udf*`, `udf_handler`, `find_udf`, `udf_init`,
`mysql_create_function`, or `mysql_drop_function` symbols. UDF DDL was already
rejected in embedded mode; this removes the remaining `mysql.func` and dynamic
library runtime path.

The `window-function-size-profile` attempt then removed dedicated
window-function item and execution objects from the aggressive embedded profile.
On top of the UDF-runtime profile, it reduced the static archive by 610,014
bytes and the stripped linked smoke by 76,616 bytes. The archive no longer
contains `item_windowfunc.cc.o` or `sql_window.cc.o`; the linked smoke no
longer contains `Item_window_func`, dedicated `Item_sum_row_number` / rank /
`NTILE` item symbols, or `Window_func_runner` symbols. Tiny
`setup_windows()` and `Window_funcs_computation` stubs remain to satisfy
retained SELECT-path references that are unreachable after parser rejection.
The open/close smoke verifies ordinary `COUNT()` and `SUM()` still work while
`ROW_NUMBER() OVER ()`, `SUM(1) OVER ()`, and a named `WINDOW` clause fail with
the unsupported-feature diagnostic.

The `sql-crypto-function-size-profile` attempt then removed OpenSSL-backed
SQL crypto/password function entry points from the aggressive embedded profile:
`AES_ENCRYPT()`, `AES_DECRYPT()`, `MD5()`, `SHA()`, `SHA1()`, `SHA2()`,
`PASSWORD()`, `OLD_PASSWORD()`, and `RANDOM_BYTES()`. On top of the
window-function profile, it reduced the static archive by 137,974 bytes and
the stripped linked smoke by 26,016 bytes. The open/close smoke verifies each
removed function fails as an absent or unsupported function, and verifies
`VERSION()` and `CONNECTION_ID()` still execute. `libcrypto.so.3` remains
because retained embedded objects still reference `my_sha1` from
`lib_sql.cc.o`, `client.c.o`, and `password.c.o`, `my_md5` from
`sql_digest.cc.o` and `table.cc.o`, `my_make_scrambled_password*` from
`sql_acl.cc.o`, `my_random_bytes` from `log.cc.o` and
`mf_iocache_encr.cc.o`, and `my_aes_*` from `encryption.cc.o`.

The `server-encryption-size-profile` attempt then removed inherited
server-side encryption hooks for encrypted binary logs, relay logs, and
encrypted temporary IO caches from the aggressive embedded profile. On top of
the SQL crypto profile, it reduced the static archive by 17,502 bytes and the
stripped linked smoke by 7,560 bytes. The linked smoke no longer defines
`my_aes_*` or `my_random_bytes`, and `libsql_embedded.a` no longer has server
encryption references to those helpers. The open/close smoke verifies
`@@global.encrypt_binlog`, `@@global.encrypt_tmp_files`, and
`@@global.encrypt_tmp_disk_tables` all remain disabled. `libcrypto.so.3`
remains because retained objects still reference `my_sha1` from
`lib_sql.cc.o`, `client.c.o`, and `password.c.o`, `my_md5` from
`sql_digest.cc.o` and `table.cc.o`, `my_make_scrambled_password*` from
`sql_acl.cc.o`, and OpenSSL startup compatibility helpers from `openssl.c.o`.

The `openssl-digest-size-profile` attempt then replaced the retained
OpenSSL-backed internal MD5/SHA-1 wrappers with first-party minsize digest
code, omitted unused SHA-2/AES/random wrapper objects from `mysys_ssl`, and
guarded VIO's OpenSSL cleanup path under `MYLITE_DISABLE_VIO_SSL`. On top of
the server-encryption profile, it reduced the static archive by 39,888 bytes.
The stripped linked smoke grew by 680 bytes because the local digest code is
slightly larger than the tiny OpenSSL wrapper call sites, but `ldd` no longer
lists `libcrypto.so.3`. If a package vendors runtime libraries, that removes
4,597,928 bytes from the current Ubuntu 24.04 ARM64 bundle before compression.
The new `mylite-digest-smoke` verifies standard MD5/SHA-1 vectors, multi-input
wrappers, context APIs, and the stubbed startup compatibility check.

The `tc-log-mmap-size-profile` attempt then removed the real
`TC_LOG_MMAP` implementation from the aggressive embedded profile. On top of
the deeper no-binlog follow-up, it reduced the static archive by 17,500 bytes
and the stripped linked smoke by 6,120 bytes. The linked smoke no longer
defines `TC_LOG_MMAP` methods or its vtable; only the dummy `tc_log_mmap`
object, `opt_tc_log_size`, and zero-valued `Tc_log_*` status globals remain.
The compatibility harness reports no unexpected or known inherited sidecars.

The `append-query-string-size-profile` attempt then moved the retained
`append_query_string()` SQL-rendering helper into
`mylite_log_event_core_stub.cc` and removed `log_event_server.cc` from the
no-binlog embedded source list. On top of the TC log mmap profile, it reduced
the static archive by 299,846 bytes and the stripped linked smoke by 424 bytes.
The archive no longer contains `log_event_server.cc.o`; the replacement stub
object is 2,280 bytes. The compatibility harness still passes.

The `rpl-gtid-state-size-profile` attempt then removed the full
`rpl_gtid.cc` implementation from the no-binlog embedded source list and kept
only a tiny `rpl_binlog_state` lifecycle stub for static startup/shutdown. On
top of the append-query-string profile, it reduced the static archive by
128,438 bytes and the stripped linked smoke by 600 bytes. The archive no
longer contains `rpl_gtid.cc.o`; the replacement stub object is 4,136 bytes.
The linked smoke retains only tiny `rpl_binlog_state` lifecycle symbols, and
the compatibility harness still passes.

The `optimizer-trace-size-profile` attempt then removed MariaDB's full
optimizer trace diagnostics from the aggressive embedded profile while keeping
the shared JSON writer helpers needed outside optimizer tracing. On top of the
RPL GTID state profile, it reduced the static archive by 27,752 bytes and the
stripped linked smoke by 6,688 bytes. The archive no longer contains
`opt_trace.cc.o`; the replacement stub object is 22,104 bytes because it keeps
`Json_writer::add_table_name()` and `Json_writer::add_str(Item*)`. The
compatibility harness still passes.

The `backup-stage-size-profile` attempt then removed MariaDB's external backup
stage, backup lock, and backup DDL logging implementation from the aggressive
embedded profile. On top of the optimizer trace profile, it reduced the static
archive by 10,586 bytes and the stripped linked smoke by 3,400 bytes. The
archive no longer contains `backup.cc.o`; the replacement stub object is 4,880
bytes. `BACKUP STAGE` and `BACKUP LOCK` now report an unsupported embedded
statement, internal backup DDL logging is inert, and the compatibility harness
still passes.

The `json-table-size-profile` attempt then removed MariaDB's `JSON_TABLE`
table-function handler and execution implementation from the aggressive
embedded profile while retaining ordinary JSON scalar functions. On top of the
backup-stage profile, it reduced the static archive by 115,620 bytes and the
stripped linked smoke by 10,224 bytes. The archive no longer contains
`json_table.cc.o`; the replacement stub object is 11,256 bytes. The open/close
smoke verifies `JSON_VALID()` still succeeds and `JSON_TABLE(...)` reports a
stable unsupported-feature diagnostic; the compatibility harness still passes.

The `foreign-server-cache-size-profile` attempt then removed MariaDB's
`mysql.servers` foreign-server metadata cache implementation from the
aggressive embedded profile. On top of the JSON_TABLE profile, it reduced the
static archive by 30,750 bytes and the stripped linked smoke by 3,632 bytes.
The archive no longer contains `sql_servers.cc.o`; the replacement stub object
is 3,832 bytes. Startup and reload hooks are no-ops, lookup returns no server,
the embedded bootstrap still rejects `CREATE SERVER`, `ALTER SERVER`, and
`DROP SERVER`, `mysql_servers_startup=absent`, and the compatibility harness
still passes.

The `proxy-protocol-size-profile` attempt then removed MariaDB's proxy
protocol network-listener implementation from the aggressive embedded profile.
On top of the foreign-server-cache profile, it reduced the static archive by
8,012 bytes and the stripped linked smoke by 80 bytes. The archive no longer
contains `proxy_protocol.cc.o`; the replacement stub object is 3,144 bytes.
The open/close smoke verifies `proxy_protocol_networks` remains empty and
rejects non-empty values with `ER_WRONG_VALUE_FOR_VAR`. The compatibility
harness still passes.

The `event-parse-data-size-profile` attempt then removed MariaDB's full event
parser data validation implementation from the aggressive embedded profile.
On top of the proxy-protocol profile, it reduced the static archive by 11,856
bytes and the stripped linked smoke by 88 bytes. The archive no longer
contains `event_parse_data.cc.o`; the replacement parser-allocation stub object
is 2,696 bytes. Embedded bootstrap still rejects `CREATE EVENT`, `ALTER
EVENT`, and `DROP EVENT` before event metadata execution, and the compatibility
harness still passes.

The `xa-transaction-size-profile` attempt then removed MariaDB's full XA
transaction implementation from the aggressive embedded profile. On top of the
event parse-data profile, it reduced the static archive by 132,588 bytes and
the stripped linked smoke by 7,344 bytes. The archive no longer contains
`xa.cc.o`; the replacement stub object is 8,272 bytes after prepared
`XA RECOVER` metadata setup is rejected in `sql_prepare.cc`. The open/close
smoke verifies `XA START` and `XA RECOVER` report MariaDB's embedded-disabled
diagnostic, and the compatibility harness still passes.

The `trigger-runtime-size-profile` attempt then removed MariaDB's full
file-backed trigger loader and runtime from the aggressive embedded profile.
On top of the XA profile, it reduced the static archive by 71,494 bytes and
the stripped linked smoke by 13,776 bytes. The archive no longer contains
`sql_trigger.cc.o`; the replacement no-trigger stub object is 15,112 bytes.
Embedded bootstrap still rejects `CREATE TRIGGER` and `DROP TRIGGER`, ordinary
table open leaves `TABLE::triggers` null, the sidecar scan reports no `.TRG` or
`.TRN` files, and the compatibility harness still passes.

The `view-runtime-size-profile` attempt then removed MariaDB's full
file-backed view runtime from the aggressive embedded profile while preserving
the shared duplicate-column-name helpers used by derived tables, CTEs, and
UNIONs. On top of the trigger profile, it reduced the static archive by
38,128 bytes and the stripped linked smoke by 13,296 bytes. The archive no
longer contains `sql_view.cc.o`; the replacement view stub object is 9,776
bytes. Embedded bootstrap still rejects `CREATE VIEW`, `ALTER VIEW`, and
`DROP VIEW`, the sidecar scan reports no `.frm` files, and the compatibility
harness still passes.

The `table-admin-size-profile` attempt then removed MariaDB's full
table-maintenance, key-cache assignment, and index-preload execution paths from
the aggressive embedded profile while preserving prepared-statement metadata
for admin result sets. On top of the view profile, it reduced the static
archive by 32,192 bytes and the stripped linked smoke by 17,880 bytes. The
archive no longer contains `sql_admin.cc.o`; the replacement table-admin stub
object is 119,312 bytes because `Item_empty_string` metadata construction pulls
in retained `Item` COMDAT/vtable sections. The open/close smoke verifies
`ANALYZE TABLE`, `CHECK TABLE`, `OPTIMIZE TABLE`, `REPAIR TABLE`,
`CACHE INDEX`, and `LOAD INDEX INTO CACHE` report unsupported diagnostics, and
the compatibility harness still passes.

The `persistent-statistics-size-profile` attempt then removed MariaDB's
persistent engine-independent statistics tables and JSON histogram storage from
the aggressive embedded profile. On top of the table-admin profile, it reduced
the static archive by 156,716 bytes and the stripped linked smoke by 33,072
bytes. The archive no longer contains `sql_statistics.cc.o` or
`opt_histogram_json.cc.o`; the replacement persistent-statistics stub object is
5,376 bytes. The stub preserves the non-EITS fallback that copies handler row
estimates into `TABLE::used_stat_records`, because MariaDB's update/delete
planning depends on that value even when persistent statistics are disabled.
The open/close smoke and compatibility harness still pass.

The `select-procedure-runtime-size-profile` attempt then removed MariaDB's
generic `SELECT ... PROCEDURE` result-post-processing runtime after the only
built-in procedure, `PROCEDURE ANALYSE()`, was already unsupported. On top of
the persistent-statistics profile, it reduced the static archive by 120,998
bytes and the stripped linked smoke by 336 bytes. The archive no longer
contains `procedure.cc.o` or `mylite_procedure_analyse_stub.cc.o`; the
replacement `mylite_select_procedure_stub.cc.o` is 1,792 bytes. The open/close
smoke verifies `SELECT ... PROCEDURE ANALYSE()` still fails with
`ER_NOT_SUPPORTED_YET`, now at the generic procedure-clause setup layer, and
the compatibility harness still passes.

The `locale-minsize-profile` attempt then replaced MariaDB's generated full
locale table with an `en_US`-only embedded profile stub. On top of the
SELECT-procedure profile, it reduced the static archive by 289,938 bytes and
the stripped linked smoke by 58,552 bytes. The archive no longer contains
`sql_locale.cc.o`; the replacement `mylite_locale_stub.cc.o` is 8,152 bytes.
The linked smoke retained locale symbol count dropped from 117 to 4. The
open/close smoke verifies `lc_time_names=en_US`, English `DATE_FORMAT()` and
`FORMAT(..., 'en_US')` output, and MariaDB's unknown-locale diagnostic for
`SET lc_time_names='de_DE'`. The compatibility harness still passes.

The `load-data-size-profile` attempt then removed MariaDB's `LOAD DATA` /
`LOAD XML` execution object from the aggressive embedded profile. On top of
the locale profile, it reduced the static archive by 41,244 bytes and the
stripped linked smoke by 11,800 bytes. The archive no longer contains
`sql_load.cc.o`; the replacement `mylite_load_data_stub.cc.o` is 2,008 bytes
and keeps only shared parser helper methods for load-data out variables. The
linked smoke no longer contains `mysql_load()`, while parser-rooted
`select_export` / `select_dump` symbols remain for a future
`SELECT INTO OUTFILE` parser-root slice. The open/close smoke verifies both
`LOAD DATA INFILE` and `LOAD XML INFILE` report the embedded-disabled
diagnostic, and the compatibility harness still passes.

The `oz-compiler-size-profile` attempt then switched the aggressive minsize
profile's C and C++ `MinSizeRel` flags from CMake's default `-Os -DNDEBUG` to
`-Oz -DNDEBUG`. On top of the LOAD DATA profile, the stripped static archive
was unchanged, the unstripped open-close smoke shrank by 200 bytes, the
stripped open-close smoke shrank by 128 bytes, and the stripped compatibility
smoke was unchanged. The open-close `size` total changed by only -8 bytes
because `.text` shrank by 136 bytes while measured bss grew by 128 bytes. This
is kept as an aggressive-profile attempt, but it is not a meaningful
feature-pruning lever.

The `time-zone-table-size-profile` attempt then removed MariaDB's table-backed
named time-zone loader from the aggressive embedded profile. On top of the
`-Oz` profile, it reduced the static archive by 21,910 bytes, the unstripped
open-close smoke by 7,000 bytes, the stripped open-close smoke by 5,800 bytes,
and the stripped compatibility smoke by 7,480 bytes. The archive now contains
`mylite_tztime_stub.cc.o` at 27,608 bytes instead of `tztime.cc.o` at 48,896
bytes. `SET time_zone='SYSTEM'` and numeric offsets such as `+00:00` remain
supported; named zones such as `Europe/Prague` fail with
`ER_UNKNOWN_TIME_ZONE`, and `CONVERT_TZ()` with omitted named zones returns
`NULL`.

The `hidden-visibility-size-profile` attempt then set CMake's C and C++
visibility presets to hidden and enabled hidden inline visibility for the
aggressive minsize profile. On top of the time-zone table profile, it reduced
the static archive by 29,858 bytes, the unstripped open-close smoke by 43,496
bytes, the stripped open-close smoke by 32,360 bytes, and the stripped
compatibility smoke by 32,416 bytes. The public MyLite C API remains annotated
with `MYLITE_API`; this is a packaging and symbol-hygiene lever, not an SQL
feature-pruning lever. Future shared-library packaging that wants to expose
MariaDB C API or plugin service symbols needs a deliberate export policy.

The `explain-runtime-size-profile` attempt then replaced MariaDB's full
EXPLAIN/ANALYZE plan-output implementation with a MyLite minsize stub. On top
of the hidden-visibility profile, it reduced the static archive by 221,264
bytes, the unstripped open-close smoke by 41,912 bytes, the stripped
open-close smoke by 35,136 bytes, and the stripped compatibility smoke by
36,384 bytes. The archive no longer contains `sql_explain.cc.o`; the
replacement `mylite_explain_stub.cc.o` is 47,848 bytes. Ordinary SELECT,
INSERT, UPDATE, and DELETE still retain no-op EXPLAIN plan bookkeeping because
MariaDB executor paths attach analyze trackers through those objects.
`EXPLAIN SELECT 1`, `ANALYZE SELECT 1`, and `SHOW EXPLAIN FOR 1` now report
the unsupported EXPLAIN-runtime diagnostic in the aggressive minsize profile.

The `vector-type-size-profile` attempt then removed MariaDB's retained
`VECTOR` type handler from the aggressive embedded profile after vector
functions and MHNSW vector indexes were already omitted. On top of the EXPLAIN
runtime profile, it reduced the static archive by 143,372 bytes, the
unstripped open-close smoke by 15,168 bytes, the stripped open-close smoke by
7,160 bytes, and the stripped compatibility smoke by 7,336 bytes. The archive
no longer contains `sql_type_vector.cc.o`, and the open/close smoke verifies
`CREATE TABLE mylite.vector_type_rejected (v VECTOR(3))` reports
`Unknown data type: 'VECTOR'` without creating a MyLite table.

The `json-function-size-profile` attempt then removed MariaDB's ordinary JSON
SQL function implementation from the aggressive embedded profile while keeping
the internal JSON type validation shell needed by retained JSON columns. On top
of the VECTOR type profile, it reduced the static archive by 658,858 bytes, the
unstripped open-close smoke by 199,840 bytes, the stripped open-close smoke by
137,696 bytes, and the stripped compatibility smoke by 138,944 bytes. The
archive no longer contains `item_jsonfunc.cc.o` or `json_schema_helper.cc.o`;
it keeps `mylite_json_function_stub.cc.o` and `sql_type_json.cc.o`. The
open/close smoke verifies `JSON_VALID()` and `JSON_EXTRACT()` fail as unknown
functions, while `JSON_ARRAYAGG()` and `JSON_OBJECTAGG()` report explicit
unsupported diagnostics.

The `diagnostics-statement-size-profile` attempt then removed SQL
programmatic diagnostics statement runtime from the aggressive embedded
profile. On top of the JSON-function profile, it reduced the static archive by
115,754 bytes, the unstripped open-close smoke by 8,240 bytes, the stripped
open-close smoke by 6,560 bytes, and the stripped compatibility smoke by 7,720
bytes. The archive no longer contains `sql_get_diagnostics.cc.o` or
`sql_signal.cc.o`; it keeps `mylite_sql_diagnostics_stub.cc.o`. The open/close
smoke verifies `GET DIAGNOSTICS`, `SIGNAL`, and `RESIGNAL` report explicit
unsupported diagnostics while ordinary C API diagnostics and warning
enumeration continue to pass.

The `system-versioning-size-profile` attempt then removed
`item_vers.cc` from the aggressive embedded profile and hosted the remaining
unsupported temporal predicate item methods inside already-linked
`sql_select.cc`. On top of the diagnostics-statement profile, it reduced the
static archive by 114,912 bytes, the unstripped open-close smoke by 3,240
bytes, the stripped open-close smoke by 2,536 bytes, and the stripped
compatibility smoke by 2,488 bytes. The storage smoke verifies MyLite rejects
`WITH SYSTEM VERSIONING`, application `PERIOD FOR` metadata, and copy `ALTER`
attempts to add either surface while preserving the base table.

The `rpl-utility-server-size-profile` attempt then removed MariaDB's
server-side row-replication type-conversion implementation from the aggressive
embedded profile and replaced the retained field/type-handler virtual methods
with fail-closed no-replication stubs. On top of the system-versioning profile,
it reduced the static archive by 17,996 bytes, the unstripped open-close smoke
by 6,528 bytes, the stripped open-close smoke by 5,824 bytes, and the stripped
compatibility smoke by 5,840 bytes. The archive no longer contains
`rpl_utility_server.cc.o`; it keeps `rpl_utility.cc.o` and
`mylite_rpl_utility_server_stub.cc.o`. This is a small but clean embedded-only
win because retained field/type vtables and RTTI remain the dominant part of
that object boundary.

The `dynamic-column-size-profile` attempt then omitted MariaDB dynamic-column
SQL item execution and replaced `mysys/ma_dyncol.c` with fail-closed C API
stubs. On top of the RPL utility-server profile, it reduced the static archive
by 176,088 bytes, the unstripped open-close smoke by 56,816 bytes, the stripped
open-close smoke by 42,464 bytes, and the stripped compatibility smoke by
44,272 bytes. `libmysys.a` contains `mylite_dyncol_stub.c.o` instead of
`ma_dyncol.c.o`; the linked smoke no longer contains `Item_func_dyncol_*` or
`Item_dyncol_get` symbols. The open/close smoke verifies `COLUMN_CREATE`,
`COLUMN_ADD`, `COLUMN_DELETE`, and `COLUMN_GET` report explicit unsupported
diagnostics while `COLUMN_CHECK`, `COLUMN_EXISTS`, `COLUMN_LIST`, and
`COLUMN_JSON` fail as missing functions. The minsize script also sets
Connector/C's `WITH_DYNCOL=OFF`; this does not change the measured embedded
archive target but keeps accidentally built client-library targets aligned with
the same profile.

The `routine-information-schema-size-profile` attempt then made
`INFORMATION_SCHEMA.ROUTINES`, `INFORMATION_SCHEMA.PARAMETERS`, `SHOW PROCEDURE
STATUS`, and `SHOW FUNCTION STATUS` return empty result sets in the aggressive
embedded profile without scanning `mysql.proc`. On top of the dynamic-column
profile, it reduced the static archive by 24,892 bytes, the unstripped
open-close smoke by 7,888 bytes, the stripped open-close smoke by 6,400 bytes,
the unstripped compatibility smoke by 9,816 bytes, and the stripped
compatibility smoke by 8,096 bytes. `sql_show.cc.o` dropped from 598,544 bytes
to 573,992 bytes, and the linked open-close smoke no longer contains
`fill_schema_proc()`, `store_schema_proc()`, `store_schema_params()`, or
`check_proc_record()`. This is a safe but minor win; the larger stored-program
parser/runtime remains coupled to core SQL code.

The LTO build reduced the stripped linked smoke binary by 1.25 MiB, but the
static archive became 326.61 MiB and GCC emitted type/ODR mismatch warnings
around MariaDB parser and server structures, including generated parser types.
That is not a safe release lever today.

Disabling MariaDB's `SECURITY_HARDENED` CMake option is also not a size win in
the current profile. It removes stack-protector and related hardening checks,
but the measured `build/mariadb-size-no-hardening-rpl` build was larger in
both the stripped static archive and stripped linked smoke while still keeping
the same OpenSSL dynamic dependencies.

The `-fno-exceptions` compiler experiment failed before reaching SQL sources:
`vendor/mariadb/server/tpool/tpool_generic.cc` catches
`std::system_error`. Removing exception support would require a real
thread-pool and first-party allocation/error-handling slice rather than a
compiler flag.

The earlier plugin flags experiment did remove small built-ins such as `sequence`,
`thread_pool_info`, and `user_variables`, but `type_geom`, `type_inet`,
`type_uuid`, and `userstat` remained built in. The later type-plugin and
small-plugin profiles supersede that partial result.

The `type-plugin-size-profile` attempt then made `type_geom`, `type_inet`, and
`type_uuid` non-mandatory and disabled them in the MyLite minsize profile. That
reduced the static archive by 3,463,834 bytes and the stripped linked smoke by
396,104 bytes while current MyLite smokes still passed.

The `charset-small-profile` attempt then set `WITH_EXTRA_CHARSETS=none` and
fixed UCA 1400 registration for omitted base charsets. On top of the type
plugin profile, it reduced the static archive by another 2,584,666 bytes and
the stripped linked smoke binary by another 2,495,240 bytes while current MyLite
smokes still passed.

The `oracle-parser-size-profile` attempt then removed `yy_oracle.cc.o` from the
embedded archive and linked a 1,664-byte unsupported-mode stub instead. On top
of the charset-small profile, it reduced the static archive by another
1,412,822 bytes and the stripped linked smoke binary by another 589,824 bytes
while current MyLite smokes still passed. The open/close smoke now verifies
that a statement parsed after `SET sql_mode=ORACLE` fails with
`ER_NOT_SUPPORTED_YET`.

The `static-archive-strip-profile` attempt then ran `strip --strip-unneeded`
and `ranlib` on `libmariadbd.a` after building the `mysqlserver` target. On top
of the Oracle-parser profile, it reduced the static archive by another
1,337,440 bytes. The stripped linked smoke binary was unchanged, but relinking
current smokes against the stripped archive passed.

The `small-builtin-plugin-profile` attempt then disabled optional `sequence`,
`thread_pool_info`, `user_variables`, and `userstat` built-ins while leaving
mandatory `sql_sequence` support intact. On top of the static archive strip, it
reduced the static archive by another 131,980 bytes and the stripped linked
smoke binary by another 1,016 bytes.

The `xml-function-size-profile` attempt then omitted `item_xmlfunc.cc` from the
embedded source list and removed the `EXTRACTVALUE` and `UPDATEXML` native
function builders in the minsize profile. On top of the small built-in plugin
profile, it reduced the static archive by another 517,000 bytes and the
stripped linked smoke binary by another 264,240 bytes. The linked open-close
smoke binary no longer contains `Item_func_xml_extractvalue`,
`Item_func_xml_update`, or `my_xpath_function` symbols, and the open/close
smoke verifies both XML functions now fail through MariaDB's unknown-function
path.

The `gis-function-size-profile` attempt then omitted `item_geofunc.cc`,
`gcalc_tools.cc`, and `gcalc_slicescan.cc` from the embedded source list and
linked a small empty GIS registry/type-constructor shim instead. On top of the
XML function profile, it reduced the static archive by another 864,782 bytes
and the stripped linked smoke binary by another 463,440 bytes. The linked
open-close smoke binary no longer contains `Item_func_geometry_from_text`,
`Create_func_geometry_from_text`, or `Gcalc_function` symbols, and the
open/close smoke verifies `ST_ASTEXT()` now fails through MariaDB's
unknown-function path. The compatibility harness still verifies MyLite rejects
GEOMETRY columns and SPATIAL keys.

The `executable-export-size-profile` attempt then removed `ENABLE_EXPORTS TRUE`
from MyLite-owned smoke executables. On top of the GIS function profile, it left
the static archive unchanged and reduced the stripped linked smoke binary by
another 2,162,688 bytes. The dynamic symbol count in
`mylite-open-close-smoke` dropped from 28,646 to 488, and the link command no
longer includes `-Wl,--export-dynamic`. This is a linked-artifact size win, not
a static archive reduction.

The `vector-function-size-profile` attempt then omitted `item_vectorfunc.cc`
and `vector_mhnsw.cc` from the embedded source list, guarded vector native
function builders, removed the mandatory `mhnsw` built-in plugin reference in
the vector-disabled profile, and linked a small MHNSW unsupported-symbol stub.
On top of the executable-export profile, it reduced the static archive by
another 230,182 bytes and the stripped linked smoke binary by another 1,152
bytes. The archive no longer defines `builtin_maria_mhnsw_plugin`,
`Item_func_vec_*`, `Create_func_vec_*`, `FVectorNode`, or `MHNSW_Share`
symbols, and the open/close smoke verifies `VEC_FROMTEXT()` and
`VEC_DISTANCE()` fail through MariaDB's unknown-function path.

The `profiling-size-profile` attempt then set `ENABLED_PROFILING=OFF` in the
minsize CMake profile and added a `SHOW PROFILES` disabled-feature smoke check.
On top of the vector-function profile, it reduced the static archive by another
166,334 bytes and left the stripped linked smoke binary unchanged. The archive
no longer defines the full `PROFILING`, `QUERY_PROFILE`, or
`PROF_MEASUREMENT` classes, while retaining small disabled-feature entry points
for MariaDB's existing SQL and information-schema wiring.

The `help-command-size-profile` attempt then removed `sql_help.cc` from the
embedded source list and linked a small unsupported-command shim for
`mysqld_help()` and `mysqld_help_prepare()`. On top of the profiling profile,
it reduced the static archive by another 183,200 bytes and the stripped linked
smoke binary by another 65,824 bytes. The archive now contains
`mylite_help_command_stub.cc.o` instead of the full help-table implementation,
and the open/close smoke verifies `HELP 'contents'` fails with
`ER_NOT_SUPPORTED_YET`.

The `procedure-analyse-size-profile` attempt then removed `sql_analyse.cc` from
the embedded source list and linked a small unsupported-feature shim for
`proc_analyse_init()`. On top of the help-command profile, it reduced the
static archive by another 154,008 bytes and left the stripped linked smoke
binary unchanged. The archive now contains
`mylite_procedure_analyse_stub.cc.o` instead of the full result-set analyser,
and the open/close smoke verifies `SELECT ... PROCEDURE ANALYSE()` fails with
`ER_NOT_SUPPORTED_YET`.

The `relr-linker-size-profile` attempt then installed `lld` in the minsize
container and linked executable, shared-library, and module artifacts with
`-fuse-ld=lld -Wl,-z,pack-relative-relocs -Wl,--pack-dyn-relocs=relr`. On top
of the procedure-analyse profile, it left the static archive unchanged and
reduced the stripped linked smoke binary by another 4,072,080 bytes. The linked
smoke binary now contains `DT_RELR`, `DT_RELRSZ`, `DT_RELRENT`, and the
`GLIBC_ABI_DT_RELR` version dependency. This is the largest linked-runtime
reduction so far, but it requires a modern glibc loader and should be treated
as a packaging baseline decision.

The `legacy-storage-engine-size-profile` attempt then removed CSV and
MRG_MyISAM from the MyLite minsize plugin list, kept MyISAM initialized for
MariaDB's inherited internal disk temporary table path, and marked MyISAM with
`HTON_NOT_USER_SELECTABLE` so explicit `ENGINE=MyISAM` DDL fails like an
unknown engine. On top of the RELR profile, it reduced the static archive by
another 252,074 bytes and the stripped linked smoke binary by another 33,440
bytes. The open/close smoke verifies `ENGINE=CSV`, `ENGINE=MyISAM`, and
`ENGINE=MRG_MyISAM` all fail through unknown-engine diagnostics, and the full
compatibility harness passes using `MEMORY` as the MariaDB reference engine.

The `section-gc-size-profile` attempt then revisited function/data sections
after the executable-export profile stopped exporting the full linked SQL
symbol set. On top of the legacy storage-engine profile, it grew the stripped
static `libmariadbd.a` by 4,413,456 bytes because every function/data section
adds object metadata, but it reduced the stripped linked open-close smoke by
328,176 bytes. The full compatibility harness still passes and the sidecar scan
still reports no unexpected sidecars. This is a linked-runtime-size lever, not
a static-archive-size lever.

The `json-schema-valid-size-profile` attempt then omitted the
`JSON_SCHEMA_VALID()` native function builder and `json_schema.cc` from the
embedded source list while keeping ordinary JSON functions and
`json_schema_helper.cc` for retained JSON array-intersection code. On top of
the section-GC profile, it reduced the stripped static archive by another
345,732 bytes and the stripped linked open-close smoke by another 44,912 bytes.
The open/close smoke verifies `JSON_VALID()` still succeeds while
`JSON_SCHEMA_VALID()` fails through MariaDB's unknown-function path. The full
compatibility harness passes and the sidecar scan reports no unexpected
sidecars.

The `query-cache-size-profile` attempt then removed `sql_cache.cc.o` and
`emb_qcache.cc.o` from the embedded minsize archive, linked a disabled
`Query_cache` shim, and set `have_query_cache=NO`. On top of the JSON-schema
profile, it reduced the static archive by 73,154 bytes and the stripped linked
smoke by 23,512 bytes while keeping normal `SELECT` execution and
`SELECT SQL_CACHE` syntax working as no-cache execution.

The `oracle-function-size-profile` attempt then removed Oracle compatibility
function aliases and the Oracle native function hash from the embedded minsize
profile, routed parser-side Oracle-only constructs to unsupported diagnostics,
and stopped registering `oracle_schema` as a distinct built-in schema. On top
of the query-cache profile, it reduced the static archive by 318,034 bytes and
the stripped linked smoke by 34,376 bytes. The linked smoke binary no longer
contains Oracle-specific item vtables or native function builders; the
remaining Oracle-named linked symbols are `oracle_schema_ref` and the inherited
`Date_time_format_oracle` helper.

The `server-utility-function-size-profile` attempt then removed native builders
and item class implementations for `BENCHMARK()`, `BINLOG_GTID_POS()`,
`GET_LOCK()`, `IS_FREE_LOCK()`, `IS_USED_LOCK()`, `LOAD_FILE()`,
`MASTER_GTID_WAIT()`, `MASTER_POS_WAIT()`, `RELEASE_ALL_LOCKS()`,
`RELEASE_LOCK()`, `SLEEP()`, and `UUID_SHORT()` in the aggressive minsize
profile. Shared lifecycle helpers such as user-level-lock cleanup,
`pause_execution()`, and `server_uuid_value()` remain because other retained
MariaDB code still references them. On top of the Oracle-function profile, it
reduced the static archive by 228,044 bytes and the stripped linked smoke by
37,576 bytes. The open/close smoke verifies each removed function fails as an
unknown function and retained utility functions such as `VERSION()` and
`CONNECTION_ID()` still execute. `RANDOM_BYTES()` is removed separately by the
later SQL crypto size profile.

The `uca-collation-size-profile` attempt then made `HAVE_UCA_COLLATIONS`
optional for the aggressive minsize profile, stopped compiling the UCA 0900
alias object and UCA 1400 generated data object, retained a tiny no-UCA
`ctype-uca.c.o` for shared contraction helper symbols, disabled MariaDB's
startup remap of Unicode character-set defaults to `uca1400_ai_ci`, and set
the compiled default collation to `utf8mb4_general_ci`. On top of the
server-utility profile, it reduced the static archive by 1,777,908 bytes and
the stripped linked smoke by 1,552,864 bytes. The open/close smoke verifies
`@@collation_server=utf8mb4_general_ci`, verifies explicit
`utf8mb4_general_ci` use succeeds, and verifies `utf8mb4_uca1400_ai_ci` fails
with MariaDB's unknown-collation diagnostic.

The `regex-function-size-profile` attempt then removed PCRE-backed regular
expression execution from the aggressive minsize profile. It keeps the parsed
`REGEXP` and `RLIKE` operators as a small unsupported item path, removes
native builders for `REGEXP_INSTR()`, `REGEXP_REPLACE()`, and
`REGEXP_SUBSTR()`, and removes `pcre2-8` from the embedded merge list. On top
of the UCA-collation profile, it reduced the static archive by 77,814 bytes,
the stripped linked smoke by 15,552 bytes, and the vendored dynamic dependency
set by 592,328 bytes. The open/close smoke verifies `LIKE` still works,
`REGEXP` and `RLIKE` fail with `ER_NOT_SUPPORTED_YET`, and the removed regex
functions fail through MariaDB's unknown-function path.

The `binlog-replication-size-profile` attempt then omitted embedded
command/replay sources for `BINLOG`, replication SQL, slave glue,
fail-safe replication, and replication reporting. `rpl_injector.cc` and
`rpl_record.cc` remain because retained cleanup and row-packing paths still
reference `injector::free_instance()` and `pack_row()`. On top of the regex
profile, this reduced the stripped static archive by 23,172 bytes, while the
stripped linked smoke grew by 512 bytes because of the added unsupported
`BINLOG` smoke assertion. The result confirms that command-level replication
pruning is an archive cleanup, not the meaningful linked-runtime binlog cut;
`MYSQL_BIN_LOG`, `Log_event`, row-event helpers, GTID state, and binlog
transaction participant code remain live.

The `no-binlog-core-size-profile` attempt then compiled the remaining embedded
binlog transaction, row-event, GTID-state, and event-write entry points to
no-ops and removed the now-unreferenced `rpl_record.cc` source. On top of the
binlog-replication profile, it reduced the static archive by another 144,570
bytes and the stripped linked smoke by another 66,312 bytes. A broader removal
of `log_event.cc`, `log_event_server.cc`, `rpl_gtid.cc`, `gtid_index.cc`,
`rpl_filter.cc`, and `rpl_injector.cc` failed the final executable link because
embedded startup/cleanup, table-open filtering, and generic `log.cc` helpers
still root those symbols.

A later no-binlog follow-up guarded embedded binlog file open/recovery,
annotated row-event logging, `Gtid_index_writer` startup/cleanup, and
binlog-space status reporting. It then removed `gtid_index.cc`,
`rpl_injector.cc`, and `log_event.cc` from the minsize archive while keeping
`log_event_server.cc`, because `append_query_string()` remains a generic SQL
string-rendering helper outside binlog execution. On top of the OpenSSL-digest
profile, this reduced the static archive by another 240,220 bytes and the
stripped linked smoke by another 58,880 bytes. The linked smoke no longer
defines `Gtid_index_writer`, `injector`, `Format_description_log_event`,
`Binlog_checkpoint_log_event`, `Query_log_event`, `Rows_log_event`, or
`Log_event_writer` symbols.

The `myisam-admin-size-profile` attempt then omitted `mi_check.c` and compiled
MyISAM check, analyze, repair, optimize, key-cache assignment, preload, and
auto-repair admin paths as unsupported in the aggressive profile. On top of
the no-binlog-core profile, it reduced the static archive by another 116,606
bytes and the stripped linked smoke by another 64,184 bytes. The full
compatibility harness still passes because MyISAM remains available for
MariaDB's inherited disk temporary tables; user `ENGINE=MyISAM` remains hidden.

The `myisam-fulltext-size-profile` attempt then omitted MyISAM full-text
implementation sources, skipped full-text stopword startup and system
variables, stopped advertising `HA_CAN_FULLTEXT`, and compiled out direct
MyISAM full-text key update paths. On top of the MyISAM-admin profile, it
reduced the static archive by another 86,788 bytes and the stripped linked
smoke by another 29,936 bytes. The linked smoke no longer contains `ft_*.o`
members or live `ft_*`, `_mi_ft_*`, `_ft_*`, or `ha_myisam::ft_*` symbols.
The full compatibility harness still passes because ordinary MyISAM disk
temporary tables use non-full-text keys.

The `myisam-rtree-size-profile` attempt then omitted MyISAM RTREE/spatial-key
implementation sources, stopped advertising `HA_CAN_RTREEKEYS`, reported
`have_rtree_keys=NO`, and compiled generic MyISAM read/write/index paths so
they no longer reference RTREE helpers. On top of the MyISAM-full-text profile,
it reduced the static archive by another 43,796 bytes and the stripped linked
smoke by another 21,128 bytes. The linked smoke no longer contains `rt_*.o`,
`sp_key.c.o`, or live `rtree_*`/`sp_make_key` function symbols. The full
compatibility harness still passes because ordinary MyISAM disk temporary
tables use non-RTREE keys.

The `spatial-core-size-profile` attempt then removed MariaDB's retained
`spatial.cc` WKB/WKT implementation from the aggressive embedded source list
and linked a small stub for the remaining `Geometry` symbols required by
`sql_type_geom.cc`. On top of the MyISAM RTREE profile, it reduced the static
archive by another 140,742 bytes and the stripped linked smoke by another
35,872 bytes. The linked smoke no longer contains `spatial.cc.o` or live
`Gis_*` implementation symbols, while GEOMETRY type parsing and MyLite
GEOMETRY/SPATIAL rejection paths remain covered by the compatibility harness.

The `sql-sequence-size-profile` attempt then skipped MariaDB's mandatory
`sql_sequence` plugin registration for the aggressive profile, removed
`sql_sequence.cc` and `ha_sequence.cc` from the embedded source list, and
linked a small unsupported-feature stub for retained parser, table-open,
expression, and metadata references. On top of the spatial-core profile, it
reduced the static archive by another 217,508 bytes and the stripped linked
smoke by another 14,376 bytes. The linked smoke no longer contains
`sql_sequence.cc.o`, `ha_sequence.cc.o`, or
`builtin_maria_sql_sequence_plugin`; `CREATE SEQUENCE` reports unsupported,
`CREATE TABLE ... SEQUENCE=1` is rejected by MyLite, and sequence value
expressions fail through MariaDB's missing-sequence diagnostics.

The `geometry-type-size-profile` attempt then removed `sql_type_geom.cc` from
the aggressive embedded source list and linked a minimal stub for the generic
GEOMETRY type handler and type collection symbols still referenced by retained
MariaDB metadata paths. On top of the SQL-sequence profile, it reduced the
static archive by another 369,718 bytes and the stripped linked smoke by
another 44,760 bytes. The linked smoke no longer contains `sql_type_geom.cc.o`,
`Field_geom`, or concrete geometry subtype handlers; GEOMETRY/SPATIAL DDL still
fails without creating a MyLite table.

The `general1400-collation-size-profile` attempt then stopped registering
compiled `utf8mb3_general1400_as_ci` and `utf8mb4_general1400_as_ci`
collations in the aggressive UCA-disabled profile, routed retained internal
case-insensitive comparisons to `utf8mb3_general_ci`, and omitted unused UCA
5.2.0, UCA 14.0.0, and Turkish casefold definitions. On top of the GEOMETRY
type profile, it reduced the static archive by another 238,392 bytes and the
stripped linked smoke by another 215,408 bytes. The linked smoke no longer
contains `general1400` collation symbols, `my_casefold_unicode*`,
`my_casefold_turkish`, `my_u1400*casefold_index`, or
`my_u520_casefold_index`; the open/close smoke verifies
`utf8mb4_general1400_as_ci` fails as an unknown collation.

The `no-myisam-temp-spill-size-profile` experiment then omitted the mandatory
MyISAM plugin and rejected inherited disk temporary-table spill with
`ER_NOT_SUPPORTED_YET`. On top of the no-binlog-core profile, it reduced the
static archive by another 695,536 bytes and the stripped linked smoke by
246,680 bytes. This is not acceptable for the default minsize profile today:
`SHOW COLUMNS` and catalog persistence smokes fail because MariaDB routes
schema-table metadata through disk temporary tables when the result shape needs
MyISAM-compatible storage.

## Decision matrix

| Lever | Expected savings | Risk | Worth doing? | Reason |
| --- | ---: | --- | --- | --- |
| Strip copied release binaries | About 1.98 MiB on the current linked smoke binary | Low | Yes | Standard packaging step; does not change source behavior |
| Strip release static archive with `strip --strip-unneeded` | 1.28 MiB beyond Oracle-parser profile | Medium | Applied as size attempt | Current smokes relink and pass; downstream static consumers may still need coverage |
| Strip release static archive with `strip -g` | About 0.95 MiB on the current archive | Low | Fallback | Less aggressive alternative if `--strip-unneeded` breaks a consumer |
| `WITH_EXTRA_CHARSETS=complex` | About 0.08 MiB | Low | No | Savings are too small to justify a compatibility profile |
| `WITH_EXTRA_CHARSETS=none` / `charset-small-profile` | 2.46 MiB archive and 2.38 MiB stripped linked beyond type-plugin profile | High compatibility | Applied as size attempt | Current smokes pass after the UCA 1400 null-base fix, but non-default charsets are omitted |
| Make type plugins profile-gated | 3.30 MiB archive, 0.38 MiB stripped linked | Medium/high | Applied as size attempt | Current smokes pass, but `INET`, `UUID`, and spatial plugin surfaces are compatibility tradeoffs |
| Remove small optional built-ins | 0.13 MiB archive, 0.001 MiB stripped linked | Medium/low | Applied as size attempt | Current smokes pass, but plugin-provided information-schema surfaces are omitted |
| Remove or profile-gate Oracle SQL parser | 1.35 MiB archive and 0.56 MiB stripped linked beyond charset-small profile | High compatibility | Applied as size attempt | Current smokes pass, but `sql_mode=ORACLE` now fails explicitly in the minsize profile |
| Remove XML SQL functions | 0.49 MiB archive and 0.25 MiB stripped linked beyond small-builtin profile | Medium compatibility | Applied as size attempt | Current smokes pass, but `EXTRACTVALUE()` and `UPDATEXML()` now fail as unknown functions |
| Remove GIS SQL functions | 0.82 MiB archive and 0.44 MiB stripped linked beyond XML profile | High compatibility | Applied as size attempt | Current smokes pass, but native GIS functions now fail as unknown functions in the minsize profile |
| Remove unnecessary executable symbol exports | 0 archive, 2.06 MiB stripped linked beyond GIS profile | Low/medium | Applied as size attempt | Current smokes pass; this only applies to linked executables that are not dynamic-plugin hosts |
| Hide internal symbols with CMake visibility defaults | 0.03 MiB archive, 0.03 MiB stripped linked beyond time-zone tables | Low/medium packaging | Applied as aggressive linked-size attempt | Current smokes pass; MyLite C API exports remain explicit, but final shared-library packaging still needs an export policy |
| Omit EXPLAIN/ANALYZE plan-output runtime | 0.21 MiB archive, 0.034 MiB stripped linked beyond hidden visibility | High SQL compatibility | Applied as aggressive size attempt | Current smokes and harness pass; ordinary optimizer bookkeeping remains, but `EXPLAIN`, `ANALYZE`, and `SHOW EXPLAIN` are unsupported |
| Remove vector SQL functions and MHNSW | 0.22 MiB archive, negligible stripped linked beyond executable-export profile | High compatibility | Applied as size attempt | Current smokes pass, but vector functions and MHNSW vector indexes are omitted from the minsize profile |
| Remove retained `VECTOR` type handler | 0.14 MiB archive, 0.007 MiB stripped linked beyond EXPLAIN runtime | High compatibility | Applied as aggressive size attempt | Current smokes and harness pass; `VECTOR` columns now fail as an unknown data type in the minsize profile |
| Omit ordinary JSON SQL functions | 0.63 MiB archive, 0.13 MiB stripped linked beyond VECTOR type | High compatibility | Applied as aggressive size attempt | Current smokes and harness pass; `JSON_VALID()` and `JSON_EXTRACT()` are unknown, JSON aggregates are unsupported, and retained JSON type validation uses a tiny internal stub |
| Omit SQL diagnostics statements | 0.11 MiB archive, 0.006 MiB stripped linked beyond JSON functions | Medium compatibility | Applied as aggressive size attempt | Current smokes and harness pass; `GET DIAGNOSTICS`, `SIGNAL`, and `RESIGNAL` are unsupported, but internal diagnostics and MyLite C API warning access remain |
| Omit system-versioning item runtime | 0.11 MiB archive, 0.002 MiB stripped linked beyond diagnostics statements | High compatibility | Applied as aggressive size attempt | Current smokes and harness pass; MyLite temporal table metadata is now explicitly rejected, and the tiny remaining methods live in `sql_select.cc` to avoid a separate stub object |
| Omit row-replication conversion utilities | 0.02 MiB archive, 0.006 MiB stripped linked beyond system versioning | Low embedded compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; replication conversion is unsupported, but retained field/type vtables and RTTI keep the win small |
| Omit dynamic columns | 0.17 MiB archive, 0.04 MiB stripped linked beyond RPL utility server | Medium SQL/C API compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; dynamic-column SQL and blob helper APIs are rare and unrelated to MyLite storage, but inherited MariaDB client C helper behavior changes |
| Empty routine Information Schema tables | 0.02 MiB archive, 0.006 MiB stripped linked beyond dynamic columns | Low while routines are unsupported | Applied as aggressive embedded-size attempt | Current smokes and harness pass; `ROUTINES`, `PARAMETERS`, and routine `SHOW ... STATUS` paths return empty results without scanning `mysql.proc`, but the larger stored-program runtime remains |
| Disable statement profiling | 0.16 MiB archive, no stripped linked change beyond vector-function profile | Low/medium | Applied as size attempt | Current smokes pass; `SHOW PROFILE(S)` now report MariaDB's disabled-feature diagnostic |
| Remove SQL `HELP` command implementation | 0.17 MiB archive, 0.06 MiB stripped linked beyond profiling profile | Low/medium | Applied as size attempt | Current smokes pass; `HELP` now reports a stable unsupported-command diagnostic |
| Remove `PROCEDURE ANALYSE()` implementation | 0.15 MiB archive, no stripped linked change beyond HELP profile | Low/medium | Applied as size attempt | Current smokes pass; `PROCEDURE ANALYSE()` now reports a stable unsupported-feature diagnostic |
| Link runtime artifacts with lld RELR | 0 archive, 3.88 MiB stripped linked beyond procedure-analyse profile | Medium packaging | Applied as size attempt | Current smokes pass; artifacts require modern glibc `DT_RELR` support |
| Hide legacy durable storage engines | 0.24 MiB archive, 0.03 MiB stripped linked beyond RELR | Medium compatibility | Applied as size attempt | CSV/MRG are omitted; MyISAM stays internal for disk temp tables but user `ENGINE=MyISAM` is rejected |
| Section garbage collection after executable-export removal | +4.21 MiB archive, 0.31 MiB stripped linked beyond legacy engines | Medium packaging | Applied as aggressive linked-size attempt | Runtime gets smaller, but archive consumers pay a metadata cost |
| Remove `JSON_SCHEMA_VALID()` validator | 0.33 MiB archive, 0.04 MiB stripped linked beyond section GC | Medium compatibility | Applied as size attempt | Current smokes pass; ordinary JSON functions remain, but JSON Schema validation is omitted from the minsize profile |
| Remove query cache | 0.07 MiB archive, 0.02 MiB stripped linked beyond JSON schema | Low/medium compatibility | Applied as size attempt | Current smokes pass; query cache reports unavailable and `SELECT SQL_CACHE` executes without caching |
| Remove Oracle compatibility aliases | 0.30 MiB archive, 0.03 MiB stripped linked beyond query cache | High compatibility | Applied as size attempt | Current smokes pass; `SQL_MODE=ORACLE` was already unsupported, and Oracle-only aliases now fail as unknown functions |
| Remove server utility SQL functions | 0.22 MiB archive, 0.04 MiB stripped linked beyond Oracle aliases | Medium compatibility | Applied as size attempt | Current smokes pass; daemon, replication, file-host, lock, benchmark, and delay helpers now fail as unknown functions |
| Remove UCA collations and use `utf8mb4_general_ci` | 1.70 MiB archive, 1.48 MiB stripped linked beyond server utilities | High compatibility | Applied as aggressive size attempt | Current smokes pass, but MariaDB 11.8's default UCA 1400 collation and MySQL 8.0 UCA 0900 aliases are omitted |
| Remove regex SQL surfaces and PCRE2 runtime link | 0.07 MiB archive, 0.01 MiB stripped linked, 0.56 MiB vendored dependency beyond UCA profile | High compatibility | Applied as aggressive size attempt | Current smokes pass; `LIKE` remains, but `REGEXP`, `RLIKE`, and `REGEXP_*()` functions are omitted |
| Remove command-level binlog replay and replication glue | 0.02 MiB archive, no linked-runtime win beyond regex profile | Low/medium | Applied as archive cleanup | Embedded mode already blocks `BINLOG`; the real linked binlog roots remain in transaction, row-event, GTID, and sysvar paths |
| No-op core binlog entry points | 0.14 MiB archive, 0.06 MiB stripped linked beyond command-level binlog removal | Medium | Applied as aggressive size attempt | Current smokes and harness pass; this removed the first transaction, row-event, and GTID-state roots |
| Omit retained binlog event/GTID sources | 0.23 MiB archive, 0.06 MiB stripped linked beyond OpenSSL digest | Medium | Applied as aggressive size attempt | Current smokes and harness pass; `gtid_index.cc`, `log_event.cc`, and `rpl_injector.cc` are omitted, while `log_event_server.cc` remains for generic SQL string rendering |
| Omit external backup stage implementation | 0.01 MiB archive, 0.003 MiB stripped linked beyond optimizer trace | Low/medium embedded compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; server backup coordination and `ddl.log` backup-tool logging do not fit the embedded file-owned runtime |
| Omit `JSON_TABLE` table-function execution | 0.11 MiB archive, 0.01 MiB stripped linked beyond backup stage | High SQL compatibility | Applied as aggressive size attempt | Current smokes and harness pass; relational JSON table extraction is omitted from the minsize profile; later JSON-function work removes ordinary JSON functions too |
| Omit foreign-server metadata cache | 0.03 MiB archive, 0.003 MiB stripped linked beyond JSON_TABLE | Low embedded compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; foreign-server SQL is already unsupported and embedded startup already uses an empty no-`mysql.servers` path |
| Omit proxy protocol network-listener support | 0.008 MiB archive, negligible stripped linked beyond foreign-server cache | Low embedded compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; MyLite has no network listener, and `proxy_protocol_networks` remains visible but empty and disabled |
| Omit full event parser data | 0.01 MiB archive, negligible stripped linked beyond proxy protocol | Low/medium embedded compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; event DDL is already rejected and only parser allocation is needed |
| Omit XA transaction implementation | 0.13 MiB archive, 0.007 MiB stripped linked beyond event parse data | Medium/high SQL compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; XA and two-phase commit are explicit MyLite non-goals and now report embedded-disabled diagnostics |
| Omit trigger sidecar runtime | 0.07 MiB archive, 0.013 MiB stripped linked beyond XA | High SQL compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; trigger DDL is already rejected, and `.TRG`/`.TRN` sidecar loading is replaced by no-trigger table-open behavior |
| Omit view sidecar runtime | 0.04 MiB archive, 0.013 MiB stripped linked beyond trigger runtime | High SQL compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; view DDL is already rejected, and `.frm` view loading is replaced by disabled embedded stubs while retaining derived-table helpers |
| Omit table-admin maintenance commands | 0.03 MiB archive, 0.017 MiB stripped linked beyond view runtime | Medium SQL/admin compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; inherited table repair/check/statistics/key-cache maintenance is server- and engine-shaped, so MyLite should expose storage-native maintenance later |
| Omit persistent engine-independent statistics | 0.15 MiB archive, 0.032 MiB stripped linked beyond table admin | Medium optimizer/statistics compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; MyLite has no storage-native persistent statistics catalog yet, and the stub keeps handler row estimates for ordinary planning |
| Omit generic `SELECT ... PROCEDURE` runtime | 0.12 MiB archive, negligible stripped linked beyond persistent statistics | Low/medium SQL compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; MariaDB exposes only `PROCEDURE ANALYSE()` as a built-in procedure, and that was already unsupported |
| Omit non-`en_US` locale table | 0.28 MiB archive, 0.056 MiB stripped linked beyond SELECT procedure runtime | High SQL compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; `lc_time_names`, `lc_messages`, and explicit SQL locale arguments are limited to `en_US` in the most aggressive profile |
| Omit `LOAD DATA` / `LOAD XML` execution | 0.04 MiB archive, 0.011 MiB stripped linked beyond locale minsize | Medium SQL compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; host-file and client-local bulk import are omitted, while ordinary inserts and prepared binding remain |
| Omit MyISAM check/repair admin code | 0.11 MiB archive, 0.06 MiB stripped linked beyond no-binlog-core | Low/medium | Applied as size attempt | Keeps MyISAM for disk temp tables but removes unreachable admin repair/check paths from the hidden user engine |
| Omit MyISAM full-text code | 0.08 MiB archive, 0.03 MiB stripped linked beyond MyISAM admin | Low/medium | Applied as size attempt | Keeps MyISAM for disk temp tables but removes unreachable full-text paths from the hidden user engine |
| Omit MyISAM RTREE/spatial-key code | 0.04 MiB archive, 0.02 MiB stripped linked beyond MyISAM full-text | Low/medium | Applied as size attempt | Keeps MyISAM for disk temp tables but removes unreachable RTREE paths from the hidden user engine |
| Omit spatial WKB/WKT core | 0.13 MiB archive, 0.03 MiB stripped linked beyond MyISAM RTREE | Medium | Applied as size attempt | Keeps GEOMETRY parsing and rejection but removes unreachable geometry construction/formatting code from the minsize profile |
| Omit SQL sequence engine implementation | 0.21 MiB archive, 0.01 MiB stripped linked beyond spatial core | Medium/high | Applied as size attempt | Removes sequence persistence and plugin code, but parser syntax and generic sequence metadata paths remain |
| Omit GEOMETRY type implementation | 0.35 MiB archive, 0.04 MiB stripped linked beyond SQL sequence | High | Applied as aggressive size attempt | Keeps only minimal generic GEOMETRY metadata symbols; GEOMETRY/SPATIAL DDL still fails without creating a MyLite table |
| Omit general1400 collations and extended casefold tables | 0.23 MiB archive, 0.21 MiB stripped linked beyond GEOMETRY type | High | Applied as aggressive size attempt | Extends the UCA-disabled profile; ordinary `general_ci` remains, but internal non-ASCII case-insensitive comparison can diverge further from MariaDB 11.8 |
| Fold identical linked code with lld ICF | 0 archive, 0.16 MiB stripped linked beyond RPL filter | Medium packaging | Applied as aggressive linked-size attempt | Current smokes and harness pass, but `--icf=all` can make distinct functions share an address |
| Omit VIO TLS transport | 0.02 MiB archive, 0.01 MiB stripped linked, 0.70 MiB vendored dependency beyond ICF | Low/medium embedded compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; network TLS transport is unavailable, which fits the no-network embedded profile |
| Omit legacy `ENCRYPT()` / `libcrypt` | 0.02 MiB archive, 0.003 MiB stripped linked, 0.19 MiB vendored dependency beyond VIO TLS | Medium compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; `ENCRYPT()` is a Unix `crypt()` wrapper with low embedded value |
| Omit zlib compression surfaces / `libz` | 0.07 MiB archive, 0.01 MiB stripped linked, 0.13 MiB vendored dependency beyond libcrypt | Medium compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; zlib SQL functions and compressed columns are omitted while `CRC32()` remains |
| Omit legacy DES functions and key file | 0.04 MiB archive, 0.006 MiB stripped linked beyond dynamic plugin loading | Low/medium embedded compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; embedded DES execution was already disabled, and process-global DES key files do not fit MyLite's file-owned runtime |
| Omit `KDF()` SQL function | 0.03 MiB archive, 0.005 MiB stripped linked beyond DES | Medium compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; this removes direct HKDF/PBKDF2 SQL code but not the retained `libcrypto.so.3` dependency |
| Disable nonessential unwind tables | 0.18 MiB archive, 0.06 MiB stripped linked beyond KDF | Low/medium debugging tradeoff | Applied as aggressive embedded-size attempt | Current smokes and harness pass; C++ exceptions remain enabled, but native stack unwinding metadata is reduced |
| Omit UDF runtime lookup and execution | 0.11 MiB archive, 0.03 MiB stripped linked beyond unwind tables | Medium compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; embedded UDF DDL was already rejected, and the remaining `mysql.func` plus dynamic-library runtime path is server-shaped |
| Omit SQL window functions | 0.58 MiB archive, 0.07 MiB stripped linked beyond UDF runtime | High compatibility | Applied as aggressive size attempt | Current smokes and harness pass; ordinary aggregates remain, but `OVER` and named `WINDOW` analytical SQL are removed from the minsize profile |
| Omit OpenSSL-backed SQL crypto/password functions | 0.13 MiB archive, 0.02 MiB stripped linked beyond window functions | High compatibility | Applied as aggressive size attempt | Current smokes and harness pass; SQL-visible crypto/password helpers are omitted, but retained auth/digest/table roots keep `libcrypto.so.3` |
| Omit server-side encryption hooks | 0.02 MiB archive, 0.008 MiB stripped linked beyond SQL crypto | Low embedded compatibility | Applied as aggressive embedded-size attempt | Current smokes and harness pass; binlog, relay-log, and temp-file encryption need daemon/plugin key management that is outside the current embedded profile |
| Replace OpenSSL digest wrappers | 0.04 MiB archive, +0.001 MiB stripped linked, 4.38 MiB vendored dependency beyond server encryption | Medium packaging | Applied as aggressive embedded-size attempt | Current smokes and harness pass; internal MD5/SHA-1 vectors are preserved, and `libcrypto.so.3` is no longer a runtime dependency |
| Omit MyISAM temp-spill handler | 0.66 MiB archive, 0.23 MiB stripped linked beyond no-binlog-core | High | No, keep opt-in only | Breaks schema-table metadata and catalog smokes; needs a MyLite-owned disk temporary-table replacement or a compatible memory-only schema-table path |
| Remove server-only SQL subsystems | Potentially large | High | Research later | The big bytes are entangled in `libsql_embedded.a`; needs slice-by-slice fork work |
| `DISABLE_PSI_*` switches | 0 in this build | Low | No | No measured effect |
| `-fno-asynchronous-unwind-tables` | 0 in this build | Low | No | Full rebuild produced identical archive and stripped linked sizes |
| LTO | About 1.25 MiB linked, archive much larger | High | No | ODR warnings and huge archives are unacceptable for release |
| Bundle fewer dynamic libraries | Up to 4.85 MiB remaining if currently vendored | Distribution-dependent | Decide per packaging target | Current static archive does not include these libraries; OpenSSL, PCRE2, zlib, and libcrypt have already been removed from the linked runtime dependency set |

## Recommendations

Take these now:

1. Add an explicit release-packaging strip step for copied artifacts.
2. Keep `strip --strip-unneeded` for the minsize static archive while current
   consumer-link smokes pass.
3. Keep the lld RELR profile for aggressive modern-glibc package experiments,
   but re-measure without RELR before choosing a broad binary distribution
   baseline for older Linux targets.
4. Keep section GC for the most aggressive linked-runtime profile while the
   static archive growth remains an explicit tradeoff.
5. Keep the `JSON_SCHEMA_VALID()` omission in the minsize profile unless JSON
   Schema `CHECK` constraints become a compatibility target.
6. Keep the query-cache omission in the minsize profile. It is server-global
   cache state, reports unavailable through MariaDB's `have_query_cache`
   surface, and has low value in the embedded default.
7. Keep the Oracle compatibility alias omission in the aggressive minsize
   profile if MyLite's default does not target Oracle migration workloads.
   It now follows the already-applied Oracle parser removal.
8. Keep the server-utility function omission in the aggressive minsize profile.
   These functions expose daemon, replication, host-file, benchmark, and
   deliberate-delay behavior with low value in MyLite's embedded default.
9. Keep the UCA-collation omission only for the most aggressive size profile
   unless product compatibility explicitly accepts `utf8mb4_general_ci` instead
   of MariaDB 11.8's `utf8mb4_uca1400_ai_ci` default.
10. Keep the regex/PCRE2 omission only for the most aggressive size profile
   unless product compatibility accepts losing `REGEXP`, `RLIKE`, and
   `REGEXP_*()` SQL functions.
11. Keep the command-level binlog/replication source omission as a small archive
   cleanup, but do not treat it as meaningful runtime-size work.
12. Keep the no-binlog-core no-op layer and guarded event/GTID source omission
   in the aggressive minsize profile. Further binlog work should focus on the
   remaining `MYSQL_BIN_LOG` shell and `rpl_binlog_state` globals, not on
   command-level replication.
13. Keep the MyISAM admin omission in the aggressive minsize profile while
   MyISAM remains hidden from user engine selection.
14. Keep the MyISAM full-text omission in the aggressive minsize profile while
   MyLite full-text indexes remain unsupported and MyISAM is internal-only.
15. Keep the MyISAM RTREE omission in the aggressive minsize profile while
   MyLite spatial indexes remain unsupported and MyISAM is internal-only.
16. Keep the spatial-core omission in the aggressive minsize profile while
   MyLite GEOMETRY storage, SPATIAL indexes, and GIS functions remain
   unsupported.
17. Keep the SQL sequence omission in the aggressive minsize profile if MyLite
   does not need MariaDB sequence objects as a compatibility target.
18. Keep the GEOMETRY type implementation omission in the aggressive minsize
   profile while MyLite treats GEOMETRY and SPATIAL DDL as unsupported.
19. Keep the `general1400` collation and extended casefold omission only in the
   aggressive UCA-disabled minsize profile.
20. Keep lld `--icf=all` only in the aggressive linked-runtime profile unless
   release testing proves no address-identity issues in target integrations.
21. Keep VIO TLS omitted in the aggressive embedded profile while MyLite has no
   network listener or remote client login path. This removes `libssl.so.3`
   from the runtime dependency set.
22. Keep the legacy `ENCRYPT()` omission in the aggressive embedded profile.
   It removes `libcrypt.so.1` from the runtime dependency set; retained crypto
   functions still cover current smoke needs.
23. Keep the zlib compression omission in the aggressive embedded profile. It
   removes `libz.so.1` from the runtime dependency set while retaining `CRC32()`
   and rejecting compressed-column DDL explicitly.
24. Keep the legacy DES omission in the aggressive embedded profile. The size
   win is small, but embedded DES execution was already disabled and key-file
   administration is server-shaped process-global state.
25. Keep the `KDF()` omission only in the most aggressive embedded profile.
   The measured win is small and SQL-visible, but it removes uncommon direct
   OpenSSL HKDF/PBKDF2 function code.
26. Keep nonessential unwind tables disabled only in the aggressive minsize
   profile. It is a modest size win with a debugging/profiling tradeoff, and
   exception support remains enabled.
27. Keep the UDF runtime omitted in the aggressive embedded profile. It removes
   process-global `mysql.func` metadata loading and dynamic-library execution;
   embedded UDF DDL is already rejected.
28. Keep SQL window functions omitted only in the most aggressive size profile.
   The savings are meaningful, but this removes real analytical SQL
   compatibility rather than a server-only runtime surface.
29. Keep SQL crypto/password functions omitted only in the most aggressive size
   profile. The savings are modest and SQL-visible, but this is required for
   the later OpenSSL-free minsize profile.
30. Keep server-side encryption hooks omitted in the aggressive embedded
   profile. Binlog, relay-log, and temp-file encryption require server/plugin
   key-management state that does not fit the current file-owned runtime.
31. Keep the OpenSSL-free digest wrapper profile for the aggressive embedded
   target. It removes `libcrypto.so.3` from the linked runtime dependency set
   while preserving retained internal MD5/SHA-1 behavior.
32. Keep the backup-stage omission in the aggressive embedded profile. External
   server backup coordination and backup-tool `ddl.log` side effects do not fit
   the file-owned embedded runtime; MyLite-native backup needs a separate design.
33. Keep `JSON_TABLE` omitted only in the most aggressive size profile. The
   savings are real, but this removes a SQL-visible JSON transformation feature.
   The later JSON-function profile removes the remaining ordinary JSON SQL
   function runtime as an even stronger compatibility tradeoff.
34. Keep the foreign-server metadata cache omitted in the aggressive embedded
   profile while foreign-server metadata remains unsupported.
35. Keep proxy protocol support omitted in the aggressive embedded profile.
   It is a network-listener feature, and the current MyLite runtime has no
   network accept path.
36. Keep full event parser data omitted in the aggressive embedded profile
   while event DDL remains unsupported. The size win is tiny, but it removes
   server event metadata validation that cannot execute in MyLite.
37. Keep XA transactions omitted in the aggressive embedded profile while
   MyLite has no external transaction-manager or two-phase commit recovery
   design. Ordinary local transaction behavior remains covered separately.
38. Keep trigger sidecar loading and execution omitted in the aggressive
   embedded profile while trigger DDL remains unsupported. Real trigger
   support needs a MyLite catalog design rather than `.TRG` and `.TRN`
   sidecars.
39. Keep view sidecar loading and execution omitted in the aggressive embedded
   profile while view DDL remains unsupported. Real view support needs a
   MyLite catalog design rather than `.frm` sidecars, while derived-table and
   CTE column-name helpers remain retained.
40. Keep table-admin maintenance omitted in the aggressive embedded profile.
   MyLite needs storage-native integrity, statistics, repair, and compaction
   semantics before exposing `CHECK`, `REPAIR`, `ANALYZE`, or `OPTIMIZE`
   honestly; key-cache assignment and index preload are MyISAM-shaped.
41. Keep persistent engine-independent statistics omitted in the aggressive
   embedded profile. MyLite needs a storage-native statistics catalog before
   exposing durable `ANALYZE` behavior, while the current stub preserves
   handler row estimates for ordinary optimizer planning.
42. Keep the generic `SELECT ... PROCEDURE` runtime omitted in the aggressive
   embedded profile while `PROCEDURE ANALYSE()` remains unsupported. The linked
   artifact win is tiny, but it removes an extension hook with no retained
   built-in procedure implementation.
43. Keep the non-`en_US` locale table omitted only in the most aggressive size
   profile. The savings are measurable, but this removes real locale
   compatibility for localized date/time names, localized error-message
   selection, and explicit locale arguments.
44. Keep `LOAD DATA` / `LOAD XML` omitted in the aggressive embedded profile
   unless MyLite needs MariaDB-compatible SQL bulk import. The feature reads
   external files and has server/client-local file semantics; ordinary inserts
   and prepared binding remain available.
45. Keep a stripped linked smoke binary size in the build report so regressions
   are visible.
46. Keep hidden default C/C++ visibility in the aggressive minsize profile.
   It is a small but low-risk size win for the current static embedded
   artifacts, and it matches the existing `MYLITE_API` public export boundary.
   Final shared-library packaging still needs an explicit export map decision.
47. Keep EXPLAIN/ANALYZE plan-output runtime omitted only in the most
   aggressive size profile. The savings are real, but it removes an important
   diagnostics surface; ordinary optimizer plan bookkeeping must remain for
   non-EXPLAIN SQL execution.
48. Keep the retained `VECTOR` type handler omitted in the aggressive embedded
   profile while vector functions and vector indexes are already unsupported.
   Re-enable the type only if MyLite gets a real vector storage and index
   compatibility plan.
49. Keep ordinary JSON SQL functions omitted only in the most aggressive size
   profile. The linked savings are meaningful, but JSON is common application
   SQL; the retained internal stub exists only to keep JSON column validation
   linkable until JSON type support gets its own compatibility decision.
50. Keep SQL diagnostics statements omitted only in the most aggressive size
   profile. The archive saving is real but linked-runtime saving is small, and
   `SIGNAL` can be useful outside stored routines. MyLite's public diagnostics
   API keeps the embedded use case covered.
51. Keep system-versioning runtime omitted in the aggressive embedded profile
   while MyLite rejects temporal table metadata. The linked saving is tiny, but
   the archive saving is real and the rejection prevents accidental table
   definitions whose history and period semantics MyLite cannot recover.
52. Keep row-replication conversion utilities omitted in the aggressive
   embedded profile. The win is small because retained field/type vtables and
   RTTI dominate the object, but the omitted behavior is replication-only and
   the stub fails closed.
53. Keep dynamic columns omitted in the aggressive embedded profile. The win is
   larger than the surrounding late-stage feature stubs, and dynamic-column BLOB
   packing is not part of MyLite's single-file storage model. Re-enable only if
   MariaDB dynamic-column SQL/client-helper compatibility becomes a product
   requirement.
54. Keep routine Information Schema scans omitted while stored routine DDL
   remains unsupported. The win is small, but the behavior is coherent:
   `ROUTINES`, `PARAMETERS`, `SHOW PROCEDURE STATUS`, and
   `SHOW FUNCTION STATUS` return empty results instead of rooting a
   `mysql.proc` scan path that cannot find MyLite-owned routine metadata.

Do not take these now:

1. Do not enable LTO for production. The linked binary gets smaller, but the
   archive becomes much larger and the compiler reports ODR-sensitive MariaDB
   type mismatches.
2. Do not enable `MYLITE_DISABLE_MYISAM_TEMP_SPILL` in the default minsize
   profile yet. The size win is real, but MySQL/MariaDB metadata paths still
   need disk temporary tables.
3. Do not remove `SELECT INTO OUTFILE` / `DUMPFILE` by editing ordinary source
   only. The remaining roots come from generated parser actions, so this needs
   a deliberate parser-maintenance slice.
4. Do not spend time on `WITH_EXTRA_CHARSETS=complex`, PSI switches, section GC
   variants, RTTI flags, or exception-disabling compiler flags as standalone
   size work.

Research next if size becomes a release blocker:

1. Investigate whether the remaining `MYSQL_BIN_LOG` shell, `binlog_tp`, and
   `rpl_binlog_state` globals can be replaced by smaller embedded-only
   transaction/logging stubs. `log_event_server.cc` should stay until its
   generic SQL string-rendering helpers are moved cleanly.
2. Investigate whether remaining generic `MYSQL_TYPE_GEOMETRY` metadata
   branches can be removed cleanly. The current profile still keeps minimal
   GEOMETRY handler symbols so retained MariaDB type aggregation and metadata
   paths link.
3. Investigate a generated-parser-owned removal of `SELECT INTO OUTFILE` and
   `SELECT INTO DUMPFILE` if host-file export must also disappear from the
   linked runtime. The current load-data slice proves ordinary source edits do
   not remove the parser-rooted `select_export` / `select_dump` symbols.
4. Longer-term SQL-layer pruning of server-only surfaces. This is likely where
   meaningful multi-MiB savings exist, but it should be done as compatibility
   slices, not as broad dead-code removal.
5. Investigate stored-program parser/runtime removal as a separate high-risk
   slice. The routine Information Schema path is gone, but `sp_head`,
   `sp_instr`, `sp_rcontext`, stored-function item builders, and parser state
   remain coupled to the core SQL layer.
6. Investigate remaining Information Schema and `SHOW` table population paths
   for surfaces that are already unsupported in MyLite, especially
   server/privilege/plugin/process metadata that should not read inherited
   sidecar tables.
7. Separate x86-64 size measurements for lld RELR and ICF before making
   architecture-independent claims.

The best next decisions are deeper SQL-layer reductions as deliberate
compatibility work. Apart from RELR packing and section GC for linked runtime
artifacts, the current data does not support broad compiler/linker tuning as an
effective path.
