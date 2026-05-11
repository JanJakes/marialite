/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mylite.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

struct SmokeOptions
{
  std::string database;
  std::string report;
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
  std::string exec_callback_abort_message;
  std::string exec_dml_rows;
  std::string exec_duplicate_key_message;
  std::string exec_reopen_rows;
  std::string statement_effects;
  std::string prepared_param_message;
  std::string prepared_columns;
  std::string prepared_types;
  std::string prepared_rows;
  std::string prepared_reset_row;
  std::string prepared_dml_rows;
  std::string prepared_close_busy_message;
  std::vector<CaseResult> cases;
};

struct ExecCapture
{
  std::vector<std::string> columns;
  std::vector<std::string> rows;
  int abort_after= 0;
};

static bool parse_options(int argc, char **argv, SmokeOptions *options,
                          std::string *error);
static int run_smoke(const SmokeOptions &options, SmokeResult *result);
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
static bool check_exec_callback_abort(const SmokeOptions &options,
                                      SmokeResult *result);
static bool check_exec_dml_persistence(const SmokeOptions &options,
                                       SmokeResult *result);
static bool check_statement_effects(const SmokeOptions &options,
                                    SmokeResult *result);
static bool check_prepared_statement_api(const SmokeOptions &options,
                                         SmokeResult *result);
static bool exec_statement(mylite_db *db, const char *sql, const char *label,
                           SmokeResult *result);
static bool exec_query_capture(mylite_db *db, const char *sql,
                               const char *label, ExecCapture *capture,
                               SmokeResult *result);
static void append_statement_effect(SmokeResult *result, const char *label,
                                    const std::string &value);
static std::string statement_effect_summary(mylite_db *db);
static std::string prepared_row_summary(mylite_stmt *stmt);
static std::string prepared_type_summary(mylite_stmt *stmt);
static std::string hex_bytes(const void *data, size_t length);
static int capture_exec_row(void *ctx, int column_count, char **values,
                            char **column_names);
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
    else
    {
      *error= std::string("unknown argument: ") + argv[i];
      return false;
    }
  }

  return require_option(options->database, "--database", error) &&
         require_option(options->report, "--report", error);
}

static int run_smoke(const SmokeOptions &options, SmokeResult *result)
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
  const std::string missing= options.database + ".missing";
  unlink(missing.c_str());

  mylite_db *db= nullptr;
  const int rc= mylite_open_v2(missing.c_str(), &db, MYLITE_OPEN_READONLY,
                               nullptr);
  const bool ok= record_result(result, "readonly_missing", MYLITE_CANTOPEN,
                               rc, db);
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

  mylite_db *db= nullptr;
  rc= mylite_open(options.database.c_str(), &db);
  ok= record_result(result, "exec_misuse_open", MYLITE_OK, rc, db) && ok;
  if (db)
  {
    rc= mylite_exec(db, nullptr, nullptr, nullptr, nullptr);
    ok= record_result(result, "exec_null_sql", MYLITE_MISUSE, rc, db) &&
        ok;
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
  result->prepared_param_message= mylite_errmsg(db);
  ok= record_result(result, "prepare_parameter_marker", MYLITE_MISUSE, rc,
                    db) && ok;
  if (result->prepared_param_message != "parameter binding is not implemented")
    ok= false;

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

static std::string prepared_row_summary(mylite_stmt *stmt)
{
  const char *note= mylite_column_text(stmt, 1);
  const void *payload= mylite_column_blob(stmt, 2);
  return std::to_string(mylite_column_int64(stmt, 0)) + ":" +
         (note ? note : "NULL") + ":" +
         (payload ? hex_bytes(payload, mylite_column_bytes(stmt, 2)) :
          "NULL");
}

static std::string prepared_type_summary(mylite_stmt *stmt)
{
  std::vector<std::string> types;
  for (unsigned i= 0; i < mylite_column_count(stmt); ++i)
    types.push_back(std::to_string(mylite_column_type(stmt, i)));
  return join_strings(types, ",");
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
  if (!result.prepared_param_message.empty())
    report << "prepared_param_message=" << result.prepared_param_message
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
