# Copyright (c) 2006, 2011, Oracle and/or its affiliates.
# Copyright (c) 2009, 2022, MariaDB Corporation
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1335 USA

ADD_DEFINITIONS(-DMYSQL_SERVER -DEMBEDDED_LIBRARY
 ${SSL_DEFINES})

OPTION(MYLITE_DISABLE_ORACLE_PARSER
  "Disable Oracle SQL-mode parser in the embedded library" OFF)
OPTION(MYLITE_DISABLE_ORACLE_FUNCTIONS
  "Disable Oracle compatibility SQL functions in the embedded library" OFF)
OPTION(MYLITE_DISABLE_GIS_FUNCTIONS
  "Disable GIS SQL functions in the embedded library" OFF)
OPTION(MYLITE_DISABLE_FULLTEXT_MATCH
  "Disable SQL MATCH AGAINST full-text search in the embedded library" OFF)
OPTION(MYLITE_DISABLE_SQL_HANDLER_COMMAND
  "Disable SQL HANDLER commands in the embedded library" OFF)
OPTION(MYLITE_DISABLE_SELECT_OUTFILE
  "Disable SELECT INTO OUTFILE and DUMPFILE in the embedded library" OFF)
OPTION(MYLITE_DISABLE_GEOMETRY_TYPE
  "Disable geometry type implementation code in the embedded library" OFF)
OPTION(MYLITE_DISABLE_SPATIAL_CORE
  "Disable spatial WKB/WKT implementation code in the embedded library" OFF)
OPTION(MYLITE_DISABLE_HELP_COMMAND
  "Disable the HELP SQL command in the embedded library" OFF)
OPTION(MYLITE_DISABLE_PROCESSLIST_METADATA
  "Disable process-list metadata commands in the embedded library" OFF)
OPTION(MYLITE_DISABLE_SHOW_STATIC_INFO
  "Disable static server-info SHOW commands in the embedded library" OFF)
OPTION(MYLITE_DISABLE_STATUS_METADATA
  "Disable SHOW STATUS and Information Schema status metadata in the embedded library" OFF)
OPTION(MYLITE_DISABLE_SYSVAR_HELP_TEXT
  "Disable system-variable help text in the embedded library" OFF)
OPTION(MYLITE_DISABLE_OPTION_HELP_TEXT
  "Disable command-line option help text in the embedded library" OFF)
OPTION(MYLITE_DISABLE_QUERY_LOGS
  "Disable general and slow query logs in the embedded library" OFF)
OPTION(MYLITE_DISABLE_OPTIMIZER_TRACE
  "Disable optimizer trace diagnostics in the embedded library" OFF)
OPTION(MYLITE_DISABLE_BACKUP_STAGE
  "Disable external backup stage support in the embedded library" OFF)
OPTION(MYLITE_DISABLE_BINLOG_CORE
  "Disable binary-log transaction and event core in the embedded library" OFF)
OPTION(MYLITE_DISABLE_TC_LOG_MMAP
  "Disable mmap transaction coordinator log in the embedded library" OFF)
OPTION(MYLITE_DISABLE_BINLOG_REPLICATION
  "Disable user-facing binlog replay and replication glue in the embedded library" OFF)
OPTION(MYLITE_DISABLE_RPL_FILTER
  "Disable replication filter implementation in the embedded library" OFF)
OPTION(MYLITE_DISABLE_RPL_GTID_STATE
  "Disable GTID binlog state implementation in the embedded library" OFF)
OPTION(MYLITE_DISABLE_RPL_UTILITY_SERVER
  "Disable row-replication type-conversion utilities in the embedded library" OFF)
OPTION(MYLITE_DISABLE_CRYPT_FUNCTION
  "Disable the ENCRYPT SQL function in the embedded library" OFF)
OPTION(MYLITE_DISABLE_DES_FUNCTIONS
  "Disable DES SQL functions in the embedded library" OFF)
OPTION(MYLITE_DISABLE_DYNAMIC_COLUMNS
  "Disable dynamic-column SQL and C helper support in the embedded library" OFF)
OPTION(MYLITE_DISABLE_KDF_FUNCTION
  "Disable the KDF SQL function in the embedded library" OFF)
OPTION(MYLITE_DISABLE_UDF_RUNTIME
  "Disable UDF lookup and execution in the embedded library" OFF)
OPTION(MYLITE_DISABLE_SQL_CRYPTO_FUNCTIONS
  "Disable OpenSSL-backed SQL crypto functions in the embedded library" OFF)
OPTION(MYLITE_DISABLE_SERVER_ENCRYPTION
  "Disable server-side encryption hooks in the embedded library" OFF)
OPTION(MYLITE_DISABLE_OPENSSL_DIGESTS
  "Disable OpenSSL-backed digest wrappers in the embedded library" OFF)
OPTION(MYLITE_DISABLE_SQL_DIGEST
  "Disable SQL statement digest normalization in the embedded library" OFF)
OPTION(MYLITE_DISABLE_WINDOW_FUNCTIONS
  "Disable SQL window functions in the embedded library" OFF)
OPTION(MYLITE_DISABLE_ZLIB_COMPRESSION
  "Disable zlib-backed compression in the embedded minsize profile" OFF)
OPTION(MYLITE_DISABLE_JSON_SCHEMA_VALID
  "Disable the JSON_SCHEMA_VALID SQL function in the embedded library" OFF)
OPTION(MYLITE_DISABLE_JSON_TABLE
  "Disable JSON_TABLE table-function execution in the embedded library" OFF)
OPTION(MYLITE_DISABLE_JSON_FUNCTIONS
  "Disable JSON scalar SQL functions in the embedded library" OFF)
OPTION(MYLITE_DISABLE_JSON_TYPE
  "Disable the JSON data type alias in the embedded library" OFF)
OPTION(MYLITE_DISABLE_EXPLAIN_RUNTIME
  "Disable EXPLAIN and ANALYZE runtime output in the embedded library" OFF)
OPTION(MYLITE_DISABLE_FOREIGN_SERVER_CACHE
  "Disable foreign-server metadata cache implementation in the embedded library" OFF)
OPTION(MYLITE_DISABLE_EVENT_PARSE_DATA
  "Disable full event parser data implementation in the embedded library" OFF)
OPTION(MYLITE_DISABLE_TRIGGER_RUNTIME
  "Disable trigger sidecar loading and execution in the embedded library" OFF)
OPTION(MYLITE_DISABLE_VIEW_RUNTIME
  "Disable view sidecar loading and execution in the embedded library" OFF)
OPTION(MYLITE_DISABLE_PROXY_PROTOCOL
  "Disable proxy-protocol network-listener support in the embedded library" OFF)
OPTION(MYLITE_DISABLE_XA_TRANSACTIONS
  "Disable XA transaction support in the embedded library" OFF)
OPTION(MYLITE_DISABLE_PROCEDURE_ANALYSE
  "Disable the PROCEDURE ANALYSE SELECT procedure in the embedded library" OFF)
OPTION(MYLITE_DISABLE_SELECT_PROCEDURE_RUNTIME
  "Disable SELECT PROCEDURE clause runtime in the embedded library" OFF)
OPTION(MYLITE_DISABLE_TABLE_ADMIN
  "Disable table maintenance SQL commands in the embedded library" OFF)
OPTION(MYLITE_DISABLE_PERSISTENT_STATISTICS
  "Disable persistent engine-independent statistics in the embedded library" OFF)
OPTION(MYLITE_DISABLE_QUERY_CACHE
  "Disable the query cache in the embedded library" OFF)
OPTION(MYLITE_DISABLE_REGEX_FUNCTIONS
  "Disable regular expression SQL functions in the embedded library" OFF)
OPTION(MYLITE_DISABLE_ROUTINE_INFORMATION_SCHEMA
  "Disable stored routine Information Schema scans in the embedded library" OFF)
OPTION(MYLITE_DISABLE_SERVER_UTILITY_FUNCTIONS
  "Disable server utility SQL functions in the embedded library" OFF)
OPTION(MYLITE_DISABLE_SQL_DIAGNOSTICS_STATEMENTS
  "Disable SQL SIGNAL, RESIGNAL, and GET DIAGNOSTICS statements in the embedded library" OFF)
OPTION(MYLITE_DISABLE_PLSQL_CURSOR_ATTRIBUTES
  "Disable PL/SQL cursor attribute expressions in the embedded library" OFF)
OPTION(MYLITE_DISABLE_STORED_FUNCTION_LOOKUP
  "Disable stored function lookup for unknown SQL functions in the embedded library" OFF)
OPTION(MYLITE_DISABLE_STORED_PROGRAM_RUNTIME
  "Disable stored program compiler and runtime in the embedded library" OFF)
OPTION(MYLITE_DISABLE_FULL_ERROR_MESSAGES
  "Use compact server error messages in the embedded library" OFF)
OPTION(MYLITE_DISABLE_SYSTEM_VERSIONING
  "Disable system-versioned table runtime in the embedded library" OFF)
OPTION(MYLITE_DISABLE_SQL_SEQUENCE
  "Disable SQL sequence support in the embedded library" OFF)
OPTION(MYLITE_DISABLE_VECTOR_FUNCTIONS
  "Disable vector SQL functions in the embedded library" OFF)
OPTION(MYLITE_DISABLE_VECTOR_TYPE
  "Disable the VECTOR SQL data type in the embedded library" OFF)
OPTION(MYLITE_DISABLE_XML_FUNCTIONS
  "Disable XML SQL functions in the embedded library" OFF)

IF(MYLITE_DISABLE_VECTOR_TYPE AND NOT MYLITE_DISABLE_VECTOR_FUNCTIONS)
  MESSAGE(FATAL_ERROR
    "MYLITE_DISABLE_VECTOR_TYPE requires MYLITE_DISABLE_VECTOR_FUNCTIONS")
ENDIF()

IF(MYLITE_DISABLE_VECTOR_FUNCTIONS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_VECTOR_FUNCTIONS)
ENDIF()
IF(MYLITE_DISABLE_FULLTEXT_MATCH)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_FULLTEXT_MATCH)
ENDIF()
IF(MYLITE_DISABLE_SQL_HANDLER_COMMAND)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_SQL_HANDLER_COMMAND)
ENDIF()
IF(MYLITE_DISABLE_SELECT_OUTFILE)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_SELECT_OUTFILE)
ENDIF()
IF(MYLITE_DISABLE_VECTOR_TYPE)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_VECTOR_TYPE)
ENDIF()
IF(MYLITE_DISABLE_XML_FUNCTIONS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_XML_FUNCTIONS)
ENDIF()
IF(MYLITE_DISABLE_JSON_SCHEMA_VALID)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_JSON_SCHEMA_VALID)
ENDIF()
IF(MYLITE_DISABLE_JSON_TABLE)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_JSON_TABLE)
ENDIF()
IF(MYLITE_DISABLE_JSON_FUNCTIONS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_JSON_FUNCTIONS)
ENDIF()
IF(MYLITE_DISABLE_JSON_TYPE)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_JSON_TYPE)
ENDIF()
IF(MYLITE_DISABLE_SQL_DIGEST)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_SQL_DIGEST)
ENDIF()
IF(MYLITE_DISABLE_SQL_DIAGNOSTICS_STATEMENTS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_SQL_DIAGNOSTICS_STATEMENTS)
ENDIF()
IF(MYLITE_DISABLE_PLSQL_CURSOR_ATTRIBUTES)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_PLSQL_CURSOR_ATTRIBUTES)
ENDIF()
IF(MYLITE_DISABLE_STORED_FUNCTION_LOOKUP)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_STORED_FUNCTION_LOOKUP)
ENDIF()
IF(MYLITE_DISABLE_STORED_PROGRAM_RUNTIME)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_STORED_PROGRAM_RUNTIME)
ENDIF()
IF(MYLITE_DISABLE_FULL_ERROR_MESSAGES)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_FULL_ERROR_MESSAGES)
ENDIF()
IF(MYLITE_DISABLE_SYSTEM_VERSIONING)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_SYSTEM_VERSIONING)
ENDIF()
IF(MYLITE_DISABLE_EXPLAIN_RUNTIME)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_EXPLAIN_RUNTIME)
ENDIF()
IF(MYLITE_DISABLE_FOREIGN_SERVER_CACHE)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_FOREIGN_SERVER_CACHE)
ENDIF()
IF(MYLITE_DISABLE_EVENT_PARSE_DATA)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_EVENT_PARSE_DATA)
ENDIF()
IF(MYLITE_DISABLE_TRIGGER_RUNTIME)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_TRIGGER_RUNTIME)
ENDIF()
IF(MYLITE_DISABLE_VIEW_RUNTIME)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_VIEW_RUNTIME)
ENDIF()
IF(MYLITE_DISABLE_PROXY_PROTOCOL)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_PROXY_PROTOCOL)
ENDIF()
IF(MYLITE_DISABLE_XA_TRANSACTIONS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_XA_TRANSACTIONS)
ENDIF()
IF(MYLITE_DISABLE_OPTIMIZER_TRACE)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_OPTIMIZER_TRACE)
ENDIF()
IF(MYLITE_DISABLE_BACKUP_STAGE)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_BACKUP_STAGE)
ENDIF()
IF(MYLITE_DISABLE_BINLOG_CORE)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_BINLOG_CORE)
ENDIF()
IF(MYLITE_DISABLE_TC_LOG_MMAP)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_TC_LOG_MMAP)
ENDIF()
IF(MYLITE_DISABLE_BINLOG_REPLICATION)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_BINLOG_REPLICATION)
ENDIF()
IF(MYLITE_DISABLE_RPL_FILTER)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_RPL_FILTER)
ENDIF()
IF(MYLITE_DISABLE_RPL_GTID_STATE)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_RPL_GTID_STATE)
ENDIF()
IF(MYLITE_DISABLE_CRYPT_FUNCTION)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_CRYPT_FUNCTION)
ENDIF()
IF(MYLITE_DISABLE_DES_FUNCTIONS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_DES_FUNCTIONS)
ENDIF()
IF(MYLITE_DISABLE_DYNAMIC_COLUMNS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_DYNAMIC_COLUMNS)
ENDIF()
IF(MYLITE_DISABLE_KDF_FUNCTION)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_KDF_FUNCTION)
ENDIF()
IF(MYLITE_DISABLE_UDF_RUNTIME)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_UDF_RUNTIME)
ENDIF()
IF(MYLITE_DISABLE_SQL_CRYPTO_FUNCTIONS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_SQL_CRYPTO_FUNCTIONS)
ENDIF()
IF(MYLITE_DISABLE_SERVER_ENCRYPTION)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_SERVER_ENCRYPTION)
ENDIF()
IF(MYLITE_DISABLE_OPENSSL_DIGESTS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_OPENSSL_DIGESTS)
ENDIF()
IF(MYLITE_DISABLE_WINDOW_FUNCTIONS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_WINDOW_FUNCTIONS)
ENDIF()
IF(MYLITE_DISABLE_SELECT_PROCEDURE_RUNTIME)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_SELECT_PROCEDURE_RUNTIME)
ENDIF()
IF(MYLITE_DISABLE_PROCESSLIST_METADATA)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_PROCESSLIST_METADATA)
ENDIF()
IF(MYLITE_DISABLE_SHOW_STATIC_INFO)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_SHOW_STATIC_INFO)
ENDIF()
IF(MYLITE_DISABLE_STATUS_METADATA)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_STATUS_METADATA)
ENDIF()
IF(MYLITE_DISABLE_SYSVAR_HELP_TEXT)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_SYSVAR_HELP_TEXT)
ENDIF()
IF(MYLITE_DISABLE_OPTION_HELP_TEXT)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_OPTION_HELP_TEXT)
ENDIF()
IF(MYLITE_DISABLE_QUERY_LOGS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_QUERY_LOGS)
ENDIF()
IF(MYLITE_DISABLE_ZLIB_COMPRESSION)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_ZLIB_COMPRESSION)
ENDIF()
IF(MYLITE_DISABLE_TABLE_ADMIN)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_TABLE_ADMIN)
ENDIF()
IF(MYLITE_DISABLE_PERSISTENT_STATISTICS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_PERSISTENT_STATISTICS)
ENDIF()
IF(MYLITE_DISABLE_QUERY_CACHE)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_QUERY_CACHE)
ENDIF()
IF(MYLITE_DISABLE_REGEX_FUNCTIONS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_REGEX_FUNCTIONS)
ENDIF()
IF(MYLITE_DISABLE_ROUTINE_INFORMATION_SCHEMA)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_ROUTINE_INFORMATION_SCHEMA)
ENDIF()
IF(MYLITE_DISABLE_REGEX_FUNCTIONS AND NOT MYLITE_DISABLE_JSON_SCHEMA_VALID)
  MESSAGE(FATAL_ERROR
    "MYLITE_DISABLE_REGEX_FUNCTIONS requires "
    "MYLITE_DISABLE_JSON_SCHEMA_VALID because JSON Schema pattern validation "
    "uses the regular expression engine")
ENDIF()
IF(MYLITE_DISABLE_JSON_FUNCTIONS AND NOT MYLITE_DISABLE_JSON_SCHEMA_VALID)
  MESSAGE(FATAL_ERROR
    "MYLITE_DISABLE_JSON_FUNCTIONS requires MYLITE_DISABLE_JSON_SCHEMA_VALID")
ENDIF()
IF(MYLITE_DISABLE_JSON_FUNCTIONS AND NOT MYLITE_DISABLE_JSON_TABLE)
  MESSAGE(FATAL_ERROR
    "MYLITE_DISABLE_JSON_FUNCTIONS requires MYLITE_DISABLE_JSON_TABLE")
ENDIF()
IF(MYLITE_DISABLE_JSON_TYPE AND NOT MYLITE_DISABLE_JSON_FUNCTIONS)
  MESSAGE(FATAL_ERROR
    "MYLITE_DISABLE_JSON_TYPE requires MYLITE_DISABLE_JSON_FUNCTIONS")
ENDIF()
IF(MYLITE_DISABLE_SERVER_UTILITY_FUNCTIONS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_SERVER_UTILITY_FUNCTIONS)
ENDIF()
IF(MYLITE_DISABLE_SQL_SEQUENCE)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_SQL_SEQUENCE)
ENDIF()
IF(MYLITE_DISABLE_ORACLE_FUNCTIONS)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_ORACLE_FUNCTIONS)
ENDIF()
IF(MYLITE_DISABLE_GEOMETRY_TYPE)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_GEOMETRY_TYPE)
ENDIF()
IF(MYLITE_DISABLE_SPATIAL_CORE)
  ADD_DEFINITIONS(-DMYLITE_DISABLE_SPATIAL_CORE)
ENDIF()
IF(MYLITE_DISABLE_GEOMETRY_TYPE AND NOT MYLITE_DISABLE_GIS_FUNCTIONS)
  MESSAGE(FATAL_ERROR
    "MYLITE_DISABLE_GEOMETRY_TYPE requires MYLITE_DISABLE_GIS_FUNCTIONS")
ENDIF()
IF(MYLITE_DISABLE_GEOMETRY_TYPE AND NOT MYLITE_DISABLE_SPATIAL_CORE)
  MESSAGE(FATAL_ERROR
    "MYLITE_DISABLE_GEOMETRY_TYPE requires MYLITE_DISABLE_SPATIAL_CORE")
ENDIF()
IF(MYLITE_DISABLE_SPATIAL_CORE AND NOT MYLITE_DISABLE_GIS_FUNCTIONS)
  MESSAGE(FATAL_ERROR
    "MYLITE_DISABLE_SPATIAL_CORE requires MYLITE_DISABLE_GIS_FUNCTIONS")
ENDIF()
IF(MYLITE_DISABLE_RPL_FILTER AND NOT MYLITE_DISABLE_BINLOG_CORE)
  MESSAGE(FATAL_ERROR
    "MYLITE_DISABLE_RPL_FILTER requires MYLITE_DISABLE_BINLOG_CORE")
ENDIF()
IF(MYLITE_DISABLE_RPL_GTID_STATE AND NOT MYLITE_DISABLE_BINLOG_CORE)
  MESSAGE(FATAL_ERROR
    "MYLITE_DISABLE_RPL_GTID_STATE requires MYLITE_DISABLE_BINLOG_CORE")
ENDIF()
IF(MYLITE_DISABLE_RPL_UTILITY_SERVER AND NOT MYLITE_DISABLE_BINLOG_REPLICATION)
  MESSAGE(FATAL_ERROR
    "MYLITE_DISABLE_RPL_UTILITY_SERVER requires "
    "MYLITE_DISABLE_BINLOG_REPLICATION")
ENDIF()
IF(MYLITE_DISABLE_TC_LOG_MMAP AND NOT MYLITE_DISABLE_BINLOG_CORE)
  MESSAGE(FATAL_ERROR
    "MYLITE_DISABLE_TC_LOG_MMAP requires MYLITE_DISABLE_BINLOG_CORE")
ENDIF()

INCLUDE_DIRECTORIES(
${CMAKE_SOURCE_DIR}/include 
${CMAKE_SOURCE_DIR}/libmysqld
${CMAKE_SOURCE_DIR}/sql
${CMAKE_BINARY_DIR}/sql 
${PCRE_INCLUDE_DIRS}
${LIBFMT_INCLUDE_DIR}
${ZLIB_INCLUDE_DIRS}
${SSL_INCLUDE_DIRS}
${SSL_INTERNAL_INCLUDE_DIRS}
)

SET(GEN_SOURCES
${CMAKE_BINARY_DIR}/sql/sql_yacc.hh
${CMAKE_BINARY_DIR}/sql/yy_mariadb.cc
${CMAKE_BINARY_DIR}/sql/lex_hash.h
)
IF(NOT MYLITE_DISABLE_ORACLE_PARSER)
  LIST(APPEND GEN_SOURCES
    ${CMAKE_BINARY_DIR}/sql/yy_oracle.hh
    ${CMAKE_BINARY_DIR}/sql/yy_oracle.cc
  )
ENDIF()

SET_SOURCE_FILES_PROPERTIES(${GEN_SOURCES} PROPERTIES GENERATED TRUE)

IF(CMAKE_C_COMPILER_ID MATCHES "Clang" AND
   NOT CMAKE_C_COMPILER_VERSION VERSION_LESS "13.0.0")
  SET(PARSER_WARNING_SOURCES ${CMAKE_BINARY_DIR}/sql/yy_mariadb.cc)
  IF(NOT MYLITE_DISABLE_ORACLE_PARSER)
    LIST(APPEND PARSER_WARNING_SOURCES ${CMAKE_BINARY_DIR}/sql/yy_oracle.cc)
  ENDIF()
  ADD_COMPILE_FLAGS(${PARSER_WARNING_SOURCES}
    COMPILE_FLAGS "-Wno-unused-but-set-variable")
ENDIF()

SET(SQL_EMBEDDED_SOURCES emb_qcache.cc libmysqld.c lib_sql.cc 
           libmysql.c ../sql-common/errmsg.c
           ../sql-common/client.c
           ../sql/cset_narrowing.cc
           ../sql-common/my_user.c ../sql-common/pack.c
           ../sql-common/client_plugin.c
           ../sql/password.c ../sql/discover.cc ../sql/derror.cc 
           ../sql/field.cc ../sql/field_conv.cc ../sql/field_comp.cc
           ../sql/filesort_utils.cc ../sql/sql_digest.cc
           ../sql/filesort.cc ../sql/grant.cc
           ../sql/gstream.cc ../sql/slave.cc
           ../sql/signal_handler.cc
           ../sql/handler.cc ../sql/hash_filo.cc ../sql/hostname.cc 
           ../sql/init.cc ../sql/item_buff.cc ../sql/item_cmpfunc.cc 
           ../sql/item.cc ../sql/item_create.cc ../sql/item_func.cc 
           ../sql/item_geofunc.cc ../sql/item_row.cc ../sql/item_strfunc.cc 
           ../sql/item_subselect.cc ../sql/item_sum.cc ../sql/item_timefunc.cc 
           ../sql/item_xmlfunc.cc ../sql/item_jsonfunc.cc
           ../sql/json_schema.cc ../sql/json_schema_helper.cc
           ../sql/key.cc ../sql/lock.cc ../sql/log.cc ../sql/log_cache.cc
           ../sql/log_event.cc ../sql/log_event_server.cc 
           ../sql/mf_iocache.cc ../sql/my_decimal.cc 
           ../sql/net_serv.cc ../sql/opt_range.cc
           ../sql/opt_group_by_cardinality.cc
           ../sql/opt_rewrite_date_cmp.cc
           ../sql/opt_rewrite_remove_casefold.cc
           ../sql/opt_sargable_left.cc
           ../sql/opt_sum.cc
           ../sql/opt_vcol_substitution.cc
           ../sql/parse_file.cc ../sql/procedure.cc ../sql/protocol.cc 
           ../sql/records.cc ../sql/repl_failsafe.cc ../sql/rpl_filter.cc
           ../sql/rpl_record.cc ../sql/des_key_file.cc
           ../sql/rpl_injector.cc ../sql/set_var.cc ../sql/spatial.cc 
           ../sql/sp_cache.cc ../sql/sp.cc ../sql/sp_head.cc 
           ../sql/sp_pcontext.cc ../sql/sp_rcontext.cc ../sql/sql_acl.cc 
           ../sql/sql_analyse.cc ../sql/sql_base.cc ../sql/sql_cache.cc 
           ../sql/sql_class.cc ../sql/sql_crypt.cc ../sql/sql_cursor.cc 
           ../sql/sql_db.cc ../sql/sql_delete.cc ../sql/sql_derived.cc 
           ../sql/sql_do.cc ../sql/sql_error.cc ../sql/sql_handler.cc
            ../sql/sql_get_diagnostics.cc
           ../sql/sql_help.cc ../sql/sql_insert.cc ../sql/datadict.cc
           ../sql/sql_admin.cc ../sql/sql_truncate.cc ../sql/sql_reload.cc
           ../sql/sql_lex.cc ../sql/keycaches.cc
           ../sql/sql_list.cc ../sql/sql_load.cc ../sql/sql_locale.cc 
           ../sql/sql_binlog.cc ../sql/sql_manager.cc
           ../sql/sql_parse.cc ../sql/sql_bootstrap.cc
           ../sql/sql_partition.cc ../sql/sql_plugin.cc 
           ../sql/debug_sync.cc ../sql/debug.cc
           ../sql/opt_table_elimination.cc
           ../sql/sql_prepare.cc ../sql/sql_rename.cc ../sql/sql_repl.cc 
           ../sql/sql_select.cc ../sql/sql_servers.cc
           ../sql/group_by_handler.cc ../sql/derived_handler.cc
           ../sql/select_handler.cc
           ../sql/sql_show.cc ../sql/sql_state.c 
           ../sql/sql_statistics.cc ../sql/sql_string.cc
           ../sql/sql_table.cc ../sql/sql_test.cc
           ../sql/ddl_log.cc ../sql/item_vectorfunc.cc
           ../sql/sql_trigger.cc ../sql/sql_udf.cc ../sql/sql_union.cc
           ../sql/sql_update.cc ../sql/sql_view.cc ../sql/sql_profile.cc
           ../sql/gcalc_tools.cc ../sql/gcalc_slicescan.cc
           ../sql/strfunc.cc ../sql/table.cc ../sql/thr_malloc.cc
           ../sql/sql_time.cc ../sql/tztime.cc ../sql/uniques.cc ../sql/unireg.cc
           ../sql/partition_info.cc ../sql/sql_connect.cc 
           ../sql/scheduler.cc ../sql/sql_audit.cc
           ../sql/sql_alter.cc ../sql/sql_partition_admin.cc
           ../sql/event_parse_data.cc
           ../sql/sql_signal.cc
           ../sql/sys_vars.cc ../sql/vector_mhnsw.cc
           ${CMAKE_BINARY_DIR}/sql/sql_builtin.cc
           ../sql/mdl.cc ../sql/transaction.cc
           ../sql/sql_join_cache.cc
           ../sql/multi_range_read.cc
           ../sql/opt_index_cond_pushdown.cc
           ../sql/opt_subselect.cc
           ../sql/create_options.cc
           ../sql/rpl_utility.cc
           ../sql/rpl_utility_server.cc
           ../sql/rpl_reporting.cc
           ../sql/sql_expression_cache.cc
           ../sql/my_apc.cc ../sql/my_apc.h
           ../sql/my_json_writer.cc ../sql/my_json_writer.h
	   ../sql/rpl_gtid.cc ../sql/gtid_index.cc
           ../sql/sql_explain.cc ../sql/sql_explain.h
           ../sql/sql_analyze_stmt.cc ../sql/sql_analyze_stmt.h
           ../sql/compat56.cc
           ../sql/sql_schema.cc
           ../sql/lex_charset.cc ../sql/charset_collations.cc
           ../sql/sql_type.cc ../sql/sql_type.h
           ../sql/sql_mode.cc
           ../sql/sql_type_string.cc
           ../sql/sql_type_json.cc
           ../sql/sql_type_geom.cc ../sql/sql_type_vector.cc
           ../sql/table_cache.cc ../sql/mf_iocache_encr.cc
           ../sql/wsrep_dummy.cc ../sql/encryption.cc
           ../sql/item_windowfunc.cc ../sql/sql_window.cc
           ../sql/sql_cte.cc
           ../sql/sql_sequence.cc ../sql/sql_sequence.h
           ../sql/ha_sequence.cc ../sql/ha_sequence.h
           ../sql/temporary_tables.cc
           ../sql/proxy_protocol.cc ../sql/backup.cc
           ../sql/sql_tvc.cc ../sql/sql_tvc.h
           ../sql/opt_split.cc
           ../sql/rowid_filter.cc ../sql/rowid_filter.h
           ../sql/item_vers.cc
           ../sql/opt_trace.cc
           ../sql/xa.cc
           ../sql/json_table.cc
           ../sql/opt_histogram_json.cc
           ../sql/sp_instr.cc
           ${GEN_SOURCES}
           ${MYSYS_LIBWRAP_SOURCE}
)

IF(MYLITE_DISABLE_ORACLE_PARSER)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_oracle_parser_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_GIS_FUNCTIONS)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES
    ../sql/item_geofunc.cc
    ../sql/gcalc_tools.cc
    ../sql/gcalc_slicescan.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_gis_function_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_SPATIAL_CORE)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/spatial.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_spatial_core_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_GEOMETRY_TYPE)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_type_geom.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_geometry_type_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_SQL_SEQUENCE)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES
    ../sql/sql_sequence.cc
    ../sql/sql_sequence.h
    ../sql/ha_sequence.cc
    ../sql/ha_sequence.h)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_sql_sequence_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_STORED_PROGRAM_RUNTIME)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES
    ../sql/sp.cc
    ../sql/sp_cache.cc
    ../sql/sp_head.cc
    ../sql/sp_pcontext.cc
    ../sql/sp_rcontext.cc
    ../sql/sp_instr.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_stored_program_runtime_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_HELP_COMMAND)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_help.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_help_command_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_SQL_HANDLER_COMMAND)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_handler.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_sql_handler_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_OPTIMIZER_TRACE)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/opt_trace.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_optimizer_trace_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_BACKUP_STAGE)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/backup.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_backup_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_JSON_SCHEMA_VALID)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/json_schema.cc)
ENDIF()
IF(MYLITE_DISABLE_JSON_TABLE)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/json_table.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_json_table_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_JSON_FUNCTIONS)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES
    ../sql/item_jsonfunc.cc
    ../sql/json_schema_helper.cc)
  IF(MYLITE_DISABLE_JSON_TYPE)
    LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_type_json.cc)
    LIST(APPEND SQL_EMBEDDED_SOURCES mylite_json_type_stub.cc)
  ELSE()
    LIST(APPEND SQL_EMBEDDED_SOURCES mylite_json_function_stub.cc)
  ENDIF()
ENDIF()
IF(MYLITE_DISABLE_SQL_DIGEST)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_digest.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_sql_digest_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_SQL_DIAGNOSTICS_STATEMENTS)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES
    ../sql/sql_get_diagnostics.cc
    ../sql/sql_signal.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_sql_diagnostics_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_SYSTEM_VERSIONING)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/item_vers.cc)
ENDIF()
IF(MYLITE_DISABLE_EXPLAIN_RUNTIME)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_explain.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_explain_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_FOREIGN_SERVER_CACHE)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_servers.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_foreign_server_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_EVENT_PARSE_DATA)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/event_parse_data.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_event_parse_data_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_TRIGGER_RUNTIME)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_trigger.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_trigger_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_VIEW_RUNTIME)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_view.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_view_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_PROXY_PROTOCOL)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/proxy_protocol.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_proxy_protocol_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_XA_TRANSACTIONS)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/xa.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_xa_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_BINLOG_REPLICATION)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES
    ../sql/repl_failsafe.cc
    ../sql/rpl_reporting.cc
    ../sql/slave.cc
    ../sql/sql_binlog.cc
    ../sql/sql_repl.cc)
ENDIF()
IF(MYLITE_DISABLE_BINLOG_CORE)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES
    ../sql/gtid_index.cc
    ../sql/log_event.cc
    ../sql/log_event_server.cc
    ../sql/rpl_injector.cc
    ../sql/rpl_record.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_log_event_core_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_RPL_GTID_STATE)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/rpl_gtid.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_rpl_gtid_state_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_RPL_UTILITY_SERVER)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/rpl_utility_server.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_rpl_utility_server_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_RPL_FILTER)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/rpl_filter.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_rpl_filter_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_DES_FUNCTIONS)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/des_key_file.cc)
ENDIF()
IF(MYLITE_DISABLE_UDF_RUNTIME)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_udf.cc)
ENDIF()
IF(MYLITE_DISABLE_SERVER_ENCRYPTION)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES
    ../sql/encryption.cc
    ../sql/mf_iocache_encr.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_server_encryption_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_WINDOW_FUNCTIONS)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES
    ../sql/item_windowfunc.cc
    ../sql/sql_window.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_window_function_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_PROCEDURE_ANALYSE)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_analyse.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_procedure_analyse_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_SELECT_PROCEDURE_RUNTIME)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES
    ../sql/procedure.cc
    mylite_procedure_analyse_stub.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_select_procedure_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_TABLE_ADMIN)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_admin.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_table_admin_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_PERSISTENT_STATISTICS)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES
    ../sql/sql_statistics.cc
    ../sql/opt_histogram_json.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_persistent_statistics_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_QUERY_CACHE)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES emb_qcache.cc ../sql/sql_cache.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_query_cache_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_VECTOR_FUNCTIONS)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES
    ../sql/item_vectorfunc.cc
    ../sql/vector_mhnsw.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_vector_function_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_VECTOR_TYPE)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_type_vector.cc)
ENDIF()
IF(MYLITE_DISABLE_EXTRA_LOCALES)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_locale.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_locale_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_LOAD_DATA)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/sql_load.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_load_data_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_TIME_ZONE_TABLES)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/tztime.cc)
  LIST(APPEND SQL_EMBEDDED_SOURCES mylite_tztime_stub.cc)
ENDIF()
IF(MYLITE_DISABLE_XML_FUNCTIONS)
  LIST(REMOVE_ITEM SQL_EMBEDDED_SOURCES ../sql/item_xmlfunc.cc)
ENDIF()


ADD_CONVENIENCE_LIBRARY(sql_embedded ${SQL_EMBEDDED_SOURCES})
DTRACE_INSTRUMENT(sql_embedded)
ADD_DEPENDENCIES(sql_embedded GenError GenServerSource)
IF(TARGET pcre2)
  IF(NOT MYLITE_DISABLE_REGEX_FUNCTIONS)
    ADD_DEPENDENCIES(sql_embedded pcre2)
  ENDIF()
ENDIF()
IF(TARGET libfmt)
  ADD_DEPENDENCIES(sql_embedded libfmt)
ENDIF()
TARGET_LINK_LIBRARIES(sql_embedded LINK_PRIVATE tpool ${CRC32_LIBRARY})

# On Windows, static embedded server library is called mysqlserver.lib
# On Unix, it is libmysqld.a
IF(WIN32)
  SET(MYSQLSERVER_OUTPUT_NAME mysqlserver)
  SET(COMPONENT_MYSQLSERVER "Embedded")
  SET(COMPONENT_LIBMYSQLD "Embedded")
ELSE()
  SET(MYSQLSERVER_OUTPUT_NAME mariadbd)
  SET(COMPONENT_MYSQLSERVER "Development")
  SET(COMPONENT_LIBMYSQLD "Server")
ENDIF()


SET(LIBS 
  dbug strings mysys mysys_ssl vio
  ${LIBWRAP} ${CMAKE_DL_LIBS}
  ${EMBEDDED_PLUGIN_LIBS}
  sql_embedded
)
IF(NOT MYLITE_DISABLE_ZLIB_COMPRESSION)
  LIST(APPEND LIBS ${ZLIB_LIBRARIES})
ENDIF()
IF(NOT MYLITE_DISABLE_CRYPT_FUNCTION)
  LIST(APPEND LIBS ${LIBCRYPT})
ENDIF()
IF(NOT MYLITE_DISABLE_OPENSSL_DIGESTS)
  IF(MYLITE_DISABLE_VIO_SSL AND OPENSSL_CRYPTO_LIBRARY)
    LIST(APPEND LIBS ${OPENSSL_CRYPTO_LIBRARY})
  ELSE()
    LIST(APPEND LIBS ${SSL_LIBRARIES})
  ENDIF()
ENDIF()
IF(NOT MYLITE_DISABLE_REGEX_FUNCTIONS)
  LIST(APPEND LIBS pcre2-8)
ENDIF()

# Some storage engine were compiled for embedded specifically
# (with corresponding target ${engine}_embedded)
SET(EMBEDDED_LIBS)
FOREACH(LIB ${LIBS})
  IF(TARGET ${LIB}_embedded)
    LIST(APPEND EMBEDDED_LIBS ${LIB}_embedded)
  ELSE()
    LIST(APPEND EMBEDDED_LIBS ${LIB})
  ENDIF()
ENDFOREACH()

MERGE_LIBRARIES(mysqlserver STATIC ${EMBEDDED_LIBS} 
  OUTPUT_NAME ${MYSQLSERVER_OUTPUT_NAME} COMPONENT ${COMPONENT_MYSQLSERVER})
IF(UNIX)
  INSTALL_SYMLINK(libmysqld.a mysqlserver ${INSTALL_LIBDIR} ${COMPONENT_MYSQLSERVER})
ENDIF()
INSTALL(FILES embedded_priv.h DESTINATION ${INSTALL_INCLUDEDIR}/server/private COMPONENT  ${COMPONENT_MYSQLSERVER})


SET(CLIENT_API_FUNCTIONS_5_1
get_tty_password
mysql_thread_end
mysql_thread_init
myodbc_remove_escape
mysql_affected_rows
mysql_autocommit
mysql_stmt_bind_param
mysql_stmt_bind_result
mysql_change_user
mysql_character_set_name
mysql_close
mysql_commit
mysql_data_seek
mysql_debug
mysql_dump_debug_info
mysql_eof
mysql_errno
mysql_error
mysql_escape_string
mysql_hex_string
mysql_stmt_execute
mysql_stmt_fetch
mysql_stmt_fetch_column
mysql_fetch_field
mysql_fetch_field_direct
mysql_fetch_fields
mysql_fetch_lengths
mysql_fetch_row
mysql_field_count
mysql_field_seek
mysql_field_tell
mysql_free_result
mysql_get_parameters
mysql_get_client_info
mysql_get_host_info
mysql_get_proto_info
mysql_get_server_info
mysql_get_client_version
mysql_get_ssl_cipher
mysql_info
mysql_init
mysql_insert_id
mysql_kill
mysql_set_server_option
mysql_list_dbs
mysql_list_fields
mysql_list_processes
mysql_list_tables
mysql_more_results
mysql_next_result
mysql_num_fields
mysql_num_rows
mysql_options
mysql_stmt_param_count
mysql_stmt_param_metadata
mysql_ping
mysql_stmt_result_metadata
mysql_query
mysql_read_query_result
mysql_real_connect
mysql_real_escape_string
mysql_real_query
mysql_refresh
mysql_rollback
mysql_row_seek
mysql_row_tell
mysql_select_db
mysql_stmt_send_long_data
mysql_send_query
mysql_shutdown
mysql_ssl_set
mysql_stat
mysql_stmt_affected_rows
mysql_stmt_close
mysql_stmt_reset
mysql_stmt_data_seek
mysql_stmt_errno
mysql_stmt_error
mysql_stmt_free_result
mysql_stmt_num_rows
mysql_stmt_row_seek
mysql_stmt_row_tell
mysql_stmt_store_result
mysql_store_result
mysql_thread_id
mysql_thread_safe
mysql_use_result
mysql_warning_count
mysql_stmt_sqlstate
mysql_sqlstate
mysql_get_server_version
mysql_stmt_prepare
mysql_stmt_init
mysql_stmt_insert_id
mysql_stmt_attr_get
mysql_stmt_attr_set
mysql_stmt_field_count
mysql_set_local_infile_default
mysql_set_local_infile_handler
mysql_embedded
mysql_server_init
mysql_server_end
mysql_set_character_set
mysql_get_character_set_info
# These are documented in Paul DuBois' MySQL book,
# so we treat them as part of the de-facto API.
handle_options
load_defaults
free_defaults
my_print_help
)

SET(CLIENT_API_FUNCTIONS_5_5
my_progname
mysql_stmt_next_result
# Charsets
my_charset_bin
my_charset_latin1
my_charset_utf8mb3_general_ci
# Client plugins
mysql_client_find_plugin
mysql_client_register_plugin
mysql_load_plugin
mysql_load_plugin_v
mysql_plugin_options

#dynamic columns api
dynamic_column_create
dynamic_column_create_many
dynamic_column_update
dynamic_column_update_many
dynamic_column_exists
dynamic_column_list
dynamic_column_get
dynamic_column_prepare_decimal
mariadb_dyncol_create_many_num
mariadb_dyncol_create_many_named
mariadb_dyncol_update_many_num
mariadb_dyncol_update_many_named
mariadb_dyncol_exists_num
mariadb_dyncol_exists_named
mariadb_dyncol_free
mariadb_dyncol_list_num
mariadb_dyncol_list_named
mariadb_dyncol_get_num
mariadb_dyncol_get_named
mariadb_dyncol_has_names
mariadb_dyncol_check
mariadb_dyncol_json
mariadb_dyncol_val_str
mariadb_dyncol_val_long
mariadb_dyncol_val_double
mariadb_dyncol_unpack
mariadb_dyncol_unpack_free
mariadb_dyncol_column_cmp_named
mariadb_dyncol_column_count
mariadb_dyncol_prepare_decimal
#
mariadb_deinitialize_ssl
# low-level API to MySQL protocol
mysql_net_read_packet
mysql_net_field_length
# Added in MariaDB-10.0 to stay compatible with MySQL-5.6, yuck!
mysql_options4
)


SET(CLIENT_API_FUNCTIONS_10_5
mariadb_field_attr
)

SET(CLIENT_API_FUNCTIONS
  ${CLIENT_API_FUNCTIONS_5_1}
  ${CLIENT_API_FUNCTIONS_5_5}
  ${CLIENT_API_FUNCTIONS_10_5}
)


# List of exported functions in embedded (client api except client plugin or 
# async (*_start/*_cont functions)

SET(EMBEDDED_API)

FOREACH(f ${CLIENT_API_FUNCTIONS})
  IF(f MATCHES "plugin|_start$|_cont$")
    # Ignore functions, embedded does not export them
  ELSE()
    SET(EMBEDDED_API ${EMBEDDED_API} ${f})
  ENDIF()	
ENDFOREACH()

IF(NOT DISABLE_SHARED)
  MERGE_LIBRARIES(libmysqld SHARED mysqlserver EXPORTS ${EMBEDDED_API}
    COMPONENT  ${COMPONENT_LIBMYSQLD})
  IF(UNIX)
    # Name the shared library, handle versioning (provides same api as client
    # library hence the same version)
    SET_TARGET_PROPERTIES(libmysqld PROPERTIES 
      OUTPUT_NAME mariadbd
      SOVERSION "${SHARED_LIB_MAJOR_VERSION}")
    INSTALL_SYMLINK(libmysqld.so libmysqld ${INSTALL_LIBDIR} ${COMPONENT_LIBMYSQLD})
    # Clean direct output flags, as 2 targets have the same base name
    # libmysqld
    SET_TARGET_PROPERTIES(libmysqld PROPERTIES CLEAN_DIRECT_OUTPUT 1)
    SET_TARGET_PROPERTIES(mysqlserver PROPERTIES CLEAN_DIRECT_OUTPUT 1)
    TARGET_LINK_LIBRARIES(mysqlserver LINK_PRIVATE tpool ${CRC32_LIBRARY})
    IF(LIBMYSQLD_SO_EXTRA_LIBS)
       TARGET_LINK_LIBRARIES(libmysqld LINK_PRIVATE ${LIBMYSQLD_SO_EXTRA_LIBS})
    ENDIF()
  ENDIF()
ENDIF()
