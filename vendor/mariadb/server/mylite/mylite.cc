/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mylite.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <new>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <mysql.h>
#include <mysqld_error.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifndef MYLITE_DEFAULT_LC_MESSAGES_DIR
#define MYLITE_DEFAULT_LC_MESSAGES_DIR ""
#endif

struct mylite_db
{
  MYSQL *mysql= nullptr;
  std::string filename;
  std::string runtime_dir;
  int errcode= MYLITE_OK;
  int extended_errcode= MYLITE_OK;
  unsigned mariadb_errno= 0;
  std::string sqlstate= "00000";
  std::string errmsg= "not an error";
  std::string warning_message;
  bool runtime_ref= false;
  unsigned open_statements= 0;
};

enum PreparedParameterKind
{
  PARAMETER_UNBOUND,
  PARAMETER_NULL,
  PARAMETER_INT64,
  PARAMETER_UINT64,
  PARAMETER_DOUBLE,
  PARAMETER_TEXT,
  PARAMETER_BLOB
};

struct PreparedParameter
{
  PreparedParameterKind kind= PARAMETER_UNBOUND;
  my_bool is_null= 0;
  long long int64_value= 0;
  unsigned long long uint64_value= 0;
  double double_value= 0.0;
  std::vector<char> bytes;
  unsigned long length= 0;
};

struct MysqlEffectSnapshot
{
  my_ulonglong affected_rows= 0;
  my_ulonglong insert_id= 0;
  unsigned field_count= 0;
  unsigned server_status= 0;
  unsigned warning_count= 0;
};

struct mylite_stmt
{
  mylite_db *db= nullptr;
  MYSQL_STMT *stmt= nullptr;
  MYSQL_RES *metadata= nullptr;
  std::vector<std::string> column_names;
  std::vector<int> column_types;
  std::vector<PreparedParameter> parameters;
  std::vector<MYSQL_BIND> param_binds;
  std::vector<MYSQL_BIND> result_binds;
  std::vector<std::vector<char>> column_buffers;
  std::vector<unsigned long> column_lengths;
  std::vector<my_bool> column_is_null;
  std::vector<my_bool> column_errors;
  bool executed= false;
  bool result_bound= false;
  bool has_current_row= false;
  bool done= false;
};

namespace {

struct RuntimeState
{
  bool started= false;
  bool readonly= false;
  unsigned refs= 0;
  std::string filename;
  std::string runtime_dir;
  std::vector<std::string> server_args;
  std::vector<char *> server_argv;
};

std::mutex runtime_mutex;
RuntimeState runtime;
bool runtime_shutdown_registered= false;

int set_error(mylite_db *db, int code, unsigned mariadb_errno,
              const char *sqlstate, const std::string &message);
void clear_error(mylite_db *db);
bool validate_open_inputs(const char *filename, unsigned flags,
                          const char *profile, mylite_db *db);
bool make_absolute_path(const char *filename, std::string *path,
                        std::string *message);
bool prepare_primary_file(const std::string &path, unsigned flags,
                          std::string *message);
bool ensure_runtime_directories(const std::string &runtime_dir,
                                std::string *message);
bool ensure_directory(const std::string &path, std::string *message);
std::string parent_path(const std::string &path);
bool is_directory(const std::string &path);
bool start_runtime(const std::string &filename,
                   const std::string &runtime_dir,
                   bool readonly,
                   std::string *message);
std::vector<std::string> build_server_args(const std::string &runtime_dir);
const char *default_lc_messages_dir();
void rebuild_server_argv();
void stop_runtime();
void stop_runtime_at_exit();
int connect_handle(mylite_db *db);
int execute_result_callback(mylite_db *db, MYSQL_RES *result,
                            mylite_exec_callback callback, void *ctx);
int execute_prepared_statement(mylite_stmt *stmt);
int bind_prepared_parameters(mylite_stmt *stmt);
int bind_prepared_result(mylite_stmt *stmt);
int fetch_prepared_row(mylite_stmt *stmt);
int fetch_truncated_columns(mylite_stmt *stmt);
int bind_prepared_bytes(mylite_stmt *stmt, unsigned index,
                        PreparedParameterKind kind, const void *value,
                        size_t value_len, void (*destructor)(void *));
int get_bind_parameter(mylite_stmt *stmt, unsigned index,
                       PreparedParameter **param);
bool custom_bind_destructor(void (*destructor)(void *));
void reset_prepared_result_state(mylite_stmt *stmt);
void free_statement_metadata(mylite_stmt *stmt);
int set_error_from_stmt(mylite_stmt *stmt);
int set_error_from_mysql_stmt(mylite_db *db, MYSQL_STMT *stmt);
int set_error_from_mysql(mylite_db *db);
int return_error_with_message(mylite_db *db, char **errmsg);
int return_standalone_error(int code, const char *message, char **errmsg);
char *duplicate_message(const std::string &message);
int classify_mariadb_error(unsigned error, const char *sqlstate,
                           const char *message);
MysqlEffectSnapshot snapshot_mysql_effects(MYSQL *mysql);
void restore_mysql_effects(MYSQL *mysql, const MysqlEffectSnapshot &snapshot);
unsigned map_warning_level_name(const char *level, size_t length);
bool valid_column(const mylite_stmt *stmt, unsigned column);
int map_column_type(const MYSQL_FIELD &field);
enum_field_types bind_buffer_type(int column_type);

} // namespace

extern "C" int mylite_open(const char *filename, mylite_db **out_db)
{
  return mylite_open_v2(filename, out_db,
                        MYLITE_OPEN_READWRITE | MYLITE_OPEN_CREATE, nullptr);
}

extern "C" int mylite_open_v2(const char *filename, mylite_db **out_db,
                              unsigned flags, const char *profile)
{
  if (!out_db)
    return MYLITE_MISUSE;

  *out_db= nullptr;
  mylite_db *db= new (std::nothrow) mylite_db;
  if (!db)
    return MYLITE_NOMEM;
  *out_db= db;

  if (!validate_open_inputs(filename, flags, profile, db))
    return db->errcode;

  std::string path_message;
  if (!make_absolute_path(filename, &db->filename, &path_message))
    return set_error(db, MYLITE_CANTOPEN, 0, "HY000", path_message);
  db->runtime_dir= db->filename + ".mylite-runtime";
  const bool readonly= (flags & MYLITE_OPEN_READONLY) != 0;

  std::lock_guard<std::mutex> guard(runtime_mutex);
  if (runtime.started && runtime.filename != db->filename)
  {
    return set_error(db, MYLITE_BUSY, 0, "HY000",
                     "another MyLite database path is already initialized in "
                     "this process");
  }
  if (runtime.started && runtime.readonly != readonly)
  {
    return set_error(db, MYLITE_BUSY, 0, "HY000",
                     "MyLite runtime is already initialized with a different "
                     "open mode");
  }

  if (!prepare_primary_file(db->filename, flags, &path_message))
    return set_error(db, MYLITE_CANTOPEN, 0, "HY000", path_message);

  if (!ensure_runtime_directories(db->runtime_dir, &path_message))
    return set_error(db, MYLITE_CANTOPEN, 0, "HY000", path_message);

  const bool started_here= !runtime.started;
  if (started_here && !start_runtime(db->filename, db->runtime_dir,
                                     readonly, &path_message))
    return set_error(db, MYLITE_ERROR, 0, "HY000", path_message);

  const int connect_result= connect_handle(db);
  if (connect_result != MYLITE_OK)
    return connect_result;

  ++runtime.refs;
  db->runtime_ref= true;
  clear_error(db);
  return MYLITE_OK;
}

extern "C" int mylite_close(mylite_db *db)
{
  if (!db)
    return MYLITE_OK;

  std::lock_guard<std::mutex> guard(runtime_mutex);
  if (db->open_statements != 0)
  {
    return set_error(db, MYLITE_BUSY, 0, "HY000",
                     "database handle has active statements");
  }

  if (db->mysql)
  {
    mysql_close(db->mysql);
    db->mysql= nullptr;
  }

  if (db->runtime_ref)
  {
    if (runtime.refs > 0)
      --runtime.refs;
    db->runtime_ref= false;
  }

  delete db;
  return MYLITE_OK;
}

extern "C" int mylite_exec(mylite_db *db, const char *sql,
                           mylite_exec_callback callback, void *ctx,
                           char **errmsg)
{
  if (errmsg)
    *errmsg= nullptr;

  if (!db)
    return return_standalone_error(MYLITE_MISUSE, "bad database handle",
                                   errmsg);
  if (!sql)
  {
    set_error(db, MYLITE_MISUSE, 0, "HY000", "SQL string is required");
    return return_error_with_message(db, errmsg);
  }
  if (!db->mysql)
  {
    set_error(db, MYLITE_MISUSE, 0, "HY000",
              "database handle is not open");
    return return_error_with_message(db, errmsg);
  }

  clear_error(db);
  if (mysql_real_query(db->mysql, sql,
                       static_cast<unsigned long>(std::strlen(sql))) != 0)
  {
    set_error_from_mysql(db);
    return return_error_with_message(db, errmsg);
  }

  const unsigned field_count= mysql_field_count(db->mysql);
  if (field_count == 0)
  {
    clear_error(db);
    return MYLITE_OK;
  }

  MYSQL_RES *result= mysql_store_result(db->mysql);
  if (!result)
  {
    if (mysql_errno(db->mysql) != 0)
      set_error_from_mysql(db);
    else
      set_error(db, MYLITE_ERROR, 0, "HY000", "mysql_store_result failed");
    return return_error_with_message(db, errmsg);
  }

  const int callback_result= execute_result_callback(db, result, callback,
                                                     ctx);
  mysql_free_result(result);
  if (callback_result != MYLITE_OK)
    return return_error_with_message(db, errmsg);

  clear_error(db);
  return MYLITE_OK;
}

extern "C" void mylite_free(void *ptr)
{
  std::free(ptr);
}

extern "C" long long mylite_changes(mylite_db *db)
{
  if (!db || !db->mysql)
    return -1;

  const my_ulonglong rows= mysql_affected_rows(db->mysql);
  if (rows == ~static_cast<my_ulonglong>(0))
    return -1;
  if (rows > static_cast<my_ulonglong>(LLONG_MAX))
    return LLONG_MAX;
  return static_cast<long long>(rows);
}

extern "C" unsigned long long mylite_last_insert_id(mylite_db *db)
{
  return db && db->mysql ?
    static_cast<unsigned long long>(mysql_insert_id(db->mysql)) : 0;
}

extern "C" unsigned mylite_warning_count(mylite_db *db)
{
  return db && db->mysql ? mysql_warning_count(db->mysql) : 0;
}

extern "C" int mylite_warning(mylite_db *db, unsigned index,
                              unsigned *level, unsigned *code,
                              const char **message)
{
  if (level)
    *level= 0;
  if (code)
    *code= 0;
  if (message)
    *message= nullptr;

  if (!db)
    return MYLITE_MISUSE;
  if (!level || !code || !message)
    return set_error(db, MYLITE_MISUSE, 0, "HY000",
                     "warning output pointers are required");
  if (!db->mysql)
    return set_error(db, MYLITE_MISUSE, 0, "HY000",
                     "database handle is not open");

  const MysqlEffectSnapshot snapshot= snapshot_mysql_effects(db->mysql);
  const std::string query= "SHOW WARNINGS LIMIT " + std::to_string(index) +
                           ", 1";
  if (mysql_real_query(db->mysql, query.c_str(),
                       static_cast<unsigned long>(query.length())) != 0)
  {
    const int rc= set_error_from_mysql(db);
    restore_mysql_effects(db->mysql, snapshot);
    return rc;
  }

  MYSQL_RES *result= mysql_store_result(db->mysql);
  if (!result)
  {
    const int rc= mysql_errno(db->mysql) != 0 ?
      set_error_from_mysql(db) :
      set_error(db, MYLITE_ERROR, 0, "HY000", "mysql_store_result failed");
    restore_mysql_effects(db->mysql, snapshot);
    return rc;
  }

  MYSQL_ROW row= mysql_fetch_row(result);
  if (!row)
  {
    mysql_free_result(result);
    restore_mysql_effects(db->mysql, snapshot);
    return set_error(db, MYLITE_NOTFOUND, 0, "02000",
                     "warning index is out of range");
  }

  unsigned long *lengths= mysql_fetch_lengths(result);
  const unsigned long level_length= lengths && row[0] ?
    lengths[0] : (row[0] ? std::strlen(row[0]) : 0);
  const unsigned long message_length= lengths && row[2] ?
    lengths[2] : (row[2] ? std::strlen(row[2]) : 0);

  try
  {
    db->warning_message.assign(row[2] ? row[2] : "",
                               static_cast<size_t>(message_length));
  }
  catch (const std::bad_alloc &)
  {
    mysql_free_result(result);
    restore_mysql_effects(db->mysql, snapshot);
    return set_error(db, MYLITE_NOMEM, 0, "HY001", "out of memory");
  }

  *level= map_warning_level_name(row[0], static_cast<size_t>(level_length));
  *code= row[1] ? static_cast<unsigned>(std::strtoul(row[1], nullptr, 10)) :
    0;
  *message= db->warning_message.c_str();

  mysql_free_result(result);
  restore_mysql_effects(db->mysql, snapshot);
  clear_error(db);
  return MYLITE_OK;
}

extern "C" int mylite_prepare(mylite_db *db, const char *sql,
                              size_t sql_len, mylite_stmt **out_stmt,
                              const char **tail)
{
  if (out_stmt)
    *out_stmt= nullptr;
  if (tail)
    *tail= sql;

  if (!db)
    return MYLITE_MISUSE;
  if (!out_stmt)
    return set_error(db, MYLITE_MISUSE, 0, "HY000",
                     "statement output pointer is required");
  if (!sql)
    return set_error(db, MYLITE_MISUSE, 0, "HY000",
                     "SQL string is required");
  if (!db->mysql)
    return set_error(db, MYLITE_MISUSE, 0, "HY000",
                     "database handle is not open");

  const size_t effective_len= sql_len != 0 ? sql_len : std::strlen(sql);
  if (effective_len > ULONG_MAX)
    return set_error(db, MYLITE_MISUSE, 0, "HY000",
                     "SQL string is too long");

  MYSQL_STMT *mysql_stmt= mysql_stmt_init(db->mysql);
  if (!mysql_stmt)
    return set_error(db, MYLITE_NOMEM, 0, "HY001", "mysql_stmt_init failed");

  if (mysql_stmt_prepare(mysql_stmt, sql,
                         static_cast<unsigned long>(effective_len)) != 0)
  {
    const int rc= set_error_from_mysql_stmt(db, mysql_stmt);
    mysql_stmt_close(mysql_stmt);
    return rc;
  }

  mylite_stmt *stmt= new (std::nothrow) mylite_stmt;
  if (!stmt)
  {
    mysql_stmt_close(mysql_stmt);
    return set_error(db, MYLITE_NOMEM, 0, "HY001", "out of memory");
  }

  stmt->db= db;
  stmt->stmt= mysql_stmt;
  stmt->metadata= mysql_stmt_result_metadata(mysql_stmt);
  if (stmt->metadata)
  {
    const unsigned column_count= mysql_num_fields(stmt->metadata);
    MYSQL_FIELD *fields= mysql_fetch_fields(stmt->metadata);
    if (!fields && column_count != 0)
    {
      free_statement_metadata(stmt);
      mysql_stmt_close(mysql_stmt);
      delete stmt;
      return set_error(db, MYLITE_ERROR, 0, "HY000",
                       "could not read prepared result metadata");
    }

    try
    {
      stmt->column_names.reserve(column_count);
      stmt->column_types.reserve(column_count);
      for (unsigned i= 0; i < column_count; ++i)
      {
        stmt->column_names.push_back(fields[i].name ? fields[i].name : "");
        stmt->column_types.push_back(map_column_type(fields[i]));
      }
    }
    catch (const std::bad_alloc &)
    {
      free_statement_metadata(stmt);
      mysql_stmt_close(mysql_stmt);
      delete stmt;
      return set_error(db, MYLITE_NOMEM, 0, "HY001", "out of memory");
    }
  }

  const unsigned long param_count= mysql_stmt_param_count(mysql_stmt);
  if (param_count > UINT_MAX)
  {
    free_statement_metadata(stmt);
    mysql_stmt_close(mysql_stmt);
    delete stmt;
    return set_error(db, MYLITE_MISUSE, 0, "HY000",
                     "prepared statement has too many parameters");
  }

  try
  {
    stmt->parameters.assign(static_cast<size_t>(param_count),
                            PreparedParameter());
  }
  catch (const std::bad_alloc &)
  {
    free_statement_metadata(stmt);
    mysql_stmt_close(mysql_stmt);
    delete stmt;
    return set_error(db, MYLITE_NOMEM, 0, "HY001", "out of memory");
  }

  ++db->open_statements;
  *out_stmt= stmt;
  if (tail)
    *tail= sql + effective_len;
  clear_error(db);
  return MYLITE_OK;
}

extern "C" int mylite_step(mylite_stmt *stmt)
{
  if (!stmt || !stmt->stmt || !stmt->db)
    return MYLITE_MISUSE;
  if (stmt->done)
    return MYLITE_DONE;
  if (!stmt->executed)
    return execute_prepared_statement(stmt);
  return fetch_prepared_row(stmt);
}

extern "C" int mylite_reset(mylite_stmt *stmt)
{
  if (!stmt || !stmt->stmt || !stmt->db)
    return MYLITE_MISUSE;
  if (mysql_stmt_reset(stmt->stmt) != 0)
    return set_error_from_stmt(stmt);
  reset_prepared_result_state(stmt);
  clear_error(stmt->db);
  return MYLITE_OK;
}

extern "C" int mylite_finalize(mylite_stmt *stmt)
{
  if (!stmt)
    return MYLITE_OK;

  mylite_db *db= stmt->db;
  free_statement_metadata(stmt);
  const int close_error= stmt->stmt && mysql_stmt_close(stmt->stmt) != 0;
  stmt->stmt= nullptr;
  if (db && db->open_statements > 0)
    --db->open_statements;
  delete stmt;

  if (close_error && db)
    return set_error(db, MYLITE_ERROR, 0, "HY000",
                     "mysql_stmt_close failed");
  if (db)
    clear_error(db);
  return close_error ? MYLITE_ERROR : MYLITE_OK;
}

extern "C" int mylite_bind_null(mylite_stmt *stmt, unsigned index)
{
  PreparedParameter *param= nullptr;
  const int rc= get_bind_parameter(stmt, index, &param);
  if (rc != MYLITE_OK)
    return rc;

  param->kind= PARAMETER_NULL;
  param->is_null= 1;
  param->int64_value= 0;
  param->uint64_value= 0;
  param->double_value= 0.0;
  param->bytes.clear();
  param->length= 0;
  clear_error(stmt->db);
  return MYLITE_OK;
}

extern "C" int mylite_bind_int64(mylite_stmt *stmt, unsigned index,
                                  long long value)
{
  PreparedParameter *param= nullptr;
  const int rc= get_bind_parameter(stmt, index, &param);
  if (rc != MYLITE_OK)
    return rc;

  param->kind= PARAMETER_INT64;
  param->is_null= 0;
  param->int64_value= value;
  param->uint64_value= 0;
  param->double_value= 0.0;
  param->bytes.clear();
  param->length= 0;
  clear_error(stmt->db);
  return MYLITE_OK;
}

extern "C" int mylite_bind_uint64(mylite_stmt *stmt, unsigned index,
                                   unsigned long long value)
{
  PreparedParameter *param= nullptr;
  const int rc= get_bind_parameter(stmt, index, &param);
  if (rc != MYLITE_OK)
    return rc;

  param->kind= PARAMETER_UINT64;
  param->is_null= 0;
  param->int64_value= 0;
  param->uint64_value= value;
  param->double_value= 0.0;
  param->bytes.clear();
  param->length= 0;
  clear_error(stmt->db);
  return MYLITE_OK;
}

extern "C" int mylite_bind_double(mylite_stmt *stmt, unsigned index,
                                   double value)
{
  PreparedParameter *param= nullptr;
  const int rc= get_bind_parameter(stmt, index, &param);
  if (rc != MYLITE_OK)
    return rc;

  param->kind= PARAMETER_DOUBLE;
  param->is_null= 0;
  param->int64_value= 0;
  param->uint64_value= 0;
  param->double_value= value;
  param->bytes.clear();
  param->length= 0;
  clear_error(stmt->db);
  return MYLITE_OK;
}

extern "C" int mylite_bind_text(mylite_stmt *stmt, unsigned index,
                                 const char *value, size_t value_len,
                                 void (*destructor)(void *))
{
  return bind_prepared_bytes(stmt, index, PARAMETER_TEXT, value, value_len,
                             destructor);
}

extern "C" int mylite_bind_blob(mylite_stmt *stmt, unsigned index,
                                 const void *value, size_t value_len,
                                 void (*destructor)(void *))
{
  return bind_prepared_bytes(stmt, index, PARAMETER_BLOB, value, value_len,
                             destructor);
}

extern "C" unsigned mylite_column_count(mylite_stmt *stmt)
{
  return stmt ? static_cast<unsigned>(stmt->column_names.size()) : 0;
}

extern "C" const char *mylite_column_name(mylite_stmt *stmt, unsigned column)
{
  return valid_column(stmt, column) ? stmt->column_names[column].c_str() :
    nullptr;
}

extern "C" int mylite_column_type(mylite_stmt *stmt, unsigned column)
{
  if (!valid_column(stmt, column))
    return MYLITE_NULL;
  if (stmt->has_current_row && stmt->column_is_null[column])
    return MYLITE_NULL;
  return stmt->column_types[column];
}

extern "C" long long mylite_column_int64(mylite_stmt *stmt, unsigned column)
{
  const char *value= mylite_column_text(stmt, column);
  if (!value)
    return 0;
  return std::strtoll(value, nullptr, 10);
}

extern "C" unsigned long long mylite_column_uint64(mylite_stmt *stmt,
                                                   unsigned column)
{
  const char *value= mylite_column_text(stmt, column);
  if (!value)
    return 0;
  return std::strtoull(value, nullptr, 10);
}

extern "C" double mylite_column_double(mylite_stmt *stmt, unsigned column)
{
  const char *value= mylite_column_text(stmt, column);
  if (!value)
    return 0.0;
  return std::strtod(value, nullptr);
}

extern "C" const char *mylite_column_text(mylite_stmt *stmt, unsigned column)
{
  if (!valid_column(stmt, column) || !stmt->has_current_row ||
      stmt->column_is_null[column])
    return nullptr;
  return stmt->column_buffers[column].data();
}

extern "C" const void *mylite_column_blob(mylite_stmt *stmt, unsigned column)
{
  return mylite_column_text(stmt, column);
}

extern "C" size_t mylite_column_bytes(mylite_stmt *stmt, unsigned column)
{
  if (!valid_column(stmt, column) || !stmt->has_current_row ||
      stmt->column_is_null[column])
    return 0;
  return static_cast<size_t>(stmt->column_lengths[column]);
}

extern "C" int mylite_errcode(mylite_db *db)
{
  return db ? db->errcode : MYLITE_MISUSE;
}

extern "C" int mylite_extended_errcode(mylite_db *db)
{
  return db ? db->extended_errcode : MYLITE_MISUSE;
}

extern "C" unsigned mylite_mariadb_errno(mylite_db *db)
{
  return db ? db->mariadb_errno : 0;
}

extern "C" const char *mylite_sqlstate(mylite_db *db)
{
  return db ? db->sqlstate.c_str() : "HY000";
}

extern "C" const char *mylite_errmsg(mylite_db *db)
{
  return db ? db->errmsg.c_str() : "bad database handle";
}

namespace {

int set_error(mylite_db *db, int code, unsigned mariadb_errno,
              const char *sqlstate, const std::string &message)
{
  db->errcode= code;
  db->extended_errcode= code;
  db->mariadb_errno= mariadb_errno;
  db->sqlstate= sqlstate ? sqlstate : "HY000";
  db->errmsg= message;
  return code;
}

void clear_error(mylite_db *db)
{
  set_error(db, MYLITE_OK, 0, "00000", "not an error");
}

bool validate_open_inputs(const char *filename, unsigned flags,
                          const char *profile, mylite_db *db)
{
  if (!filename || !filename[0])
  {
    set_error(db, MYLITE_MISUSE, 0, "HY000", "filename is required");
    return false;
  }

  if (profile && std::strcmp(profile, "default") != 0)
  {
    set_error(db, MYLITE_MISUSE, 0, "HY000", "unsupported profile");
    return false;
  }

  const bool readonly= (flags & MYLITE_OPEN_READONLY) != 0;
  const bool readwrite= (flags & MYLITE_OPEN_READWRITE) != 0;
  const bool create= (flags & MYLITE_OPEN_CREATE) != 0;
  const unsigned supported= MYLITE_OPEN_READONLY | MYLITE_OPEN_READWRITE |
                            MYLITE_OPEN_CREATE;

  if ((flags & ~supported) != 0 || readonly == readwrite ||
      (readonly && create))
  {
    set_error(db, MYLITE_MISUSE, 0, "HY000", "unsupported open flags");
    return false;
  }

  return true;
}

bool make_absolute_path(const char *filename, std::string *path,
                        std::string *message)
{
  if (filename[0] == '/')
  {
    *path= filename;
    return true;
  }

  char *cwd= getcwd(nullptr, 0);
  if (!cwd)
  {
    *message= std::string("could not resolve current directory: ") +
              std::strerror(errno);
    return false;
  }

  *path= std::string(cwd) + "/" + filename;
  std::free(cwd);
  return true;
}

bool prepare_primary_file(const std::string &path, unsigned flags,
                          std::string *message)
{
  const bool readonly= (flags & MYLITE_OPEN_READONLY) != 0;
  const bool create= (flags & MYLITE_OPEN_CREATE) != 0;
  if (create && !ensure_directory(parent_path(path), message))
    return false;

  int open_flags= readonly ? O_RDONLY : O_RDWR;
  if (create)
    open_flags|= O_CREAT;
  open_flags|= O_CLOEXEC;

  const int fd= open(path.c_str(), open_flags, 0666);
  if (fd < 0)
  {
    *message= std::string("could not open primary file: ") +
              std::strerror(errno);
    return false;
  }

  if (close(fd) != 0)
  {
    *message= std::string("could not close primary file: ") +
              std::strerror(errno);
    return false;
  }

  return true;
}

bool ensure_runtime_directories(const std::string &runtime_dir,
                                std::string *message)
{
  return ensure_directory(runtime_dir, message) &&
         ensure_directory(runtime_dir + "/datadir", message) &&
         ensure_directory(runtime_dir + "/tmp", message);
}

bool ensure_directory(const std::string &path, std::string *message)
{
  if (mkdir(path.c_str(), 0777) == 0)
    return true;
  if (errno == EEXIST && is_directory(path))
    return true;

  *message= std::string("could not create directory ") + path + ": " +
            std::strerror(errno);
  return false;
}

std::string parent_path(const std::string &path)
{
  const std::string::size_type slash= path.find_last_of('/');
  if (slash == std::string::npos)
    return ".";
  if (slash == 0)
    return "/";
  return path.substr(0, slash);
}

bool is_directory(const std::string &path)
{
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool start_runtime(const std::string &filename,
                   const std::string &runtime_dir,
                   bool readonly,
                   std::string *message)
{
  runtime.filename= filename;
  runtime.readonly= readonly;
  runtime.runtime_dir= runtime_dir;
  runtime.server_args= build_server_args(runtime_dir);
  rebuild_server_argv();

  char server_group[]= "server";
  char embedded_group[]= "embedded";
  char *groups[]= { server_group, embedded_group, nullptr };

  if (mysql_server_init(static_cast<int>(runtime.server_argv.size()),
                        runtime.server_argv.data(), groups) != 0)
  {
    mysql_server_end();
    *message= "mysql_server_init failed";
    runtime= RuntimeState();
    return false;
  }

  runtime.started= true;
  if (!runtime_shutdown_registered)
  {
    std::atexit(stop_runtime_at_exit);
    runtime_shutdown_registered= true;
  }
  return true;
}

std::vector<std::string> build_server_args(const std::string &runtime_dir)
{
  std::vector<std::string> args= {
    "libmylite",
    "--no-defaults",
    "--datadir=" + runtime_dir + "/datadir",
    "--tmpdir=" + runtime_dir + "/tmp",
    "--mylite-catalog-file=" + runtime.filename,
    std::string("--lc-messages-dir=") + default_lc_messages_dir(),
    "--skip-grant-tables",
    "--skip-networking",
    "--skip-name-resolve",
    "--skip-external-locking",
    "--skip-slave-start",
    "--log-output=NONE",
    "--pid-file=" + runtime_dir + "/mariadb.pid",
    "--socket=" + runtime_dir + "/mariadb.sock"
  };
  if (runtime.readonly)
    args.push_back("--mylite-read-only=ON");
  return args;
}

const char *default_lc_messages_dir()
{
  const char *dir= std::getenv("MYLITE_LC_MESSAGES_DIR");
  if (dir && dir[0])
    return dir;
  return MYLITE_DEFAULT_LC_MESSAGES_DIR;
}

void rebuild_server_argv()
{
  runtime.server_argv.clear();
  runtime.server_argv.reserve(runtime.server_args.size());
  for (std::string &arg : runtime.server_args)
    runtime.server_argv.push_back(const_cast<char *>(arg.c_str()));
}

void stop_runtime()
{
  if (runtime.started)
    mysql_server_end();
  runtime= RuntimeState();
}

void stop_runtime_at_exit()
{
  std::lock_guard<std::mutex> guard(runtime_mutex);
  stop_runtime();
}

int connect_handle(mylite_db *db)
{
  MYSQL *mysql= mysql_init(nullptr);
  if (!mysql)
    return set_error(db, MYLITE_NOMEM, 0, "HY000", "mysql_init failed");

  if (!mysql_real_connect(mysql, nullptr, "root", nullptr, nullptr, 0, nullptr,
                          0))
  {
    const unsigned error= mysql_errno(mysql);
    const std::string sqlstate= mysql_sqlstate(mysql);
    const std::string message= std::string("mysql_real_connect failed: ") +
                               mysql_error(mysql);
    mysql_close(mysql);
    return set_error(db, MYLITE_ERROR, error, sqlstate.c_str(), message);
  }

  db->mysql= mysql;
  return MYLITE_OK;
}

int execute_result_callback(mylite_db *db, MYSQL_RES *result,
                            mylite_exec_callback callback, void *ctx)
{
  if (!callback)
    return MYLITE_OK;

  const unsigned column_count= mysql_num_fields(result);
  if (column_count > static_cast<unsigned>(INT_MAX))
    return set_error(db, MYLITE_ERROR, 0, "HY000", "too many result columns");

  MYSQL_FIELD *fields= mysql_fetch_fields(result);
  if (!fields && column_count != 0)
    return set_error(db, MYLITE_ERROR, 0, "HY000",
                     "could not read result metadata");

  try
  {
    std::vector<char *> column_names(column_count);
    for (unsigned i= 0; i < column_count; ++i)
      column_names[i]= fields[i].name ? fields[i].name : const_cast<char *>("");

    std::vector<char *> values(column_count);
    MYSQL_ROW row;
    while ((row= mysql_fetch_row(result)))
    {
      for (unsigned i= 0; i < column_count; ++i)
        values[i]= row[i];
      if (callback(ctx, static_cast<int>(column_count), values.data(),
                   column_names.data()) != 0)
      {
        return set_error(db, MYLITE_ERROR, 0, "HY000",
                         "callback requested abort");
      }
    }
  }
  catch (const std::bad_alloc &)
  {
    return set_error(db, MYLITE_NOMEM, 0, "HY001", "out of memory");
  }

  if (mysql_errno(db->mysql) != 0)
    return set_error_from_mysql(db);
  return MYLITE_OK;
}

int execute_prepared_statement(mylite_stmt *stmt)
{
  mylite_db *db= stmt->db;
  clear_error(db);

  const int param_bind_result= bind_prepared_parameters(stmt);
  if (param_bind_result != MYLITE_OK)
    return param_bind_result;

  my_bool update_max_length= 1;
  if (mysql_stmt_attr_set(stmt->stmt, STMT_ATTR_UPDATE_MAX_LENGTH,
                          &update_max_length) != 0)
    return set_error_from_stmt(stmt);

  if (mysql_stmt_execute(stmt->stmt) != 0)
    return set_error_from_stmt(stmt);

  stmt->executed= true;
  if (mysql_stmt_field_count(stmt->stmt) == 0)
  {
    stmt->done= true;
    return MYLITE_DONE;
  }

  if (mysql_stmt_store_result(stmt->stmt) != 0)
    return set_error_from_stmt(stmt);

  const int bind_result= bind_prepared_result(stmt);
  if (bind_result != MYLITE_OK)
    return bind_result;
  return fetch_prepared_row(stmt);
}

int bind_prepared_parameters(mylite_stmt *stmt)
{
  const size_t param_count= stmt->parameters.size();
  if (param_count == 0)
    return MYLITE_OK;

  try
  {
    stmt->param_binds.assign(param_count, MYSQL_BIND());
  }
  catch (const std::bad_alloc &)
  {
    return set_error(stmt->db, MYLITE_NOMEM, 0, "HY001", "out of memory");
  }

  for (size_t i= 0; i < param_count; ++i)
  {
    PreparedParameter &param= stmt->parameters[i];
    if (param.kind == PARAMETER_UNBOUND)
      return set_error(stmt->db, MYLITE_MISUSE, 0, "HY000",
                       "not all parameters are bound");

    MYSQL_BIND &bind= stmt->param_binds[i];
    switch (param.kind) {
    case PARAMETER_NULL:
      bind.buffer_type= MYSQL_TYPE_NULL;
      break;
    case PARAMETER_INT64:
      bind.buffer_type= MYSQL_TYPE_LONGLONG;
      bind.buffer= &param.int64_value;
      break;
    case PARAMETER_UINT64:
      bind.buffer_type= MYSQL_TYPE_LONGLONG;
      bind.buffer= &param.uint64_value;
      bind.is_unsigned= 1;
      break;
    case PARAMETER_DOUBLE:
      bind.buffer_type= MYSQL_TYPE_DOUBLE;
      bind.buffer= &param.double_value;
      break;
    case PARAMETER_TEXT:
      bind.buffer_type= MYSQL_TYPE_STRING;
      bind.buffer= param.bytes.data();
      bind.buffer_length= param.length;
      bind.length= &param.length;
      bind.is_null= &param.is_null;
      break;
    case PARAMETER_BLOB:
      bind.buffer_type= MYSQL_TYPE_BLOB;
      bind.buffer= param.bytes.data();
      bind.buffer_length= param.length;
      bind.length= &param.length;
      bind.is_null= &param.is_null;
      break;
    case PARAMETER_UNBOUND:
      return set_error(stmt->db, MYLITE_MISUSE, 0, "HY000",
                       "not all parameters are bound");
    }
  }

  if (mysql_stmt_bind_param(stmt->stmt, stmt->param_binds.data()) != 0)
    return set_error_from_stmt(stmt);
  return MYLITE_OK;
}

int bind_prepared_result(mylite_stmt *stmt)
{
  if (!stmt->metadata)
    stmt->metadata= mysql_stmt_result_metadata(stmt->stmt);
  if (!stmt->metadata)
    return set_error(stmt->db, MYLITE_ERROR, 0, "HY000",
                     "prepared statement has no result metadata");

  const unsigned column_count= mysql_stmt_field_count(stmt->stmt);
  if (column_count != stmt->column_names.size())
    return set_error(stmt->db, MYLITE_ERROR, 0, "HY000",
                     "prepared result metadata changed");

  MYSQL_FIELD *fields= mysql_fetch_fields(stmt->metadata);
  if (!fields && column_count != 0)
    return set_error(stmt->db, MYLITE_ERROR, 0, "HY000",
                     "could not read prepared result metadata");

  try
  {
    stmt->result_binds.assign(column_count, MYSQL_BIND());
    stmt->column_buffers.assign(column_count, std::vector<char>());
    stmt->column_lengths.assign(column_count, 0);
    stmt->column_is_null.assign(column_count, 0);
    stmt->column_errors.assign(column_count, 0);

    for (unsigned i= 0; i < column_count; ++i)
    {
      unsigned long capacity= fields[i].max_length;
      if (capacity == 0)
        capacity= 1;
      if (capacity == ULONG_MAX)
        return set_error(stmt->db, MYLITE_ERROR, 0, "HY000",
                         "prepared result column is too large");

      stmt->column_buffers[i].assign(static_cast<size_t>(capacity) + 1, 0);
      MYSQL_BIND &bind= stmt->result_binds[i];
      bind.buffer_type= bind_buffer_type(stmt->column_types[i]);
      bind.buffer= stmt->column_buffers[i].data();
      bind.buffer_length= capacity;
      bind.length= &stmt->column_lengths[i];
      bind.is_null= &stmt->column_is_null[i];
      bind.error= &stmt->column_errors[i];
    }
  }
  catch (const std::bad_alloc &)
  {
    return set_error(stmt->db, MYLITE_NOMEM, 0, "HY001", "out of memory");
  }

  if (mysql_stmt_bind_result(stmt->stmt, stmt->result_binds.data()) != 0)
    return set_error_from_stmt(stmt);

  stmt->result_bound= true;
  return MYLITE_OK;
}

int fetch_prepared_row(mylite_stmt *stmt)
{
  if (!stmt->result_bound)
    return set_error(stmt->db, MYLITE_MISUSE, 0, "HY000",
                     "prepared statement result is not bound");

  for (unsigned i= 0; i < stmt->column_errors.size(); ++i)
    stmt->column_errors[i]= 0;

  const int fetch_result= mysql_stmt_fetch(stmt->stmt);
  if (fetch_result == MYSQL_NO_DATA)
  {
    stmt->has_current_row= false;
    stmt->done= true;
    clear_error(stmt->db);
    return MYLITE_DONE;
  }
  if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED)
    return set_error_from_stmt(stmt);

  if (fetch_result == MYSQL_DATA_TRUNCATED)
  {
    const int truncate_result= fetch_truncated_columns(stmt);
    if (truncate_result != MYLITE_OK)
      return truncate_result;
  }

  for (unsigned i= 0; i < stmt->column_buffers.size(); ++i)
  {
    if (!stmt->column_is_null[i])
    {
      const size_t length= static_cast<size_t>(stmt->column_lengths[i]);
      if (length >= stmt->column_buffers[i].size())
        stmt->column_buffers[i].push_back('\0');
      else
        stmt->column_buffers[i][length]= '\0';
    }
  }

  stmt->has_current_row= true;
  clear_error(stmt->db);
  return MYLITE_ROW;
}

int fetch_truncated_columns(mylite_stmt *stmt)
{
  bool rebind_needed= false;
  try
  {
    for (unsigned i= 0; i < stmt->column_buffers.size(); ++i)
    {
      if (!stmt->column_errors[i] &&
          stmt->column_lengths[i] <= stmt->result_binds[i].buffer_length)
        continue;

      const unsigned long length= stmt->column_lengths[i];
      if (length == ULONG_MAX)
        return set_error(stmt->db, MYLITE_ERROR, 0, "HY000",
                         "prepared result column is too large");

      stmt->column_buffers[i].assign(static_cast<size_t>(length) + 1, 0);
      stmt->result_binds[i].buffer= stmt->column_buffers[i].data();
      stmt->result_binds[i].buffer_length= length;
      rebind_needed= true;

      MYSQL_BIND fetch_bind= MYSQL_BIND();
      fetch_bind.buffer_type= stmt->result_binds[i].buffer_type;
      fetch_bind.buffer= stmt->column_buffers[i].data();
      fetch_bind.buffer_length= length;
      fetch_bind.length= &stmt->column_lengths[i];
      fetch_bind.is_null= &stmt->column_is_null[i];
      fetch_bind.error= &stmt->column_errors[i];

      if (mysql_stmt_fetch_column(stmt->stmt, &fetch_bind, i, 0) != 0)
        return set_error_from_stmt(stmt);
    }
  }
  catch (const std::bad_alloc &)
  {
    return set_error(stmt->db, MYLITE_NOMEM, 0, "HY001", "out of memory");
  }

  if (rebind_needed &&
      mysql_stmt_bind_result(stmt->stmt, stmt->result_binds.data()) != 0)
    return set_error_from_stmt(stmt);

  return MYLITE_OK;
}

int bind_prepared_bytes(mylite_stmt *stmt, unsigned index,
                        PreparedParameterKind kind, const void *value,
                        size_t value_len, void (*destructor)(void *))
{
  PreparedParameter *param= nullptr;
  const int rc= get_bind_parameter(stmt, index, &param);
  if (rc != MYLITE_OK)
    return rc;

  if (!value)
    return mylite_bind_null(stmt, index);

  if (value_len > ULONG_MAX)
    return set_error(stmt->db, MYLITE_MISUSE, 0, "HY000",
                     "bound value is too large");

  std::vector<char> bytes;
  try
  {
    const char *begin= static_cast<const char *>(value);
    bytes.assign(begin, begin + value_len);
    if (bytes.empty())
      bytes.push_back('\0');
  }
  catch (const std::bad_alloc &)
  {
    return set_error(stmt->db, MYLITE_NOMEM, 0, "HY001", "out of memory");
  }

  param->kind= kind;
  param->is_null= 0;
  param->int64_value= 0;
  param->uint64_value= 0;
  param->double_value= 0.0;
  param->bytes.swap(bytes);
  param->length= static_cast<unsigned long>(value_len);
  clear_error(stmt->db);

  if (custom_bind_destructor(destructor))
    destructor(const_cast<void *>(value));
  return MYLITE_OK;
}

int get_bind_parameter(mylite_stmt *stmt, unsigned index,
                       PreparedParameter **param)
{
  if (param)
    *param= nullptr;
  if (!stmt || !stmt->stmt || !stmt->db)
    return MYLITE_MISUSE;
  if (stmt->executed)
    return set_error(stmt->db, MYLITE_MISUSE, 0, "HY000",
                     "statement must be reset before rebinding parameters");
  if (index == 0 || index > stmt->parameters.size())
    return set_error(stmt->db, MYLITE_MISUSE, 0, "HY000",
                     "parameter index is out of range");

  if (param)
    *param= &stmt->parameters[index - 1];
  return MYLITE_OK;
}

bool custom_bind_destructor(void (*destructor)(void *))
{
  return destructor && destructor != MYLITE_TRANSIENT;
}

void reset_prepared_result_state(mylite_stmt *stmt)
{
  stmt->param_binds.clear();
  stmt->result_binds.clear();
  stmt->column_buffers.clear();
  stmt->column_lengths.clear();
  stmt->column_is_null.clear();
  stmt->column_errors.clear();
  stmt->executed= false;
  stmt->result_bound= false;
  stmt->has_current_row= false;
  stmt->done= false;
}

void free_statement_metadata(mylite_stmt *stmt)
{
  if (stmt->metadata)
  {
    mysql_free_result(stmt->metadata);
    stmt->metadata= nullptr;
  }
  reset_prepared_result_state(stmt);
}

int set_error_from_stmt(mylite_stmt *stmt)
{
  return set_error_from_mysql_stmt(stmt->db, stmt->stmt);
}

int set_error_from_mysql_stmt(mylite_db *db, MYSQL_STMT *stmt)
{
  const unsigned error= mysql_stmt_errno(stmt);
  const char *sqlstate= mysql_stmt_sqlstate(stmt);
  const char *message= mysql_stmt_error(stmt);
  const int code= classify_mariadb_error(error, sqlstate, message);
  return set_error(db, code, error, sqlstate,
                   message && message[0] ? message :
                   "MariaDB prepared statement failed");
}

int set_error_from_mysql(mylite_db *db)
{
  const unsigned error= mysql_errno(db->mysql);
  const char *sqlstate= mysql_sqlstate(db->mysql);
  const char *message= mysql_error(db->mysql);
  const int code= classify_mariadb_error(error, sqlstate, message);
  return set_error(db, code, error, sqlstate,
                   message && message[0] ? message : "MariaDB query failed");
}

int return_error_with_message(mylite_db *db, char **errmsg)
{
  if (!errmsg)
    return db->errcode;

  char *message= duplicate_message(db->errmsg);
  if (!message)
    return set_error(db, MYLITE_NOMEM, 0, "HY001", "out of memory");

  *errmsg= message;
  return db->errcode;
}

int return_standalone_error(int code, const char *message, char **errmsg)
{
  if (!errmsg)
    return code;

  char *copy= duplicate_message(message ? message : "");
  if (!copy)
    return MYLITE_NOMEM;

  *errmsg= copy;
  return code;
}

char *duplicate_message(const std::string &message)
{
  char *copy= static_cast<char *>(std::malloc(message.length() + 1));
  if (!copy)
    return nullptr;

  std::memcpy(copy, message.c_str(), message.length() + 1);
  return copy;
}

int classify_mariadb_error(unsigned error, const char *sqlstate,
                           const char *message)
{
  if (sqlstate && sqlstate[0] == '2' && sqlstate[1] == '3')
    return MYLITE_CONSTRAINT;
  if (sqlstate && sqlstate[0] == '2' && sqlstate[1] == '5')
    return MYLITE_READONLY;
  if (error == ER_OPEN_AS_READONLY)
    return MYLITE_READONLY;
  if (error == ER_CANT_CREATE_TABLE && message &&
      std::strstr(message, "Table is read only"))
    return MYLITE_READONLY;
  return MYLITE_ERROR;
}

MysqlEffectSnapshot snapshot_mysql_effects(MYSQL *mysql)
{
  MysqlEffectSnapshot snapshot;
  snapshot.affected_rows= mysql->affected_rows;
  snapshot.insert_id= mysql->insert_id;
  snapshot.field_count= mysql->field_count;
  snapshot.server_status= mysql->server_status;
  snapshot.warning_count= mysql->warning_count;
  return snapshot;
}

void restore_mysql_effects(MYSQL *mysql, const MysqlEffectSnapshot &snapshot)
{
  mysql->affected_rows= snapshot.affected_rows;
  mysql->insert_id= snapshot.insert_id;
  mysql->field_count= snapshot.field_count;
  mysql->server_status= snapshot.server_status;
  mysql->warning_count= snapshot.warning_count;
}

unsigned map_warning_level_name(const char *level, size_t length)
{
  if (level && length == 4 && std::strncmp(level, "Note", length) == 0)
    return MYLITE_WARNING_NOTE;
  if (level && length == 7 && std::strncmp(level, "Warning", length) == 0)
    return MYLITE_WARNING_WARNING;
  if (level && length == 5 && std::strncmp(level, "Error", length) == 0)
    return MYLITE_WARNING_ERROR;
  return 0;
}

bool valid_column(const mylite_stmt *stmt, unsigned column)
{
  return stmt && column < stmt->column_names.size();
}

int map_column_type(const MYSQL_FIELD &field)
{
  switch (field.type)
  {
  case MYSQL_TYPE_TINY:
  case MYSQL_TYPE_SHORT:
  case MYSQL_TYPE_LONG:
  case MYSQL_TYPE_LONGLONG:
  case MYSQL_TYPE_INT24:
  case MYSQL_TYPE_YEAR:
    return MYLITE_INTEGER;
  case MYSQL_TYPE_DECIMAL:
  case MYSQL_TYPE_FLOAT:
  case MYSQL_TYPE_DOUBLE:
  case MYSQL_TYPE_NEWDECIMAL:
    return MYLITE_FLOAT;
  case MYSQL_TYPE_TINY_BLOB:
  case MYSQL_TYPE_MEDIUM_BLOB:
  case MYSQL_TYPE_LONG_BLOB:
  case MYSQL_TYPE_BLOB:
  case MYSQL_TYPE_GEOMETRY:
    return (field.flags & BINARY_FLAG) ? MYLITE_BLOB : MYLITE_TEXT;
  case MYSQL_TYPE_NULL:
    return MYLITE_NULL;
  default:
    return MYLITE_TEXT;
  }
}

enum_field_types bind_buffer_type(int column_type)
{
  return column_type == MYLITE_BLOB ? MYSQL_TYPE_BLOB : MYSQL_TYPE_STRING;
}

} // namespace
