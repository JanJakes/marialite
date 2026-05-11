/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include <mysql.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

struct SmokeOptions
{
  std::string datadir;
  std::string tmpdir;
  std::string lc_messages_dir;
  std::string runtime_dir;
  std::string catalog_file;
  std::string persistence_phase= "none";
  std::string report;
};

struct SmokeResult
{
  int status= 1;
  std::string phase;
  std::string message;
  std::string engine;
  std::string support;
  std::string transactions;
  std::string discovered_table;
  std::string count;
  std::string created_count;
  std::string altered_column;
  std::string copy_alter_rows;
  std::string copy_alter_note_order_ids;
  std::string copy_alter_null_note_ids;
  std::string copy_alter_payload_lookup_id;
  std::string copy_alter_autoincrement_id;
  std::string renamed_count;
  std::string dropped_table;
  std::string row_count;
  std::string row_notes;
  std::string row_updated_note;
  std::string row_deleted_count;
  std::string unsupported_autoincrement;
  std::string large_row_value;
  std::string large_row_updated_length;
  std::string large_row_deleted_count;
  std::string blob_text_lengths;
  std::string blob_text_edges;
  std::string blob_text_updated;
  std::string blob_text_deleted_count;
  std::string blob_text_key_lookup_id;
  std::string blob_text_key_order_ids;
  std::string blob_text_key_duplicate;
  std::string blob_text_key_updated_id;
  std::string blob_text_key_deleted_count;
  std::string nullable_key_null_ids;
  std::string nullable_key_lookup_id;
  std::string nullable_key_duplicate;
  std::string nullable_key_updated_null_ids;
  std::string nullable_key_deleted_count;
  std::string nullable_composite_key_head_null_ids;
  std::string nullable_composite_key_tail_null_ids;
  std::string nullable_composite_key_duplicate;
  std::string nullable_blob_key_null_ids;
  std::string nullable_blob_key_lookup_id;
  std::string nullable_blob_key_duplicate;
  std::string unsupported_foreign_key_create;
  std::string unsupported_foreign_key_alter;
  std::string foreign_key_parent_count;
  std::string unsupported_generated_virtual;
  std::string unsupported_generated_stored;
  std::string unsupported_generated_alter;
  std::string generated_base_count;
  std::string check_constraint_insert;
  std::string check_constraint_update;
  std::string check_constraint_rows;
  std::string unsupported_geometry;
  std::string key_lookup_note;
  std::string key_order_ids;
  std::string duplicate_key;
  std::string update_duplicate_key;
  std::string unsupported_reverse_key;
  std::string autoincrement_ids;
  std::string explicit_autoincrement_ids;
  std::string persisted_count;
  std::string persisted_column;
  std::string persisted_notes;
  std::string persisted_autoincrement_ids;
  std::string persisted_key_lookup_note;
  std::string persisted_key_order_ids;
  std::string persisted_note_lookup_id;
  std::string persisted_note_order_ids;
  std::string persisted_large_lengths;
  std::string persisted_large_edges;
  std::string persisted_wide_count;
  std::string persisted_blob_text_lengths;
  std::string persisted_blob_text_edges;
  std::string persisted_blob_text_rollback;
  std::string persisted_blob_text_key_lookup_id;
  std::string persisted_blob_text_key_order_ids;
  std::string persisted_nullable_key_null_ids;
  std::string persisted_nullable_key_lookup_id;
  std::string persisted_copy_alter_rows;
  std::string persisted_copy_alter_note_order_ids;
  std::string persisted_copy_alter_null_note_ids;
  std::string persisted_copy_alter_payload_lookup_id;
  std::string persisted_copy_alter_autoincrement_id;
  std::string persisted_check_rows;
  std::string persisted_check_insert;
  std::string recovery_marker;
  std::string recovery_reclaim;
  std::string transaction_rollback_rows;
  std::string transaction_clean_savepoint_rows;
  std::string transaction_savepoint_rows;
  std::string transaction_release_rows;
  std::string transaction_rows;
  std::string transaction_rollback_warnings;
  std::string statement_autocommit_error;
  std::string statement_autocommit_rows;
  std::string statement_autocommit_warnings;
  std::string statement_transaction_error;
  std::string statement_transaction_rows;
  std::string statement_transaction_warnings;
  std::string statement_rows;
};

static bool parse_options(int argc, char **argv, SmokeOptions *options,
                          std::string *error);
static std::vector<std::string> build_server_args(const SmokeOptions &options);
static int run_smoke(const SmokeOptions &options,
                     const std::vector<std::string> &server_args,
                     SmokeResult *result);
static bool fetch_mylite_engine(MYSQL *mysql, SmokeResult *result);
static bool fetch_discovered_table(MYSQL *mysql, SmokeResult *result);
static bool fetch_probe_count(MYSQL *mysql, SmokeResult *result);
static bool exercise_ddl(MYSQL *mysql, SmokeResult *result);
static bool exercise_dml(MYSQL *mysql, SmokeResult *result);
static bool exercise_index_dml(MYSQL *mysql, SmokeResult *result);
static bool exercise_persistence_write(MYSQL *mysql, SmokeResult *result);
static bool exercise_persistence_read(MYSQL *mysql, SmokeResult *result);
static bool exercise_recovery_latest(MYSQL *mysql, SmokeResult *result);
static bool exercise_recovery_read(MYSQL *mysql, SmokeResult *result);
static bool exercise_transaction_boundary_write(MYSQL *mysql,
                                                SmokeResult *result);
static bool exercise_transaction_boundary_read(MYSQL *mysql,
                                               SmokeResult *result);
static bool exercise_statement_error_rollback_write(MYSQL *mysql,
                                                    SmokeResult *result);
static bool exercise_statement_error_rollback_read(MYSQL *mysql,
                                                   SmokeResult *result);
static bool write_recovery_page_payload(MYSQL *mysql, const char *table_name,
                                        char fill, std::string *count,
                                        SmokeResult *result);
static bool execute_statement(MYSQL *mysql, const char *statement,
                              const char *label, SmokeResult *result);
static bool execute_statement_expect_error(MYSQL *mysql, const char *statement,
                                           const char *label,
                                           std::string *value,
                                           SmokeResult *result);
static bool fetch_single_value(MYSQL *mysql, const char *query,
                               const char *label, std::string *value,
                               SmokeResult *result);
static bool fetch_warning_summary(MYSQL *mysql, const char *label,
                                  std::string *value, SmokeResult *result);
static bool verify_table_present(MYSQL *mysql, const char *query,
                                 const char *label, SmokeResult *result);
static bool verify_table_absent(MYSQL *mysql, const char *query,
                                const char *label, SmokeResult *result);
static bool phase_loads_existing_catalog(const std::string &phase);
static void write_report(const SmokeOptions &options,
                         const std::vector<std::string> &server_args,
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

  std::vector<std::string> server_args= build_server_args(options);
  SmokeResult result;
  const int status= run_smoke(options, server_args, &result);
  write_report(options, server_args, result);
  return status;
}

static bool parse_options(int argc, char **argv, SmokeOptions *options,
                          std::string *error)
{
  for (int i= 1; i < argc; ++i)
  {
    std::string value;
    if (option_value(argv[i], "--datadir=", &value))
      options->datadir= value;
    else if (option_value(argv[i], "--tmpdir=", &value))
      options->tmpdir= value;
    else if (option_value(argv[i], "--lc-messages-dir=", &value))
      options->lc_messages_dir= value;
    else if (option_value(argv[i], "--runtime-dir=", &value))
      options->runtime_dir= value;
    else if (option_value(argv[i], "--catalog-file=", &value))
      options->catalog_file= value;
    else if (option_value(argv[i], "--persistence-phase=", &value))
      options->persistence_phase= value;
    else if (option_value(argv[i], "--report=", &value))
      options->report= value;
    else
    {
      *error= std::string("unknown argument: ") + argv[i];
      return false;
    }
  }

  if (options->persistence_phase != "none" &&
      options->persistence_phase != "write" &&
      options->persistence_phase != "read" &&
      options->persistence_phase != "recovery-base" &&
      options->persistence_phase != "recovery-latest" &&
      options->persistence_phase != "recovery-read" &&
      options->persistence_phase != "transaction-write" &&
      options->persistence_phase != "transaction-read" &&
      options->persistence_phase != "lock-conflict")
  {
    *error= "unsupported persistence phase";
    return false;
  }

  if (options->persistence_phase != "none" && options->catalog_file.empty())
  {
    *error= "catalog file is required for persistence phases";
    return false;
  }

  return require_option(options->datadir, "--datadir", error) &&
         require_option(options->tmpdir, "--tmpdir", error) &&
         require_option(options->lc_messages_dir, "--lc-messages-dir", error) &&
         require_option(options->runtime_dir, "--runtime-dir", error) &&
         require_option(options->report, "--report", error);
}

static std::vector<std::string> build_server_args(const SmokeOptions &options)
{
  std::vector<std::string> args= {
    "mylite-storage-engine-smoke",
    "--no-defaults",
    "--datadir=" + options.datadir,
    "--tmpdir=" + options.tmpdir,
    "--lc-messages-dir=" + options.lc_messages_dir,
    "--skip-grant-tables",
    "--skip-networking",
    "--skip-name-resolve",
    "--skip-external-locking",
    "--skip-slave-start",
    "--log-output=NONE",
    "--pid-file=" + options.runtime_dir + "/mariadb.pid",
    "--socket=" + options.runtime_dir + "/mariadb.sock"
  };
  if (!options.catalog_file.empty())
    args.push_back("--mylite-catalog-file=" + options.catalog_file);
  return args;
}

static int run_smoke(const SmokeOptions &options,
                     const std::vector<std::string> &server_args,
                     SmokeResult *result)
{
  std::vector<char *> server_argv;
  server_argv.reserve(server_args.size());
  for (const std::string &arg : server_args)
    server_argv.push_back(const_cast<char *>(arg.c_str()));

  char server_group[]= "server";
  char embedded_group[]= "embedded";
  char *groups[]= { server_group, embedded_group, nullptr };

  bool server_started= false;
  MYSQL *mysql= nullptr;
  const bool lock_conflict_phase=
    options.persistence_phase == "lock-conflict";

  result->phase= "mysql_server_init";
  if (mysql_server_init(static_cast<int>(server_argv.size()),
                        server_argv.data(), groups) != 0)
  {
    result->message= "mysql_server_init failed";
    goto done;
  }
  server_started= true;

  result->phase= "mysql_init";
  mysql= mysql_init(nullptr);
  if (!mysql)
  {
    result->message= "mysql_init failed";
    goto done;
  }

  result->phase= "mysql_real_connect";
  if (!mysql_real_connect(mysql, nullptr, "root", nullptr, nullptr, 0, nullptr,
                          0))
  {
    result->message= std::string("mysql_real_connect failed: ") +
                     mysql_error(mysql);
    goto done;
  }

  result->phase= "engine_lookup";
  if (!fetch_mylite_engine(mysql, result))
    goto done;

  if (!lock_conflict_phase &&
      !phase_loads_existing_catalog(options.persistence_phase))
  {
    result->phase= "table_names";
    if (!fetch_discovered_table(mysql, result))
      goto done;
  }

  if (!lock_conflict_phase)
  {
    result->phase= "table_scan";
    if (!fetch_probe_count(mysql, result))
      goto done;
  }

  if (lock_conflict_phase)
  {
    result->phase= "lock_conflict_write";
    if (execute_statement(mysql,
                          "CREATE TABLE mylite.lock_conflict "
                          "(id INT) ENGINE=MYLITE",
                          "lock conflict CREATE", result))
      result->message= "lock conflict CREATE unexpectedly succeeded";
    goto done;
  }
  else if (options.persistence_phase == "write")
  {
    result->phase= "persistence_write";
    if (!exercise_persistence_write(mysql, result))
      goto done;
  }
  else if (options.persistence_phase == "recovery-base")
  {
    result->phase= "recovery_base";
    if (!exercise_persistence_write(mysql, result))
      goto done;
  }
  else if (options.persistence_phase == "recovery-latest")
  {
    result->phase= "recovery_latest";
    if (!exercise_recovery_latest(mysql, result))
      goto done;
  }
  else if (options.persistence_phase == "recovery-read")
  {
    result->phase= "recovery_read";
    if (!exercise_recovery_read(mysql, result))
      goto done;
  }
  else if (options.persistence_phase == "transaction-write")
  {
    result->phase= "transaction_boundary_write";
    if (!exercise_transaction_boundary_write(mysql, result))
      goto done;
  }
  else if (options.persistence_phase == "transaction-read")
  {
    result->phase= "transaction_boundary_read";
    if (!exercise_transaction_boundary_read(mysql, result))
      goto done;
  }
  else if (options.persistence_phase == "read")
  {
    result->phase= "persistence_read";
    if (!exercise_persistence_read(mysql, result))
      goto done;
  }
  else
  {
    result->phase= "ddl_lifecycle";
    if (!exercise_ddl(mysql, result))
      goto done;
    result->phase= "dml_lifecycle";
    if (!exercise_dml(mysql, result))
      goto done;
    result->phase= "index_lifecycle";
    if (!exercise_index_dml(mysql, result))
      goto done;
  }

  result->phase= "complete";
  result->status= 0;
  result->message= "ok";

done:
  if (mysql)
    mysql_close(mysql);
  if (server_started)
    mysql_server_end();
  return result->status;
}

static bool fetch_mylite_engine(MYSQL *mysql, SmokeResult *result)
{
  const char query[]=
    "SELECT ENGINE, SUPPORT, TRANSACTIONS FROM information_schema.ENGINES "
    "WHERE ENGINE = 'MYLITE'";

  if (mysql_query(mysql, query))
  {
    result->message= std::string("engine query failed: ") +
                     mysql_error(mysql);
    return false;
  }

  MYSQL_RES *res= mysql_store_result(mysql);
  if (!res)
  {
    result->message= std::string("engine query result failed: ") +
                     mysql_error(mysql);
    return false;
  }

  bool ok= false;
  MYSQL_ROW row= mysql_fetch_row(res);
  if (mysql_num_fields(res) != 3)
    result->message= "engine query returned an unexpected column count";
  else if (!row || !row[0] || !row[1] || !row[2])
    result->message= "MYLITE engine was not registered";
  else if (std::strcmp(row[0], "MYLITE") != 0)
  {
    result->engine= row[0];
    result->message= "engine query returned an unexpected engine";
  }
  else if (std::strcmp(row[2], "YES") != 0)
  {
    result->transactions= row[2];
    result->message= "MYLITE engine did not report transaction support";
  }
  else if (mysql_fetch_row(res) != nullptr)
    result->message= "engine query returned more than one row";
  else
  {
    result->engine= row[0];
    result->support= row[1];
    result->transactions= row[2];
    ok= true;
  }

  mysql_free_result(res);
  return ok;
}

static bool fetch_discovered_table(MYSQL *mysql, SmokeResult *result)
{
  if (mysql_query(mysql, "SHOW TABLES FROM mylite"))
  {
    result->message= std::string("show tables failed: ") +
                     mysql_error(mysql);
    return false;
  }

  MYSQL_RES *res= mysql_store_result(mysql);
  if (!res)
  {
    result->message= std::string("show tables result failed: ") +
                     mysql_error(mysql);
    return false;
  }

  bool ok= false;
  MYSQL_ROW row= mysql_fetch_row(res);
  if (mysql_num_fields(res) != 1)
    result->message= "SHOW TABLES returned an unexpected column count";
  else if (!row || !row[0])
    result->message= "MYLITE seed table was not discovered";
  else if (std::strcmp(row[0], "probe") != 0)
  {
    result->discovered_table= row[0];
    result->message= "SHOW TABLES returned an unexpected table";
  }
  else if (mysql_fetch_row(res) != nullptr)
    result->message= "SHOW TABLES returned more than one row";
  else
  {
    result->discovered_table= row[0];
    ok= true;
  }

  mysql_free_result(res);
  return ok;
}

static bool fetch_probe_count(MYSQL *mysql, SmokeResult *result)
{
  if (mysql_query(mysql, "SELECT COUNT(*) FROM mylite.probe"))
  {
    result->message= std::string("probe count failed: ") +
                     mysql_error(mysql);
    return false;
  }

  MYSQL_RES *res= mysql_store_result(mysql);
  if (!res)
  {
    result->message= std::string("probe count result failed: ") +
                     mysql_error(mysql);
    return false;
  }

  bool ok= false;
  MYSQL_ROW row= mysql_fetch_row(res);
  if (mysql_num_fields(res) != 1)
    result->message= "probe count returned an unexpected column count";
  else if (!row || !row[0])
    result->message= "probe count returned no value";
  else if (std::strcmp(row[0], "0") != 0)
  {
    result->count= row[0];
    result->message= "probe count returned an unexpected value";
  }
  else if (mysql_fetch_row(res) != nullptr)
    result->message= "probe count returned more than one row";
  else
  {
    result->count= row[0];
    ok= true;
  }

  mysql_free_result(res);
  return ok;
}

static bool exercise_ddl(MYSQL *mysql, SmokeResult *result)
{
  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.created "
                         "(id INT) ENGINE=MYLITE",
                         "CREATE TABLE", result))
    return false;
  if (!execute_statement(mysql, "FLUSH TABLES", "FLUSH TABLES after CREATE",
                         result))
    return false;

  if (!fetch_single_value(mysql, "SELECT COUNT(*) FROM mylite.created",
                          "created count", &result->created_count, result))
    return false;
  if (result->created_count != "0")
  {
    result->message= "created table count returned an unexpected value";
    return false;
  }

  if (!execute_statement(mysql,
                         "ALTER TABLE mylite.created "
                         "ADD COLUMN note VARCHAR(12), ALGORITHM=COPY",
                         "ALTER TABLE", result))
    return false;
  if (!execute_statement(mysql, "FLUSH TABLES", "FLUSH TABLES after ALTER",
                         result))
    return false;

  if (!fetch_single_value(mysql,
                          "SHOW COLUMNS FROM mylite.created LIKE 'note'",
                          "altered column", &result->altered_column, result))
    return false;
  if (result->altered_column != "note")
  {
    result->message= "altered table did not expose the added column";
    return false;
  }

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.copy_alter_rows "
                         "(id INT NOT NULL AUTO_INCREMENT, "
                         "note VARCHAR(12), payload TEXT NOT NULL, "
                         "marker BLOB NOT NULL, PRIMARY KEY(id), "
                         "KEY note_key(note), "
                         "KEY payload_prefix(payload(8))) ENGINE=MYLITE",
                         "CREATE populated copy ALTER table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.copy_alter_rows "
                         "(note, payload, marker) VALUES "
                         "('beta', 'payload-beta', 'm1'), "
                         "(NULL, 'payload-null', 'm2'), "
                         "('alpha', 'payload-alpha', 'm3')",
                         "INSERT populated copy ALTER rows", result))
    return false;
  if (!execute_statement(mysql,
                         "ALTER TABLE mylite.copy_alter_rows "
                         "ADD COLUMN status VARCHAR(12) NOT NULL "
                         "DEFAULT 'active', ALGORITHM=COPY",
                         "ALTER populated table", result))
    return false;
  if (!execute_statement(mysql, "FLUSH TABLES",
                         "FLUSH TABLES after populated ALTER", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT("
                          "id, ':', COALESCE(note, 'NULL'), ':', "
                          "CHAR_LENGTH(payload), ':', LENGTH(marker), ':', "
                          "status) ORDER BY id SEPARATOR ',') "
                          "FROM mylite.copy_alter_rows",
                          "copy ALTER rows",
                          &result->copy_alter_rows, result))
    return false;
  if (result->copy_alter_rows !=
      "1:beta:12:2:active,2:NULL:12:2:active,3:alpha:13:2:active")
  {
    result->message= "copy ALTER rows returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY note "
                          "SEPARATOR ',') FROM mylite.copy_alter_rows "
                          "FORCE INDEX(note_key)",
                          "copy ALTER note key order",
                          &result->copy_alter_note_order_ids, result))
    return false;
  if (result->copy_alter_note_order_ids != "2,3,1")
  {
    result->message= "copy ALTER note key order returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.copy_alter_rows FORCE INDEX(note_key) "
                          "WHERE note IS NULL",
                          "copy ALTER nullable key lookup",
                          &result->copy_alter_null_note_ids, result))
    return false;
  if (result->copy_alter_null_note_ids != "2")
  {
    result->message= "copy ALTER nullable key lookup returned an "
                     "unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT id FROM mylite.copy_alter_rows "
                          "FORCE INDEX(payload_prefix) "
                          "WHERE payload = 'payload-alpha'",
                          "copy ALTER payload prefix lookup",
                          &result->copy_alter_payload_lookup_id, result))
    return false;
  if (result->copy_alter_payload_lookup_id != "3")
  {
    result->message= "copy ALTER payload prefix lookup returned an "
                     "unexpected value";
    return false;
  }
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.copy_alter_rows "
                         "(note, payload, marker) VALUES "
                         "('gamma', 'payload-gamma', 'm4')",
                         "INSERT after copy ALTER", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT MAX(id) FROM mylite.copy_alter_rows",
                          "copy ALTER autoincrement id",
                          &result->copy_alter_autoincrement_id, result))
    return false;
  if (result->copy_alter_autoincrement_id != "4")
  {
    result->message= "copy ALTER autoincrement returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql, "DROP TABLE mylite.copy_alter_rows",
                         "DROP populated copy ALTER table", result))
    return false;

  if (!execute_statement(mysql,
                         "RENAME TABLE mylite.created TO mylite.renamed",
                         "RENAME TABLE", result))
    return false;
  if (!execute_statement(mysql, "FLUSH TABLES", "FLUSH TABLES after RENAME",
                         result))
    return false;

  if (!fetch_single_value(mysql, "SELECT COUNT(*) FROM mylite.renamed",
                          "renamed count", &result->renamed_count, result))
    return false;
  if (result->renamed_count != "0")
  {
    result->message= "renamed table count returned an unexpected value";
    return false;
  }

  if (!execute_statement(mysql, "DROP TABLE mylite.renamed", "DROP TABLE",
                         result))
    return false;
  if (!execute_statement(mysql, "FLUSH TABLES", "FLUSH TABLES after DROP",
                         result))
    return false;

  if (!verify_table_absent(mysql, "SHOW TABLES FROM mylite LIKE 'renamed'",
                           "renamed table", result))
    return false;
  result->dropped_table= "renamed";

  return fetch_discovered_table(mysql, result);
}

static bool exercise_dml(MYSQL *mysql, SmokeResult *result)
{
  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.rows "
                         "(id INT, note VARCHAR(12)) ENGINE=MYLITE",
                         "CREATE rows table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.rows VALUES "
                         "(1, 'one'), (2, 'two')",
                         "INSERT rows", result))
    return false;

  if (!fetch_single_value(mysql, "SELECT COUNT(*) FROM mylite.rows",
                          "row count", &result->row_count, result))
    return false;
  if (result->row_count != "2")
  {
    result->message= "row count returned an unexpected value";
    return false;
  }

  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(note ORDER BY id SEPARATOR ',') "
                          "FROM mylite.rows",
                          "row notes", &result->row_notes, result))
    return false;
  if (result->row_notes != "one,two")
  {
    result->message= "row notes returned an unexpected value";
    return false;
  }

  if (!execute_statement(mysql,
                         "UPDATE mylite.rows SET note = 'deux' WHERE id = 2",
                         "UPDATE rows", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT note FROM mylite.rows WHERE id = 2",
                          "updated row note", &result->row_updated_note,
                          result))
    return false;
  if (result->row_updated_note != "deux")
  {
    result->message= "updated row note returned an unexpected value";
    return false;
  }

  if (!execute_statement(mysql, "DELETE FROM mylite.rows WHERE id = 1",
                         "DELETE rows", result))
    return false;
  if (!fetch_single_value(mysql, "SELECT COUNT(*) FROM mylite.rows",
                          "deleted row count",
                          &result->row_deleted_count, result))
    return false;
  if (result->row_deleted_count != "1")
  {
    result->message= "deleted row count returned an unexpected value";
    return false;
  }

  if (!execute_statement(mysql, "DROP TABLE mylite.rows", "DROP rows table",
                         result))
    return false;
  if (!execute_statement(mysql, "FLUSH TABLES",
                         "FLUSH TABLES after DML DROP", result))
    return false;

  if (!verify_table_absent(mysql, "SHOW TABLES FROM mylite LIKE 'rows'",
                           "rows table", result))
    return false;

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.large_rows "
                         "(id INT, note VARCHAR(5000) NOT NULL) "
                         "ENGINE=MYLITE",
                         "CREATE large row table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.large_rows VALUES "
                         "(1, REPEAT('a', 5000)), "
                         "(2, REPEAT('b', 4200))",
                         "INSERT large rows", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT CONCAT(CHAR_LENGTH(note), ':', "
                          "LEFT(note, 1), ':', RIGHT(note, 1)) "
                          "FROM mylite.large_rows WHERE id = 1",
                          "large row value", &result->large_row_value,
                          result))
    return false;
  if (result->large_row_value != "5000:a:a")
  {
    result->message= "large row value returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql,
                         "UPDATE mylite.large_rows "
                         "SET note = REPEAT('c', 4500) WHERE id = 2",
                         "UPDATE large row", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT CHAR_LENGTH(note) "
                          "FROM mylite.large_rows WHERE id = 2",
                          "large row updated length",
                          &result->large_row_updated_length, result))
    return false;
  if (result->large_row_updated_length != "4500")
  {
    result->message= "large row update returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql,
                         "DELETE FROM mylite.large_rows WHERE id = 1",
                         "DELETE large row", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT COUNT(*) FROM mylite.large_rows",
                          "large row deleted count",
                          &result->large_row_deleted_count, result))
    return false;
  if (result->large_row_deleted_count != "1")
  {
    result->message= "large row delete returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql, "DROP TABLE mylite.large_rows",
                         "DROP large row table", result))
    return false;
  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.blob_text_rows "
                         "(id INT NOT NULL, note TEXT NOT NULL, "
                         "payload BLOB NOT NULL) ENGINE=MYLITE",
                         "CREATE BLOB/TEXT row table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.blob_text_rows VALUES "
                         "(1, REPEAT('n', 3000), REPEAT('p', 5000)), "
                         "(2, 'short', '')",
                         "INSERT BLOB/TEXT rows", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT(CHAR_LENGTH(note), "
                          "':', LENGTH(payload)) ORDER BY id SEPARATOR ',') "
                          "FROM mylite.blob_text_rows",
                          "BLOB/TEXT lengths", &result->blob_text_lengths,
                          result))
    return false;
  if (result->blob_text_lengths != "3000:5000,5:0")
  {
    result->message= "BLOB/TEXT lengths returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT(LEFT(note, 1), ':', "
                          "RIGHT(payload, 1)) ORDER BY id SEPARATOR ',') "
                          "FROM mylite.blob_text_rows",
                          "BLOB/TEXT edges", &result->blob_text_edges,
                          result))
    return false;
  if (result->blob_text_edges != "n:p,s:")
  {
    result->message= "BLOB/TEXT edges returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql,
                         "UPDATE mylite.blob_text_rows "
                         "SET note = REPEAT('u', 4200), "
                         "payload = REPEAT('q', 4100) WHERE id = 2",
                         "UPDATE BLOB/TEXT row", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT CONCAT(CHAR_LENGTH(note), ':', "
                          "LENGTH(payload), ':', LEFT(note, 1), ':', "
                          "RIGHT(payload, 1)) "
                          "FROM mylite.blob_text_rows WHERE id = 2",
                          "updated BLOB/TEXT row",
                          &result->blob_text_updated, result))
    return false;
  if (result->blob_text_updated != "4200:4100:u:q")
  {
    result->message= "BLOB/TEXT update returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql,
                         "DELETE FROM mylite.blob_text_rows WHERE id = 1",
                         "DELETE BLOB/TEXT row", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT COUNT(*) FROM mylite.blob_text_rows",
                          "BLOB/TEXT deleted count",
                          &result->blob_text_deleted_count, result))
    return false;
  if (result->blob_text_deleted_count != "1")
  {
    result->message= "BLOB/TEXT delete returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql, "DROP TABLE mylite.blob_text_rows",
                         "DROP BLOB/TEXT row table", result))
    return false;

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.blob_text_key_rows "
                         "(id INT NOT NULL, note TEXT NOT NULL, "
                         "payload BLOB NOT NULL, PRIMARY KEY(id), "
                         "KEY note_prefix(note(6)), "
                         "UNIQUE KEY payload_prefix(payload(4))) "
                         "ENGINE=MYLITE",
                         "CREATE BLOB/TEXT key table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.blob_text_key_rows VALUES "
                         "(1, 'alpha-row', 'aa11-first'), "
                         "(2, 'beta-row', 'bb22-second'), "
                         "(3, 'gamma-row', 'cc33-third')",
                         "INSERT BLOB/TEXT key rows", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT id FROM mylite.blob_text_key_rows "
                          "FORCE INDEX(note_prefix) WHERE note = 'beta-row'",
                          "BLOB/TEXT key lookup",
                          &result->blob_text_key_lookup_id, result))
    return false;
  if (result->blob_text_key_lookup_id != "2")
  {
    result->message= "BLOB/TEXT key lookup returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY note "
                          "SEPARATOR ',') "
                          "FROM mylite.blob_text_key_rows "
                          "FORCE INDEX(note_prefix)",
                          "BLOB/TEXT key order",
                          &result->blob_text_key_order_ids, result))
    return false;
  if (result->blob_text_key_order_ids != "1,2,3")
  {
    result->message= "BLOB/TEXT key order returned an unexpected value";
    return false;
  }
  if (!execute_statement_expect_error(mysql,
                                      "INSERT INTO mylite.blob_text_key_rows "
                                      "VALUES (4, 'delta-row', 'aa11-dupe')",
                                      "duplicate BLOB/TEXT key",
                                      &result->blob_text_key_duplicate,
                                      result))
    return false;
  if (!execute_statement(mysql,
                         "UPDATE mylite.blob_text_key_rows "
                         "SET note = 'delta-row', payload = 'dd44-third' "
                         "WHERE id = 3",
                         "UPDATE BLOB/TEXT key row", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT id FROM mylite.blob_text_key_rows "
                          "FORCE INDEX(note_prefix) "
                          "WHERE note = 'delta-row'",
                          "updated BLOB/TEXT key lookup",
                          &result->blob_text_key_updated_id, result))
    return false;
  if (result->blob_text_key_updated_id != "3")
  {
    result->message= "BLOB/TEXT key update returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql,
                         "DELETE FROM mylite.blob_text_key_rows WHERE id = 1",
                         "DELETE BLOB/TEXT key row", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT COUNT(*) "
                          "FROM mylite.blob_text_key_rows "
                          "FORCE INDEX(note_prefix) "
                          "WHERE note = 'alpha-row'",
                          "deleted BLOB/TEXT key count",
                          &result->blob_text_key_deleted_count, result))
    return false;
  if (result->blob_text_key_deleted_count != "0")
  {
    result->message= "BLOB/TEXT key delete returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql, "DROP TABLE mylite.blob_text_key_rows",
                         "DROP BLOB/TEXT key table", result))
    return false;

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.nullable_key_rows "
                         "(id INT NOT NULL, note VARCHAR(12), "
                         "code VARCHAR(12), PRIMARY KEY(id), "
                         "KEY note_key(note), UNIQUE KEY code_unique(code)) "
                         "ENGINE=MYLITE",
                         "CREATE nullable key table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.nullable_key_rows VALUES "
                         "(1, NULL, NULL), (2, NULL, NULL), "
                         "(3, 'alpha', 'alpha'), (4, 'beta', 'beta')",
                         "INSERT nullable key rows", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.nullable_key_rows "
                          "FORCE INDEX(note_key) WHERE note IS NULL",
                          "nullable key null ids",
                          &result->nullable_key_null_ids, result))
    return false;
  if (result->nullable_key_null_ids != "1,2")
  {
    result->message= "nullable key NULL lookup returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT id FROM mylite.nullable_key_rows "
                          "FORCE INDEX(note_key) WHERE note = 'beta'",
                          "nullable key lookup",
                          &result->nullable_key_lookup_id, result))
    return false;
  if (result->nullable_key_lookup_id != "4")
  {
    result->message= "nullable key lookup returned an unexpected value";
    return false;
  }
  if (!execute_statement_expect_error(mysql,
                                      "INSERT INTO mylite.nullable_key_rows "
                                      "VALUES (5, 'gamma', 'alpha')",
                                      "duplicate nullable unique key",
                                      &result->nullable_key_duplicate,
                                      result))
    return false;
  if (!execute_statement(mysql,
                         "UPDATE mylite.nullable_key_rows "
                         "SET note = NULL, code = 'gamma' WHERE id = 4",
                         "UPDATE nullable key row", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.nullable_key_rows "
                          "FORCE INDEX(note_key) WHERE note IS NULL",
                          "updated nullable key null ids",
                          &result->nullable_key_updated_null_ids, result))
    return false;
  if (result->nullable_key_updated_null_ids != "1,2,4")
  {
    result->message= "nullable key update returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql,
                         "DELETE FROM mylite.nullable_key_rows WHERE id = 1",
                         "DELETE nullable key row", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT COUNT(*) "
                          "FROM mylite.nullable_key_rows "
                          "FORCE INDEX(note_key) WHERE note IS NULL",
                          "deleted nullable key count",
                          &result->nullable_key_deleted_count, result))
    return false;
  if (result->nullable_key_deleted_count != "2")
  {
    result->message= "nullable key delete returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql, "DROP TABLE mylite.nullable_key_rows",
                         "DROP nullable key table", result))
    return false;

  if (!execute_statement(mysql,
                         "CREATE TABLE "
                         "mylite.nullable_composite_key_rows "
                         "(id INT NOT NULL, left_note VARCHAR(12), "
                         "right_note VARCHAR(12), PRIMARY KEY(id), "
                         "UNIQUE KEY note_pair(left_note, right_note)) "
                         "ENGINE=MYLITE",
                         "CREATE nullable composite key table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO "
                         "mylite.nullable_composite_key_rows VALUES "
                         "(1, NULL, 'same'), (2, NULL, 'same'), "
                         "(3, 'same', NULL), (4, 'same', NULL), "
                         "(5, 'left', 'right')",
                         "INSERT nullable composite key rows", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.nullable_composite_key_rows "
                          "FORCE INDEX(note_pair) "
                          "WHERE left_note IS NULL AND right_note = 'same'",
                          "nullable composite key head NULL ids",
                          &result->nullable_composite_key_head_null_ids,
                          result))
    return false;
  if (result->nullable_composite_key_head_null_ids != "1,2")
  {
    result->message= "nullable composite key head NULL lookup returned an "
                     "unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.nullable_composite_key_rows "
                          "FORCE INDEX(note_pair) "
                          "WHERE left_note = 'same' AND right_note IS NULL",
                          "nullable composite key tail NULL ids",
                          &result->nullable_composite_key_tail_null_ids,
                          result))
    return false;
  if (result->nullable_composite_key_tail_null_ids != "3,4")
  {
    result->message= "nullable composite key tail NULL lookup returned an "
                     "unexpected value";
    return false;
  }
  if (!execute_statement_expect_error(
        mysql,
        "INSERT INTO mylite.nullable_composite_key_rows "
        "VALUES (6, 'left', 'right')",
        "duplicate nullable composite key",
        &result->nullable_composite_key_duplicate,
        result))
    return false;
  if (!execute_statement(mysql,
                         "DROP TABLE mylite.nullable_composite_key_rows",
                         "DROP nullable composite key table", result))
    return false;

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.nullable_blob_key_rows "
                         "(id INT NOT NULL, note TEXT, payload BLOB, "
                         "PRIMARY KEY(id), KEY note_prefix(note(6)), "
                         "UNIQUE KEY payload_prefix(payload(4))) "
                         "ENGINE=MYLITE",
                         "CREATE nullable BLOB/TEXT key table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.nullable_blob_key_rows VALUES "
                         "(1, NULL, NULL), (2, NULL, NULL), "
                         "(3, 'theta-row', 'zz11-first'), "
                         "(4, 'kappa-row', 'yy22-second')",
                         "INSERT nullable BLOB/TEXT key rows", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.nullable_blob_key_rows "
                          "FORCE INDEX(note_prefix) WHERE note IS NULL",
                          "nullable BLOB/TEXT key null ids",
                          &result->nullable_blob_key_null_ids, result))
    return false;
  if (result->nullable_blob_key_null_ids != "1,2")
  {
    result->message= "nullable BLOB/TEXT key NULL lookup returned an "
                     "unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT id FROM mylite.nullable_blob_key_rows "
                          "FORCE INDEX(note_prefix) "
                          "WHERE note = 'theta-row'",
                          "nullable BLOB/TEXT key lookup",
                          &result->nullable_blob_key_lookup_id, result))
    return false;
  if (result->nullable_blob_key_lookup_id != "3")
  {
    result->message= "nullable BLOB/TEXT key lookup returned an "
                     "unexpected value";
    return false;
  }
  if (!execute_statement_expect_error(mysql,
                                      "INSERT INTO "
                                      "mylite.nullable_blob_key_rows "
                                      "VALUES "
                                      "(5, 'omega-row', 'zz11-dupe')",
                                      "duplicate nullable BLOB/TEXT key",
                                      &result->nullable_blob_key_duplicate,
                                      result))
    return false;
  if (!execute_statement(mysql,
                         "DROP TABLE mylite.nullable_blob_key_rows",
                         "DROP nullable BLOB/TEXT key table", result))
    return false;
  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.foreign_key_parent "
                         "(id INT NOT NULL, PRIMARY KEY(id)) ENGINE=MYLITE",
                         "CREATE foreign key parent table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.foreign_key_parent VALUES (1)",
                         "INSERT foreign key parent row", result))
    return false;
  if (!execute_statement_expect_error(
        mysql,
        "CREATE TABLE mylite.unsupported_foreign_child "
        "(id INT NOT NULL, parent_id INT NOT NULL, PRIMARY KEY(id), "
        "KEY parent_key(parent_id), "
        "CONSTRAINT parent_fk FOREIGN KEY(parent_id) "
        "REFERENCES mylite.foreign_key_parent(id)) ENGINE=MYLITE",
        "unsupported foreign key create",
        &result->unsupported_foreign_key_create,
        result))
    return false;
  if (!verify_table_absent(mysql,
                           "SHOW TABLES FROM mylite "
                           "LIKE 'unsupported_foreign_child'",
                           "unsupported foreign key child", result))
    return false;
  if (!execute_statement_expect_error(
        mysql,
        "ALTER TABLE mylite.foreign_key_parent "
        "ADD CONSTRAINT self_fk FOREIGN KEY(id) "
        "REFERENCES mylite.foreign_key_parent(id), ALGORITHM=COPY",
        "unsupported foreign key alter",
        &result->unsupported_foreign_key_alter,
        result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT COUNT(*) "
                          "FROM mylite.foreign_key_parent",
                          "foreign key parent count",
                          &result->foreign_key_parent_count, result))
    return false;
  if (result->foreign_key_parent_count != "1")
  {
    result->message= "foreign key parent count returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql, "DROP TABLE mylite.foreign_key_parent",
                         "DROP foreign key parent table", result))
    return false;
  if (!execute_statement_expect_error(
        mysql,
        "CREATE TABLE mylite.unsupported_generated_virtual "
        "(id INT NOT NULL, doubled INT GENERATED ALWAYS AS (id * 2) "
        "VIRTUAL) ENGINE=MYLITE",
        "unsupported virtual generated column",
        &result->unsupported_generated_virtual,
        result))
    return false;
  if (!verify_table_absent(mysql,
                           "SHOW TABLES FROM mylite "
                           "LIKE 'unsupported_generated_virtual'",
                           "unsupported virtual generated table", result))
    return false;
  if (!execute_statement_expect_error(
        mysql,
        "CREATE TABLE mylite.unsupported_generated_stored "
        "(id INT NOT NULL, doubled INT GENERATED ALWAYS AS (id * 2) "
        "STORED) ENGINE=MYLITE",
        "unsupported stored generated column",
        &result->unsupported_generated_stored,
        result))
    return false;
  if (!verify_table_absent(mysql,
                           "SHOW TABLES FROM mylite "
                           "LIKE 'unsupported_generated_stored'",
                           "unsupported stored generated table", result))
    return false;
  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.generated_base "
                         "(id INT NOT NULL, value INT NOT NULL, "
                         "PRIMARY KEY(id)) ENGINE=MYLITE",
                         "CREATE generated column base table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.generated_base VALUES (1, 2)",
                         "INSERT generated column base row", result))
    return false;
  if (!execute_statement_expect_error(
        mysql,
        "ALTER TABLE mylite.generated_base "
        "ADD COLUMN doubled INT GENERATED ALWAYS AS (value * 2) "
        "VIRTUAL, ALGORITHM=COPY",
        "unsupported generated column alter",
        &result->unsupported_generated_alter,
        result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT COUNT(*) FROM mylite.generated_base",
                          "generated column base count",
                          &result->generated_base_count, result))
    return false;
  if (result->generated_base_count != "1")
  {
    result->message= "generated column base count returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql, "DROP TABLE mylite.generated_base",
                         "DROP generated column base table", result))
    return false;
  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.check_rows "
                         "(id INT NOT NULL, "
                         "qty INT NOT NULL CHECK (qty > 0), "
                         "note VARCHAR(12), PRIMARY KEY(id), "
                         "CONSTRAINT note_not_empty "
                         "CHECK (note IS NULL OR note <> '')) "
                         "ENGINE=MYLITE",
                         "CREATE CHECK constraint table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.check_rows VALUES "
                         "(1, 5, 'ok'), (2, 3, NULL)",
                         "INSERT CHECK constraint rows", result))
    return false;
  if (!execute_statement_expect_error(mysql,
                                      "INSERT INTO mylite.check_rows VALUES "
                                      "(3, 0, 'bad')",
                                      "CHECK constraint insert",
                                      &result->check_constraint_insert,
                                      result))
    return false;
  if (!execute_statement_expect_error(mysql,
                                      "UPDATE mylite.check_rows "
                                      "SET note = '' WHERE id = 1",
                                      "CHECK constraint update",
                                      &result->check_constraint_update,
                                      result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT("
                          "id, ':', qty, ':', COALESCE(note, 'NULL')) "
                          "ORDER BY id SEPARATOR ',') "
                          "FROM mylite.check_rows",
                          "CHECK constraint rows",
                          &result->check_constraint_rows, result))
    return false;
  if (result->check_constraint_rows != "1:5:ok,2:3:NULL")
  {
    result->message= "CHECK constraint rows returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql, "DROP TABLE mylite.check_rows",
                         "DROP CHECK constraint table", result))
    return false;
  if (!execute_statement_expect_error(mysql,
                                      "CREATE TABLE mylite.unsupported_geometry "
                                      "(id INT NOT NULL, location GEOMETRY NOT NULL) "
                                      "ENGINE=MYLITE",
                                      "unsupported GEOMETRY table",
                                      &result->unsupported_geometry, result))
    return false;
  if (!execute_statement_expect_error(mysql,
                                      "CREATE TABLE "
                                      "mylite.unsupported_reverse_key "
                                      "(id INT NOT NULL, "
                                      "note VARCHAR(12) NOT NULL, "
                                      "KEY note_key(note DESC)) "
                                      "ENGINE=MYLITE",
                                      "unsupported reverse key table",
                                      &result->unsupported_reverse_key,
                                      result))
    return false;
  if (!execute_statement_expect_error(
        mysql,
        "CREATE TABLE mylite.unsupported_autoincrement "
        "(tenant INT NOT NULL, id INT NOT NULL AUTO_INCREMENT, "
        "PRIMARY KEY(tenant, id)) ENGINE=MYLITE",
        "unsupported autoincrement table", &result->unsupported_autoincrement,
        result))
    return false;
  return true;
}

static bool exercise_index_dml(MYSQL *mysql, SmokeResult *result)
{
  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.keyed "
                         "(id INT NOT NULL, note VARCHAR(12) NOT NULL, "
                         "PRIMARY KEY(id), KEY note_key(note)) "
                         "ENGINE=MYLITE",
                         "CREATE keyed table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.keyed VALUES "
                         "(2, 'two'), (1, 'one'), (3, 'three')",
                         "INSERT keyed rows", result))
    return false;

  if (!fetch_single_value(mysql,
                          "SELECT note FROM mylite.keyed FORCE INDEX(PRIMARY) "
                          "WHERE id = 2",
                          "key lookup note", &result->key_lookup_note,
                          result))
    return false;
  if (result->key_lookup_note != "two")
  {
    result->message= "key lookup returned an unexpected value";
    return false;
  }

  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.keyed FORCE INDEX(PRIMARY)",
                          "key order ids", &result->key_order_ids, result))
    return false;
  if (result->key_order_ids != "1,2,3")
  {
    result->message= "key order returned an unexpected value";
    return false;
  }

  if (!execute_statement_expect_error(mysql,
                                      "INSERT INTO mylite.keyed VALUES "
                                      "(2, 'duplicate')",
                                      "duplicate primary key",
                                      &result->duplicate_key, result))
    return false;
  if (!execute_statement_expect_error(mysql,
                                      "UPDATE mylite.keyed SET id = 1 "
                                      "WHERE id = 3",
                                      "duplicate primary key update",
                                      &result->update_duplicate_key, result))
    return false;

  if (!execute_statement(mysql, "DROP TABLE mylite.keyed",
                         "DROP keyed table", result))
    return false;

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.auto_rows "
                         "(id INT NOT NULL AUTO_INCREMENT, "
                         "note VARCHAR(12) NOT NULL, PRIMARY KEY(id)) "
                         "ENGINE=MYLITE",
                         "CREATE autoincrement table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.auto_rows (note) VALUES "
                         "('one'), ('two')",
                         "INSERT generated autoincrement rows", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.auto_rows (id, note) VALUES "
                         "(10, 'ten')",
                         "INSERT explicit autoincrement row", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.auto_rows (note) VALUES "
                         "('eleven')",
                         "INSERT next autoincrement row", result))
    return false;
  if (!execute_statement(mysql, "DELETE FROM mylite.auto_rows WHERE id = 11",
                         "DELETE generated autoincrement row", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.auto_rows (note) VALUES "
                         "('twelve')",
                         "INSERT post-delete autoincrement row", result))
    return false;

  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.auto_rows",
                          "autoincrement ids", &result->autoincrement_ids,
                          result))
    return false;
  if (result->autoincrement_ids != "1,2,10,12")
  {
    result->message= "autoincrement ids returned an unexpected value";
    return false;
  }

  if (!execute_statement(mysql, "DROP TABLE mylite.auto_rows",
                         "DROP autoincrement table", result))
    return false;

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.auto_explicit "
                         "(id INT NOT NULL AUTO_INCREMENT, "
                         "note VARCHAR(12) NOT NULL, PRIMARY KEY(id)) "
                         "ENGINE=MYLITE",
                         "CREATE explicit autoincrement table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.auto_explicit (id, note) VALUES "
                         "(20, 'twenty')",
                         "INSERT first explicit autoincrement row", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.auto_explicit (note) VALUES "
                         "('twentyone')",
                         "INSERT generated after explicit row", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.auto_explicit",
                          "explicit autoincrement ids",
                          &result->explicit_autoincrement_ids, result))
    return false;
  if (result->explicit_autoincrement_ids != "20,21")
  {
    result->message= "explicit autoincrement ids returned an unexpected value";
    return false;
  }

  return execute_statement(mysql, "DROP TABLE mylite.auto_explicit",
                           "DROP explicit autoincrement table", result);
}

static bool exercise_persistence_write(MYSQL *mysql, SmokeResult *result)
{
  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.persisted "
                         "(id INT, note VARCHAR(12)) ENGINE=MYLITE",
                         "CREATE persisted table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.persisted VALUES "
                         "(7, 'seven'), (8, 'eight')",
                         "INSERT persisted rows", result))
    return false;
  if (!execute_statement(mysql, "FLUSH TABLES",
                         "FLUSH TABLES after persisted INSERT", result))
    return false;

  if (!fetch_single_value(mysql, "SELECT COUNT(*) FROM mylite.persisted",
                          "persisted write count",
                          &result->persisted_count, result))
    return false;
  if (result->persisted_count != "2")
  {
    result->message= "persisted write count returned an unexpected value";
    return false;
  }

  if (!fetch_single_value(mysql,
                          "SHOW COLUMNS FROM mylite.persisted LIKE 'note'",
                          "persisted write column",
                          &result->persisted_column, result))
    return false;
  if (result->persisted_column != "note")
  {
    result->message= "persisted write table did not expose the note column";
    return false;
  }

  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(note ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted",
                          "persisted write notes",
                          &result->persisted_notes, result))
    return false;
  if (result->persisted_notes != "seven,eight")
  {
    result->message= "persisted write notes returned an unexpected value";
    return false;
  }

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.persisted_auto "
                         "(id INT NOT NULL AUTO_INCREMENT, "
                         "note VARCHAR(12) NOT NULL, PRIMARY KEY(id)) "
                         "ENGINE=MYLITE",
                         "CREATE persisted autoincrement table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.persisted_auto (note) VALUES "
                         "('one')",
                         "INSERT persisted generated row", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.persisted_auto (id, note) VALUES "
                         "(5, 'five')",
                         "INSERT persisted explicit row", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.persisted_auto (note) VALUES "
                         "('six')",
                         "INSERT persisted next row", result))
    return false;

  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_auto",
                          "persisted write autoincrement ids",
                          &result->persisted_autoincrement_ids, result))
    return false;
  if (result->persisted_autoincrement_ids != "1,5,6")
  {
    result->message= "persisted write autoincrement ids returned an "
                     "unexpected value";
    return false;
  }

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.persisted_keyed "
                         "(id INT NOT NULL, note VARCHAR(12) NOT NULL, "
                         "PRIMARY KEY(id), KEY note_key(note)) "
                         "ENGINE=MYLITE",
                         "CREATE persisted keyed table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.persisted_keyed VALUES "
                         "(2, 'two'), (1, 'one'), (3, 'three')",
                         "INSERT persisted keyed rows", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT note FROM mylite.persisted_keyed "
                          "FORCE INDEX(PRIMARY) WHERE id = 2",
                          "persisted write key lookup note",
                          &result->persisted_key_lookup_note, result))
    return false;
  if (result->persisted_key_lookup_note != "two")
  {
    result->message= "persisted write key lookup returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_keyed FORCE INDEX(PRIMARY)",
                          "persisted write key order ids",
                          &result->persisted_key_order_ids, result))
    return false;
  if (result->persisted_key_order_ids != "1,2,3")
  {
    result->message= "persisted write key order returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT id FROM mylite.persisted_keyed "
                          "FORCE INDEX(note_key) WHERE note = 'three'",
                          "persisted write note lookup id",
                          &result->persisted_note_lookup_id, result))
    return false;
  if (result->persisted_note_lookup_id != "3")
  {
    result->message= "persisted write note lookup returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY note SEPARATOR ',') "
                          "FROM mylite.persisted_keyed FORCE INDEX(note_key)",
                          "persisted write note order ids",
                          &result->persisted_note_order_ids, result))
    return false;
  if (result->persisted_note_order_ids != "1,3,2")
  {
    result->message= "persisted write note order returned an unexpected value";
    return false;
  }

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.persisted_large "
                         "(id INT NOT NULL, note VARCHAR(5000) NOT NULL) "
                         "ENGINE=MYLITE",
                         "CREATE persisted large row table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.persisted_large VALUES "
                         "(1, REPEAT('x', 5000)), "
                         "(2, REPEAT('y', 4200))",
                         "INSERT persisted large rows", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CHAR_LENGTH(note) "
                          "ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_large",
                          "persisted write large lengths",
                          &result->persisted_large_lengths, result))
    return false;
  if (result->persisted_large_lengths != "5000,4200")
  {
    result->message= "persisted write large lengths returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT(LEFT(note, 1), "
                          "RIGHT(note, 1)) ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_large",
                          "persisted write large edges",
                          &result->persisted_large_edges, result))
    return false;
  if (result->persisted_large_edges != "xx,yy")
  {
    result->message= "persisted write large edges returned an unexpected value";
    return false;
  }

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.persisted_wide "
                         "(id INT, note VARCHAR(900) NOT NULL) "
                         "ENGINE=MYLITE",
                         "CREATE persisted wide table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.persisted_wide VALUES "
                         "(1, REPEAT('a', 900)), "
                         "(2, REPEAT('b', 900)), "
                         "(3, REPEAT('c', 900)), "
                         "(4, REPEAT('d', 900)), "
                         "(5, REPEAT('e', 900)), "
                         "(6, REPEAT('f', 900))",
                         "INSERT persisted wide rows", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT COUNT(*) FROM mylite.persisted_wide",
                          "persisted write wide count",
                          &result->persisted_wide_count, result))
    return false;
  if (result->persisted_wide_count != "6")
  {
    result->message= "persisted write wide count returned an unexpected value";
    return false;
  }

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.persisted_blob_text "
                         "(id INT NOT NULL, note TEXT NOT NULL, "
                         "payload BLOB NOT NULL) ENGINE=MYLITE",
                         "CREATE persisted BLOB/TEXT table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.persisted_blob_text VALUES "
                         "(1, REPEAT('x', 5000), REPEAT('a', 4200)), "
                         "(2, REPEAT('y', 300), '')",
                         "INSERT persisted BLOB/TEXT rows", result))
    return false;
  if (!execute_statement(mysql, "START TRANSACTION",
                         "START persisted BLOB/TEXT rollback", result))
    return false;
  if (!execute_statement(mysql,
                         "UPDATE mylite.persisted_blob_text "
                         "SET note = REPEAT('r', 4500), "
                         "payload = REPEAT('s', 4100) WHERE id = 1",
                         "UPDATE persisted BLOB/TEXT rollback row", result))
    return false;
  if (!execute_statement(mysql, "ROLLBACK",
                         "ROLLBACK persisted BLOB/TEXT update", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT(CHAR_LENGTH(note), "
                          "':', LENGTH(payload)) ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_blob_text",
                          "persisted BLOB/TEXT rollback",
                          &result->persisted_blob_text_rollback, result))
    return false;
  if (result->persisted_blob_text_rollback != "5000:4200,300:0")
  {
    result->message= "persisted BLOB/TEXT rollback returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql,
                         "UPDATE mylite.persisted_blob_text "
                         "SET note = REPEAT('z', 700), "
                         "payload = REPEAT('w', 800) WHERE id = 2",
                         "UPDATE persisted BLOB/TEXT row", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT(CHAR_LENGTH(note), "
                          "':', LENGTH(payload)) ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_blob_text",
                          "persisted BLOB/TEXT lengths",
                          &result->persisted_blob_text_lengths, result))
    return false;
  if (result->persisted_blob_text_lengths != "5000:4200,700:800")
  {
    result->message= "persisted BLOB/TEXT lengths returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT(LEFT(note, 1), ':', "
                          "RIGHT(payload, 1)) ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_blob_text",
                          "persisted BLOB/TEXT edges",
                          &result->persisted_blob_text_edges, result))
    return false;
  if (result->persisted_blob_text_edges != "x:a,z:w")
  {
    result->message= "persisted BLOB/TEXT edges returned an unexpected value";
    return false;
  }

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.persisted_blob_text_keyed "
                         "(id INT NOT NULL, note TEXT NOT NULL, "
                         "payload BLOB NOT NULL, PRIMARY KEY(id), "
                         "KEY note_prefix(note(6)), "
                         "UNIQUE KEY payload_prefix(payload(4))) "
                         "ENGINE=MYLITE",
                         "CREATE persisted BLOB/TEXT key table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.persisted_blob_text_keyed "
                         "VALUES "
                         "(1, 'alpha-persist', 'aa11-persist'), "
                         "(2, 'beta-persist', 'bb22-persist'), "
                         "(3, 'gamma-persist', 'cc33-persist')",
                         "INSERT persisted BLOB/TEXT key rows", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT id "
                          "FROM mylite.persisted_blob_text_keyed "
                          "FORCE INDEX(note_prefix) "
                          "WHERE note = 'beta-persist'",
                          "persisted write BLOB/TEXT key lookup",
                          &result->persisted_blob_text_key_lookup_id,
                          result))
    return false;
  if (result->persisted_blob_text_key_lookup_id != "2")
  {
    result->message= "persisted write BLOB/TEXT key lookup returned an "
                     "unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY note "
                          "SEPARATOR ',') "
                          "FROM mylite.persisted_blob_text_keyed "
                          "FORCE INDEX(note_prefix)",
                          "persisted write BLOB/TEXT key order",
                          &result->persisted_blob_text_key_order_ids,
                          result))
    return false;
  if (result->persisted_blob_text_key_order_ids != "1,2,3")
  {
    result->message= "persisted write BLOB/TEXT key order returned an "
                     "unexpected value";
    return false;
  }

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.persisted_nullable_keyed "
                         "(id INT NOT NULL, note VARCHAR(12), "
                         "code VARCHAR(12), PRIMARY KEY(id), "
                         "KEY note_key(note), UNIQUE KEY code_unique(code)) "
                         "ENGINE=MYLITE",
                         "CREATE persisted nullable keyed table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.persisted_nullable_keyed "
                         "VALUES "
                         "(1, NULL, NULL), (2, NULL, NULL), "
                         "(3, 'alpha', 'alpha'), (4, 'beta', 'beta')",
                         "INSERT persisted nullable keyed rows", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_nullable_keyed "
                          "FORCE INDEX(note_key) WHERE note IS NULL",
                          "persisted write nullable key null ids",
                          &result->persisted_nullable_key_null_ids,
                          result))
    return false;
  if (result->persisted_nullable_key_null_ids != "1,2")
  {
    result->message= "persisted write nullable key NULL lookup returned an "
                     "unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT id "
                          "FROM mylite.persisted_nullable_keyed "
                          "FORCE INDEX(note_key) WHERE note = 'beta'",
                          "persisted write nullable key lookup",
                          &result->persisted_nullable_key_lookup_id,
                          result))
    return false;
  if (result->persisted_nullable_key_lookup_id != "4")
  {
    result->message= "persisted write nullable key lookup returned an "
                     "unexpected value";
    return false;
  }

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.persisted_copy_alter "
                         "(id INT NOT NULL AUTO_INCREMENT, "
                         "note VARCHAR(12), payload TEXT NOT NULL, "
                         "marker BLOB NOT NULL, PRIMARY KEY(id), "
                         "KEY note_key(note), "
                         "KEY payload_prefix(payload(8))) ENGINE=MYLITE",
                         "CREATE persisted copy ALTER table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.persisted_copy_alter "
                         "(note, payload, marker) VALUES "
                         "('beta', 'payload-beta', 'm1'), "
                         "(NULL, 'payload-null', 'm2'), "
                         "('alpha', 'payload-alpha', 'm3')",
                         "INSERT persisted copy ALTER rows", result))
    return false;
  if (!execute_statement(mysql,
                         "ALTER TABLE mylite.persisted_copy_alter "
                         "ADD COLUMN status VARCHAR(12) NOT NULL "
                         "DEFAULT 'active', ALGORITHM=COPY",
                         "ALTER persisted populated table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.persisted_copy_alter "
                         "(note, payload, marker) VALUES "
                         "('gamma', 'payload-gamma', 'm4')",
                         "INSERT persisted row after copy ALTER", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT("
                          "id, ':', COALESCE(note, 'NULL'), ':', "
                          "CHAR_LENGTH(payload), ':', LENGTH(marker), ':', "
                          "status) ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_copy_alter",
                          "persisted write copy ALTER rows",
                          &result->persisted_copy_alter_rows, result))
    return false;
  if (result->persisted_copy_alter_rows !=
      "1:beta:12:2:active,2:NULL:12:2:active,3:alpha:13:2:active,"
      "4:gamma:13:2:active")
  {
    result->message= "persisted write copy ALTER rows returned an "
                     "unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY note "
                          "SEPARATOR ',') "
                          "FROM mylite.persisted_copy_alter "
                          "FORCE INDEX(note_key)",
                          "persisted write copy ALTER note key order",
                          &result->persisted_copy_alter_note_order_ids,
                          result))
    return false;
  if (result->persisted_copy_alter_note_order_ids != "2,3,1,4")
  {
    result->message= "persisted write copy ALTER note key order returned an "
                     "unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_copy_alter "
                          "FORCE INDEX(note_key) WHERE note IS NULL",
                          "persisted write copy ALTER nullable key lookup",
                          &result->persisted_copy_alter_null_note_ids,
                          result))
    return false;
  if (result->persisted_copy_alter_null_note_ids != "2")
  {
    result->message= "persisted write copy ALTER nullable key lookup "
                     "returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT id FROM mylite.persisted_copy_alter "
                          "FORCE INDEX(payload_prefix) "
                          "WHERE payload = 'payload-alpha'",
                          "persisted write copy ALTER payload lookup",
                          &result->persisted_copy_alter_payload_lookup_id,
                          result))
    return false;
  if (result->persisted_copy_alter_payload_lookup_id != "3")
  {
    result->message= "persisted write copy ALTER payload lookup returned an "
                     "unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT MAX(id) "
                          "FROM mylite.persisted_copy_alter",
                          "persisted write copy ALTER autoincrement id",
                          &result->persisted_copy_alter_autoincrement_id,
                          result))
    return false;
  if (result->persisted_copy_alter_autoincrement_id != "4")
  {
    result->message= "persisted write copy ALTER autoincrement returned an "
                     "unexpected value";
    return false;
  }

  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.persisted_check_rows "
                         "(id INT NOT NULL, "
                         "qty INT NOT NULL CHECK (qty > 0), "
                         "note VARCHAR(12), PRIMARY KEY(id), "
                         "CONSTRAINT note_not_empty "
                         "CHECK (note IS NULL OR note <> '')) "
                         "ENGINE=MYLITE",
                         "CREATE persisted CHECK constraint table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.persisted_check_rows VALUES "
                         "(1, 5, 'ok'), (2, 3, NULL)",
                         "INSERT persisted CHECK constraint rows", result))
    return false;
  if (!execute_statement_expect_error(
        mysql,
        "INSERT INTO mylite.persisted_check_rows VALUES (3, -1, 'bad')",
        "persisted write CHECK constraint insert",
        &result->persisted_check_insert,
        result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT("
                          "id, ':', qty, ':', COALESCE(note, 'NULL')) "
                          "ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_check_rows",
                          "persisted write CHECK constraint rows",
                          &result->persisted_check_rows, result))
    return false;
  if (result->persisted_check_rows != "1:5:ok,2:3:NULL")
  {
    result->message= "persisted write CHECK constraint rows returned an "
                     "unexpected value";
    return false;
  }

  return true;
}

static bool exercise_persistence_read(MYSQL *mysql, SmokeResult *result)
{
  if (!execute_statement(mysql, "FLUSH TABLES",
                         "FLUSH TABLES before persisted read", result))
    return false;

  if (!verify_table_present(mysql,
                            "SHOW TABLES FROM mylite LIKE 'persisted'",
                            "persisted table", result))
    return false;

  if (!fetch_single_value(mysql, "SELECT COUNT(*) FROM mylite.persisted",
                          "persisted read count",
                          &result->persisted_count, result))
    return false;
  if (result->persisted_count != "2")
  {
    result->message= "persisted read count returned an unexpected value";
    return false;
  }

  if (!fetch_single_value(mysql,
                          "SHOW COLUMNS FROM mylite.persisted LIKE 'note'",
                          "persisted read column",
                          &result->persisted_column, result))
    return false;
  if (result->persisted_column != "note")
  {
    result->message= "persisted read table did not expose the note column";
    return false;
  }

  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(note ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted",
                          "persisted read notes",
                          &result->persisted_notes, result))
    return false;
  if (result->persisted_notes != "seven,eight")
  {
    result->message= "persisted read notes returned an unexpected value";
    return false;
  }

  if (!verify_table_present(mysql,
                            "SHOW TABLES FROM mylite LIKE 'persisted_auto'",
                            "persisted autoincrement table", result))
    return false;
  const bool append_autoincrement= result->phase == "persistence_read";
  if (append_autoincrement &&
      !execute_statement(mysql,
                         "INSERT INTO mylite.persisted_auto (note) VALUES "
                         "('seven')",
                         "INSERT reopened autoincrement row", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_auto",
                          "persisted read autoincrement ids",
                          &result->persisted_autoincrement_ids, result))
    return false;
  const std::string expected_ids= append_autoincrement ? "1,5,6,7" : "1,5,6";
  if (result->persisted_autoincrement_ids != expected_ids)
  {
    result->message= "persisted read autoincrement ids returned an "
                     "unexpected value";
    return false;
  }

  if (!verify_table_present(mysql,
                            "SHOW TABLES FROM mylite LIKE 'persisted_keyed'",
                            "persisted keyed table", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT note FROM mylite.persisted_keyed "
                          "FORCE INDEX(PRIMARY) WHERE id = 2",
                          "persisted read key lookup note",
                          &result->persisted_key_lookup_note, result))
    return false;
  if (result->persisted_key_lookup_note != "two")
  {
    result->message= "persisted read key lookup returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_keyed FORCE INDEX(PRIMARY)",
                          "persisted read key order ids",
                          &result->persisted_key_order_ids, result))
    return false;
  if (result->persisted_key_order_ids != "1,2,3")
  {
    result->message= "persisted read key order returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT id FROM mylite.persisted_keyed "
                          "FORCE INDEX(note_key) WHERE note = 'three'",
                          "persisted read note lookup id",
                          &result->persisted_note_lookup_id, result))
    return false;
  if (result->persisted_note_lookup_id != "3")
  {
    result->message= "persisted read note lookup returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY note SEPARATOR ',') "
                          "FROM mylite.persisted_keyed FORCE INDEX(note_key)",
                          "persisted read note order ids",
                          &result->persisted_note_order_ids, result))
    return false;
  if (result->persisted_note_order_ids != "1,3,2")
  {
    result->message= "persisted read note order returned an unexpected value";
    return false;
  }

  if (!verify_table_present(mysql,
                            "SHOW TABLES FROM mylite LIKE 'persisted_large'",
                            "persisted large table", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CHAR_LENGTH(note) "
                          "ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_large",
                          "persisted read large lengths",
                          &result->persisted_large_lengths, result))
    return false;
  if (result->persisted_large_lengths != "5000,4200")
  {
    result->message= "persisted read large lengths returned an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT(LEFT(note, 1), "
                          "RIGHT(note, 1)) ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_large",
                          "persisted read large edges",
                          &result->persisted_large_edges, result))
    return false;
  if (result->persisted_large_edges != "xx,yy")
  {
    result->message= "persisted read large edges returned an unexpected value";
    return false;
  }

  if (!verify_table_present(mysql,
                            "SHOW TABLES FROM mylite LIKE 'persisted_wide'",
                            "persisted wide table", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT COUNT(*) FROM mylite.persisted_wide",
                          "persisted read wide count",
                          &result->persisted_wide_count, result))
    return false;
  if (result->persisted_wide_count != "6")
  {
    result->message= "persisted read wide count returned an unexpected value";
    return false;
  }

  if (!verify_table_present(mysql,
                            "SHOW TABLES FROM mylite "
                            "LIKE 'persisted_blob_text'",
                            "persisted BLOB/TEXT table", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT(CHAR_LENGTH(note), "
                          "':', LENGTH(payload)) ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_blob_text",
                          "persisted read BLOB/TEXT lengths",
                          &result->persisted_blob_text_lengths, result))
    return false;
  if (result->persisted_blob_text_lengths != "5000:4200,700:800")
  {
    result->message= "persisted read BLOB/TEXT lengths returned an "
                     "unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT(LEFT(note, 1), ':', "
                          "RIGHT(payload, 1)) ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_blob_text",
                          "persisted read BLOB/TEXT edges",
                          &result->persisted_blob_text_edges, result))
    return false;
  if (result->persisted_blob_text_edges != "x:a,z:w")
  {
    result->message= "persisted read BLOB/TEXT edges returned an "
                     "unexpected value";
    return false;
  }

  if (!verify_table_present(mysql,
                            "SHOW TABLES FROM mylite "
                            "LIKE 'persisted_blob_text_keyed'",
                            "persisted BLOB/TEXT key table", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT id "
                          "FROM mylite.persisted_blob_text_keyed "
                          "FORCE INDEX(note_prefix) "
                          "WHERE note = 'beta-persist'",
                          "persisted read BLOB/TEXT key lookup",
                          &result->persisted_blob_text_key_lookup_id,
                          result))
    return false;
  if (result->persisted_blob_text_key_lookup_id != "2")
  {
    result->message= "persisted read BLOB/TEXT key lookup returned an "
                     "unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY note "
                          "SEPARATOR ',') "
                          "FROM mylite.persisted_blob_text_keyed "
                          "FORCE INDEX(note_prefix)",
                          "persisted read BLOB/TEXT key order",
                          &result->persisted_blob_text_key_order_ids,
                          result))
    return false;
  if (result->persisted_blob_text_key_order_ids != "1,2,3")
  {
    result->message= "persisted read BLOB/TEXT key order returned an "
                     "unexpected value";
    return false;
  }

  if (!verify_table_present(mysql,
                            "SHOW TABLES FROM mylite "
                            "LIKE 'persisted_nullable_keyed'",
                            "persisted nullable keyed table", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_nullable_keyed "
                          "FORCE INDEX(note_key) WHERE note IS NULL",
                          "persisted read nullable key null ids",
                          &result->persisted_nullable_key_null_ids,
                          result))
    return false;
  if (result->persisted_nullable_key_null_ids != "1,2")
  {
    result->message= "persisted read nullable key NULL lookup returned an "
                     "unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT id "
                          "FROM mylite.persisted_nullable_keyed "
                          "FORCE INDEX(note_key) WHERE note = 'beta'",
                          "persisted read nullable key lookup",
                          &result->persisted_nullable_key_lookup_id,
                          result))
    return false;
  if (result->persisted_nullable_key_lookup_id != "4")
  {
    result->message= "persisted read nullable key lookup returned an "
                     "unexpected value";
    return false;
  }

  if (!verify_table_present(mysql,
                            "SHOW TABLES FROM mylite "
                            "LIKE 'persisted_copy_alter'",
                            "persisted copy ALTER table", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT("
                          "id, ':', COALESCE(note, 'NULL'), ':', "
                          "CHAR_LENGTH(payload), ':', LENGTH(marker), ':', "
                          "status) ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_copy_alter",
                          "persisted read copy ALTER rows",
                          &result->persisted_copy_alter_rows, result))
    return false;
  if (result->persisted_copy_alter_rows !=
      "1:beta:12:2:active,2:NULL:12:2:active,3:alpha:13:2:active,"
      "4:gamma:13:2:active")
  {
    result->message= "persisted read copy ALTER rows returned an "
                     "unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY note "
                          "SEPARATOR ',') "
                          "FROM mylite.persisted_copy_alter "
                          "FORCE INDEX(note_key)",
                          "persisted read copy ALTER note key order",
                          &result->persisted_copy_alter_note_order_ids,
                          result))
    return false;
  if (result->persisted_copy_alter_note_order_ids != "2,3,1,4")
  {
    result->message= "persisted read copy ALTER note key order returned an "
                     "unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(id ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_copy_alter "
                          "FORCE INDEX(note_key) WHERE note IS NULL",
                          "persisted read copy ALTER nullable key lookup",
                          &result->persisted_copy_alter_null_note_ids,
                          result))
    return false;
  if (result->persisted_copy_alter_null_note_ids != "2")
  {
    result->message= "persisted read copy ALTER nullable key lookup returned "
                     "an unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT id FROM mylite.persisted_copy_alter "
                          "FORCE INDEX(payload_prefix) "
                          "WHERE payload = 'payload-alpha'",
                          "persisted read copy ALTER payload lookup",
                          &result->persisted_copy_alter_payload_lookup_id,
                          result))
    return false;
  if (result->persisted_copy_alter_payload_lookup_id != "3")
  {
    result->message= "persisted read copy ALTER payload lookup returned an "
                     "unexpected value";
    return false;
  }
  if (!fetch_single_value(mysql,
                          "SELECT MAX(id) "
                          "FROM mylite.persisted_copy_alter",
                          "persisted read copy ALTER autoincrement id",
                          &result->persisted_copy_alter_autoincrement_id,
                          result))
    return false;
  if (result->persisted_copy_alter_autoincrement_id != "4")
  {
    result->message= "persisted read copy ALTER autoincrement returned an "
                     "unexpected value";
    return false;
  }

  if (!verify_table_present(mysql,
                            "SHOW TABLES FROM mylite "
                            "LIKE 'persisted_check_rows'",
                            "persisted CHECK constraint table", result))
    return false;
  if (!fetch_single_value(mysql,
                          "SELECT GROUP_CONCAT(CONCAT("
                          "id, ':', qty, ':', COALESCE(note, 'NULL')) "
                          "ORDER BY id SEPARATOR ',') "
                          "FROM mylite.persisted_check_rows",
                          "persisted read CHECK constraint rows",
                          &result->persisted_check_rows, result))
    return false;
  if (result->persisted_check_rows != "1:5:ok,2:3:NULL")
  {
    result->message= "persisted read CHECK constraint rows returned an "
                     "unexpected value";
    return false;
  }
  if (!execute_statement_expect_error(
        mysql,
        "INSERT INTO mylite.persisted_check_rows VALUES (3, 0, 'bad')",
        "persisted read CHECK constraint insert",
        &result->persisted_check_insert,
        result))
    return false;

  return true;
}

static bool exercise_recovery_latest(MYSQL *mysql, SmokeResult *result)
{
  if (!exercise_persistence_read(mysql, result))
    return false;

  std::string create=
    "CREATE TABLE mylite.recovery_marker (id INT NOT NULL";
  for (int i= 1; i <= 900; ++i)
  {
    create.append(", c");
    create.append(std::to_string(i));
    create.append(" INT");
  }
  create.append(") ENGINE=MYLITE");
  if (!execute_statement(mysql, create.c_str(), "CREATE recovery marker",
                         result))
    return false;
  if (!execute_statement(mysql, "FLUSH TABLES",
                         "FLUSH TABLES after recovery marker write",
                         result))
    return false;

  if (!verify_table_present(mysql,
                            "SHOW TABLES FROM mylite "
                            "LIKE 'recovery_marker'",
                            "recovery marker", result))
    return false;
  result->recovery_marker= "present";
  return true;
}

static bool exercise_recovery_read(MYSQL *mysql, SmokeResult *result)
{
  if (!exercise_persistence_read(mysql, result))
    return false;

  if (!verify_table_absent(mysql,
                           "SHOW TABLES FROM mylite "
                           "LIKE 'recovery_marker'",
                           "recovery marker", result))
    return false;
  result->recovery_marker= "absent";

  if (!write_recovery_page_payload(mysql, "recovery_reclaim", 'r',
                                   &result->recovery_reclaim, result))
    return false;
  if (!execute_statement(mysql, "FLUSH TABLES",
                         "FLUSH TABLES after recovery reclaim write",
                         result))
    return false;
  return true;
}

static bool exercise_transaction_boundary_write(MYSQL *mysql,
                                                SmokeResult *result)
{
  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.transaction_boundary "
                         "(id INT NOT NULL, note VARCHAR(12) NOT NULL, "
                         "PRIMARY KEY(id)) ENGINE=MYLITE",
                         "CREATE transaction boundary table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.transaction_boundary VALUES "
                         "(1, 'one'), (2, 'two')",
                         "INSERT transaction boundary baseline", result))
    return false;
  if (!execute_statement(mysql, "FLUSH TABLES",
                         "FLUSH TABLES after transaction baseline", result))
    return false;

  if (!execute_statement(mysql, "START TRANSACTION",
                         "START transaction boundary transaction", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.transaction_boundary VALUES "
                         "(3, 'three')",
                         "INSERT transaction boundary row", result))
    return false;
  if (!execute_statement(mysql,
                         "UPDATE mylite.transaction_boundary "
                         "SET note = 'deux' WHERE id = 2",
                         "UPDATE transaction boundary row", result))
    return false;
  if (!execute_statement(mysql,
                         "DELETE FROM mylite.transaction_boundary "
                         "WHERE id = 1",
                         "DELETE transaction boundary row", result))
    return false;
  if (!execute_statement(mysql, "ROLLBACK",
                         "ROLLBACK transaction boundary transaction", result))
    return false;
  if (!fetch_warning_summary(mysql, "ROLLBACK warnings",
                             &result->transaction_rollback_warnings, result))
    return false;
  if (result->transaction_rollback_warnings != "none")
  {
    result->message= "ROLLBACK warning returned an unexpected value";
    return false;
  }

  if (!fetch_single_value(
        mysql,
        "SELECT GROUP_CONCAT(CONCAT(id, ':', note) "
        "ORDER BY id SEPARATOR ',') "
        "FROM mylite.transaction_boundary",
        "transaction boundary rollback rows",
        &result->transaction_rollback_rows, result))
    return false;
  if (result->transaction_rollback_rows != "1:one,2:two")
  {
    result->message= "transaction boundary rows were not rolled back";
    return false;
  }

  if (!execute_statement(mysql, "START TRANSACTION",
                         "START transaction savepoint transaction", result))
    return false;
  std::string clean_savepoint_count;
  if (!fetch_single_value(mysql,
                          "SELECT COUNT(*) "
                          "FROM mylite.transaction_boundary",
                          "transaction clean savepoint participant count",
                          &clean_savepoint_count, result))
    return false;
  if (clean_savepoint_count != "2")
  {
    result->message= "transaction clean savepoint count returned an "
                     "unexpected value";
    return false;
  }
  if (!execute_statement(mysql, "SAVEPOINT clean_sp",
                         "SAVEPOINT before transaction boundary DML", result))
    return false;
  if (!execute_statement(mysql,
                         "UPDATE mylite.transaction_boundary "
                         "SET note = 'uno' WHERE id = 1",
                         "UPDATE after clean savepoint", result))
    return false;
  if (!execute_statement(mysql, "ROLLBACK TO SAVEPOINT clean_sp",
                         "ROLLBACK TO clean savepoint", result))
    return false;
  if (!fetch_single_value(
        mysql,
        "SELECT GROUP_CONCAT(CONCAT(id, ':', note) "
        "ORDER BY id SEPARATOR ',') "
        "FROM mylite.transaction_boundary",
        "transaction clean savepoint rows",
        &result->transaction_clean_savepoint_rows, result))
    return false;
  if (result->transaction_clean_savepoint_rows != "1:one,2:two")
  {
    result->message= "clean savepoint rows were not restored";
    return false;
  }
  if (!execute_statement(mysql,
                         "UPDATE mylite.transaction_boundary "
                         "SET note = 'uno' WHERE id = 1",
                         "UPDATE transaction boundary savepoint row", result))
    return false;
  if (!execute_statement(mysql, "SAVEPOINT sp1",
                         "SAVEPOINT transaction boundary sp1", result))
    return false;
  if (!execute_statement(mysql,
                         "UPDATE mylite.transaction_boundary "
                         "SET note = 'dos' WHERE id = 2",
                         "UPDATE transaction boundary savepoint row", result))
    return false;
  if (!execute_statement(mysql, "SAVEPOINT sp2",
                         "SAVEPOINT transaction boundary sp2", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.transaction_boundary VALUES "
                         "(4, 'four')",
                         "INSERT transaction boundary savepoint row", result))
    return false;
  if (!execute_statement(mysql, "ROLLBACK TO SAVEPOINT sp1",
                         "ROLLBACK TO transaction boundary sp1", result))
    return false;
  if (!fetch_single_value(
        mysql,
        "SELECT GROUP_CONCAT(CONCAT(id, ':', note) "
        "ORDER BY id SEPARATOR ',') "
        "FROM mylite.transaction_boundary",
        "transaction savepoint rows", &result->transaction_savepoint_rows,
        result))
    return false;
  if (result->transaction_savepoint_rows != "1:uno,2:two")
  {
    result->message= "transaction savepoint rows were not restored";
    return false;
  }
  if (!execute_statement(mysql,
                         "UPDATE mylite.transaction_boundary "
                         "SET note = 'dos' WHERE id = 2",
                         "UPDATE transaction boundary release row", result))
    return false;
  if (!execute_statement(mysql, "SAVEPOINT sp3",
                         "SAVEPOINT transaction boundary sp3", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.transaction_boundary VALUES "
                         "(4, 'four')",
                         "INSERT transaction boundary release row", result))
    return false;
  if (!execute_statement(mysql, "RELEASE SAVEPOINT sp3",
                         "RELEASE transaction boundary sp3", result))
    return false;
  if (!fetch_single_value(
        mysql,
        "SELECT GROUP_CONCAT(CONCAT(id, ':', note) "
        "ORDER BY id SEPARATOR ',') "
        "FROM mylite.transaction_boundary",
        "transaction release rows", &result->transaction_release_rows,
        result))
    return false;
  if (result->transaction_release_rows != "1:uno,2:dos,4:four")
  {
    result->message= "transaction release rows returned an unexpected value";
    return false;
  }
  if (!execute_statement(mysql, "COMMIT",
                         "COMMIT transaction savepoint transaction", result))
    return false;
  if (!fetch_single_value(
        mysql,
        "SELECT GROUP_CONCAT(CONCAT(id, ':', note) "
        "ORDER BY id SEPARATOR ',') "
        "FROM mylite.transaction_boundary",
        "transaction boundary commit rows", &result->transaction_rows,
        result))
    return false;
  if (result->transaction_rows != "1:uno,2:dos,4:four")
  {
    result->message= "transaction boundary rows did not commit";
    return false;
  }

  if (!exercise_statement_error_rollback_write(mysql, result))
    return false;

  return execute_statement(mysql, "FLUSH TABLES",
                           "FLUSH TABLES after transaction boundary",
                           result);
}

static bool exercise_transaction_boundary_read(MYSQL *mysql,
                                               SmokeResult *result)
{
  if (!execute_statement(mysql, "FLUSH TABLES",
                         "FLUSH TABLES before transaction boundary read",
                         result))
    return false;
  if (!verify_table_present(
        mysql,
        "SHOW TABLES FROM mylite LIKE 'transaction_boundary'",
        "transaction boundary table", result))
    return false;
  if (!fetch_single_value(
        mysql,
        "SELECT GROUP_CONCAT(CONCAT(id, ':', note) "
        "ORDER BY id SEPARATOR ',') "
        "FROM mylite.transaction_boundary",
        "transaction boundary persisted rows", &result->transaction_rows,
        result))
    return false;
  if (result->transaction_rows != "1:uno,2:dos,4:four")
  {
    result->message= "transaction boundary rows did not persist";
    return false;
  }
  return exercise_statement_error_rollback_read(mysql, result);
}

static bool exercise_statement_error_rollback_write(MYSQL *mysql,
                                                    SmokeResult *result)
{
  if (!execute_statement(mysql,
                         "CREATE TABLE mylite.statement_error "
                         "(id INT NOT NULL, note VARCHAR(12) NOT NULL, "
                         "PRIMARY KEY(id)) ENGINE=MYLITE",
                         "CREATE statement error table", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.statement_error VALUES "
                         "(1, 'one')",
                         "INSERT statement error baseline", result))
    return false;
  if (!execute_statement_expect_error(
        mysql,
        "INSERT INTO mylite.statement_error VALUES "
        "(2, 'two'), (1, 'duplicate')",
        "duplicate autocommit statement insert",
        &result->statement_autocommit_error, result))
    return false;
  if (!fetch_warning_summary(mysql, "duplicate autocommit statement warnings",
                             &result->statement_autocommit_warnings, result))
    return false;
  if (result->statement_autocommit_warnings.find(":1196:") !=
      std::string::npos)
  {
    result->message= "autocommit statement rollback emitted warning 1196";
    return false;
  }
  if (!fetch_single_value(
        mysql,
        "SELECT GROUP_CONCAT(CONCAT(id, ':', note) "
        "ORDER BY id SEPARATOR ',') "
        "FROM mylite.statement_error",
        "statement autocommit rows",
        &result->statement_autocommit_rows, result))
    return false;
  if (result->statement_autocommit_rows != "1:one")
  {
    result->message= "autocommit statement rollback leaked rows";
    return false;
  }

  if (!execute_statement(mysql, "START TRANSACTION",
                         "START statement error transaction", result))
    return false;
  if (!execute_statement(mysql,
                         "INSERT INTO mylite.statement_error VALUES "
                         "(3, 'three')",
                         "INSERT statement error transaction row", result))
    return false;
  if (!execute_statement_expect_error(
        mysql,
        "INSERT INTO mylite.statement_error VALUES "
        "(4, 'four'), (1, 'duplicate')",
        "duplicate transaction statement insert",
        &result->statement_transaction_error, result))
    return false;
  if (!fetch_warning_summary(mysql, "duplicate transaction statement warnings",
                             &result->statement_transaction_warnings, result))
    return false;
  if (result->statement_transaction_warnings.find(":1196:") !=
      std::string::npos)
  {
    result->message= "transaction statement rollback emitted warning 1196";
    return false;
  }
  if (!fetch_single_value(
        mysql,
        "SELECT GROUP_CONCAT(CONCAT(id, ':', note) "
        "ORDER BY id SEPARATOR ',') "
        "FROM mylite.statement_error",
        "statement transaction rows",
        &result->statement_transaction_rows, result))
    return false;
  if (result->statement_transaction_rows != "1:one,3:three")
  {
    result->message= "transaction statement rollback returned unexpected rows";
    return false;
  }
  if (!execute_statement(mysql, "COMMIT",
                         "COMMIT statement error transaction", result))
    return false;
  if (!fetch_single_value(
        mysql,
        "SELECT GROUP_CONCAT(CONCAT(id, ':', note) "
        "ORDER BY id SEPARATOR ',') "
        "FROM mylite.statement_error",
        "statement committed rows", &result->statement_rows, result))
    return false;
  if (result->statement_rows != "1:one,3:three")
  {
    result->message= "statement error rows did not commit";
    return false;
  }

  return true;
}

static bool exercise_statement_error_rollback_read(MYSQL *mysql,
                                                   SmokeResult *result)
{
  if (!verify_table_present(mysql,
                            "SHOW TABLES FROM mylite LIKE 'statement_error'",
                            "statement error table", result))
    return false;
  if (!fetch_single_value(
        mysql,
        "SELECT GROUP_CONCAT(CONCAT(id, ':', note) "
        "ORDER BY id SEPARATOR ',') "
        "FROM mylite.statement_error",
        "statement persisted rows", &result->statement_rows, result))
    return false;
  if (result->statement_rows != "1:one,3:three")
  {
    result->message= "statement error rows did not persist";
    return false;
  }
  return true;
}

static bool write_recovery_page_payload(MYSQL *mysql, const char *table_name,
                                        char fill, std::string *count,
                                        SmokeResult *result)
{
  std::string create= "CREATE TABLE mylite.";
  create.append(table_name);
  create.append(" (id INT NOT NULL, note VARCHAR(5000) NOT NULL) "
                "ENGINE=MYLITE");
  if (!execute_statement(mysql, create.c_str(), "CREATE recovery page table",
                         result))
    return false;

  std::string insert= "INSERT INTO mylite.";
  insert.append(table_name);
  insert.append(" VALUES ");
  const int row_count= 13;
  for (int i= 1; i <= row_count; ++i)
  {
    if (i > 1)
      insert.append(", ");
    insert.push_back('(');
    insert.append(std::to_string(i));
    insert.append(", REPEAT('");
    insert.push_back(fill);
    insert.append("', 5000))");
  }
  if (!execute_statement(mysql, insert.c_str(),
                         "INSERT recovery page rows", result))
    return false;

  std::string query= "SELECT COUNT(*) FROM mylite.";
  query.append(table_name);
  if (!fetch_single_value(mysql, query.c_str(), "recovery page row count",
                          count, result))
    return false;
  if (*count != std::to_string(row_count))
  {
    result->message= "recovery page row count returned an unexpected value";
    return false;
  }

  return true;
}

static bool execute_statement(MYSQL *mysql, const char *statement,
                              const char *label, SmokeResult *result)
{
  if (!mysql_query(mysql, statement))
    return true;

  result->message= std::string(label) + " failed: " + mysql_error(mysql);
  return false;
}

static bool execute_statement_expect_error(MYSQL *mysql, const char *statement,
                                           const char *label,
                                           std::string *value,
                                           SmokeResult *result)
{
  if (mysql_query(mysql, statement))
  {
    *value= "rejected";
    return true;
  }

  result->message= std::string(label) + " unexpectedly succeeded";
  return false;
}

static bool fetch_single_value(MYSQL *mysql, const char *query,
                               const char *label, std::string *value,
                               SmokeResult *result)
{
  if (mysql_query(mysql, query))
  {
    result->message= std::string(label) + " query failed: " +
                     mysql_error(mysql);
    return false;
  }

  MYSQL_RES *res= mysql_store_result(mysql);
  if (!res)
  {
    result->message= std::string(label) + " result failed: " +
                     mysql_error(mysql);
    return false;
  }

  bool ok= false;
  MYSQL_ROW row= mysql_fetch_row(res);
  if (mysql_num_fields(res) < 1)
    result->message= std::string(label) +
                     " returned an unexpected column count";
  else if (!row || !row[0])
    result->message= std::string(label) + " returned no value";
  else if (mysql_fetch_row(res) != nullptr)
    result->message= std::string(label) + " returned more than one row";
  else
  {
    *value= row[0];
    ok= true;
  }

  mysql_free_result(res);
  return ok;
}

static bool fetch_warning_summary(MYSQL *mysql, const char *label,
                                  std::string *value, SmokeResult *result)
{
  if (mysql_query(mysql, "SHOW WARNINGS"))
  {
    result->message= std::string(label) + " query failed: " +
                     mysql_error(mysql);
    return false;
  }

  MYSQL_RES *res= mysql_store_result(mysql);
  if (!res)
  {
    result->message= std::string(label) + " result failed: " +
                     mysql_error(mysql);
    return false;
  }

  value->clear();
  MYSQL_ROW row;
  while ((row= mysql_fetch_row(res)))
  {
    if (mysql_num_fields(res) < 3 || !row[0] || !row[1] || !row[2])
    {
      mysql_free_result(res);
      result->message= std::string(label) +
                       " returned an unexpected warning row";
      return false;
    }
    if (!value->empty())
      value->push_back(';');
    value->append(row[0]);
    value->push_back(':');
    value->append(row[1]);
    value->push_back(':');
    value->append(row[2]);
  }

  if (value->empty())
    *value= "none";
  mysql_free_result(res);
  return true;
}

static bool verify_table_present(MYSQL *mysql, const char *query,
                                 const char *label, SmokeResult *result)
{
  if (mysql_query(mysql, query))
  {
    result->message= std::string(label) + " presence query failed: " +
                     mysql_error(mysql);
    return false;
  }

  MYSQL_RES *res= mysql_store_result(mysql);
  if (!res)
  {
    result->message= std::string(label) + " presence result failed: " +
                     mysql_error(mysql);
    return false;
  }

  bool ok= false;
  MYSQL_ROW row= mysql_fetch_row(res);
  if (!row || !row[0])
    result->message= std::string(label) + " was not discovered";
  else if (mysql_fetch_row(res) != nullptr)
    result->message= std::string(label) + " query returned more than one row";
  else
    ok= true;

  mysql_free_result(res);
  return ok;
}

static bool verify_table_absent(MYSQL *mysql, const char *query,
                                const char *label, SmokeResult *result)
{
  if (mysql_query(mysql, query))
  {
    result->message= std::string(label) + " absence query failed: " +
                     mysql_error(mysql);
    return false;
  }

  MYSQL_RES *res= mysql_store_result(mysql);
  if (!res)
  {
    result->message= std::string(label) + " absence result failed: " +
                     mysql_error(mysql);
    return false;
  }

  const bool ok= mysql_fetch_row(res) == nullptr;
  if (!ok)
    result->message= std::string(label) + " still exists after DROP";

  mysql_free_result(res);
  return ok;
}

static bool phase_loads_existing_catalog(const std::string &phase)
{
  return phase == "read" ||
         phase == "recovery-latest" ||
         phase == "recovery-read" ||
         phase == "transaction-read";
}

static void write_report(const SmokeOptions &options,
                         const std::vector<std::string> &server_args,
                         const SmokeResult &result)
{
  std::ofstream report(options.report.c_str());
  if (!report)
  {
    std::cerr << "could not open report: " << options.report << std::endl;
    return;
  }

  report << "# MyLite Storage Engine Smoke Report\n\n";
  report << "## Paths\n\n";
  report << "datadir=" << options.datadir << "\n";
  report << "tmpdir=" << options.tmpdir << "\n";
  report << "lc_messages_dir=" << options.lc_messages_dir << "\n";
  report << "runtime_dir=" << options.runtime_dir << "\n\n";
  if (!options.catalog_file.empty())
    report << "catalog_file=" << options.catalog_file << "\n";
  report << "persistence_phase=" << options.persistence_phase << "\n\n";

  report << "## Server Arguments\n\n";
  for (const std::string &arg : server_args)
    report << arg << "\n";
  report << "\n";

  report << "## Result\n\n";
  report << "status=" << result.status << "\n";
  report << "phase=" << result.phase << "\n";
  report << "message=" << result.message << "\n";
  if (!result.engine.empty())
    report << "engine=" << result.engine << "\n";
  if (!result.support.empty())
    report << "support=" << result.support << "\n";
  if (!result.transactions.empty())
    report << "transactions=" << result.transactions << "\n";
  if (!result.discovered_table.empty())
    report << "discovered_table=" << result.discovered_table << "\n";
  if (!result.count.empty())
    report << "count=" << result.count << "\n";
  if (!result.created_count.empty())
    report << "created_count=" << result.created_count << "\n";
  if (!result.altered_column.empty())
    report << "altered_column=" << result.altered_column << "\n";
  if (!result.copy_alter_rows.empty())
    report << "copy_alter_rows=" << result.copy_alter_rows << "\n";
  if (!result.copy_alter_note_order_ids.empty())
    report << "copy_alter_note_order_ids="
           << result.copy_alter_note_order_ids << "\n";
  if (!result.copy_alter_null_note_ids.empty())
    report << "copy_alter_null_note_ids="
           << result.copy_alter_null_note_ids << "\n";
  if (!result.copy_alter_payload_lookup_id.empty())
    report << "copy_alter_payload_lookup_id="
           << result.copy_alter_payload_lookup_id << "\n";
  if (!result.copy_alter_autoincrement_id.empty())
    report << "copy_alter_autoincrement_id="
           << result.copy_alter_autoincrement_id << "\n";
  if (!result.renamed_count.empty())
    report << "renamed_count=" << result.renamed_count << "\n";
  if (!result.dropped_table.empty())
    report << "dropped_table=" << result.dropped_table << "\n";
  if (!result.row_count.empty())
    report << "row_count=" << result.row_count << "\n";
  if (!result.row_notes.empty())
    report << "row_notes=" << result.row_notes << "\n";
  if (!result.row_updated_note.empty())
    report << "row_updated_note=" << result.row_updated_note << "\n";
  if (!result.row_deleted_count.empty())
    report << "row_deleted_count=" << result.row_deleted_count << "\n";
  if (!result.unsupported_autoincrement.empty())
    report << "unsupported_autoincrement="
           << result.unsupported_autoincrement << "\n";
  if (!result.large_row_value.empty())
    report << "large_row_value=" << result.large_row_value << "\n";
  if (!result.large_row_updated_length.empty())
    report << "large_row_updated_length="
           << result.large_row_updated_length << "\n";
  if (!result.large_row_deleted_count.empty())
    report << "large_row_deleted_count="
           << result.large_row_deleted_count << "\n";
  if (!result.blob_text_lengths.empty())
    report << "blob_text_lengths=" << result.blob_text_lengths << "\n";
  if (!result.blob_text_edges.empty())
    report << "blob_text_edges=" << result.blob_text_edges << "\n";
  if (!result.blob_text_updated.empty())
    report << "blob_text_updated=" << result.blob_text_updated << "\n";
  if (!result.blob_text_deleted_count.empty())
    report << "blob_text_deleted_count="
           << result.blob_text_deleted_count << "\n";
  if (!result.blob_text_key_lookup_id.empty())
    report << "blob_text_key_lookup_id="
           << result.blob_text_key_lookup_id << "\n";
  if (!result.blob_text_key_order_ids.empty())
    report << "blob_text_key_order_ids="
           << result.blob_text_key_order_ids << "\n";
  if (!result.blob_text_key_duplicate.empty())
    report << "blob_text_key_duplicate="
           << result.blob_text_key_duplicate << "\n";
  if (!result.blob_text_key_updated_id.empty())
    report << "blob_text_key_updated_id="
           << result.blob_text_key_updated_id << "\n";
  if (!result.blob_text_key_deleted_count.empty())
    report << "blob_text_key_deleted_count="
           << result.blob_text_key_deleted_count << "\n";
  if (!result.nullable_key_null_ids.empty())
    report << "nullable_key_null_ids="
           << result.nullable_key_null_ids << "\n";
  if (!result.nullable_key_lookup_id.empty())
    report << "nullable_key_lookup_id="
           << result.nullable_key_lookup_id << "\n";
  if (!result.nullable_key_duplicate.empty())
    report << "nullable_key_duplicate="
           << result.nullable_key_duplicate << "\n";
  if (!result.nullable_key_updated_null_ids.empty())
    report << "nullable_key_updated_null_ids="
           << result.nullable_key_updated_null_ids << "\n";
  if (!result.nullable_key_deleted_count.empty())
    report << "nullable_key_deleted_count="
           << result.nullable_key_deleted_count << "\n";
  if (!result.nullable_composite_key_head_null_ids.empty())
    report << "nullable_composite_key_head_null_ids="
           << result.nullable_composite_key_head_null_ids << "\n";
  if (!result.nullable_composite_key_tail_null_ids.empty())
    report << "nullable_composite_key_tail_null_ids="
           << result.nullable_composite_key_tail_null_ids << "\n";
  if (!result.nullable_composite_key_duplicate.empty())
    report << "nullable_composite_key_duplicate="
           << result.nullable_composite_key_duplicate << "\n";
  if (!result.nullable_blob_key_null_ids.empty())
    report << "nullable_blob_key_null_ids="
           << result.nullable_blob_key_null_ids << "\n";
  if (!result.nullable_blob_key_lookup_id.empty())
    report << "nullable_blob_key_lookup_id="
           << result.nullable_blob_key_lookup_id << "\n";
  if (!result.nullable_blob_key_duplicate.empty())
    report << "nullable_blob_key_duplicate="
           << result.nullable_blob_key_duplicate << "\n";
  if (!result.unsupported_foreign_key_create.empty())
    report << "unsupported_foreign_key_create="
           << result.unsupported_foreign_key_create << "\n";
  if (!result.unsupported_foreign_key_alter.empty())
    report << "unsupported_foreign_key_alter="
           << result.unsupported_foreign_key_alter << "\n";
  if (!result.foreign_key_parent_count.empty())
    report << "foreign_key_parent_count="
           << result.foreign_key_parent_count << "\n";
  if (!result.unsupported_generated_virtual.empty())
    report << "unsupported_generated_virtual="
           << result.unsupported_generated_virtual << "\n";
  if (!result.unsupported_generated_stored.empty())
    report << "unsupported_generated_stored="
           << result.unsupported_generated_stored << "\n";
  if (!result.unsupported_generated_alter.empty())
    report << "unsupported_generated_alter="
           << result.unsupported_generated_alter << "\n";
  if (!result.generated_base_count.empty())
    report << "generated_base_count="
           << result.generated_base_count << "\n";
  if (!result.check_constraint_insert.empty())
    report << "check_constraint_insert="
           << result.check_constraint_insert << "\n";
  if (!result.check_constraint_update.empty())
    report << "check_constraint_update="
           << result.check_constraint_update << "\n";
  if (!result.check_constraint_rows.empty())
    report << "check_constraint_rows="
           << result.check_constraint_rows << "\n";
  if (!result.unsupported_geometry.empty())
    report << "unsupported_geometry=" << result.unsupported_geometry << "\n";
  if (!result.key_lookup_note.empty())
    report << "key_lookup_note=" << result.key_lookup_note << "\n";
  if (!result.key_order_ids.empty())
    report << "key_order_ids=" << result.key_order_ids << "\n";
  if (!result.duplicate_key.empty())
    report << "duplicate_key=" << result.duplicate_key << "\n";
  if (!result.update_duplicate_key.empty())
    report << "update_duplicate_key=" << result.update_duplicate_key << "\n";
  if (!result.unsupported_reverse_key.empty())
    report << "unsupported_reverse_key="
           << result.unsupported_reverse_key << "\n";
  if (!result.autoincrement_ids.empty())
    report << "autoincrement_ids=" << result.autoincrement_ids << "\n";
  if (!result.explicit_autoincrement_ids.empty())
    report << "explicit_autoincrement_ids="
           << result.explicit_autoincrement_ids << "\n";
  if (!result.persisted_count.empty())
    report << "persisted_count=" << result.persisted_count << "\n";
  if (!result.persisted_column.empty())
    report << "persisted_column=" << result.persisted_column << "\n";
  if (!result.persisted_notes.empty())
    report << "persisted_notes=" << result.persisted_notes << "\n";
  if (!result.persisted_autoincrement_ids.empty())
    report << "persisted_autoincrement_ids="
           << result.persisted_autoincrement_ids << "\n";
  if (!result.persisted_key_lookup_note.empty())
    report << "persisted_key_lookup_note="
           << result.persisted_key_lookup_note << "\n";
  if (!result.persisted_key_order_ids.empty())
    report << "persisted_key_order_ids="
           << result.persisted_key_order_ids << "\n";
  if (!result.persisted_note_lookup_id.empty())
    report << "persisted_note_lookup_id="
           << result.persisted_note_lookup_id << "\n";
  if (!result.persisted_note_order_ids.empty())
    report << "persisted_note_order_ids="
           << result.persisted_note_order_ids << "\n";
  if (!result.persisted_large_lengths.empty())
    report << "persisted_large_lengths="
           << result.persisted_large_lengths << "\n";
  if (!result.persisted_large_edges.empty())
    report << "persisted_large_edges="
           << result.persisted_large_edges << "\n";
  if (!result.persisted_wide_count.empty())
    report << "persisted_wide_count=" << result.persisted_wide_count << "\n";
  if (!result.persisted_blob_text_lengths.empty())
    report << "persisted_blob_text_lengths="
           << result.persisted_blob_text_lengths << "\n";
  if (!result.persisted_blob_text_edges.empty())
    report << "persisted_blob_text_edges="
           << result.persisted_blob_text_edges << "\n";
  if (!result.persisted_blob_text_rollback.empty())
    report << "persisted_blob_text_rollback="
           << result.persisted_blob_text_rollback << "\n";
  if (!result.persisted_blob_text_key_lookup_id.empty())
    report << "persisted_blob_text_key_lookup_id="
           << result.persisted_blob_text_key_lookup_id << "\n";
  if (!result.persisted_blob_text_key_order_ids.empty())
    report << "persisted_blob_text_key_order_ids="
           << result.persisted_blob_text_key_order_ids << "\n";
  if (!result.persisted_nullable_key_null_ids.empty())
    report << "persisted_nullable_key_null_ids="
           << result.persisted_nullable_key_null_ids << "\n";
  if (!result.persisted_nullable_key_lookup_id.empty())
    report << "persisted_nullable_key_lookup_id="
           << result.persisted_nullable_key_lookup_id << "\n";
  if (!result.persisted_copy_alter_rows.empty())
    report << "persisted_copy_alter_rows="
           << result.persisted_copy_alter_rows << "\n";
  if (!result.persisted_copy_alter_note_order_ids.empty())
    report << "persisted_copy_alter_note_order_ids="
           << result.persisted_copy_alter_note_order_ids << "\n";
  if (!result.persisted_copy_alter_null_note_ids.empty())
    report << "persisted_copy_alter_null_note_ids="
           << result.persisted_copy_alter_null_note_ids << "\n";
  if (!result.persisted_copy_alter_payload_lookup_id.empty())
    report << "persisted_copy_alter_payload_lookup_id="
           << result.persisted_copy_alter_payload_lookup_id << "\n";
  if (!result.persisted_copy_alter_autoincrement_id.empty())
    report << "persisted_copy_alter_autoincrement_id="
           << result.persisted_copy_alter_autoincrement_id << "\n";
  if (!result.persisted_check_rows.empty())
    report << "persisted_check_rows="
           << result.persisted_check_rows << "\n";
  if (!result.persisted_check_insert.empty())
    report << "persisted_check_insert="
           << result.persisted_check_insert << "\n";
  if (!result.recovery_marker.empty())
    report << "recovery_marker=" << result.recovery_marker << "\n";
  if (!result.recovery_reclaim.empty())
    report << "recovery_reclaim=" << result.recovery_reclaim << "\n";
  if (!result.transaction_rollback_rows.empty())
    report << "transaction_rollback_rows="
           << result.transaction_rollback_rows << "\n";
  if (!result.transaction_clean_savepoint_rows.empty())
    report << "transaction_clean_savepoint_rows="
           << result.transaction_clean_savepoint_rows << "\n";
  if (!result.transaction_savepoint_rows.empty())
    report << "transaction_savepoint_rows="
           << result.transaction_savepoint_rows << "\n";
  if (!result.transaction_release_rows.empty())
    report << "transaction_release_rows="
           << result.transaction_release_rows << "\n";
  if (!result.transaction_rows.empty())
    report << "transaction_rows=" << result.transaction_rows << "\n";
  if (!result.transaction_rollback_warnings.empty())
    report << "transaction_rollback_warnings="
           << result.transaction_rollback_warnings << "\n";
  if (!result.statement_autocommit_error.empty())
    report << "statement_autocommit_error="
           << result.statement_autocommit_error << "\n";
  if (!result.statement_autocommit_rows.empty())
    report << "statement_autocommit_rows="
           << result.statement_autocommit_rows << "\n";
  if (!result.statement_autocommit_warnings.empty())
    report << "statement_autocommit_warnings="
           << result.statement_autocommit_warnings << "\n";
  if (!result.statement_transaction_error.empty())
    report << "statement_transaction_error="
           << result.statement_transaction_error << "\n";
  if (!result.statement_transaction_rows.empty())
    report << "statement_transaction_rows="
           << result.statement_transaction_rows << "\n";
  if (!result.statement_transaction_warnings.empty())
    report << "statement_transaction_warnings="
           << result.statement_transaction_warnings << "\n";
  if (!result.statement_rows.empty())
    report << "statement_rows=" << result.statement_rows << "\n";
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
