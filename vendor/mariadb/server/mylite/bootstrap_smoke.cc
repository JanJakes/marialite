/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include <mysql.h>
#include <mysqld_error.h>

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
  std::string report;
};

struct UnsupportedResult
{
  std::string label;
  std::string statement;
  unsigned int error= 0;
  std::string sqlstate;
  std::string message;
  bool passed= false;
};

struct SmokeResult
{
  int status= 1;
  std::string phase;
  std::string message;
  std::string value;
  std::vector<UnsupportedResult> unsupported;
};

static bool parse_options(int argc, char **argv, SmokeOptions *options,
                          std::string *error);
static std::vector<std::string> build_server_args(const SmokeOptions &options);
static int run_smoke(const SmokeOptions &options,
                     const std::vector<std::string> &server_args,
                     SmokeResult *result);
static bool fetch_select_one(MYSQL *mysql, SmokeResult *result);
static bool check_unsupported_statements(MYSQL *mysql, SmokeResult *result);
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
    else if (option_value(argv[i], "--report=", &value))
      options->report= value;
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
         require_option(options->report, "--report", error);
}

static std::vector<std::string> build_server_args(const SmokeOptions &options)
{
  return {
    "mylite-embedded-bootstrap-smoke",
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
}

static int run_smoke(const SmokeOptions &,
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

  result->phase= "select";
  if (!fetch_select_one(mysql, result))
    goto done;

  result->phase= "unsupported";
  if (!check_unsupported_statements(mysql, result))
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

static bool fetch_select_one(MYSQL *mysql, SmokeResult *result)
{
  if (mysql_query(mysql, "SELECT 1"))
  {
    result->message= std::string("mysql_query failed: ") + mysql_error(mysql);
    return false;
  }

  MYSQL_RES *res= mysql_store_result(mysql);
  if (!res)
  {
    result->message= std::string("mysql_store_result failed: ") +
                     mysql_error(mysql);
    return false;
  }

  bool ok= false;
  MYSQL_ROW row= mysql_fetch_row(res);
  if (mysql_num_fields(res) != 1)
    result->message= "SELECT 1 returned an unexpected column count";
  else if (!row || !row[0])
    result->message= "SELECT 1 returned no value";
  else if (std::strcmp(row[0], "1") != 0)
  {
    result->value= row[0];
    result->message= "SELECT 1 returned an unexpected value";
  }
  else if (mysql_fetch_row(res) != nullptr)
    result->message= "SELECT 1 returned more than one row";
  else
  {
    result->value= row[0];
    ok= true;
  }

  mysql_free_result(res);
  return ok;
}

static bool check_unsupported_statements(MYSQL *mysql, SmokeResult *result)
{
  struct UnsupportedStatement
  {
    const char *label;
    const char *statement;
  };

  const UnsupportedStatement statements[]= {
    {
      "create_udf",
      "CREATE FUNCTION mylite_unsupported RETURNS INTEGER "
      "SONAME 'mylite_unsupported.so'"
    },
    {
      "install_plugin",
      "INSTALL PLUGIN mylite_unsupported SONAME 'mylite_unsupported.so'"
    },
    {
      "uninstall_plugin",
      "UNINSTALL PLUGIN mylite_unsupported"
    },
    {
      "create_server",
      "CREATE SERVER mylite_unsupported FOREIGN DATA WRAPPER mysql "
      "OPTIONS (HOST 'localhost', DATABASE 'test', USER 'u', PASSWORD 'p')"
    },
    {
      "alter_server",
      "ALTER SERVER mylite_unsupported OPTIONS (HOST 'localhost')"
    },
    {
      "drop_server",
      "DROP SERVER mylite_unsupported"
    }
  };

  bool ok= true;
  for (const UnsupportedStatement &statement : statements)
  {
    UnsupportedResult unsupported;
    unsupported.label= statement.label;
    unsupported.statement= statement.statement;

    if (mysql_query(mysql, statement.statement) == 0)
    {
      MYSQL_RES *res= mysql_store_result(mysql);
      if (res)
        mysql_free_result(res);
      unsupported.message= "statement unexpectedly succeeded";
      ok= false;
    }
    else
    {
      unsupported.error= mysql_errno(mysql);
      unsupported.sqlstate= mysql_sqlstate(mysql);
      unsupported.message= mysql_error(mysql);
      unsupported.passed= unsupported.error == ER_OPTION_PREVENTS_STATEMENT;
      ok= ok && unsupported.passed;
    }

    result->unsupported.push_back(unsupported);
  }

  if (!ok)
    result->message= "unsupported statement check failed";
  return ok;
}

static void write_report(const SmokeOptions &options,
                         const std::vector<std::string> &server_args,
                         const SmokeResult &result)
{
  std::ofstream report(options.report.c_str(), std::ios::out | std::ios::trunc);
  if (!report)
  {
    std::cerr << "could not open report: " << options.report << std::endl;
    return;
  }

  report << "# MyLite Embedded Bootstrap Smoke Report\n\n";
  report << "## Paths\n\n";
  report << "datadir=" << options.datadir << "\n";
  report << "tmpdir=" << options.tmpdir << "\n";
  report << "lc_messages_dir=" << options.lc_messages_dir << "\n";
  report << "runtime_dir=" << options.runtime_dir << "\n\n";

  report << "## Server Arguments\n\n";
  for (const std::string &arg : server_args)
    report << arg << "\n";
  report << "\n";

  report << "## Result\n\n";
  report << "status=" << result.status << "\n";
  report << "phase=" << result.phase << "\n";
  report << "message=" << result.message << "\n";
  if (!result.value.empty())
    report << "value=" << result.value << "\n";

  report << "\n## Unsupported Surface Results\n\n";
  if (result.unsupported.empty())
  {
    report << "none\n";
    return;
  }

  for (const UnsupportedResult &unsupported : result.unsupported)
  {
    report << "label=" << unsupported.label << "\n";
    report << "passed=" << (unsupported.passed ? "yes" : "no") << "\n";
    report << "errno=" << unsupported.error << "\n";
    report << "sqlstate=" << unsupported.sqlstate << "\n";
    report << "message=" << unsupported.message << "\n";
    report << "statement=" << unsupported.statement << "\n\n";
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
