/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mylite.h"
#include "mysqld_error.h"

#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

struct SmokeOptions
{
  std::string database;
  std::string report;
  std::string mode= "default";
};

struct FileSnapshot
{
  off_t size= 0;
  std::time_t mtime_sec= 0;
  long mtime_nsec= 0;
};

struct CaseResult
{
  std::string label;
  int expected= MYLITE_OK;
  int actual= MYLITE_OK;
  int errcode= MYLITE_OK;
  int extended_errcode= MYLITE_OK;
  unsigned mariadb_errno= 0;
  std::string sqlstate;
  std::string message;
  bool passed= false;
};

struct SmokeResult
{
  int status= 1;
  std::string phase;
  std::string message;
  std::string exec_null_db_message;
  std::string exec_scalar_columns;
  std::string exec_scalar_rows;
  std::string exec_collation_rows;
  std::string exec_uca_collation_message;
  std::string exec_general1400_collation_message;
  std::string exec_locale_profile_rows;
  std::string exec_locale_removed_message;
  std::string exec_oracle_mode_message;
  std::string exec_oracle_function_standard_rows;
  std::string exec_oracle_function_message;
  std::string exec_xml_extractvalue_message;
  std::string exec_xml_updatexml_message;
  std::string exec_gis_function_message;
  std::string exec_vector_fromtext_message;
  std::string exec_vector_distance_message;
  std::string exec_json_valid_rows;
  std::string exec_json_schema_valid_message;
  std::string exec_json_table_message;
  std::string exec_regex_like_rows;
  std::string exec_regex_messages;
  std::string exec_window_aggregate_rows;
  std::string exec_window_function_messages;
  std::string exec_binlog_replication_message;
  std::string exec_xa_transaction_messages;
  std::string exec_server_encryption_rows;
  std::string exec_server_encryption_set_messages;
  std::string exec_proxy_protocol_rows;
  std::string exec_proxy_protocol_set_message;
  std::string exec_server_utility_standard_rows;
  std::string exec_server_utility_messages;
  std::string exec_sql_crypto_function_messages;
  std::string exec_crypt_function_message;
  std::string exec_des_function_messages;
  std::string exec_kdf_function_message;
  std::string exec_zlib_compression_have_rows;
  std::string exec_zlib_compression_crc32_rows;
  std::string exec_zlib_compression_messages;
  std::string exec_zlib_compressed_column_message;
  std::string exec_dynamic_plugin_loading_have_rows;
  std::string exec_query_cache_have_rows;
  std::string exec_query_cache_size_rows;
  std::string exec_query_cache_type_rows;
  std::string exec_query_cache_resize_rows;
  std::string exec_query_cache_select_rows;
  std::string exec_show_profiles_message;
  std::string exec_help_message;
  std::string exec_procedure_analyse_message;
  std::string exec_table_admin_messages;
  std::string exec_sequence_messages;
  std::string exec_csv_engine_message;
  std::string exec_myisam_engine_message;
  std::string exec_mrg_myisam_engine_message;
  std::string exec_memory_temp_rows;
  std::string exec_disk_temp_message;
  std::string exec_callback_abort_message;
  std::string exec_dml_rows;
  std::string exec_duplicate_key_message;
  std::string exec_reopen_rows;
  std::string statement_effects;
  std::string warning_misuse_message;
  std::string warning_first;
  std::string warning_notfound_message;
  std::string warning_error;
  std::string warning_effects;
  std::string readonly_rows;
  std::string readonly_insert_message;
  std::string readonly_create_message;
  std::string readonly_prepare_message;
  std::string readonly_file;
  std::string exclusive_existing_message;
  std::string uri_rows;
  std::string uri_readonly_rows;
  std::string uri_readonly_insert_message;
  std::string prepared_unbound_message;
  std::string prepared_invalid_bind_message;
  std::string prepared_rebind_message;
  std::string prepared_columns;
  std::string prepared_types;
  std::string prepared_rows;
  std::string prepared_reset_row;
  std::string prepared_dml_rows;
  std::string prepared_bound_rows;
  std::string prepared_bound_reset_rows;
  std::string prepared_bind_destructor_count;
  std::string prepared_close_busy_message;
  std::vector<CaseResult> cases;
};

struct ExecCapture
{
  std::vector<std::string> columns;
  std::vector<std::string> rows;
  int abort_after= 0;
};

static int bind_destructor_calls= 0;

static bool parse_options(int argc, char **argv, SmokeOptions *options,
                          std::string *error);
static int run_smoke(const SmokeOptions &options, SmokeResult *result);
static int run_default_smoke(const SmokeOptions &options,
                             SmokeResult *result);
static int run_readonly_smoke(const SmokeOptions &options,
                              SmokeResult *result);
static int run_exclusive_smoke(const SmokeOptions &options,
                               SmokeResult *result);
static int run_uri_smoke(const SmokeOptions &options, SmokeResult *result);
static int run_uri_readonly_smoke(const SmokeOptions &options,
                                  SmokeResult *result);
static bool check_close_null(SmokeResult *result);
static bool check_null_out_db(const SmokeOptions &options, SmokeResult *result);
static bool check_null_filename(SmokeResult *result);
static bool check_readonly_missing(const SmokeOptions &options,
                                   SmokeResult *result);
static bool check_bad_profile(const SmokeOptions &options,
                              SmokeResult *result);
static bool check_bad_flags(const SmokeOptions &options, SmokeResult *result);
static bool check_default_open_close(const SmokeOptions &options,
                                     SmokeResult *result);
static bool check_repeated_open_close(const SmokeOptions &options,
                                      SmokeResult *result);
static bool check_same_path_multi_handle(const SmokeOptions &options,
                                         SmokeResult *result);
static bool check_different_path_busy(const SmokeOptions &options,
                                      SmokeResult *result);
static bool check_exec_misuse(const SmokeOptions &options,
                              SmokeResult *result);
static bool check_exec_scalar(const SmokeOptions &options,
                              SmokeResult *result);
static bool check_collation_profile(const SmokeOptions &options,
                                    SmokeResult *result);
static bool check_locale_profile(const SmokeOptions &options,
                                 SmokeResult *result);
static bool check_oracle_mode_unsupported(const SmokeOptions &options,
                                          SmokeResult *result);
static bool check_oracle_functions_unsupported(const SmokeOptions &options,
                                               SmokeResult *result);
static bool check_xml_functions_unsupported(const SmokeOptions &options,
                                            SmokeResult *result);
static bool check_gis_functions_unsupported(const SmokeOptions &options,
                                            SmokeResult *result);
static bool check_vector_functions_unsupported(const SmokeOptions &options,
                                               SmokeResult *result);
static bool check_json_schema_valid_unsupported(const SmokeOptions &options,
                                                SmokeResult *result);
static bool check_json_table_unsupported(const SmokeOptions &options,
                                         SmokeResult *result);
static bool check_regex_functions_unsupported(const SmokeOptions &options,
                                              SmokeResult *result);
static bool check_window_functions_unsupported(const SmokeOptions &options,
                                               SmokeResult *result);
static bool check_binlog_replication_unsupported(const SmokeOptions &options,
                                                 SmokeResult *result);
static bool check_xa_transactions_unsupported(const SmokeOptions &options,
                                              SmokeResult *result);
static bool check_server_encryption_profile(const SmokeOptions &options,
                                            SmokeResult *result);
static bool check_proxy_protocol_profile(const SmokeOptions &options,
                                         SmokeResult *result);
static bool check_server_utility_functions_unsupported(
  const SmokeOptions &options, SmokeResult *result);
static bool check_sql_crypto_functions_unsupported(const SmokeOptions &options,
                                                   SmokeResult *result);
static bool check_crypt_function_unsupported(const SmokeOptions &options,
                                             SmokeResult *result);
static bool check_des_functions_unsupported(const SmokeOptions &options,
                                            SmokeResult *result);
static bool check_kdf_function_unsupported(const SmokeOptions &options,
                                           SmokeResult *result);
static bool check_zlib_compression_unsupported(const SmokeOptions &options,
                                               SmokeResult *result);
static bool check_dynamic_plugin_loading_unsupported(
  const SmokeOptions &options, SmokeResult *result);
static bool check_query_cache_unsupported(const SmokeOptions &options,
                                          SmokeResult *result);
static bool check_profiling_unsupported(const SmokeOptions &options,
                                        SmokeResult *result);
static bool check_help_unsupported(const SmokeOptions &options,
                                   SmokeResult *result);
static bool check_procedure_analyse_unsupported(const SmokeOptions &options,
                                                SmokeResult *result);
static bool check_table_admin_unsupported(const SmokeOptions &options,
                                          SmokeResult *result);
static bool check_sql_sequence_unsupported(const SmokeOptions &options,
                                           SmokeResult *result);
static bool check_legacy_storage_engines_unsupported(
  const SmokeOptions &options, SmokeResult *result);
static bool check_temp_spill_profile(const SmokeOptions &options,
                                     SmokeResult *result);
static bool check_exec_callback_abort(const SmokeOptions &options,
                                      SmokeResult *result);
static bool check_exec_dml_persistence(const SmokeOptions &options,
                                       SmokeResult *result);
static bool check_statement_effects(const SmokeOptions &options,
                                    SmokeResult *result);
static bool check_prepared_statement_api(const SmokeOptions &options,
                                         SmokeResult *result);
static bool check_readonly_existing_database(const SmokeOptions &options,
                                             SmokeResult *result);
static bool check_exclusive_open(const SmokeOptions &options,
                                 SmokeResult *result);
static bool check_uri_open(const SmokeOptions &options, SmokeResult *result);
static bool check_uri_readonly_open(const SmokeOptions &options,
                                    SmokeResult *result);
static bool exec_statement(mylite_db *db, const char *sql, const char *label,
                           SmokeResult *result);
static bool exec_query_capture(mylite_db *db, const char *sql,
                               const char *label, ExecCapture *capture,
                               SmokeResult *result);
static void append_statement_effect(SmokeResult *result, const char *label,
                                    const std::string &value);
static std::string statement_effect_summary(mylite_db *db);
static std::string warning_summary(unsigned level, unsigned code,
                                   const char *message);
static std::string prepared_row_summary(mylite_stmt *stmt);
static std::string prepared_bound_row_summary(mylite_stmt *stmt);
static std::string prepared_type_summary(mylite_stmt *stmt);
static std::string file_uri_for_path(const std::string &path,
                                     const char *query);
static std::string uri_percent_encode_path(const std::string &path);
static std::string hex_bytes(const void *data, size_t length);
static bool snapshot_file(const std::string &path, FileSnapshot *snapshot,
                          std::string *message);
static std::string file_snapshot_summary(const FileSnapshot &snapshot);
static int capture_exec_row(void *ctx, int column_count, char **values,
                            char **column_names);
static void count_bind_destructor(void *ptr);
static bool record_result(SmokeResult *result, const char *label, int expected,
                          int actual, mylite_db *db);
static std::string join_strings(const std::vector<std::string> &values,
                                const char *separator);
static void write_report(const SmokeOptions &options,
                         const SmokeResult &result);
static bool option_value(const char *arg, const char *name, std::string *value);
static bool require_option(const std::string &value, const char *name,
                           std::string *error);

int main(int argc, char **argv)
{
  SmokeOptions options;
  std::string error;
  if (!parse_options(argc, argv, &options, &error))
  {
    std::cerr << error << std::endl;
    return 2;
  }

  SmokeResult result;
  const int status= run_smoke(options, &result);
  write_report(options, result);
  return status;
}

static bool parse_options(int argc, char **argv, SmokeOptions *options,
                          std::string *error)
{
  for (int i= 1; i < argc; ++i)
  {
    std::string value;
    if (option_value(argv[i], "--database=", &value))
      options->database= value;
    else if (option_value(argv[i], "--report=", &value))
      options->report= value;
    else if (option_value(argv[i], "--mode=", &value))
      options->mode= value;
    else
    {
      *error= std::string("unknown argument: ") + argv[i];
      return false;
    }
  }

  if (!require_option(options->database, "--database", error) ||
      !require_option(options->report, "--report", error))
    return false;
  if (options->mode != "default" && options->mode != "readonly" &&
      options->mode != "exclusive" && options->mode != "uri" &&
      options->mode != "uri-readonly")
  {
    *error= "unsupported mode: " + options->mode;
    return false;
  }
  return true;
}

static int run_smoke(const SmokeOptions &options, SmokeResult *result)
{
  if (options.mode == "readonly")
    return run_readonly_smoke(options, result);
  if (options.mode == "exclusive")
    return run_exclusive_smoke(options, result);
  if (options.mode == "uri")
    return run_uri_smoke(options, result);
  if (options.mode == "uri-readonly")
    return run_uri_readonly_smoke(options, result);
  return run_default_smoke(options, result);
}

static int run_default_smoke(const SmokeOptions &options, SmokeResult *result)
{
  bool ok= true;

  result->phase= "close_null";
  ok= check_close_null(result) && ok;

  result->phase= "null_out_db";
  ok= check_null_out_db(options, result) && ok;

  result->phase= "null_filename";
  ok= check_null_filename(result) && ok;

  result->phase= "readonly_missing";
  ok= check_readonly_missing(options, result) && ok;

  result->phase= "bad_profile";
  ok= check_bad_profile(options, result) && ok;

  result->phase= "bad_flags";
  ok= check_bad_flags(options, result) && ok;

  result->phase= "default_open_close";
  ok= check_default_open_close(options, result) && ok;

  result->phase= "repeated_open_close";
  ok= check_repeated_open_close(options, result) && ok;

  result->phase= "same_path_multi_handle";
  ok= check_same_path_multi_handle(options, result) && ok;

  result->phase= "different_path_busy";
  ok= check_different_path_busy(options, result) && ok;

  result->phase= "exec_misuse";
  ok= check_exec_misuse(options, result) && ok;

  result->phase= "exec_scalar";
  ok= check_exec_scalar(options, result) && ok;

  result->phase= "collation_profile";
  ok= check_collation_profile(options, result) && ok;

  result->phase= "locale_profile";
  ok= check_locale_profile(options, result) && ok;

  result->phase= "oracle_mode_unsupported";
  ok= check_oracle_mode_unsupported(options, result) && ok;

  result->phase= "oracle_functions_unsupported";
  ok= check_oracle_functions_unsupported(options, result) && ok;

  result->phase= "xml_functions_unsupported";
  ok= check_xml_functions_unsupported(options, result) && ok;

  result->phase= "gis_functions_unsupported";
  ok= check_gis_functions_unsupported(options, result) && ok;

  result->phase= "vector_functions_unsupported";
  ok= check_vector_functions_unsupported(options, result) && ok;

  result->phase= "json_schema_valid_unsupported";
  ok= check_json_schema_valid_unsupported(options, result) && ok;

  result->phase= "json_table_unsupported";
  ok= check_json_table_unsupported(options, result) && ok;

  result->phase= "regex_functions_unsupported";
  ok= check_regex_functions_unsupported(options, result) && ok;

  result->phase= "window_functions_unsupported";
  ok= check_window_functions_unsupported(options, result) && ok;

  result->phase= "binlog_replication_unsupported";
  ok= check_binlog_replication_unsupported(options, result) && ok;

  result->phase= "xa_transactions_unsupported";
  ok= check_xa_transactions_unsupported(options, result) && ok;

  result->phase= "server_encryption_profile";
  ok= check_server_encryption_profile(options, result) && ok;

  result->phase= "proxy_protocol_profile";
  ok= check_proxy_protocol_profile(options, result) && ok;

  result->phase= "server_utility_functions_unsupported";
  ok= check_server_utility_functions_unsupported(options, result) && ok;

  result->phase= "sql_crypto_functions_unsupported";
  ok= check_sql_crypto_functions_unsupported(options, result) && ok;

  result->phase= "crypt_function_unsupported";
  ok= check_crypt_function_unsupported(options, result) && ok;

  result->phase= "des_functions_unsupported";
  ok= check_des_functions_unsupported(options, result) && ok;

  result->phase= "kdf_function_unsupported";
  ok= check_kdf_function_unsupported(options, result) && ok;

  result->phase= "zlib_compression_unsupported";
  ok= check_zlib_compression_unsupported(options, result) && ok;

  result->phase= "dynamic_plugin_loading_unsupported";
  ok= check_dynamic_plugin_loading_unsupported(options, result) && ok;

  result->phase= "query_cache_unsupported";
  ok= check_query_cache_unsupported(options, result) && ok;

  result->phase= "profiling_unsupported";
  ok= check_profiling_unsupported(options, result) && ok;

  result->phase= "help_unsupported";
  ok= check_help_unsupported(options, result) && ok;

  result->phase= "procedure_analyse_unsupported";
  ok= check_procedure_analyse_unsupported(options, result) && ok;

  result->phase= "table_admin_unsupported";
  ok= check_table_admin_unsupported(options, result) && ok;

  result->phase= "sql_sequence_unsupported";
  ok= check_sql_sequence_unsupported(options, result) && ok;

  result->phase= "legacy_storage_engines_unsupported";
  ok= check_legacy_storage_engines_unsupported(options, result) && ok;

  result->phase= "temp_spill_profile";
  ok= check_temp_spill_profile(options, result) && ok;

  result->phase= "exec_callback_abort";
  ok= check_exec_callback_abort(options, result) && ok;

  result->phase= "exec_dml_persistence";
  ok= check_exec_dml_persistence(options, result) && ok;

  result->phase= "statement_effects";
  ok= check_statement_effects(options, result) && ok;

  result->phase= "prepared_statements";
  ok= check_prepared_statement_api(options, result) && ok;

  if (!ok)
  {
    result->message= "open/close or exec smoke failed";
    return result->status;
  }

  result->phase= "complete";
  result->status= 0;
  result->message= "ok";
  return result->status;
}

static int run_readonly_smoke(const SmokeOptions &options, SmokeResult *result)
{
  bool ok= true;

  result->phase= "readonly_existing_database";
  ok= check_readonly_existing_database(options, result) && ok;

  if (!ok)
  {
    result->message= "read-only smoke failed";
    return result->status;
  }

  result->phase= "complete";
  result->status= 0;
  result->message= "ok";
  return result->status;
}

static int run_exclusive_smoke(const SmokeOptions &options, SmokeResult *result)
{
  bool ok= true;

  result->phase= "exclusive_open";
  ok= check_exclusive_open(options, result) && ok;

  if (!ok)
  {
    result->message= "exclusive smoke failed";
    return result->status;
  }

  result->phase= "complete";
  result->status= 0;
  result->message= "ok";
  return result->status;
}

static int run_uri_smoke(const SmokeOptions &options, SmokeResult *result)
{
  bool ok= true;

  result->phase= "uri_open";
  ok= check_uri_open(options, result) && ok;

  if (!ok)
  {
    result->message= "URI smoke failed";
    return result->status;
  }

  result->phase= "complete";
  result->status= 0;
  result->message= "ok";
  return result->status;
}

static int run_uri_readonly_smoke(const SmokeOptions &options,
                                  SmokeResult *result)
{
  bool ok= true;

  result->phase= "uri_readonly_open";
  ok= check_uri_readonly_open(options, result) && ok;

  if (!ok)
  {
    result->message= "URI read-only smoke failed";
    return result->status;
  }

  result->phase= "complete";
  result->status= 0;
  result->message= "ok";
  return result->status;
}

static bool check_close_null(SmokeResult *result)
{
  const int rc= mylite_close(nullptr);
  return record_result(result, "close_null", MYLITE_OK, rc, nullptr);
}

static bool check_null_out_db(const SmokeOptions &options, SmokeResult *result)
{
  const int rc= mylite_open(options.database.c_str(), nullptr);
  return record_result(result, "null_out_db", MYLITE_MISUSE, rc, nullptr);
}

static bool check_null_filename(SmokeResult *result)
{
  mylite_db *db= nullptr;
  const int rc= mylite_open(nullptr, &db);
  const bool ok= record_result(result, "null_filename", MYLITE_MISUSE, rc, db);
  mylite_close(db);
  return ok;
}

static bool check_readonly_missing(const SmokeOptions &options,
                                   SmokeResult *result)
{
  const std::string missing_dir= options.database + ".readonly-missing-dir";
  const std::string missing= missing_dir + "/missing.mylite";
  unlink(missing.c_str());
  rmdir(missing_dir.c_str());

  mylite_db *db= nullptr;
  const int rc= mylite_open_v2(missing.c_str(), &db, MYLITE_OPEN_READONLY,
                               nullptr);
  bool ok= record_result(result, "readonly_missing", MYLITE_CANTOPEN, rc,
                         db);
  if (access(missing_dir.c_str(), F_OK) == 0)
    ok= false;
  mylite_close(db);
  return ok;
}

static bool check_bad_profile(const SmokeOptions &options,
                              SmokeResult *result)
{
  mylite_db *db= nullptr;
  const int rc= mylite_open_v2(options.database.c_str(), &db,
                               MYLITE_OPEN_READWRITE | MYLITE_OPEN_CREATE,
                               "strict");
  const bool ok= record_result(result, "bad_profile", MYLITE_MISUSE, rc, db);
  mylite_close(db);
  return ok;
}

static bool check_bad_flags(const SmokeOptions &options, SmokeResult *result)
{
  mylite_db *db= nullptr;
  const int rc= mylite_open_v2(options.database.c_str(), &db,
                               MYLITE_OPEN_READONLY | MYLITE_OPEN_READWRITE,
                               nullptr);
  const bool ok= record_result(result, "bad_flags", MYLITE_MISUSE, rc, db);
  mylite_close(db);
  return ok;
}

static bool check_default_open_close(const SmokeOptions &options,
                                     SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "default_open", MYLITE_OK, rc, db);
  if (db)
  {
    rc= mylite_close(db);
    ok= record_result(result, "default_close", MYLITE_OK, rc, nullptr) && ok;
  }
  return ok;
}

static bool check_repeated_open_close(const SmokeOptions &options,
                                      SmokeResult *result)
{
  bool ok= true;
  for (int i= 0; i < 2; ++i)
  {
    mylite_db *db= nullptr;
    int rc= mylite_open(options.database.c_str(), &db);
    const std::string open_label= std::string("repeated_open_") +
                                  static_cast<char>('1' + i);
    ok= record_result(result, open_label.c_str(), MYLITE_OK, rc, db) && ok;
    if (db)
    {
      rc= mylite_close(db);
      const std::string close_label= std::string("repeated_close_") +
                                     static_cast<char>('1' + i);
      ok= record_result(result, close_label.c_str(), MYLITE_OK, rc,
                        nullptr) && ok;
    }
  }
  return ok;
}

static bool check_same_path_multi_handle(const SmokeOptions &options,
                                         SmokeResult *result)
{
  mylite_db *first= nullptr;
  mylite_db *second= nullptr;

  int rc= mylite_open(options.database.c_str(), &first);
  bool ok= record_result(result, "same_path_first_open", MYLITE_OK, rc,
                         first);

  rc= mylite_open(options.database.c_str(), &second);
  ok= record_result(result, "same_path_second_open", MYLITE_OK, rc,
                    second) && ok;

  if (second)
  {
    rc= mylite_close(second);
    ok= record_result(result, "same_path_second_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  if (first)
  {
    rc= mylite_close(first);
    ok= record_result(result, "same_path_first_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_different_path_busy(const SmokeOptions &options,
                                      SmokeResult *result)
{
  mylite_db *first= nullptr;
  mylite_db *second= nullptr;
  const std::string other= options.database + ".other";

  int rc= mylite_open(options.database.c_str(), &first);
  bool ok= record_result(result, "busy_first_open", MYLITE_OK, rc, first);

  rc= mylite_open(other.c_str(), &second);
  ok= record_result(result, "busy_second_open", MYLITE_BUSY, rc, second) &&
      ok;

  mylite_close(second);
  if (first)
  {
    rc= mylite_close(first);
    ok= record_result(result, "busy_first_close", MYLITE_OK, rc, nullptr) &&
        ok;
  }
  return ok;
}

static bool check_exec_misuse(const SmokeOptions &options,
                              SmokeResult *result)
{
  bool ok= true;
  char *errmsg= nullptr;
  int rc= mylite_exec(nullptr, "SELECT 1", nullptr, nullptr, &errmsg);
  if (errmsg)
  {
    result->exec_null_db_message= errmsg;
    mylite_free(errmsg);
  }
  ok= record_result(result, "exec_null_db", MYLITE_MISUSE, rc, nullptr) &&
      ok;
  if (result->exec_null_db_message != "bad database handle")
    ok= false;

  unsigned warning_level= 99;
  unsigned warning_code= 99;
  const char *warning_message= "not cleared";
  rc= mylite_warning(nullptr, 0, &warning_level, &warning_code,
                     &warning_message);
  ok= record_result(result, "warning_null_db", MYLITE_MISUSE, rc,
                    nullptr) && ok;
  if (warning_level != 0 || warning_code != 0 || warning_message != nullptr)
    ok= false;

  mylite_db *db= nullptr;
  rc= mylite_open(options.database.c_str(), &db);
  ok= record_result(result, "exec_misuse_open", MYLITE_OK, rc, db) && ok;
  if (db)
  {
    rc= mylite_exec(db, nullptr, nullptr, nullptr, nullptr);
    ok= record_result(result, "exec_null_sql", MYLITE_MISUSE, rc, db) &&
        ok;
    rc= mylite_warning(db, 0, nullptr, &warning_code, &warning_message);
    result->warning_misuse_message= mylite_errmsg(db);
    ok= record_result(result, "warning_missing_output", MYLITE_MISUSE, rc,
                      db) && ok;
    if (result->warning_misuse_message !=
        "warning output pointers are required")
      ok= false;
    rc= mylite_close(db);
    ok= record_result(result, "exec_misuse_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  mylite_free(nullptr);
  return ok;
}

static bool check_exec_scalar(const SmokeOptions &options,
                              SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "exec_scalar_open", MYLITE_OK, rc, db);
  if (db)
  {
    ExecCapture capture;
    ok= exec_query_capture(db, "SELECT 1 + 2 AS total, NULL AS empty",
                           "exec_scalar_select", &capture, result) && ok;
    result->exec_scalar_columns= join_strings(capture.columns, ",");
    result->exec_scalar_rows= join_strings(capture.rows, ",");
    if (result->exec_scalar_columns != "total,empty" ||
        result->exec_scalar_rows != "3:NULL")
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "exec_scalar_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_collation_profile(const SmokeOptions &options,
                                    SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "collation_profile_open", MYLITE_OK, rc, db);
  if (db)
  {
    ExecCapture capture;
#ifdef MYLITE_DISABLE_UCA_COLLATIONS
    ok= exec_query_capture(
          db,
          "SELECT @@collation_server, "
          "_utf8mb4'a' COLLATE utf8mb4_general_ci = _utf8mb4'a'",
          "collation_profile_select", &capture, result) && ok;
    result->exec_collation_rows= join_strings(capture.rows, ",");
    if (result->exec_collation_rows != "utf8mb4_general_ci:1")
      ok= false;

    char *errmsg= nullptr;
    rc= mylite_exec(db,
                    "SELECT _utf8mb4'a' COLLATE utf8mb4_uca1400_ai_ci",
                    nullptr, nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_uca_collation_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "collation_uca1400_select", MYLITE_ERROR, rc,
                      db) && ok;
    if (mylite_mariadb_errno(db) != ER_UNKNOWN_COLLATION ||
        result->exec_uca_collation_message.find("utf8mb4_uca1400_ai_ci") ==
          std::string::npos)
      ok= false;

#ifdef MYLITE_DISABLE_GENERAL1400_COLLATIONS
    errmsg= nullptr;
    rc= mylite_exec(db,
                    "SELECT _utf8mb4'a' COLLATE utf8mb4_general1400_as_ci",
                    nullptr, nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_general1400_collation_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "collation_general1400_select", MYLITE_ERROR,
                      rc, db) && ok;
    if (mylite_mariadb_errno(db) != ER_UNKNOWN_COLLATION ||
        result->exec_general1400_collation_message.find(
          "utf8mb4_general1400_as_ci") == std::string::npos)
      ok= false;
#endif
#else
    ok= exec_query_capture(
          db,
          "SELECT @@collation_server, "
          "_utf8mb4'a' COLLATE utf8mb4_uca1400_ai_ci = _utf8mb4'a'",
          "collation_profile_select", &capture, result) && ok;
    result->exec_collation_rows= join_strings(capture.rows, ",");
    if (result->exec_collation_rows != "utf8mb4_uca1400_ai_ci:1")
      ok= false;
#endif

    rc= mylite_close(db);
    ok= record_result(result, "collation_profile_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_locale_profile(const SmokeOptions &options,
                                 SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "locale_profile_open", MYLITE_OK, rc, db);
  if (db)
  {
    ok= exec_statement(db, "SET lc_time_names='en_US'",
                       "locale_profile_set_time_en_us", result) && ok;
    ok= exec_statement(db, "SET lc_messages='en_US'",
                       "locale_profile_set_messages_en_us", result) && ok;

    ExecCapture capture;
    ok= exec_query_capture(
          db,
          "SELECT @@lc_time_names, "
          "DATE_FORMAT('2024-01-02','%M:%W'), "
          "FORMAT(1234.5, 2, 'en_US')",
          "locale_profile_select", &capture, result) && ok;
    result->exec_locale_profile_rows= join_strings(capture.rows, ",");
    if (result->exec_locale_profile_rows !=
        "en_US:January:Tuesday:1,234.50")
      ok= false;

#ifdef MYLITE_DISABLE_EXTRA_LOCALES
    char *errmsg= nullptr;
    rc= mylite_exec(db, "SET lc_time_names='de_DE'", nullptr, nullptr,
                    &errmsg);
    if (errmsg)
    {
      result->exec_locale_removed_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "locale_profile_set_time_removed",
                      MYLITE_ERROR, rc, db) && ok;
    if (mylite_mariadb_errno(db) != ER_UNKNOWN_LOCALE ||
        result->exec_locale_removed_message.find("de_DE") ==
          std::string::npos)
      ok= false;
#endif

    rc= mylite_close(db);
    ok= record_result(result, "locale_profile_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_oracle_mode_unsupported(const SmokeOptions &options,
                                          SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "oracle_mode_open", MYLITE_OK, rc, db);
  if (db)
  {
    ok= exec_statement(db, "SET sql_mode=ORACLE", "oracle_mode_set",
                       result) && ok;

    char *errmsg= nullptr;
    rc= mylite_exec(db, "SELECT 1", nullptr, nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_oracle_mode_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "oracle_mode_select", MYLITE_ERROR, rc,
                      db) && ok;
    if (mylite_mariadb_errno(db) != ER_NOT_SUPPORTED_YET ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_oracle_mode_message.find("Oracle SQL mode") ==
          std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "oracle_mode_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_oracle_functions_unsupported(const SmokeOptions &options,
                                               SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "oracle_function_open", MYLITE_OK, rc, db);
  if (db)
  {
    ExecCapture standard_capture;
    ok= exec_query_capture(
          db,
          "SELECT CONCAT('a','b'), LPAD('x',2,'0'), RPAD('x',2,'0'), "
          "LTRIM(' x'), RTRIM('x '), SUBSTR('abc',2,1), "
          "REPLACE('abc','b','x'), TRIM(' x ')",
          "oracle_function_standard", &standard_capture, result) && ok;
    result->exec_oracle_function_standard_rows=
      join_strings(standard_capture.rows, ",");
    if (result->exec_oracle_function_standard_rows !=
        "ab:0x:x0:x:x:b:axc:x")
      ok= false;

    char *errmsg= nullptr;
    rc= mylite_exec(db, "SELECT DECODE_ORACLE(1,1,10)", nullptr,
                    nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_oracle_function_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "oracle_decode_function_select",
                      MYLITE_ERROR, rc, db) && ok;
    if (mylite_mariadb_errno(db) != ER_SP_DOES_NOT_EXIST ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_oracle_function_message.find("DECODE_ORACLE") ==
          std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "oracle_function_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_xml_functions_unsupported(const SmokeOptions &options,
                                            SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "xml_function_open", MYLITE_OK, rc, db);
  if (db)
  {
    char *errmsg= nullptr;
    rc= mylite_exec(db, "SELECT EXTRACTVALUE('<a>x</a>', '/a')",
                    nullptr, nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_xml_extractvalue_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "xml_extractvalue_select", MYLITE_ERROR, rc,
                      db) && ok;
    if (mylite_mariadb_errno(db) != ER_SP_DOES_NOT_EXIST ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_xml_extractvalue_message.find("EXTRACTVALUE") ==
          std::string::npos)
      ok= false;

    errmsg= nullptr;
    rc= mylite_exec(db,
                    "SELECT UPDATEXML('<a>x</a>', '/a', '<b>y</b>')",
                    nullptr, nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_xml_updatexml_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "xml_updatexml_select", MYLITE_ERROR, rc,
                      db) && ok;
    if (mylite_mariadb_errno(db) != ER_SP_DOES_NOT_EXIST ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_xml_updatexml_message.find("UPDATEXML") ==
          std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "xml_function_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_gis_functions_unsupported(const SmokeOptions &options,
                                            SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "gis_function_open", MYLITE_OK, rc, db);
  if (db)
  {
    char *errmsg= nullptr;
    rc= mylite_exec(db, "SELECT ST_ASTEXT(0x00)", nullptr, nullptr,
                    &errmsg);
    if (errmsg)
    {
      result->exec_gis_function_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "gis_st_astext_select", MYLITE_ERROR, rc,
                      db) && ok;
    if (mylite_mariadb_errno(db) != ER_SP_DOES_NOT_EXIST ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_gis_function_message.find("ST_ASTEXT") ==
          std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "gis_function_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_vector_functions_unsupported(const SmokeOptions &options,
                                               SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "vector_function_open", MYLITE_OK, rc, db);
  if (db)
  {
    char *errmsg= nullptr;
    rc= mylite_exec(db, "SELECT VEC_FROMTEXT('[1,2]')", nullptr, nullptr,
                    &errmsg);
    if (errmsg)
    {
      result->exec_vector_fromtext_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "vector_fromtext_select", MYLITE_ERROR, rc,
                      db) && ok;
    if (mylite_mariadb_errno(db) != ER_SP_DOES_NOT_EXIST ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_vector_fromtext_message.find("VEC_FROMTEXT") ==
          std::string::npos)
      ok= false;

    errmsg= nullptr;
    rc= mylite_exec(db, "SELECT VEC_DISTANCE(x'0000803F', x'0000803F')",
                    nullptr, nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_vector_distance_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "vector_distance_select", MYLITE_ERROR, rc,
                      db) && ok;
    if (mylite_mariadb_errno(db) != ER_SP_DOES_NOT_EXIST ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_vector_distance_message.find("VEC_DISTANCE") ==
          std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "vector_function_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_json_schema_valid_unsupported(const SmokeOptions &options,
                                                SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "json_schema_valid_open", MYLITE_OK, rc, db);
  if (db)
  {
    ExecCapture capture;
    ok= exec_query_capture(db, "SELECT JSON_VALID('{\"ok\":1}') AS ok",
                           "json_valid_select", &capture, result) && ok;
    result->exec_json_valid_rows= join_strings(capture.rows, ",");
    if (result->exec_json_valid_rows != "1")
      ok= false;

    char *errmsg= nullptr;
    rc= mylite_exec(db,
                    "SELECT JSON_SCHEMA_VALID('{\"type\":\"object\"}', '{}')",
                    nullptr, nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_json_schema_valid_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "json_schema_valid_select", MYLITE_ERROR, rc,
                      db) && ok;
    if (mylite_mariadb_errno(db) != ER_SP_DOES_NOT_EXIST ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_json_schema_valid_message.find("JSON_SCHEMA_VALID") ==
          std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "json_schema_valid_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_json_table_unsupported(const SmokeOptions &options,
                                         SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "json_table_open", MYLITE_OK, rc, db);
  if (db)
  {
    char *errmsg= nullptr;
    rc= mylite_exec(
          db,
          "SELECT jt.id FROM JSON_TABLE('[1]', '$[*]' "
          "COLUMNS(id INT PATH '$')) AS jt",
          nullptr, nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_json_table_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "json_table_select", MYLITE_ERROR, rc, db) &&
        ok;
    if (mylite_mariadb_errno(db) != ER_NOT_SUPPORTED_YET ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_json_table_message.find("JSON_TABLE") ==
          std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "json_table_close", MYLITE_OK, rc, nullptr) &&
        ok;
  }
  return ok;
}

static bool check_regex_functions_unsupported(const SmokeOptions &options,
                                              SmokeResult *result)
{
  struct UnsupportedRegex
  {
    const char *label;
    const char *sql;
    const char *name;
    unsigned expected_errno;
  };

  static const UnsupportedRegex statements[] = {
    {"regex_operator", "SELECT 'abc' REGEXP 'a'", "REGEXP",
     ER_NOT_SUPPORTED_YET},
    {"regex_rlike_operator", "SELECT 'abc' RLIKE 'a'", "REGEXP",
     ER_NOT_SUPPORTED_YET},
    {"regex_instr_function", "SELECT REGEXP_INSTR('abc','a')",
     "REGEXP_INSTR", ER_SP_DOES_NOT_EXIST},
    {"regex_replace_function", "SELECT REGEXP_REPLACE('abc','a','x')",
     "REGEXP_REPLACE", ER_SP_DOES_NOT_EXIST},
    {"regex_substr_function", "SELECT REGEXP_SUBSTR('abc','a')",
     "REGEXP_SUBSTR", ER_SP_DOES_NOT_EXIST},
  };

  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "regex_function_open", MYLITE_OK, rc, db);
  if (db)
  {
    ExecCapture like_capture;
    ok= exec_query_capture(db, "SELECT 'abc' LIKE 'a%'",
                           "regex_like_select", &like_capture, result) && ok;
    result->exec_regex_like_rows= join_strings(like_capture.rows, ",");
    if (result->exec_regex_like_rows != "1")
      ok= false;

    std::vector<std::string> messages;
    for (size_t i= 0; i < sizeof(statements) / sizeof(statements[0]); ++i)
    {
      char *errmsg= nullptr;
      rc= mylite_exec(db, statements[i].sql, nullptr, nullptr, &errmsg);
      std::string message= errmsg ? errmsg : mylite_errmsg(db);
      if (errmsg)
        mylite_free(errmsg);
      messages.push_back(std::string(statements[i].name) + "=" + message);

      ok= record_result(result, statements[i].label, MYLITE_ERROR, rc,
                        db) && ok;
      if (mylite_mariadb_errno(db) != statements[i].expected_errno ||
          std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
          message.find(statements[i].name) == std::string::npos)
        ok= false;
    }
    result->exec_regex_messages= join_strings(messages, " | ");

    rc= mylite_close(db);
    ok= record_result(result, "regex_function_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_window_functions_unsupported(const SmokeOptions &options,
                                               SmokeResult *result)
{
  struct UnsupportedWindow
  {
    const char *label;
    const char *sql;
    const char *name;
  };

  static const UnsupportedWindow statements[] = {
    {"window_row_number", "SELECT ROW_NUMBER() OVER ()", "window functions"},
    {"window_sum_over", "SELECT SUM(1) OVER ()", "window functions"},
    {"window_named_clause",
     "SELECT x FROM (SELECT 1 AS x) AS t WINDOW w AS (ORDER BY x)",
     "window functions"},
  };

  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "window_function_open", MYLITE_OK, rc, db);
  if (db)
  {
    ExecCapture aggregate_capture;
    ok= exec_query_capture(db, "SELECT COUNT(*), SUM(1)",
                           "window_plain_aggregate", &aggregate_capture,
                           result) && ok;
    result->exec_window_aggregate_rows=
      join_strings(aggregate_capture.rows, ",");
    if (result->exec_window_aggregate_rows != "1:1")
      ok= false;

    std::vector<std::string> messages;
    for (size_t i= 0; i < sizeof(statements) / sizeof(statements[0]); ++i)
    {
      char *errmsg= nullptr;
      rc= mylite_exec(db, statements[i].sql, nullptr, nullptr, &errmsg);
      std::string message= errmsg ? errmsg : mylite_errmsg(db);
      if (errmsg)
        mylite_free(errmsg);
      messages.push_back(std::string(statements[i].label) + "=" + message);

      ok= record_result(result, statements[i].label, MYLITE_ERROR, rc,
                        db) && ok;
      if (mylite_mariadb_errno(db) != ER_NOT_SUPPORTED_YET ||
          std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
          message.find(statements[i].name) == std::string::npos)
        ok= false;
    }
    result->exec_window_function_messages= join_strings(messages, " | ");

    rc= mylite_close(db);
    ok= record_result(result, "window_function_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_binlog_replication_unsupported(const SmokeOptions &options,
                                                 SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "binlog_replication_open", MYLITE_OK, rc,
                         db);
  if (db)
  {
    char *errmsg= nullptr;
    rc= mylite_exec(db, "BINLOG 'ZmFrZQ=='", nullptr, nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_binlog_replication_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "binlog_statement", MYLITE_ERROR, rc, db) &&
        ok;
    if (mylite_mariadb_errno(db) != ER_OPTION_PREVENTS_STATEMENT ||
        std::strcmp(mylite_sqlstate(db), "HY000") != 0 ||
        result->exec_binlog_replication_message.find("embedded option") ==
          std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "binlog_replication_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_xa_transactions_unsupported(const SmokeOptions &options,
                                              SmokeResult *result)
{
  struct XaAttempt
  {
    const char *label;
    const char *sql;
  };

  const XaAttempt attempts[]=
  {
    {"xa_start", "XA START 'mylite-xa'"},
    {"xa_recover", "XA RECOVER"}
  };

  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "xa_transaction_open", MYLITE_OK, rc, db);
  if (db)
  {
    std::vector<std::string> messages;
    for (size_t i= 0; i < sizeof(attempts) / sizeof(attempts[0]); ++i)
    {
      char *errmsg= nullptr;
      rc= mylite_exec(db, attempts[i].sql, nullptr, nullptr, &errmsg);
      const std::string message= errmsg ? errmsg : mylite_errmsg(db);
      if (errmsg)
        mylite_free(errmsg);

      ok= record_result(result, attempts[i].label, MYLITE_ERROR, rc, db) &&
          ok;
      if (mylite_mariadb_errno(db) != ER_OPTION_PREVENTS_STATEMENT ||
          std::strcmp(mylite_sqlstate(db), "HY000") != 0 ||
          message.find("embedded option") == std::string::npos)
        ok= false;
      messages.push_back(message);
    }
    result->exec_xa_transaction_messages= join_strings(messages, " | ");

    rc= mylite_close(db);
    ok= record_result(result, "xa_transaction_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_server_encryption_profile(const SmokeOptions &options,
                                            SmokeResult *result)
{
  struct SetAttempt
  {
    const char *label;
    const char *sql;
    const char *name;
  };

  static const SetAttempt set_attempts[] = {
    {"server_encryption_set_binlog", "SET GLOBAL encrypt_binlog=ON",
     "encrypt_binlog"},
    {"server_encryption_set_tmp_files", "SET GLOBAL encrypt_tmp_files=ON",
     "encrypt_tmp_files"},
    {"server_encryption_set_tmp_disk_tables",
     "SET GLOBAL encrypt_tmp_disk_tables=ON", "encrypt_tmp_disk_tables"}
  };

  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "server_encryption_open", MYLITE_OK, rc, db);
  if (db)
  {
    ExecCapture capture;
    ok= exec_query_capture(
          db,
          "SELECT IF(@@global.encrypt_binlog, '1', '0'), "
          "IF(@@global.encrypt_tmp_files, '1', '0'), "
          "IF(@@global.encrypt_tmp_disk_tables, '1', '0')",
          "server_encryption_disabled", &capture, result) && ok;
    result->exec_server_encryption_rows= join_strings(capture.rows, ",");
    if (result->exec_server_encryption_rows != "0:0:0")
      ok= false;

    std::vector<std::string> messages;
    for (size_t i= 0; i < sizeof(set_attempts) / sizeof(set_attempts[0]); ++i)
    {
      char *errmsg= nullptr;
      rc= mylite_exec(db, set_attempts[i].sql, nullptr, nullptr, &errmsg);
      std::string message= errmsg ? errmsg : mylite_errmsg(db);
      if (errmsg)
        mylite_free(errmsg);
      messages.push_back(std::string(set_attempts[i].name) + "=" + message);

      ok= record_result(result, set_attempts[i].label, MYLITE_ERROR, rc,
                        db) && ok;
      if (message.find("read only") == std::string::npos &&
          message.find("read-only") == std::string::npos)
        ok= false;
    }
    result->exec_server_encryption_set_messages= join_strings(messages, " | ");

    ExecCapture after_capture;
    ok= exec_query_capture(
          db,
          "SELECT IF(@@global.encrypt_binlog, '1', '0'), "
          "IF(@@global.encrypt_tmp_files, '1', '0'), "
          "IF(@@global.encrypt_tmp_disk_tables, '1', '0')",
          "server_encryption_still_disabled", &after_capture, result) && ok;
    if (join_strings(after_capture.rows, ",") != "0:0:0")
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "server_encryption_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_proxy_protocol_profile(const SmokeOptions &options,
                                         SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "proxy_protocol_open", MYLITE_OK, rc, db);
  if (db)
  {
    ExecCapture capture;
    ok= exec_query_capture(db,
                           "SHOW GLOBAL VARIABLES LIKE "
                           "'proxy_protocol_networks'",
                           "proxy_protocol_networks_show", &capture,
                           result) && ok;
    result->exec_proxy_protocol_rows= join_strings(capture.rows, ",");
    if (result->exec_proxy_protocol_rows != "proxy_protocol_networks:")
      ok= false;

    char *errmsg= nullptr;
    rc= mylite_exec(db, "SET GLOBAL proxy_protocol_networks='*'",
                    nullptr, nullptr, &errmsg);
    result->exec_proxy_protocol_set_message=
      errmsg ? errmsg : mylite_errmsg(db);
    if (errmsg)
      mylite_free(errmsg);

    ok= record_result(result, "proxy_protocol_networks_set",
                      MYLITE_ERROR, rc, db) && ok;
    if (mylite_mariadb_errno(db) != ER_WRONG_VALUE_FOR_VAR ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_proxy_protocol_set_message.find(
          "proxy_protocol_networks") == std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "proxy_protocol_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_server_utility_functions_unsupported(
  const SmokeOptions &options, SmokeResult *result)
{
  struct UnsupportedFunction
  {
    const char *label;
    const char *sql;
    const char *name;
  };

  static const UnsupportedFunction functions[] = {
    {"server_utility_benchmark", "SELECT BENCHMARK(1,1+1)", "BENCHMARK"},
    {"server_utility_binlog_gtid_pos",
     "SELECT BINLOG_GTID_POS('binlog.000001', 4)", "BINLOG_GTID_POS"},
    {"server_utility_get_lock", "SELECT GET_LOCK('mylite',0)", "GET_LOCK"},
    {"server_utility_is_free_lock", "SELECT IS_FREE_LOCK('mylite')",
     "IS_FREE_LOCK"},
    {"server_utility_is_used_lock", "SELECT IS_USED_LOCK('mylite')",
     "IS_USED_LOCK"},
    {"server_utility_load_file", "SELECT LOAD_FILE('/tmp/mylite-none')",
     "LOAD_FILE"},
    {"server_utility_master_gtid_wait",
     "SELECT MASTER_GTID_WAIT('0-1-1',0)", "MASTER_GTID_WAIT"},
    {"server_utility_master_pos_wait",
     "SELECT MASTER_POS_WAIT('binlog.000001',4,0)", "MASTER_POS_WAIT"},
    {"server_utility_release_all_locks", "SELECT RELEASE_ALL_LOCKS()",
     "RELEASE_ALL_LOCKS"},
    {"server_utility_release_lock", "SELECT RELEASE_LOCK('mylite')",
     "RELEASE_LOCK"},
    {"server_utility_sleep", "SELECT SLEEP(0)", "SLEEP"},
    {"server_utility_uuid_short", "SELECT UUID_SHORT()", "UUID_SHORT"},
  };

  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "server_utility_function_open",
                         MYLITE_OK, rc, db);
  if (db)
  {
    ExecCapture standard_capture;
    ok= exec_query_capture(
          db,
          "SELECT LENGTH(VERSION()) > 0, CONNECTION_ID() >= 0",
          "server_utility_standard", &standard_capture, result) && ok;
    result->exec_server_utility_standard_rows=
      join_strings(standard_capture.rows, ",");
    if (result->exec_server_utility_standard_rows != "1:1")
      ok= false;

    std::vector<std::string> messages;
    for (size_t i= 0; i < sizeof(functions) / sizeof(functions[0]); ++i)
    {
      char *errmsg= nullptr;
      rc= mylite_exec(db, functions[i].sql, nullptr, nullptr, &errmsg);
      std::string message= errmsg ? errmsg : mylite_errmsg(db);
      if (errmsg)
        mylite_free(errmsg);
      messages.push_back(std::string(functions[i].name) + "=" + message);

      ok= record_result(result, functions[i].label, MYLITE_ERROR, rc,
                        db) && ok;
      if (mylite_mariadb_errno(db) != ER_SP_DOES_NOT_EXIST ||
          std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
          message.find(functions[i].name) == std::string::npos)
        ok= false;
    }
    result->exec_server_utility_messages= join_strings(messages, " | ");

    rc= mylite_close(db);
    ok= record_result(result, "server_utility_function_close",
                      MYLITE_OK, rc, nullptr) && ok;
  }
  return ok;
}

static bool check_sql_crypto_functions_unsupported(const SmokeOptions &options,
                                                   SmokeResult *result)
{
  struct UnsupportedFunction
  {
    const char *label;
    const char *sql;
    const char *name;
    int expected_errno;
  };

  static const UnsupportedFunction functions[] = {
    {"sql_crypto_aes_encrypt", "SELECT AES_ENCRYPT('mylite','key')",
     "AES_ENCRYPT", ER_SP_DOES_NOT_EXIST},
    {"sql_crypto_aes_decrypt", "SELECT AES_DECRYPT('mylite','key')",
     "AES_DECRYPT", ER_SP_DOES_NOT_EXIST},
    {"sql_crypto_md5", "SELECT MD5('mylite')", "MD5",
     ER_SP_DOES_NOT_EXIST},
    {"sql_crypto_sha", "SELECT SHA('mylite')", "SHA",
     ER_SP_DOES_NOT_EXIST},
    {"sql_crypto_sha1", "SELECT SHA1('mylite')", "SHA1",
     ER_SP_DOES_NOT_EXIST},
    {"sql_crypto_sha2", "SELECT SHA2('mylite',256)", "SHA2",
     ER_SP_DOES_NOT_EXIST},
    {"sql_crypto_password", "SELECT PASSWORD('mylite')",
     "OpenSSL-backed SQL crypto", ER_NOT_SUPPORTED_YET},
    {"sql_crypto_old_password", "SELECT OLD_PASSWORD('mylite')",
     "OLD_PASSWORD", ER_SP_DOES_NOT_EXIST},
    {"sql_crypto_random_bytes", "SELECT RANDOM_BYTES(4)",
     "RANDOM_BYTES", ER_SP_DOES_NOT_EXIST},
  };

  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "sql_crypto_function_open", MYLITE_OK,
                         rc, db);
  if (db)
  {
    std::vector<std::string> messages;
    for (size_t i= 0; i < sizeof(functions) / sizeof(functions[0]); ++i)
    {
      char *errmsg= nullptr;
      rc= mylite_exec(db, functions[i].sql, nullptr, nullptr, &errmsg);
      std::string message= errmsg ? errmsg : mylite_errmsg(db);
      if (errmsg)
        mylite_free(errmsg);
      messages.push_back(std::string(functions[i].label) + "=" + message);

      ok= record_result(result, functions[i].label, MYLITE_ERROR, rc,
                        db) && ok;
      if (mylite_mariadb_errno(db) != functions[i].expected_errno ||
          std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
          message.find(functions[i].name) == std::string::npos)
        ok= false;
    }
    result->exec_sql_crypto_function_messages=
      join_strings(messages, " | ");

    rc= mylite_close(db);
    ok= record_result(result, "sql_crypto_function_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_crypt_function_unsupported(const SmokeOptions &options,
                                             SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "crypt_function_open",
                         MYLITE_OK, rc, db);
  if (db)
  {
    char *errmsg= nullptr;
    rc= mylite_exec(db, "SELECT ENCRYPT('mylite','aa')", nullptr, nullptr,
                    &errmsg);
    result->exec_crypt_function_message= errmsg ? errmsg : mylite_errmsg(db);
    if (errmsg)
      mylite_free(errmsg);

    ok= record_result(result, "crypt_function_encrypt", MYLITE_ERROR, rc,
                      db) && ok;
    if (mylite_mariadb_errno(db) != ER_SP_DOES_NOT_EXIST ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_crypt_function_message.find("ENCRYPT") ==
          std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "crypt_function_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_des_functions_unsupported(const SmokeOptions &options,
                                            SmokeResult *result)
{
  struct UnsupportedFunction
  {
    const char *label;
    const char *sql;
    const char *name;
  };

  static const UnsupportedFunction functions[] = {
    {"des_function_encrypt", "SELECT DES_ENCRYPT('mylite')", "DES_ENCRYPT"},
    {"des_function_decrypt", "SELECT DES_DECRYPT('mylite')", "DES_DECRYPT"},
  };

  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "des_function_open", MYLITE_OK, rc, db);
  if (db)
  {
    std::vector<std::string> messages;
    for (size_t i= 0; i < sizeof(functions) / sizeof(functions[0]); ++i)
    {
      char *errmsg= nullptr;
      rc= mylite_exec(db, functions[i].sql, nullptr, nullptr, &errmsg);
      std::string message= errmsg ? errmsg : mylite_errmsg(db);
      if (errmsg)
        mylite_free(errmsg);
      messages.push_back(std::string(functions[i].name) + "=" + message);

      ok= record_result(result, functions[i].label, MYLITE_ERROR, rc,
                        db) && ok;
      if (mylite_mariadb_errno(db) != ER_SP_DOES_NOT_EXIST ||
          std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
          message.find(functions[i].name) == std::string::npos)
        ok= false;
    }
    result->exec_des_function_messages= join_strings(messages, " | ");

    rc= mylite_close(db);
    ok= record_result(result, "des_function_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_kdf_function_unsupported(const SmokeOptions &options,
                                           SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "kdf_function_open", MYLITE_OK, rc, db);
  if (db)
  {
    char *errmsg= nullptr;
    rc= mylite_exec(db, "SELECT KDF('mylite','salt')", nullptr, nullptr,
                    &errmsg);
    result->exec_kdf_function_message= errmsg ? errmsg : mylite_errmsg(db);
    if (errmsg)
      mylite_free(errmsg);

    ok= record_result(result, "kdf_function_select", MYLITE_ERROR, rc,
                      db) && ok;
    if (mylite_mariadb_errno(db) != ER_SP_DOES_NOT_EXIST ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_kdf_function_message.find("KDF") == std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "kdf_function_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_zlib_compression_unsupported(const SmokeOptions &options,
                                               SmokeResult *result)
{
  struct UnsupportedFunction
  {
    const char *label;
    const char *sql;
    const char *name;
  };

  static const UnsupportedFunction functions[] = {
    {"zlib_compress_function", "SELECT COMPRESS('mylite')", "COMPRESS"},
    {"zlib_uncompress_function", "SELECT UNCOMPRESS('mylite')",
     "UNCOMPRESS"},
    {"zlib_uncompressed_length_function",
     "SELECT UNCOMPRESSED_LENGTH('mylite')", "UNCOMPRESSED_LENGTH"},
  };

  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "zlib_compression_open",
                         MYLITE_OK, rc, db);
  if (db)
  {
    ExecCapture have_capture;
    ok= exec_query_capture(db, "SHOW VARIABLES LIKE 'have_compress'",
                           "zlib_have_compress", &have_capture, result) &&
        ok;
    result->exec_zlib_compression_have_rows=
      join_strings(have_capture.rows, ",");
    if (result->exec_zlib_compression_have_rows != "have_compress:NO")
      ok= false;

    ExecCapture crc_capture;
    ok= exec_query_capture(db, "SELECT CRC32('mylite')",
                           "zlib_crc32_select", &crc_capture, result) && ok;
    result->exec_zlib_compression_crc32_rows=
      join_strings(crc_capture.rows, ",");
    if (result->exec_zlib_compression_crc32_rows != "2971119272")
      ok= false;

    std::vector<std::string> messages;
    for (size_t i= 0; i < sizeof(functions) / sizeof(functions[0]); ++i)
    {
      char *errmsg= nullptr;
      rc= mylite_exec(db, functions[i].sql, nullptr, nullptr, &errmsg);
      std::string message= errmsg ? errmsg : mylite_errmsg(db);
      if (errmsg)
        mylite_free(errmsg);
      messages.push_back(std::string(functions[i].name) + "=" + message);

      ok= record_result(result, functions[i].label, MYLITE_ERROR, rc,
                        db) && ok;
      if (mylite_mariadb_errno(db) != ER_SP_DOES_NOT_EXIST ||
          std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
          message.find(functions[i].name) == std::string::npos)
        ok= false;
    }
    result->exec_zlib_compression_messages= join_strings(messages, " | ");

    char *errmsg= nullptr;
    rc= mylite_exec(db,
                    "CREATE TABLE mylite.compressed_column_unsupported "
                    "(note VARCHAR(20) COMPRESSED) ENGINE=MYLITE",
                    nullptr, nullptr, &errmsg);
    result->exec_zlib_compressed_column_message=
      errmsg ? errmsg : mylite_errmsg(db);
    if (errmsg)
      mylite_free(errmsg);
    ok= record_result(result, "zlib_compressed_column_create",
                      MYLITE_ERROR, rc, db) && ok;
    if (mylite_mariadb_errno(db) != ER_UNKNOWN_COMPRESSION_METHOD ||
        result->exec_zlib_compressed_column_message.find("zlib") ==
          std::string::npos)
      ok= false;

    ExecCapture compressed_table_capture;
    ok= exec_query_capture(
          db,
          "SHOW TABLES FROM mylite LIKE 'compressed_column_unsupported'",
          "zlib_compressed_column_show", &compressed_table_capture, result) &&
        ok;
    if (!compressed_table_capture.rows.empty())
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "zlib_compression_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_dynamic_plugin_loading_unsupported(
  const SmokeOptions &options, SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "dynamic_plugin_loading_open",
                         MYLITE_OK, rc, db);
  if (db)
  {
    ExecCapture have_capture;
    ok= exec_query_capture(db, "SHOW VARIABLES LIKE 'have_dynamic_loading'",
                           "dynamic_plugin_loading_have", &have_capture,
                           result) && ok;
    result->exec_dynamic_plugin_loading_have_rows=
      join_strings(have_capture.rows, ",");
    if (result->exec_dynamic_plugin_loading_have_rows !=
        "have_dynamic_loading:NO")
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "dynamic_plugin_loading_close",
                      MYLITE_OK, rc, nullptr) && ok;
  }
  return ok;
}

static bool check_query_cache_unsupported(const SmokeOptions &options,
                                          SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "query_cache_open", MYLITE_OK, rc, db);
  if (db)
  {
    ExecCapture have_capture;
    ok= exec_query_capture(db, "SHOW VARIABLES LIKE 'have_query_cache'",
                           "query_cache_have", &have_capture, result) && ok;
    result->exec_query_cache_have_rows= join_strings(have_capture.rows, ",");
    if (result->exec_query_cache_have_rows != "have_query_cache:NO")
      ok= false;

    ExecCapture size_capture;
    ok= exec_query_capture(db, "SHOW VARIABLES LIKE 'query_cache_size'",
                           "query_cache_size", &size_capture, result) && ok;
    result->exec_query_cache_size_rows= join_strings(size_capture.rows, ",");
    if (result->exec_query_cache_size_rows != "query_cache_size:0")
      ok= false;

    ok= exec_statement(db, "SET GLOBAL query_cache_type=ON",
                       "query_cache_type_on", result) && ok;
    ExecCapture type_capture;
    ok= exec_query_capture(db, "SHOW GLOBAL VARIABLES LIKE 'query_cache_type'",
                           "query_cache_type", &type_capture, result) && ok;
    result->exec_query_cache_type_rows= join_strings(type_capture.rows, ",");
    if (result->exec_query_cache_type_rows != "query_cache_type:OFF")
      ok= false;

    ok= exec_statement(db, "SET GLOBAL query_cache_size=1048576",
                       "query_cache_resize", result) && ok;
    ExecCapture resize_capture;
    ok= exec_query_capture(db, "SHOW VARIABLES LIKE 'query_cache_size'",
                           "query_cache_resize_size", &resize_capture,
                           result) && ok;
    result->exec_query_cache_resize_rows=
      join_strings(resize_capture.rows, ",");
    if (result->exec_query_cache_resize_rows != "query_cache_size:0")
      ok= false;

    ExecCapture select_capture;
    ok= exec_query_capture(db, "SELECT SQL_CACHE 1 AS one",
                           "query_cache_select", &select_capture, result) &&
        ok;
    result->exec_query_cache_select_rows=
      join_strings(select_capture.rows, ",");
    if (result->exec_query_cache_select_rows != "1")
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "query_cache_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_profiling_unsupported(const SmokeOptions &options,
                                        SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "profiling_open", MYLITE_OK, rc, db);
  if (db)
  {
    char *errmsg= nullptr;
    rc= mylite_exec(db, "SHOW PROFILES", nullptr, nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_show_profiles_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "profiling_show_profiles", MYLITE_ERROR, rc,
                      db) && ok;
    if (mylite_mariadb_errno(db) != ER_FEATURE_DISABLED ||
        std::strcmp(mylite_sqlstate(db), "HY000") != 0 ||
        result->exec_show_profiles_message.find("SHOW PROFILES") ==
          std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "profiling_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_help_unsupported(const SmokeOptions &options,
                                   SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "help_open", MYLITE_OK, rc, db);
  if (db)
  {
    char *errmsg= nullptr;
    rc= mylite_exec(db, "HELP 'contents'", nullptr, nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_help_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "help_contents", MYLITE_ERROR, rc, db) && ok;
    if (mylite_mariadb_errno(db) != ER_NOT_SUPPORTED_YET ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_help_message.find("HELP command") == std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "help_close", MYLITE_OK, rc, nullptr) && ok;
  }
  return ok;
}

static bool check_procedure_analyse_unsupported(const SmokeOptions &options,
                                                SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "procedure_analyse_open", MYLITE_OK, rc,
                         db);
  if (db)
  {
    ok= exec_statement(db,
                       "DROP TABLE IF EXISTS "
                       "mylite.procedure_analyse_rows",
                       "procedure_analyse_drop_existing", result) && ok;
    ok= exec_statement(db,
                       "CREATE TABLE mylite.procedure_analyse_rows "
                       "(id INT NOT NULL, note VARCHAR(20), PRIMARY KEY(id)) "
                       "ENGINE=MYLITE",
                       "procedure_analyse_create_table", result) && ok;
    ok= exec_statement(db,
                       "INSERT INTO mylite.procedure_analyse_rows VALUES "
                       "(1, 'one')",
                       "procedure_analyse_insert_row", result) && ok;

    char *errmsg= nullptr;
    rc= mylite_exec(db,
                    "SELECT id, note FROM mylite.procedure_analyse_rows "
                    "PROCEDURE ANALYSE()",
                    nullptr, nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_procedure_analyse_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "procedure_analyse_select", MYLITE_ERROR, rc,
                      db) && ok;
    if (mylite_mariadb_errno(db) != ER_NOT_SUPPORTED_YET ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_procedure_analyse_message.find("SELECT PROCEDURE clause") ==
          std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "procedure_analyse_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_table_admin_unsupported(const SmokeOptions &options,
                                          SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "table_admin_open", MYLITE_OK, rc, db);
  if (db)
  {
    ok= exec_statement(db,
                       "DROP TABLE IF EXISTS mylite.table_admin_rows",
                       "table_admin_drop_existing", result) && ok;
    ok= exec_statement(db,
                       "CREATE TABLE mylite.table_admin_rows "
                       "(id INT NOT NULL, note VARCHAR(20), PRIMARY KEY(id)) "
                       "ENGINE=MYLITE",
                       "table_admin_create_table", result) && ok;
    ok= exec_statement(db,
                       "INSERT INTO mylite.table_admin_rows VALUES "
                       "(1, 'one')",
                       "table_admin_insert_row", result) && ok;

    struct AdminCase
    {
      const char *label;
      const char *sql;
      const char *message_fragment;
    };

    static const AdminCase cases[] = {
      {"analyze_table", "ANALYZE TABLE mylite.table_admin_rows",
       "ANALYZE TABLE"},
      {"check_table", "CHECK TABLE mylite.table_admin_rows", "CHECK TABLE"},
      {"optimize_table", "OPTIMIZE TABLE mylite.table_admin_rows",
       "OPTIMIZE TABLE"},
      {"repair_table", "REPAIR TABLE mylite.table_admin_rows",
       "REPAIR TABLE"},
      {"cache_index", "CACHE INDEX mylite.table_admin_rows IN DEFAULT",
       "CACHE INDEX"},
      {"load_index", "LOAD INDEX INTO CACHE mylite.table_admin_rows",
       "LOAD INDEX INTO CACHE"}
    };

    std::vector<std::string> messages;
    for (const AdminCase &admin_case : cases)
    {
      char *errmsg= nullptr;
      rc= mylite_exec(db, admin_case.sql, nullptr, nullptr, &errmsg);
      std::string message= errmsg ? errmsg : mylite_errmsg(db);
      if (errmsg)
        mylite_free(errmsg);
      messages.push_back(std::string(admin_case.label) + ":" + message);

      const std::string label= std::string("table_admin_") +
        admin_case.label;
      ok= record_result(result, label.c_str(), MYLITE_ERROR, rc, db) && ok;
      if (mylite_mariadb_errno(db) != ER_NOT_SUPPORTED_YET ||
          std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
          message.find(admin_case.message_fragment) == std::string::npos)
        ok= false;
    }

    result->exec_table_admin_messages= join_strings(messages, "|");

    rc= mylite_close(db);
    ok= record_result(result, "table_admin_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_sql_sequence_unsupported(const SmokeOptions &options,
                                           SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "sql_sequence_open", MYLITE_OK, rc, db);
  if (db)
  {
    struct SequenceCase
    {
      const char *label;
      const char *sql;
    };

    SequenceCase cases[]=
    {
      { "create_sequence",
        "CREATE SEQUENCE mylite.unsupported_sequence" },
      { "create_table_sequence_option",
        "CREATE TABLE mylite.unsupported_sequence_table "
        "(next_not_cached_value BIGINT NOT NULL) ENGINE=MYLITE SEQUENCE=1" },
      { "next_value_for",
        "SELECT NEXT VALUE FOR mylite.unsupported_sequence" },
      { "nextval_function",
        "SELECT NEXTVAL(mylite.unsupported_sequence)" },
      { "lastval_function",
        "SELECT LASTVAL(mylite.unsupported_sequence)" },
      { "setval_function",
        "SELECT SETVAL(mylite.unsupported_sequence, 1)" }
    };

    std::vector<std::string> messages;
    for (const SequenceCase &sequence_case : cases)
    {
      char *errmsg= nullptr;
      rc= mylite_exec(db, sequence_case.sql, nullptr, nullptr, &errmsg);

      std::string message;
      if (errmsg)
      {
        message= errmsg;
        mylite_free(errmsg);
      }
      messages.push_back(std::string(sequence_case.label) + ":" + message);

      const std::string label= std::string("sql_sequence_") +
        sequence_case.label;
      ok= record_result(result, label.c_str(), MYLITE_ERROR, rc, db) && ok;
      if (mylite_mariadb_errno(db) == 0 ||
          (message.find("SEQUENCE") == std::string::npos &&
           message.find("sequence") == std::string::npos))
        ok= false;
    }

    result->exec_sequence_messages= join_strings(messages, "|");

    rc= mylite_close(db);
    ok= record_result(result, "sql_sequence_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_legacy_storage_engines_unsupported(
  const SmokeOptions &options, SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "legacy_engines_open", MYLITE_OK, rc, db);
  if (db)
  {
    ok= exec_statement(db, "SET SESSION sql_mode='NO_ENGINE_SUBSTITUTION'",
                       "legacy_engines_sql_mode", result) && ok;

    struct EngineCase
    {
      const char *name;
      const char *label;
      std::string *message;
    };

    EngineCase engines[]=
    {
      { "CSV", "csv", &result->exec_csv_engine_message },
      { "MyISAM", "myisam", &result->exec_myisam_engine_message },
      { "MRG_MyISAM", "mrg_myisam",
        &result->exec_mrg_myisam_engine_message }
    };

    for (EngineCase &engine : engines)
    {
      const std::string sql= std::string("CREATE TABLE mylite.") +
        engine.label + "_engine_rows (id INT) ENGINE=" + engine.name;
      char *errmsg= nullptr;
      rc= mylite_exec(db, sql.c_str(), nullptr, nullptr, &errmsg);
      if (errmsg)
      {
        *engine.message= errmsg;
        mylite_free(errmsg);
      }

      const std::string case_label= std::string(engine.label) +
        "_engine_create";
      ok= record_result(result, case_label.c_str(), MYLITE_ERROR, rc, db) &&
        ok;
      if (mylite_mariadb_errno(db) != ER_UNKNOWN_STORAGE_ENGINE ||
          std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
          engine.message->find(engine.name) == std::string::npos)
        ok= false;
    }

    rc= mylite_close(db);
    ok= record_result(result, "legacy_engines_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_temp_spill_profile(const SmokeOptions &options,
                                     SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "temp_spill_open", MYLITE_OK, rc, db);
  if (db)
  {
    ExecCapture memory_capture;
    ok= exec_query_capture(
          db,
          "SELECT a FROM (SELECT 2 AS a UNION ALL SELECT 1 AS a) AS t "
          "ORDER BY a",
          "memory_temp_select", &memory_capture, result) && ok;
    result->exec_memory_temp_rows= join_strings(memory_capture.rows, ",");
    if (result->exec_memory_temp_rows != "1,2")
      ok= false;

    ok= exec_statement(db, "SET SESSION big_tables=1",
                       "disk_temp_big_tables", result) && ok;

#ifdef MYLITE_DISABLE_MYISAM_TEMP_SPILL
    char *errmsg= nullptr;
    rc= mylite_exec(
          db,
          "SELECT a FROM (SELECT 2 AS a UNION ALL SELECT 1 AS a) AS t "
          "ORDER BY a",
          nullptr, nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_disk_temp_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "disk_temp_select", MYLITE_ERROR, rc, db) && ok;
    if (mylite_mariadb_errno(db) != ER_NOT_SUPPORTED_YET ||
        std::strcmp(mylite_sqlstate(db), "42000") != 0 ||
        result->exec_disk_temp_message.find("MyISAM temporary table spill") ==
          std::string::npos)
      ok= false;
#else
    ExecCapture disk_capture;
    ok= exec_query_capture(
          db,
          "SELECT a FROM (SELECT 2 AS a UNION ALL SELECT 1 AS a) AS t "
          "ORDER BY a",
          "disk_temp_select", &disk_capture, result) && ok;
    result->exec_disk_temp_message= join_strings(disk_capture.rows, ",");
    if (result->exec_disk_temp_message != "1,2")
      ok= false;
#endif

    rc= mylite_close(db);
    ok= record_result(result, "temp_spill_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_exec_callback_abort(const SmokeOptions &options,
                                      SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "exec_abort_open", MYLITE_OK, rc, db);
  if (db)
  {
    ExecCapture capture;
    capture.abort_after= 1;
    char *errmsg= nullptr;
    rc= mylite_exec(db, "SELECT 1 AS value UNION ALL SELECT 2",
                    capture_exec_row, &capture, &errmsg);
    if (errmsg)
    {
      result->exec_callback_abort_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "exec_callback_abort", MYLITE_ERROR, rc,
                      db) && ok;
    if (result->exec_callback_abort_message != "callback requested abort")
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "exec_abort_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  return ok;
}

static bool check_exec_dml_persistence(const SmokeOptions &options,
                                       SmokeResult *result)
{
  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  bool ok= record_result(result, "exec_dml_open", MYLITE_OK, rc, db);
  if (db)
  {
    ok= exec_statement(db, "DROP TABLE IF EXISTS mylite.exec_rows",
                       "exec_drop_existing", result) && ok;
    ok= exec_statement(db,
                       "CREATE TABLE mylite.exec_rows "
                       "(id INT NOT NULL, note VARCHAR(20), PRIMARY KEY(id)) "
                       "ENGINE=MYLITE",
                       "exec_create_table", result) && ok;
    ok= exec_statement(db,
                       "INSERT INTO mylite.exec_rows VALUES "
                       "(1, 'one'), (2, NULL)",
                       "exec_insert_rows", result) && ok;

    ExecCapture capture;
    ok= exec_query_capture(db,
                           "SELECT id, COALESCE(note, 'NULL') AS note "
                           "FROM mylite.exec_rows ORDER BY id",
                           "exec_select_rows", &capture, result) && ok;
    result->exec_dml_rows= join_strings(capture.rows, ",");
    if (result->exec_dml_rows != "1:one,2:NULL")
      ok= false;

    char *errmsg= nullptr;
    rc= mylite_exec(db, "INSERT INTO mylite.exec_rows VALUES (1, 'again')",
                    nullptr, nullptr, &errmsg);
    if (errmsg)
    {
      result->exec_duplicate_key_message= errmsg;
      mylite_free(errmsg);
    }
    ok= record_result(result, "exec_duplicate_key", MYLITE_CONSTRAINT, rc,
                      db) && ok;
    if (mylite_mariadb_errno(db) != 1062 ||
        std::strcmp(mylite_sqlstate(db), "23000") != 0 ||
        result->exec_duplicate_key_message.find("Duplicate entry '1'") ==
          std::string::npos)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "exec_dml_close", MYLITE_OK, rc, nullptr) &&
        ok;
  }

  db= nullptr;
  rc= mylite_open(options.database.c_str(), &db);
  ok= record_result(result, "exec_reopen", MYLITE_OK, rc, db) && ok;
  if (db)
  {
    ExecCapture capture;
    ok= exec_query_capture(db,
                           "SELECT id, COALESCE(note, 'NULL') AS note "
                           "FROM mylite.exec_rows ORDER BY id",
                           "exec_reopen_select", &capture, result) && ok;
    result->exec_reopen_rows= join_strings(capture.rows, ",");
    if (result->exec_reopen_rows != "1:one,2:NULL")
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "exec_reopen_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }

  return ok;
}

static bool check_statement_effects(const SmokeOptions &options,
                                    SmokeResult *result)
{
  bool ok= true;
  append_statement_effect(result, "null", statement_effect_summary(nullptr));
  if (statement_effect_summary(nullptr) != "-1:0:0")
    ok= false;

  mylite_db *db= nullptr;
  int rc= mylite_open(options.database.c_str(), &db);
  ok= record_result(result, "effects_open", MYLITE_OK, rc, db) && ok;
  if (db)
  {
    ok= exec_statement(db, "DROP TABLE IF EXISTS mylite.effects_rows",
                       "effects_drop_existing", result) && ok;
    ok= exec_statement(db,
                       "CREATE TABLE mylite.effects_rows "
                       "(id INT NOT NULL AUTO_INCREMENT, "
                       "note VARCHAR(20), PRIMARY KEY(id)) ENGINE=MYLITE",
                       "effects_create_table", result) && ok;

    ok= exec_statement(db,
                       "INSERT INTO mylite.effects_rows (note) VALUES "
                       "('one'), ('two')",
                       "effects_insert_rows", result) && ok;
    const std::string insert_effects= statement_effect_summary(db);
    append_statement_effect(result, "insert", insert_effects);
    if (insert_effects != "2:1:0")
      ok= false;

    ok= exec_statement(db,
                       "UPDATE mylite.effects_rows SET note = note "
                       "WHERE id = 1",
                       "effects_noop_update", result) && ok;
    const std::string noop_update_effects= statement_effect_summary(db);
    append_statement_effect(result, "noop_update", noop_update_effects);
    if (noop_update_effects != "0:0:0")
      ok= false;

    ok= exec_statement(db,
                       "UPDATE mylite.effects_rows SET note = 'two-updated' "
                       "WHERE id = 2",
                       "effects_update", result) && ok;
    const std::string update_effects= statement_effect_summary(db);
    append_statement_effect(result, "update", update_effects);
    if (update_effects != "1:0:0")
      ok= false;

    ok= exec_statement(db,
                       "INSERT IGNORE INTO mylite.effects_rows (id, note) "
                       "VALUES (1, 'ignored')",
                       "effects_insert_ignore_duplicate", result) && ok;
    const std::string warning_effects= statement_effect_summary(db);
    append_statement_effect(result, "warning", warning_effects);
    if (warning_effects != "0:0:1")
      ok= false;

    unsigned warning_level= 0;
    unsigned warning_code= 0;
    const char *warning_message= nullptr;
    const std::string effects_before_warning_lookup=
      statement_effect_summary(db);
    rc= mylite_warning(db, 0, &warning_level, &warning_code,
                       &warning_message);
    const std::string effects_after_warning_lookup=
      statement_effect_summary(db);
    result->warning_first= warning_summary(warning_level, warning_code,
                                           warning_message);
    result->warning_effects= effects_before_warning_lookup + "->" +
                             effects_after_warning_lookup;
    ok= record_result(result, "effects_warning_lookup", MYLITE_OK, rc,
                      db) && ok;
    if (warning_level != MYLITE_WARNING_WARNING || warning_code != 1062 ||
        !warning_message ||
        std::strstr(warning_message, "Duplicate entry '1'") == nullptr ||
        effects_before_warning_lookup != effects_after_warning_lookup)
      ok= false;

    rc= mylite_warning(db, 1, &warning_level, &warning_code,
                       &warning_message);
    result->warning_notfound_message= mylite_errmsg(db);
    ok= record_result(result, "effects_warning_notfound", MYLITE_NOTFOUND,
                      rc, db) && ok;
    if (warning_level != 0 || warning_code != 0 ||
        warning_message != nullptr ||
        result->warning_notfound_message != "warning index is out of range")
      ok= false;

    ok= exec_statement(db,
                       "DELETE FROM mylite.effects_rows WHERE id = 2",
                       "effects_delete", result) && ok;
    const std::string delete_effects= statement_effect_summary(db);
    append_statement_effect(result, "delete", delete_effects);
    if (delete_effects != "1:0:0")
      ok= false;

    rc= mylite_exec(db,
                    "INSERT INTO mylite.effects_rows (id, note) "
                    "VALUES (1, 'duplicate')",
                    nullptr, nullptr, nullptr);
    const long long duplicate_changes= mylite_changes(db);
    const unsigned duplicate_errno= mylite_mariadb_errno(db);
    const std::string duplicate_sqlstate= mylite_sqlstate(db);
    append_statement_effect(result, "duplicate",
                            std::to_string(duplicate_changes) + ":" +
                            std::to_string(duplicate_errno) + ":" +
                            duplicate_sqlstate);
    ok= record_result(result, "effects_duplicate_key", MYLITE_CONSTRAINT,
                      rc, db) && ok;
    if (duplicate_changes != -1 || duplicate_errno != 1062 ||
        duplicate_sqlstate != "23000")
      ok= false;

    rc= mylite_warning(db, 0, &warning_level, &warning_code,
                       &warning_message);
    result->warning_error= warning_summary(warning_level, warning_code,
                                           warning_message);
    ok= record_result(result, "effects_duplicate_warning_lookup",
                      MYLITE_OK, rc, db) && ok;
    if (warning_level != MYLITE_WARNING_ERROR || warning_code != 1062 ||
        !warning_message ||
        std::strstr(warning_message, "Duplicate entry '1'") == nullptr ||
        mylite_changes(db) != duplicate_changes)
      ok= false;

    rc= mylite_close(db);
    ok= record_result(result, "effects_close", MYLITE_OK, rc, nullptr) &&
        ok;
  }

  return ok;
}

static bool check_prepared_statement_api(const SmokeOptions &options,
                                         SmokeResult *result)
{
  bool ok= true;
  mylite_stmt *stmt= nullptr;
  const char *tail= nullptr;
  int rc= mylite_prepare(nullptr, "SELECT 1", 0, &stmt, &tail);
  ok= record_result(result, "prepare_null_db", MYLITE_MISUSE, rc,
                    nullptr) && ok;

  rc= mylite_finalize(nullptr);
  ok= record_result(result, "finalize_null", MYLITE_OK, rc, nullptr) && ok;

  mylite_db *db= nullptr;
  rc= mylite_open(options.database.c_str(), &db);
  ok= record_result(result, "prepared_open", MYLITE_OK, rc, db) && ok;
  if (!db)
    return ok;

  rc= mylite_prepare(db, nullptr, 0, &stmt, &tail);
  ok= record_result(result, "prepare_null_sql", MYLITE_MISUSE, rc, db) &&
      ok;

  rc= mylite_prepare(db, "SELECT ?", 0, &stmt, &tail);
  ok= record_result(result, "prepare_parameter_marker", MYLITE_OK, rc,
                    db) && ok;
  if (stmt)
  {
    rc= mylite_step(stmt);
    result->prepared_unbound_message= mylite_errmsg(db);
    ok= record_result(result, "prepared_unbound_step", MYLITE_MISUSE, rc,
                      db) && ok;
    if (result->prepared_unbound_message != "not all parameters are bound")
      ok= false;

    rc= mylite_bind_int64(stmt, 2, 42);
    result->prepared_invalid_bind_message= mylite_errmsg(db);
    ok= record_result(result, "prepared_invalid_bind_index", MYLITE_MISUSE,
                      rc, db) && ok;
    if (result->prepared_invalid_bind_message !=
        "parameter index is out of range")
      ok= false;

    rc= mylite_bind_int64(stmt, 1, 42);
    ok= record_result(result, "prepared_bind_marker", MYLITE_OK, rc,
                      db) && ok;
    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_bound_marker_row", MYLITE_ROW, rc,
                      db) && ok;
    if (rc == MYLITE_ROW && mylite_column_int64(stmt, 0) != 42)
      ok= false;
    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_bound_marker_done", MYLITE_DONE,
                      rc, db) && ok;

    rc= mylite_bind_int64(stmt, 1, 43);
    result->prepared_rebind_message= mylite_errmsg(db);
    ok= record_result(result, "prepared_rebind_without_reset",
                      MYLITE_MISUSE, rc, db) && ok;
    if (result->prepared_rebind_message !=
        "statement must be reset before rebinding parameters")
      ok= false;

    rc= mylite_finalize(stmt);
    stmt= nullptr;
    ok= record_result(result, "prepared_finalize_marker", MYLITE_OK, rc,
                      db) && ok;
  }

  ok= exec_statement(db, "DROP TABLE IF EXISTS mylite.prepared_rows",
                     "prepared_drop_existing", result) && ok;
  ok= exec_statement(db,
                     "CREATE TABLE mylite.prepared_rows "
                     "(id INT NOT NULL, note VARCHAR(20), payload BLOB, "
                     "PRIMARY KEY(id)) ENGINE=MYLITE",
                     "prepared_create_table", result) && ok;
  ok= exec_statement(db,
                     "INSERT INTO mylite.prepared_rows VALUES "
                     "(1, 'one', 0x610062), (2, NULL, NULL)",
                     "prepared_insert_rows", result) && ok;

  const char *select_sql=
    "SELECT id, note, payload FROM mylite.prepared_rows ORDER BY id";
  rc= mylite_prepare(db, select_sql, 0, &stmt, &tail);
  ok= record_result(result, "prepare_select", MYLITE_OK, rc, db) && ok;
  if (stmt)
  {
    if (tail != select_sql + std::strlen(select_sql))
      ok= false;

    std::vector<std::string> columns;
    for (unsigned i= 0; i < mylite_column_count(stmt); ++i)
      columns.push_back(mylite_column_name(stmt, i));
    result->prepared_columns= join_strings(columns, ",");
    result->prepared_types= prepared_type_summary(stmt);
    if (result->prepared_columns != "id,note,payload" ||
        result->prepared_types != "1,3,4")
      ok= false;

    std::vector<std::string> rows;
    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_select_row_1", MYLITE_ROW, rc,
                      db) && ok;
    if (rc == MYLITE_ROW)
      rows.push_back(prepared_row_summary(stmt));

    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_select_row_2", MYLITE_ROW, rc,
                      db) && ok;
    if (rc == MYLITE_ROW)
      rows.push_back(prepared_row_summary(stmt));

    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_select_done", MYLITE_DONE, rc,
                      db) && ok;
    result->prepared_rows= join_strings(rows, ",");
    if (result->prepared_rows != "1:one:610062,2:NULL:NULL")
      ok= false;

    rc= mylite_reset(stmt);
    ok= record_result(result, "prepared_reset", MYLITE_OK, rc, db) && ok;
    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_reset_row", MYLITE_ROW, rc, db) &&
        ok;
    if (rc == MYLITE_ROW)
      result->prepared_reset_row= prepared_row_summary(stmt);
    if (result->prepared_reset_row != "1:one:610062")
      ok= false;

    rc= mylite_finalize(stmt);
    stmt= nullptr;
    ok= record_result(result, "prepared_finalize_select", MYLITE_OK, rc,
                      db) && ok;
  }

  rc= mylite_prepare(db,
                     "INSERT INTO mylite.prepared_rows "
                     "(id, note, payload) VALUES (3, 'three', 0x7a)",
                     0, &stmt, &tail);
  ok= record_result(result, "prepare_insert", MYLITE_OK, rc, db) && ok;
  if (stmt)
  {
    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_insert_done", MYLITE_DONE, rc,
                      db) && ok;
    rc= mylite_finalize(stmt);
    stmt= nullptr;
    ok= record_result(result, "prepared_finalize_insert", MYLITE_OK, rc,
                      db) && ok;
  }

  ExecCapture capture;
  ok= exec_query_capture(db,
                         "SELECT id, COALESCE(note, 'NULL') AS note "
                         "FROM mylite.prepared_rows ORDER BY id",
                         "prepared_dml_select", &capture, result) && ok;
  result->prepared_dml_rows= join_strings(capture.rows, ",");
  if (result->prepared_dml_rows != "1:one,2:NULL,3:three")
    ok= false;

  ok= exec_statement(db, "DROP TABLE IF EXISTS mylite.prepared_bind_rows",
                     "prepared_bind_drop_existing", result) && ok;
  ok= exec_statement(db,
                     "CREATE TABLE mylite.prepared_bind_rows "
                     "(id INT NOT NULL, signed_value BIGINT, "
                     "unsigned_value BIGINT UNSIGNED, amount DOUBLE, "
                     "note VARCHAR(40), payload BLOB, PRIMARY KEY(id)) "
                     "ENGINE=MYLITE",
                     "prepared_bind_create_table", result) && ok;

  rc= mylite_prepare(db,
                     "INSERT INTO mylite.prepared_bind_rows "
                     "(id, signed_value, unsigned_value, amount, note, "
                     "payload) VALUES (?, ?, ?, ?, ?, ?)",
                     0, &stmt, &tail);
  ok= record_result(result, "prepare_bound_insert", MYLITE_OK, rc, db) &&
      ok;
  if (stmt)
  {
    const char payload_one[]= { 'a', '\0', 'b' };
    ok= record_result(result, "bind_row1_id", MYLITE_OK,
                      mylite_bind_int64(stmt, 1, 1), db) && ok;
    ok= record_result(result, "bind_row1_signed", MYLITE_OK,
                      mylite_bind_int64(stmt, 2, -1234567890123LL),
                      db) && ok;
    ok= record_result(result, "bind_row1_unsigned", MYLITE_OK,
                      mylite_bind_uint64(stmt, 3, 9223372036854775810ULL),
                      db) && ok;
    ok= record_result(result, "bind_row1_double", MYLITE_OK,
                      mylite_bind_double(stmt, 4, 3.5), db) && ok;
    ok= record_result(result, "bind_row1_text", MYLITE_OK,
                      mylite_bind_text(stmt, 5, "hello", 5,
                                       MYLITE_TRANSIENT),
                      db) && ok;
    ok= record_result(result, "bind_row1_blob", MYLITE_OK,
                      mylite_bind_blob(stmt, 6, payload_one,
                                       sizeof(payload_one),
                                       MYLITE_TRANSIENT),
                      db) && ok;
    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_bound_insert_row1", MYLITE_DONE,
                      rc, db) && ok;

    rc= mylite_reset(stmt);
    ok= record_result(result, "prepared_bound_reset_row2", MYLITE_OK, rc,
                      db) && ok;
    ok= record_result(result, "bind_row2_id", MYLITE_OK,
                      mylite_bind_int64(stmt, 1, 2), db) && ok;
    ok= record_result(result, "bind_row2_signed_null", MYLITE_OK,
                      mylite_bind_null(stmt, 2), db) && ok;
    ok= record_result(result, "bind_row2_unsigned", MYLITE_OK,
                      mylite_bind_uint64(stmt, 3, 7), db) && ok;
    ok= record_result(result, "bind_row2_double", MYLITE_OK,
                      mylite_bind_double(stmt, 4, -2.25), db) && ok;
    ok= record_result(result, "bind_row2_text_null", MYLITE_OK,
                      mylite_bind_text(stmt, 5, nullptr, 0,
                                       MYLITE_STATIC),
                      db) && ok;
    ok= record_result(result, "bind_row2_blob_null", MYLITE_OK,
                      mylite_bind_blob(stmt, 6, nullptr, 0,
                                       MYLITE_STATIC),
                      db) && ok;
    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_bound_insert_row2", MYLITE_DONE,
                      rc, db) && ok;

    bind_destructor_calls= 0;
    rc= mylite_reset(stmt);
    ok= record_result(result, "prepared_bound_reset_row3", MYLITE_OK, rc,
                      db) && ok;
    const char payload_three[]= { 'z' };
    char custom_note[]= "custom";
    ok= record_result(result, "bind_row3_id", MYLITE_OK,
                      mylite_bind_int64(stmt, 1, 3), db) && ok;
    ok= record_result(result, "bind_row3_signed", MYLITE_OK,
                      mylite_bind_int64(stmt, 2, 0), db) && ok;
    ok= record_result(result, "bind_row3_unsigned", MYLITE_OK,
                      mylite_bind_uint64(stmt, 3, 0), db) && ok;
    ok= record_result(result, "bind_row3_double", MYLITE_OK,
                      mylite_bind_double(stmt, 4, 0.25), db) && ok;
    ok= record_result(result, "bind_row3_text_destructor", MYLITE_OK,
                      mylite_bind_text(stmt, 5, custom_note,
                                       std::strlen(custom_note),
                                       count_bind_destructor),
                      db) && ok;
    ok= record_result(result, "bind_row3_blob", MYLITE_OK,
                      mylite_bind_blob(stmt, 6, payload_three,
                                       sizeof(payload_three),
                                       MYLITE_STATIC),
                      db) && ok;
    result->prepared_bind_destructor_count=
      std::to_string(bind_destructor_calls);
    if (bind_destructor_calls != 1)
      ok= false;
    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_bound_insert_row3", MYLITE_DONE,
                      rc, db) && ok;

    rc= mylite_finalize(stmt);
    stmt= nullptr;
    ok= record_result(result, "prepared_finalize_bound_insert", MYLITE_OK,
                      rc, db) && ok;
  }

  rc= mylite_prepare(db,
                     "SELECT id, signed_value, unsigned_value, amount, "
                     "note, payload FROM mylite.prepared_bind_rows "
                     "ORDER BY id",
                     0, &stmt, &tail);
  ok= record_result(result, "prepare_bound_select", MYLITE_OK, rc, db) &&
      ok;
  if (stmt)
  {
    std::vector<std::string> rows;
    while ((rc= mylite_step(stmt)) == MYLITE_ROW)
      rows.push_back(prepared_bound_row_summary(stmt));
    ok= record_result(result, "prepared_bound_select_done", MYLITE_DONE,
                      rc, db) && ok;
    result->prepared_bound_rows= join_strings(rows, ",");
    if (result->prepared_bound_rows !=
        "1:-1234567890123:9223372036854775810:3.500000:hello:610062,"
        "2:NULL:7:-2.250000:NULL:NULL,"
        "3:0:0:0.250000:custom:7a")
      ok= false;

    rc= mylite_finalize(stmt);
    stmt= nullptr;
    ok= record_result(result, "prepared_finalize_bound_select", MYLITE_OK,
                      rc, db) && ok;
  }

  rc= mylite_prepare(db,
                     "SELECT id FROM mylite.prepared_bind_rows "
                     "WHERE id = ?",
                     0, &stmt, &tail);
  ok= record_result(result, "prepare_bound_reset_select", MYLITE_OK, rc,
                    db) && ok;
  if (stmt)
  {
    std::vector<std::string> reset_rows;
    ok= record_result(result, "bind_reset_id_1", MYLITE_OK,
                      mylite_bind_int64(stmt, 1, 1), db) && ok;
    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_bound_reset_first_row",
                      MYLITE_ROW, rc, db) && ok;
    if (rc == MYLITE_ROW)
      reset_rows.push_back(std::to_string(mylite_column_int64(stmt, 0)));
    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_bound_reset_first_done",
                      MYLITE_DONE, rc, db) && ok;

    rc= mylite_reset(stmt);
    ok= record_result(result, "prepared_bound_reset_preserve", MYLITE_OK,
                      rc, db) && ok;
    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_bound_reset_preserved_row",
                      MYLITE_ROW, rc, db) && ok;
    if (rc == MYLITE_ROW)
      reset_rows.push_back(std::to_string(mylite_column_int64(stmt, 0)));
    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_bound_reset_preserved_done",
                      MYLITE_DONE, rc, db) && ok;

    rc= mylite_reset(stmt);
    ok= record_result(result, "prepared_bound_reset_rebind", MYLITE_OK,
                      rc, db) && ok;
    ok= record_result(result, "bind_reset_id_2", MYLITE_OK,
                      mylite_bind_int64(stmt, 1, 2), db) && ok;
    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_bound_reset_rebound_row",
                      MYLITE_ROW, rc, db) && ok;
    if (rc == MYLITE_ROW)
      reset_rows.push_back(std::to_string(mylite_column_int64(stmt, 0)));
    rc= mylite_step(stmt);
    ok= record_result(result, "prepared_bound_reset_rebound_done",
                      MYLITE_DONE, rc, db) && ok;

    result->prepared_bound_reset_rows= join_strings(reset_rows, ",");
    if (result->prepared_bound_reset_rows != "1,1,2")
      ok= false;

    rc= mylite_finalize(stmt);
    stmt= nullptr;
    ok= record_result(result, "prepared_finalize_bound_reset", MYLITE_OK,
                      rc, db) && ok;
  }

  rc= mylite_prepare(db, "SELECT 1", 0, &stmt, &tail);
  ok= record_result(result, "prepare_close_busy_select", MYLITE_OK, rc,
                    db) && ok;
  if (stmt)
  {
    rc= mylite_close(db);
    result->prepared_close_busy_message= mylite_errmsg(db);
    ok= record_result(result, "prepared_close_busy", MYLITE_BUSY, rc,
                      db) && ok;
    if (result->prepared_close_busy_message !=
        "database handle has active statements")
      ok= false;

    rc= mylite_finalize(stmt);
    stmt= nullptr;
    ok= record_result(result, "prepared_finalize_busy", MYLITE_OK, rc,
                      db) && ok;
  }

  rc= mylite_close(db);
  ok= record_result(result, "prepared_close", MYLITE_OK, rc, nullptr) && ok;
  return ok;
}

static bool check_readonly_existing_database(const SmokeOptions &options,
                                             SmokeResult *result)
{
  bool ok= true;
  FileSnapshot before;
  std::string snapshot_error;
  if (!snapshot_file(options.database, &before, &snapshot_error))
  {
    result->message= snapshot_error;
    return false;
  }

  mylite_db *db= nullptr;
  int rc= mylite_open_v2(options.database.c_str(), &db, MYLITE_OPEN_READONLY,
                         nullptr);
  ok= record_result(result, "readonly_open", MYLITE_OK, rc, db) && ok;
  if (!db)
    return ok;

  ExecCapture capture;
  ok= exec_query_capture(db,
                         "SELECT id, COALESCE(note, 'NULL') AS note "
                         "FROM mylite.exec_rows ORDER BY id",
                         "readonly_select_rows", &capture, result) && ok;
  result->readonly_rows= join_strings(capture.rows, ",");
  if (result->readonly_rows != "1:one,2:NULL")
    ok= false;

  mylite_db *second= nullptr;
  rc= mylite_open_v2(options.database.c_str(), &second, MYLITE_OPEN_READONLY,
                     nullptr);
  ok= record_result(result, "readonly_second_open", MYLITE_OK, rc,
                    second) && ok;
  if (second)
  {
    rc= mylite_close(second);
    ok= record_result(result, "readonly_second_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }

  mylite_db *readwrite= nullptr;
  rc= mylite_open(options.database.c_str(), &readwrite);
  ok= record_result(result, "readonly_reject_readwrite_open", MYLITE_BUSY,
                    rc, readwrite) && ok;
  mylite_close(readwrite);

  rc= mylite_exec(db, "INSERT INTO mylite.exec_rows VALUES (3, 'three')",
                  nullptr, nullptr, nullptr);
  result->readonly_insert_message= mylite_errmsg(db);
  ok= record_result(result, "readonly_insert_rejected", MYLITE_READONLY, rc,
                    db) && ok;
  if (mylite_mariadb_errno(db) != 1036 ||
      result->readonly_insert_message.find("read only") == std::string::npos)
    ok= false;

  rc= mylite_exec(db,
                  "CREATE TABLE mylite.readonly_create "
                  "(id INT NOT NULL, PRIMARY KEY(id)) ENGINE=MYLITE",
                  nullptr, nullptr, nullptr);
  result->readonly_create_message= mylite_errmsg(db);
  ok= record_result(result, "readonly_create_rejected", MYLITE_READONLY, rc,
                    db) && ok;
  if (mylite_mariadb_errno(db) != 1005 ||
      result->readonly_create_message.find("Table is read only") ==
        std::string::npos)
    ok= false;

  mylite_stmt *stmt= nullptr;
  const char *tail= nullptr;
  rc= mylite_prepare(db,
                     "INSERT INTO mylite.exec_rows VALUES (4, 'four')",
                     0, &stmt, &tail);
  ok= record_result(result, "readonly_prepare_insert", MYLITE_OK, rc,
                    db) && ok;
  if (stmt)
  {
    rc= mylite_step(stmt);
    result->readonly_prepare_message= mylite_errmsg(db);
    ok= record_result(result, "readonly_prepared_insert_rejected",
                      MYLITE_READONLY, rc, db) && ok;
    if (mylite_mariadb_errno(db) != 1036 ||
        result->readonly_prepare_message.find("read only") ==
          std::string::npos)
      ok= false;
    rc= mylite_finalize(stmt);
    ok= record_result(result, "readonly_finalize_insert", MYLITE_OK, rc,
                      db) && ok;
  }

  ExecCapture after_capture;
  ok= exec_query_capture(db,
                         "SELECT id, COALESCE(note, 'NULL') AS note "
                         "FROM mylite.exec_rows ORDER BY id",
                         "readonly_select_after_rejected",
                         &after_capture, result) && ok;
  if (join_strings(after_capture.rows, ",") != "1:one,2:NULL")
    ok= false;

  rc= mylite_close(db);
  ok= record_result(result, "readonly_close", MYLITE_OK, rc, nullptr) && ok;

  FileSnapshot after;
  if (!snapshot_file(options.database, &after, &snapshot_error))
  {
    result->message= snapshot_error;
    return false;
  }
  result->readonly_file= file_snapshot_summary(before) + "->" +
                         file_snapshot_summary(after);
  if (before.size != after.size || before.mtime_sec != after.mtime_sec ||
      before.mtime_nsec != after.mtime_nsec)
    ok= false;

  return ok;
}

static bool check_exclusive_open(const SmokeOptions &options,
                                 SmokeResult *result)
{
  bool ok= true;
  unlink(options.database.c_str());

  mylite_db *db= nullptr;
  int rc= mylite_open_v2(options.database.c_str(), &db,
                         MYLITE_OPEN_READWRITE | MYLITE_OPEN_CREATE |
                           MYLITE_OPEN_EXCLUSIVE,
                         nullptr);
  ok= record_result(result, "exclusive_create_open", MYLITE_OK, rc, db) &&
      ok;
  if (db)
  {
    rc= mylite_close(db);
    ok= record_result(result, "exclusive_create_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }
  if (access(options.database.c_str(), F_OK) != 0)
    ok= false;

  db= nullptr;
  rc= mylite_open_v2(options.database.c_str(), &db,
                     MYLITE_OPEN_READWRITE | MYLITE_OPEN_CREATE |
                       MYLITE_OPEN_EXCLUSIVE,
                     nullptr);
  result->exclusive_existing_message= db ? mylite_errmsg(db) : "";
  ok= record_result(result, "exclusive_existing_rejected", MYLITE_CANTOPEN,
                    rc, db) && ok;
  if (result->exclusive_existing_message.empty())
    ok= false;
  mylite_close(db);

  db= nullptr;
  rc= mylite_open_v2(options.database.c_str(), &db,
                     MYLITE_OPEN_READWRITE | MYLITE_OPEN_EXCLUSIVE,
                     nullptr);
  ok= record_result(result, "exclusive_without_create", MYLITE_MISUSE, rc,
                    db) && ok;
  mylite_close(db);

  db= nullptr;
  rc= mylite_open_v2(options.database.c_str(), &db,
                     MYLITE_OPEN_READONLY | MYLITE_OPEN_EXCLUSIVE,
                     nullptr);
  ok= record_result(result, "exclusive_readonly", MYLITE_MISUSE, rc,
                    db) && ok;
  mylite_close(db);

  return ok;
}

static bool check_uri_open(const SmokeOptions &options, SmokeResult *result)
{
  unlink(options.database.c_str());

  const std::string create_uri= file_uri_for_path(options.database,
                                                  "mode=rwc");
  const std::string readwrite_uri= file_uri_for_path(options.database,
                                                     "mode=rw");
  const std::string localhost_uri= "file://localhost" +
                                   uri_percent_encode_path(options.database) +
                                   "?mode=rw";
  const std::string mismatch_uri= file_uri_for_path(options.database,
                                                    "mode=ro");
  mylite_db *db= nullptr;
  int rc= mylite_open_v2("file://remote.example/tmp/remote.mylite", &db,
                         MYLITE_OPEN_URI | MYLITE_OPEN_READWRITE, nullptr);
  bool ok= record_result(result, "uri_remote_authority", MYLITE_MISUSE,
                         rc, db);
  mylite_close(db);

  db= nullptr;
  rc= mylite_open_v2("file:/tmp/bad%zz.mylite", &db,
                     MYLITE_OPEN_URI | MYLITE_OPEN_READWRITE, nullptr);
  ok= record_result(result, "uri_bad_percent", MYLITE_MISUSE, rc, db) &&
      ok;
  mylite_close(db);

  db= nullptr;
  rc= mylite_open_v2("file:/tmp/unknown.mylite?cache=shared", &db,
                     MYLITE_OPEN_URI | MYLITE_OPEN_READWRITE, nullptr);
  ok= record_result(result, "uri_unknown_parameter", MYLITE_MISUSE, rc,
                    db) && ok;
  mylite_close(db);

  db= nullptr;
  rc= mylite_open_v2("file:/tmp/duplicate.mylite?mode=rw&mode=ro", &db,
                     MYLITE_OPEN_URI, nullptr);
  ok= record_result(result, "uri_duplicate_mode", MYLITE_MISUSE, rc, db) &&
      ok;
  mylite_close(db);

  db= nullptr;
  rc= mylite_open_v2("file:/tmp/unsupported.mylite?mode=memory", &db,
                     MYLITE_OPEN_URI, nullptr);
  ok= record_result(result, "uri_unsupported_mode", MYLITE_MISUSE, rc,
                    db) && ok;
  mylite_close(db);

  db= nullptr;
  rc= mylite_open_v2(mismatch_uri.c_str(), &db,
                     MYLITE_OPEN_URI | MYLITE_OPEN_READWRITE |
                       MYLITE_OPEN_CREATE,
                     nullptr);
  ok= record_result(result, "uri_mode_flag_mismatch", MYLITE_MISUSE, rc,
                    db) && ok;
  mylite_close(db);

  db= nullptr;
  rc= mylite_open_v2("file:/tmp/fragment.mylite#ignored", &db,
                     MYLITE_OPEN_URI | MYLITE_OPEN_READWRITE, nullptr);
  ok= record_result(result, "uri_fragment", MYLITE_MISUSE, rc, db) && ok;
  mylite_close(db);

  db= nullptr;
  rc= mylite_open_v2(create_uri.c_str(), &db, MYLITE_OPEN_URI, nullptr);
  ok= record_result(result, "uri_mode_create_open", MYLITE_OK, rc, db) &&
      ok;
  if (!db)
    return ok;

  ok= exec_statement(db,
                     "CREATE TABLE mylite.uri_rows "
                     "(id INT NOT NULL, note VARCHAR(12), PRIMARY KEY(id)) "
                     "ENGINE=MYLITE",
                     "uri create table", result) && ok;
  ok= exec_statement(db,
                     "INSERT INTO mylite.uri_rows VALUES "
                     "(1, 'one'), (2, 'two')",
                     "uri insert rows", result) && ok;
  ExecCapture capture;
  ok= exec_query_capture(db,
                         "SELECT id, note FROM mylite.uri_rows ORDER BY id",
                         "uri select rows", &capture, result) && ok;
  result->uri_rows= join_strings(capture.rows, ",");
  if (result->uri_rows != "1:one,2:two")
    ok= false;
  rc= mylite_close(db);
  ok= record_result(result, "uri_mode_create_close", MYLITE_OK, rc,
                    nullptr) && ok;

  db= nullptr;
  rc= mylite_open_v2(readwrite_uri.c_str(), &db, MYLITE_OPEN_URI, nullptr);
  ok= record_result(result, "uri_mode_readwrite_open", MYLITE_OK, rc, db) &&
      ok;
  if (db)
  {
    rc= mylite_close(db);
    ok= record_result(result, "uri_mode_readwrite_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }

  db= nullptr;
  rc= mylite_open_v2(localhost_uri.c_str(), &db, MYLITE_OPEN_URI, nullptr);
  ok= record_result(result, "uri_localhost_open", MYLITE_OK, rc, db) && ok;
  if (db)
  {
    rc= mylite_close(db);
    ok= record_result(result, "uri_localhost_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }

  db= nullptr;
  rc= mylite_open_v2(options.database.c_str(), &db,
                     MYLITE_OPEN_URI | MYLITE_OPEN_READWRITE |
                       MYLITE_OPEN_CREATE,
                     nullptr);
  ok= record_result(result, "uri_plain_path_open", MYLITE_OK, rc, db) &&
      ok;
  if (db)
  {
    ExecCapture plain_capture;
    ok= exec_query_capture(db,
                           "SELECT id, note FROM mylite.uri_rows ORDER BY id",
                           "uri plain path rows", &plain_capture, result) &&
        ok;
    if (join_strings(plain_capture.rows, ",") != "1:one,2:two")
      ok= false;
    rc= mylite_close(db);
    ok= record_result(result, "uri_plain_path_close", MYLITE_OK, rc,
                      nullptr) && ok;
  }

  return ok;
}

static bool check_uri_readonly_open(const SmokeOptions &options,
                                    SmokeResult *result)
{
  const std::string readonly_uri= file_uri_for_path(options.database,
                                                    "mode=ro");
  mylite_db *db= nullptr;
  int rc= mylite_open_v2(readonly_uri.c_str(), &db, MYLITE_OPEN_URI, nullptr);
  bool ok= record_result(result, "uri_readonly_open", MYLITE_OK, rc, db);
  if (!db)
    return ok;

  ExecCapture capture;
  ok= exec_query_capture(db,
                         "SELECT id, note FROM mylite.uri_rows ORDER BY id",
                         "uri readonly rows", &capture, result) && ok;
  result->uri_readonly_rows= join_strings(capture.rows, ",");
  if (result->uri_readonly_rows != "1:one,2:two")
    ok= false;

  rc= mylite_exec(db, "INSERT INTO mylite.uri_rows VALUES (3, 'three')",
                  nullptr, nullptr, nullptr);
  result->uri_readonly_insert_message= mylite_errmsg(db);
  ok= record_result(result, "uri_readonly_insert_rejected",
                    MYLITE_READONLY, rc, db) && ok;
  if (mylite_mariadb_errno(db) != 1036 ||
      result->uri_readonly_insert_message.find("read only") ==
        std::string::npos)
    ok= false;

  rc= mylite_close(db);
  ok= record_result(result, "uri_readonly_close", MYLITE_OK, rc, nullptr) &&
      ok;
  return ok;
}

static bool exec_statement(mylite_db *db, const char *sql, const char *label,
                           SmokeResult *result)
{
  const int rc= mylite_exec(db, sql, nullptr, nullptr, nullptr);
  return record_result(result, label, MYLITE_OK, rc, db);
}

static bool exec_query_capture(mylite_db *db, const char *sql,
                               const char *label, ExecCapture *capture,
                               SmokeResult *result)
{
  const int rc= mylite_exec(db, sql, capture_exec_row, capture, nullptr);
  return record_result(result, label, MYLITE_OK, rc, db);
}

static void append_statement_effect(SmokeResult *result, const char *label,
                                    const std::string &value)
{
  if (!result->statement_effects.empty())
    result->statement_effects+= ",";
  result->statement_effects+= label;
  result->statement_effects+= "=";
  result->statement_effects+= value;
}

static std::string statement_effect_summary(mylite_db *db)
{
  return std::to_string(mylite_changes(db)) + ":" +
         std::to_string(mylite_last_insert_id(db)) + ":" +
         std::to_string(mylite_warning_count(db));
}

static std::string warning_summary(unsigned level, unsigned code,
                                   const char *message)
{
  return std::to_string(level) + ":" + std::to_string(code) + ":" +
         (message ? message : "NULL");
}

static std::string prepared_row_summary(mylite_stmt *stmt)
{
  const char *note= mylite_column_text(stmt, 1);
  const void *payload= mylite_column_blob(stmt, 2);
  return std::to_string(mylite_column_int64(stmt, 0)) + ":" +
         (note ? note : "NULL") + ":" +
         (payload ? hex_bytes(payload, mylite_column_bytes(stmt, 2)) :
          "NULL");
}

static std::string prepared_bound_row_summary(mylite_stmt *stmt)
{
  const char *note= mylite_column_text(stmt, 4);
  const void *payload= mylite_column_blob(stmt, 5);
  const std::string signed_value=
    mylite_column_type(stmt, 1) == MYLITE_NULL ?
    "NULL" : std::to_string(mylite_column_int64(stmt, 1));
  const std::string unsigned_value=
    mylite_column_type(stmt, 2) == MYLITE_NULL ?
    "NULL" : std::to_string(mylite_column_uint64(stmt, 2));
  const std::string double_value=
    mylite_column_type(stmt, 3) == MYLITE_NULL ?
    "NULL" : std::to_string(mylite_column_double(stmt, 3));
  return std::to_string(mylite_column_int64(stmt, 0)) + ":" +
         signed_value + ":" + unsigned_value + ":" + double_value + ":" +
         (note ? note : "NULL") + ":" +
         (payload ? hex_bytes(payload, mylite_column_bytes(stmt, 5)) :
          "NULL");
}

static std::string prepared_type_summary(mylite_stmt *stmt)
{
  std::vector<std::string> types;
  for (unsigned i= 0; i < mylite_column_count(stmt); ++i)
    types.push_back(std::to_string(mylite_column_type(stmt, i)));
  return join_strings(types, ",");
}

static std::string file_uri_for_path(const std::string &path,
                                     const char *query)
{
  std::string uri= "file:";
  uri+= uri_percent_encode_path(path);
  if (query && query[0])
  {
    uri+= "?";
    uri+= query;
  }
  return uri;
}

static std::string uri_percent_encode_path(const std::string &path)
{
  const char hex[]= "0123456789ABCDEF";
  std::string encoded;
  for (unsigned char value : path)
  {
    const bool safe= (value >= 'A' && value <= 'Z') ||
                     (value >= 'a' && value <= 'z') ||
                     (value >= '0' && value <= '9') ||
                     value == '/' || value == '-' || value == '_' ||
                     value == '.' || value == '~' || value == ':';
    if (safe)
    {
      encoded.push_back(static_cast<char>(value));
      continue;
    }
    encoded.push_back('%');
    encoded.push_back(hex[value >> 4]);
    encoded.push_back(hex[value & 0x0f]);
  }
  return encoded;
}

static std::string hex_bytes(const void *data, size_t length)
{
  static const char digits[]= "0123456789abcdef";
  const unsigned char *bytes= static_cast<const unsigned char *>(data);
  std::string result;
  result.reserve(length * 2);
  for (size_t i= 0; i < length; ++i)
  {
    result.push_back(digits[bytes[i] >> 4]);
    result.push_back(digits[bytes[i] & 0x0f]);
  }
  return result;
}

static bool snapshot_file(const std::string &path, FileSnapshot *snapshot,
                          std::string *message)
{
  struct stat st;
  if (stat(path.c_str(), &st) != 0)
  {
    *message= "could not stat primary file: " + path;
    return false;
  }

  snapshot->size= st.st_size;
#ifdef __APPLE__
  snapshot->mtime_sec= st.st_mtimespec.tv_sec;
  snapshot->mtime_nsec= st.st_mtimespec.tv_nsec;
#else
  snapshot->mtime_sec= st.st_mtim.tv_sec;
  snapshot->mtime_nsec= st.st_mtim.tv_nsec;
#endif
  return true;
}

static std::string file_snapshot_summary(const FileSnapshot &snapshot)
{
  return std::to_string(static_cast<long long>(snapshot.size)) + ":" +
         std::to_string(static_cast<long long>(snapshot.mtime_sec)) + "." +
         std::to_string(snapshot.mtime_nsec);
}

static void count_bind_destructor(void *ptr)
{
  if (ptr)
    ++bind_destructor_calls;
}

static int capture_exec_row(void *ctx, int column_count, char **values,
                            char **column_names)
{
  ExecCapture *capture= static_cast<ExecCapture *>(ctx);
  if (capture->columns.empty())
  {
    for (int i= 0; i < column_count; ++i)
      capture->columns.push_back(column_names[i] ? column_names[i] : "");
  }

  std::vector<std::string> row_values;
  row_values.reserve(static_cast<size_t>(column_count));
  for (int i= 0; i < column_count; ++i)
    row_values.push_back(values[i] ? values[i] : "NULL");
  capture->rows.push_back(join_strings(row_values, ":"));

  if (capture->abort_after > 0 &&
      static_cast<int>(capture->rows.size()) >= capture->abort_after)
    return 1;
  return 0;
}

static bool record_result(SmokeResult *result, const char *label, int expected,
                          int actual, mylite_db *db)
{
  CaseResult case_result;
  case_result.label= label;
  case_result.expected= expected;
  case_result.actual= actual;
  if (db)
  {
    case_result.errcode= mylite_errcode(db);
    case_result.extended_errcode= mylite_extended_errcode(db);
    case_result.mariadb_errno= mylite_mariadb_errno(db);
    case_result.sqlstate= mylite_sqlstate(db);
    case_result.message= mylite_errmsg(db);
  }
  else if (actual == MYLITE_OK)
  {
    case_result.errcode= MYLITE_OK;
    case_result.extended_errcode= MYLITE_OK;
    case_result.sqlstate= "00000";
    case_result.message= "not an error";
  }
  else
  {
    case_result.errcode= mylite_errcode(nullptr);
    case_result.extended_errcode= mylite_extended_errcode(nullptr);
    case_result.mariadb_errno= mylite_mariadb_errno(nullptr);
    case_result.sqlstate= mylite_sqlstate(nullptr);
    case_result.message= mylite_errmsg(nullptr);
  }
  case_result.passed= expected == actual;
  result->cases.push_back(case_result);
  return case_result.passed;
}

static std::string join_strings(const std::vector<std::string> &values,
                                const char *separator)
{
  std::string result;
  for (size_t i= 0; i < values.size(); ++i)
  {
    if (i != 0)
      result+= separator;
    result+= values[i];
  }
  return result;
}

static void write_report(const SmokeOptions &options,
                         const SmokeResult &result)
{
  std::ofstream report(options.report.c_str());
  if (!report)
  {
    std::cerr << "could not open report: " << options.report << std::endl;
    return;
  }

  report << "# MyLite Open Close Smoke Report\n\n";
  report << "database=" << options.database << "\n\n";
  report << "mode=" << options.mode << "\n\n";
  report << "## Result\n\n";
  report << "status=" << result.status << "\n";
  report << "phase=" << result.phase << "\n";
  report << "message=" << result.message << "\n\n";

  if (!result.exec_null_db_message.empty())
    report << "exec_null_db_message=" << result.exec_null_db_message << "\n";
  if (!result.exec_scalar_columns.empty())
    report << "exec_scalar_columns=" << result.exec_scalar_columns << "\n";
  if (!result.exec_scalar_rows.empty())
    report << "exec_scalar_rows=" << result.exec_scalar_rows << "\n";
  if (!result.exec_collation_rows.empty())
    report << "exec_collation_rows=" << result.exec_collation_rows << "\n";
  if (!result.exec_uca_collation_message.empty())
    report << "exec_uca_collation_message="
           << result.exec_uca_collation_message << "\n";
  if (!result.exec_general1400_collation_message.empty())
    report << "exec_general1400_collation_message="
           << result.exec_general1400_collation_message << "\n";
  if (!result.exec_locale_profile_rows.empty())
    report << "exec_locale_profile_rows="
           << result.exec_locale_profile_rows << "\n";
  if (!result.exec_locale_removed_message.empty())
    report << "exec_locale_removed_message="
           << result.exec_locale_removed_message << "\n";
  if (!result.exec_oracle_mode_message.empty())
    report << "exec_oracle_mode_message="
           << result.exec_oracle_mode_message << "\n";
  if (!result.exec_oracle_function_standard_rows.empty())
    report << "exec_oracle_function_standard_rows="
           << result.exec_oracle_function_standard_rows << "\n";
  if (!result.exec_oracle_function_message.empty())
    report << "exec_oracle_function_message="
           << result.exec_oracle_function_message << "\n";
  if (!result.exec_xml_extractvalue_message.empty())
    report << "exec_xml_extractvalue_message="
           << result.exec_xml_extractvalue_message << "\n";
  if (!result.exec_xml_updatexml_message.empty())
    report << "exec_xml_updatexml_message="
           << result.exec_xml_updatexml_message << "\n";
  if (!result.exec_gis_function_message.empty())
    report << "exec_gis_function_message="
           << result.exec_gis_function_message << "\n";
  if (!result.exec_vector_fromtext_message.empty())
    report << "exec_vector_fromtext_message="
           << result.exec_vector_fromtext_message << "\n";
  if (!result.exec_vector_distance_message.empty())
    report << "exec_vector_distance_message="
           << result.exec_vector_distance_message << "\n";
  if (!result.exec_json_valid_rows.empty())
    report << "exec_json_valid_rows=" << result.exec_json_valid_rows << "\n";
  if (!result.exec_json_schema_valid_message.empty())
    report << "exec_json_schema_valid_message="
           << result.exec_json_schema_valid_message << "\n";
  if (!result.exec_json_table_message.empty())
    report << "exec_json_table_message="
           << result.exec_json_table_message << "\n";
  if (!result.exec_regex_like_rows.empty())
    report << "exec_regex_like_rows=" << result.exec_regex_like_rows << "\n";
  if (!result.exec_regex_messages.empty())
    report << "exec_regex_messages=" << result.exec_regex_messages << "\n";
  if (!result.exec_window_aggregate_rows.empty())
    report << "exec_window_aggregate_rows="
           << result.exec_window_aggregate_rows << "\n";
  if (!result.exec_window_function_messages.empty())
    report << "exec_window_function_messages="
           << result.exec_window_function_messages << "\n";
  if (!result.exec_binlog_replication_message.empty())
    report << "exec_binlog_replication_message="
           << result.exec_binlog_replication_message << "\n";
  if (!result.exec_xa_transaction_messages.empty())
    report << "exec_xa_transaction_messages="
           << result.exec_xa_transaction_messages << "\n";
  if (!result.exec_server_encryption_rows.empty())
    report << "exec_server_encryption_rows="
           << result.exec_server_encryption_rows << "\n";
  if (!result.exec_server_encryption_set_messages.empty())
    report << "exec_server_encryption_set_messages="
           << result.exec_server_encryption_set_messages << "\n";
  if (!result.exec_proxy_protocol_rows.empty())
    report << "exec_proxy_protocol_rows="
           << result.exec_proxy_protocol_rows << "\n";
  if (!result.exec_proxy_protocol_set_message.empty())
    report << "exec_proxy_protocol_set_message="
           << result.exec_proxy_protocol_set_message << "\n";
  if (!result.exec_server_utility_standard_rows.empty())
    report << "exec_server_utility_standard_rows="
           << result.exec_server_utility_standard_rows << "\n";
  if (!result.exec_server_utility_messages.empty())
    report << "exec_server_utility_messages="
           << result.exec_server_utility_messages << "\n";
  if (!result.exec_sql_crypto_function_messages.empty())
    report << "exec_sql_crypto_function_messages="
           << result.exec_sql_crypto_function_messages << "\n";
  if (!result.exec_crypt_function_message.empty())
    report << "exec_crypt_function_message="
           << result.exec_crypt_function_message << "\n";
  if (!result.exec_des_function_messages.empty())
    report << "exec_des_function_messages="
           << result.exec_des_function_messages << "\n";
  if (!result.exec_kdf_function_message.empty())
    report << "exec_kdf_function_message="
           << result.exec_kdf_function_message << "\n";
  if (!result.exec_zlib_compression_have_rows.empty())
    report << "exec_zlib_compression_have_rows="
           << result.exec_zlib_compression_have_rows << "\n";
  if (!result.exec_zlib_compression_crc32_rows.empty())
    report << "exec_zlib_compression_crc32_rows="
           << result.exec_zlib_compression_crc32_rows << "\n";
  if (!result.exec_zlib_compression_messages.empty())
    report << "exec_zlib_compression_messages="
           << result.exec_zlib_compression_messages << "\n";
  if (!result.exec_zlib_compressed_column_message.empty())
    report << "exec_zlib_compressed_column_message="
           << result.exec_zlib_compressed_column_message << "\n";
  if (!result.exec_dynamic_plugin_loading_have_rows.empty())
    report << "exec_dynamic_plugin_loading_have_rows="
           << result.exec_dynamic_plugin_loading_have_rows << "\n";
  if (!result.exec_query_cache_have_rows.empty())
    report << "exec_query_cache_have_rows="
           << result.exec_query_cache_have_rows << "\n";
  if (!result.exec_query_cache_size_rows.empty())
    report << "exec_query_cache_size_rows="
           << result.exec_query_cache_size_rows << "\n";
  if (!result.exec_query_cache_type_rows.empty())
    report << "exec_query_cache_type_rows="
           << result.exec_query_cache_type_rows << "\n";
  if (!result.exec_query_cache_resize_rows.empty())
    report << "exec_query_cache_resize_rows="
           << result.exec_query_cache_resize_rows << "\n";
  if (!result.exec_query_cache_select_rows.empty())
    report << "exec_query_cache_select_rows="
           << result.exec_query_cache_select_rows << "\n";
  if (!result.exec_show_profiles_message.empty())
    report << "exec_show_profiles_message="
           << result.exec_show_profiles_message << "\n";
  if (!result.exec_help_message.empty())
    report << "exec_help_message=" << result.exec_help_message << "\n";
  if (!result.exec_procedure_analyse_message.empty())
    report << "exec_procedure_analyse_message="
           << result.exec_procedure_analyse_message << "\n";
  if (!result.exec_table_admin_messages.empty())
    report << "exec_table_admin_messages="
           << result.exec_table_admin_messages << "\n";
  if (!result.exec_sequence_messages.empty())
    report << "exec_sequence_messages=" << result.exec_sequence_messages
           << "\n";
  if (!result.exec_csv_engine_message.empty())
    report << "exec_csv_engine_message="
           << result.exec_csv_engine_message << "\n";
  if (!result.exec_myisam_engine_message.empty())
    report << "exec_myisam_engine_message="
           << result.exec_myisam_engine_message << "\n";
  if (!result.exec_mrg_myisam_engine_message.empty())
    report << "exec_mrg_myisam_engine_message="
           << result.exec_mrg_myisam_engine_message << "\n";
  if (!result.exec_memory_temp_rows.empty())
    report << "exec_memory_temp_rows=" << result.exec_memory_temp_rows << "\n";
  if (!result.exec_disk_temp_message.empty())
    report << "exec_disk_temp_message="
           << result.exec_disk_temp_message << "\n";
  if (!result.exec_callback_abort_message.empty())
    report << "exec_callback_abort_message="
           << result.exec_callback_abort_message << "\n";
  if (!result.exec_dml_rows.empty())
    report << "exec_dml_rows=" << result.exec_dml_rows << "\n";
  if (!result.exec_duplicate_key_message.empty())
    report << "exec_duplicate_key_message="
           << result.exec_duplicate_key_message << "\n";
  if (!result.exec_reopen_rows.empty())
    report << "exec_reopen_rows=" << result.exec_reopen_rows << "\n";
  if (!result.statement_effects.empty())
    report << "statement_effects=" << result.statement_effects << "\n";
  if (!result.warning_misuse_message.empty())
    report << "warning_misuse_message=" << result.warning_misuse_message
           << "\n";
  if (!result.warning_first.empty())
    report << "warning_first=" << result.warning_first << "\n";
  if (!result.warning_notfound_message.empty())
    report << "warning_notfound_message=" << result.warning_notfound_message
           << "\n";
  if (!result.warning_error.empty())
    report << "warning_error=" << result.warning_error << "\n";
  if (!result.warning_effects.empty())
    report << "warning_effects=" << result.warning_effects << "\n";
  if (!result.readonly_rows.empty())
    report << "readonly_rows=" << result.readonly_rows << "\n";
  if (!result.readonly_insert_message.empty())
    report << "readonly_insert_message=" << result.readonly_insert_message
           << "\n";
  if (!result.readonly_create_message.empty())
    report << "readonly_create_message=" << result.readonly_create_message
           << "\n";
  if (!result.readonly_prepare_message.empty())
    report << "readonly_prepare_message=" << result.readonly_prepare_message
           << "\n";
  if (!result.readonly_file.empty())
    report << "readonly_file=" << result.readonly_file << "\n";
  if (!result.exclusive_existing_message.empty())
    report << "exclusive_existing_message="
           << result.exclusive_existing_message << "\n";
  if (!result.uri_rows.empty())
    report << "uri_rows=" << result.uri_rows << "\n";
  if (!result.uri_readonly_rows.empty())
    report << "uri_readonly_rows=" << result.uri_readonly_rows << "\n";
  if (!result.uri_readonly_insert_message.empty())
    report << "uri_readonly_insert_message="
           << result.uri_readonly_insert_message << "\n";
  if (!result.prepared_unbound_message.empty())
    report << "prepared_unbound_message=" << result.prepared_unbound_message
           << "\n";
  if (!result.prepared_invalid_bind_message.empty())
    report << "prepared_invalid_bind_message="
           << result.prepared_invalid_bind_message << "\n";
  if (!result.prepared_rebind_message.empty())
    report << "prepared_rebind_message=" << result.prepared_rebind_message
           << "\n";
  if (!result.prepared_columns.empty())
    report << "prepared_columns=" << result.prepared_columns << "\n";
  if (!result.prepared_types.empty())
    report << "prepared_types=" << result.prepared_types << "\n";
  if (!result.prepared_rows.empty())
    report << "prepared_rows=" << result.prepared_rows << "\n";
  if (!result.prepared_reset_row.empty())
    report << "prepared_reset_row=" << result.prepared_reset_row << "\n";
  if (!result.prepared_dml_rows.empty())
    report << "prepared_dml_rows=" << result.prepared_dml_rows << "\n";
  if (!result.prepared_bound_rows.empty())
    report << "prepared_bound_rows=" << result.prepared_bound_rows << "\n";
  if (!result.prepared_bound_reset_rows.empty())
    report << "prepared_bound_reset_rows="
           << result.prepared_bound_reset_rows << "\n";
  if (!result.prepared_bind_destructor_count.empty())
    report << "prepared_bind_destructor_count="
           << result.prepared_bind_destructor_count << "\n";
  if (!result.prepared_close_busy_message.empty())
    report << "prepared_close_busy_message="
           << result.prepared_close_busy_message << "\n";
  report << "\n";

  report << "## Cases\n\n";
  for (const CaseResult &case_result : result.cases)
  {
    report << "label=" << case_result.label << "\n";
    report << "passed=" << (case_result.passed ? "yes" : "no") << "\n";
    report << "expected=" << case_result.expected << "\n";
    report << "actual=" << case_result.actual << "\n";
    report << "errcode=" << case_result.errcode << "\n";
    report << "extended_errcode=" << case_result.extended_errcode << "\n";
    report << "mariadb_errno=" << case_result.mariadb_errno << "\n";
    report << "sqlstate=" << case_result.sqlstate << "\n";
    report << "message=" << case_result.message << "\n\n";
  }
}

static bool option_value(const char *arg, const char *name, std::string *value)
{
  const size_t name_len= std::strlen(name);
  if (std::strncmp(arg, name, name_len) != 0)
    return false;
  *value= arg + name_len;
  return true;
}

static bool require_option(const std::string &value, const char *name,
                           std::string *error)
{
  if (!value.empty())
    return true;
  *error= std::string("missing required option: ") + name;
  return false;
}
