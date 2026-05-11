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
  std::string engine;
  std::string report;
  std::string fingerprint;
};

struct CaseResult
{
  std::string label;
  std::string value;
  bool passed= false;
  std::string message;
};

struct SmokeResult
{
  int status= 1;
  std::string phase;
  std::string message;
  std::vector<CaseResult> cases;
};

static bool parse_options(int argc, char **argv, SmokeOptions *options,
                          std::string *error);
static std::vector<std::string> build_server_args(const SmokeOptions &options);
static int run_smoke(const SmokeOptions &options,
                     const std::vector<std::string> &server_args,
                     SmokeResult *result);
static bool run_scalar_cases(MYSQL *mysql, SmokeResult *result);
static bool run_row_cases(MYSQL *mysql, const SmokeOptions &options,
                          SmokeResult *result);
static bool run_key_cases(MYSQL *mysql, const SmokeOptions &options,
                          SmokeResult *result);
static bool run_autoincrement_cases(MYSQL *mysql, const SmokeOptions &options,
                                    SmokeResult *result);
static bool execute_statement(MYSQL *mysql, const std::string &statement,
                              const char *label, SmokeResult *result);
static bool execute_statement_expect_error(MYSQL *mysql,
                                           const std::string &statement,
                                           const char *label,
                                           SmokeResult *result);
static bool fetch_single_value(MYSQL *mysql, const std::string &query,
                               const char *label, SmokeResult *result);
static bool record_case(SmokeResult *result, const char *label,
                        const std::string &value, bool passed,
                        const std::string &message);
static void write_report(const SmokeOptions &options,
                         const std::vector<std::string> &server_args,
                         const SmokeResult &result);
static void write_fingerprint(const SmokeOptions &options,
                              const SmokeResult &result);
static bool option_value(const char *arg, const char *name,
                         std::string *value);
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
  write_fingerprint(options, result);
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
    else if (option_value(argv[i], "--engine=", &value))
      options->engine= value;
    else if (option_value(argv[i], "--report=", &value))
      options->report= value;
    else if (option_value(argv[i], "--fingerprint=", &value))
      options->fingerprint= value;
    else
    {
      *error= std::string("unknown argument: ") + argv[i];
      return false;
    }
  }

  return require_option(options->datadir, "--datadir", error) &&
         require_option(options->tmpdir, "--tmpdir", error) &&
         require_option(options->lc_messages_dir, "--lc-messages-dir", error) &&
         require_option(options->runtime_dir, "--runtime-dir", error) &&
         require_option(options->engine, "--engine", error) &&
         require_option(options->report, "--report", error) &&
         require_option(options->fingerprint, "--fingerprint", error);
}

static std::vector<std::string> build_server_args(const SmokeOptions &options)
{
  std::vector<std::string> args= {
    "mylite-compatibility-smoke",
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

  result->phase= "scalar_cases";
  if (!run_scalar_cases(mysql, result))
    goto done;

  result->phase= "row_cases";
  if (!run_row_cases(mysql, options, result))
    goto done;

  result->phase= "key_cases";
  if (!run_key_cases(mysql, options, result))
    goto done;

  result->phase= "autoincrement_cases";
  if (!run_autoincrement_cases(mysql, options, result))
    goto done;

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

static bool run_scalar_cases(MYSQL *mysql, SmokeResult *result)
{
  return fetch_single_value(mysql, "SELECT 1 + 2", "scalar_add", result) &&
         fetch_single_value(mysql, "SELECT CONCAT('my', 'lite')",
                            "scalar_concat", result) &&
         fetch_single_value(mysql, "SELECT COALESCE(NULL, 'fallback')",
                            "scalar_coalesce", result);
}

static bool run_row_cases(MYSQL *mysql, const SmokeOptions &options,
                          SmokeResult *result)
{
  const std::string create=
    "CREATE TABLE mylite.compat_rows "
    "(id INT, note VARCHAR(12)) ENGINE=" + options.engine;

  return execute_statement(mysql, "DROP TABLE IF EXISTS mylite.compat_rows",
                           "row_drop_before", result) &&
         execute_statement(mysql, create, "row_create", result) &&
         execute_statement(mysql,
                           "INSERT INTO mylite.compat_rows VALUES "
                           "(1, 'one'), (2, 'two'), (3, 'three')",
                           "row_insert", result) &&
         execute_statement(mysql,
                           "UPDATE mylite.compat_rows SET note = 'TWO' "
                           "WHERE id = 2",
                           "row_update", result) &&
         execute_statement(mysql,
                           "DELETE FROM mylite.compat_rows WHERE id = 1",
                           "row_delete", result) &&
         fetch_single_value(mysql,
                            "SELECT COUNT(*) FROM mylite.compat_rows",
                            "row_count", result) &&
         fetch_single_value(mysql,
                            "SELECT GROUP_CONCAT(CONCAT(id, ':', note) "
                            "ORDER BY id SEPARATOR ',') "
                            "FROM mylite.compat_rows",
                            "row_values", result) &&
         execute_statement(mysql, "DROP TABLE mylite.compat_rows",
                           "row_drop_after", result);
}

static bool run_key_cases(MYSQL *mysql, const SmokeOptions &options,
                          SmokeResult *result)
{
  const std::string create=
    "CREATE TABLE mylite.compat_keyed "
    "(id INT NOT NULL, note VARCHAR(12) NOT NULL, "
    "PRIMARY KEY(id), KEY note_key(note)) ENGINE=" + options.engine;

  return execute_statement(mysql, "DROP TABLE IF EXISTS mylite.compat_keyed",
                           "key_drop_before", result) &&
         execute_statement(mysql, create, "key_create", result) &&
         execute_statement(mysql,
                           "INSERT INTO mylite.compat_keyed VALUES "
                           "(2, 'two'), (1, 'one'), (3, 'three')",
                           "key_insert", result) &&
         fetch_single_value(mysql,
                            "SELECT note FROM mylite.compat_keyed "
                            "FORCE INDEX(PRIMARY) WHERE id = 2",
                            "key_lookup", result) &&
         fetch_single_value(mysql,
                            "SELECT GROUP_CONCAT(id ORDER BY id "
                            "SEPARATOR ',') FROM mylite.compat_keyed "
                            "FORCE INDEX(PRIMARY)",
                            "key_order", result) &&
         execute_statement_expect_error(mysql,
                                        "INSERT INTO mylite.compat_keyed "
                                        "VALUES (2, 'duplicate')",
                                        "key_duplicate", result) &&
         execute_statement(mysql, "DROP TABLE mylite.compat_keyed",
                           "key_drop_after", result);
}

static bool run_autoincrement_cases(MYSQL *mysql, const SmokeOptions &options,
                                    SmokeResult *result)
{
  const std::string create=
    "CREATE TABLE mylite.compat_auto "
    "(id INT NOT NULL AUTO_INCREMENT, note VARCHAR(12) NOT NULL, "
    "PRIMARY KEY(id)) ENGINE=" + options.engine;

  return execute_statement(mysql, "DROP TABLE IF EXISTS mylite.compat_auto",
                           "auto_drop_before", result) &&
         execute_statement(mysql, create, "auto_create", result) &&
         execute_statement(mysql,
                           "INSERT INTO mylite.compat_auto (note) VALUES "
                           "('one'), ('two')",
                           "auto_insert_generated", result) &&
         execute_statement(mysql,
                           "INSERT INTO mylite.compat_auto (id, note) "
                           "VALUES (10, 'ten')",
                           "auto_insert_explicit", result) &&
         execute_statement(mysql,
                           "INSERT INTO mylite.compat_auto (note) VALUES "
                           "('eleven')",
                           "auto_insert_next", result) &&
         execute_statement(mysql,
                           "DELETE FROM mylite.compat_auto WHERE id = 11",
                           "auto_delete", result) &&
         execute_statement(mysql,
                           "INSERT INTO mylite.compat_auto (note) VALUES "
                           "('twelve')",
                           "auto_insert_after_delete", result) &&
         fetch_single_value(mysql,
                            "SELECT GROUP_CONCAT(id ORDER BY id "
                            "SEPARATOR ',') FROM mylite.compat_auto",
                            "auto_values", result) &&
         execute_statement(mysql, "DROP TABLE mylite.compat_auto",
                           "auto_drop_after", result);
}

static bool execute_statement(MYSQL *mysql, const std::string &statement,
                              const char *label, SmokeResult *result)
{
  if (mysql_query(mysql, statement.c_str()))
  {
    const std::string value= std::string("error:") +
      std::to_string(mysql_errno(mysql)) + ":" + mysql_sqlstate(mysql);
    const std::string message= std::string("mysql_query failed: ") +
      mysql_error(mysql);
    return record_case(result, label, value, false, message);
  }

  MYSQL_RES *res= mysql_store_result(mysql);
  if (res)
    mysql_free_result(res);
  return record_case(result, label, "ok", true, "ok");
}

static bool execute_statement_expect_error(MYSQL *mysql,
                                           const std::string &statement,
                                           const char *label,
                                           SmokeResult *result)
{
  if (mysql_query(mysql, statement.c_str()))
  {
    const std::string value= std::string("error:") +
      std::to_string(mysql_errno(mysql)) + ":" + mysql_sqlstate(mysql);
    return record_case(result, label, value, true, mysql_error(mysql));
  }

  MYSQL_RES *res= mysql_store_result(mysql);
  if (res)
    mysql_free_result(res);
  return record_case(result, label, "ok", false, "statement succeeded");
}

static bool fetch_single_value(MYSQL *mysql, const std::string &query,
                               const char *label, SmokeResult *result)
{
  if (mysql_query(mysql, query.c_str()))
  {
    const std::string value= std::string("error:") +
      std::to_string(mysql_errno(mysql)) + ":" + mysql_sqlstate(mysql);
    const std::string message= std::string("mysql_query failed: ") +
      mysql_error(mysql);
    return record_case(result, label, value, false, message);
  }

  MYSQL_RES *res= mysql_store_result(mysql);
  if (!res)
  {
    const std::string message= std::string("mysql_store_result failed: ") +
      mysql_error(mysql);
    return record_case(result, label, "error:no-result", false, message);
  }

  bool ok= false;
  std::string value;
  MYSQL_ROW row= mysql_fetch_row(res);
  if (mysql_num_fields(res) != 1)
    value= "error:column-count";
  else if (!row || !row[0])
    value= "NULL";
  else
  {
    value= row[0];
    ok= mysql_fetch_row(res) == nullptr;
  }

  mysql_free_result(res);
  return record_case(result, label, value, ok,
                     ok ? "ok" : "query returned unexpected shape");
}

static bool record_case(SmokeResult *result, const char *label,
                        const std::string &value, bool passed,
                        const std::string &message)
{
  CaseResult case_result;
  case_result.label= label;
  case_result.value= value;
  case_result.passed= passed;
  case_result.message= message;
  result->cases.push_back(case_result);
  if (!passed && result->message.empty())
    result->message= std::string(label) + ": " + message;
  return passed;
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

  report << "# MyLite Compatibility Smoke Report\n\n";
  report << "## Paths\n\n";
  report << "datadir=" << options.datadir << "\n";
  report << "tmpdir=" << options.tmpdir << "\n";
  report << "lc_messages_dir=" << options.lc_messages_dir << "\n";
  report << "runtime_dir=" << options.runtime_dir << "\n";
  if (!options.catalog_file.empty())
    report << "catalog_file=" << options.catalog_file << "\n";
  report << "engine=" << options.engine << "\n";
  report << "fingerprint=" << options.fingerprint << "\n\n";

  report << "## Server Arguments\n\n";
  for (const std::string &arg : server_args)
    report << arg << "\n";
  report << "\n";

  report << "## Result\n\n";
  report << "status=" << result.status << "\n";
  report << "phase=" << result.phase << "\n";
  report << "message=" << result.message << "\n\n";

  report << "## Cases\n\n";
  for (const CaseResult &case_result : result.cases)
  {
    report << "label=" << case_result.label << "\n";
    report << "passed=" << (case_result.passed ? "yes" : "no") << "\n";
    report << "value=" << case_result.value << "\n";
    report << "message=" << case_result.message << "\n\n";
  }
}

static void write_fingerprint(const SmokeOptions &options,
                              const SmokeResult &result)
{
  std::ofstream fingerprint(options.fingerprint.c_str());
  if (!fingerprint)
  {
    std::cerr << "could not open fingerprint: " << options.fingerprint
              << std::endl;
    return;
  }

  for (const CaseResult &case_result : result.cases)
    fingerprint << case_result.label << "=" << case_result.value << "\n";
}

static bool option_value(const char *arg, const char *name,
                         std::string *value)
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
