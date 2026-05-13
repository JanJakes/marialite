/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mariadb.h"
#include "sql_priv.h"
#include "sql_class.h"
#include "sql_get_diagnostics.h"
#include "sql_signal.h"
#include "mysqld_error.h"

const LEX_CSTRING Diag_condition_item_names[]=
{
  { STRING_WITH_LEN("CLASS_ORIGIN") },
  { STRING_WITH_LEN("SUBCLASS_ORIGIN") },
  { STRING_WITH_LEN("CONSTRAINT_CATALOG") },
  { STRING_WITH_LEN("CONSTRAINT_SCHEMA") },
  { STRING_WITH_LEN("CONSTRAINT_NAME") },
  { STRING_WITH_LEN("CATALOG_NAME") },
  { STRING_WITH_LEN("SCHEMA_NAME") },
  { STRING_WITH_LEN("TABLE_NAME") },
  { STRING_WITH_LEN("COLUMN_NAME") },
  { STRING_WITH_LEN("CURSOR_NAME") },
  { STRING_WITH_LEN("MESSAGE_TEXT") },
  { STRING_WITH_LEN("MYSQL_ERRNO") },
  { STRING_WITH_LEN("ROW_NUMBER") },

  { STRING_WITH_LEN("CONDITION_IDENTIFIER") },
  { STRING_WITH_LEN("CONDITION_NUMBER") },
  { STRING_WITH_LEN("CONNECTION_NAME") },
  { STRING_WITH_LEN("MESSAGE_LENGTH") },
  { STRING_WITH_LEN("MESSAGE_OCTET_LENGTH") },
  { STRING_WITH_LEN("PARAMETER_MODE") },
  { STRING_WITH_LEN("PARAMETER_NAME") },
  { STRING_WITH_LEN("PARAMETER_ORDINAL_POSITION") },
  { STRING_WITH_LEN("RETURNED_SQLSTATE") },
  { STRING_WITH_LEN("ROUTINE_CATALOG") },
  { STRING_WITH_LEN("ROUTINE_NAME") },
  { STRING_WITH_LEN("ROUTINE_SCHEMA") },
  { STRING_WITH_LEN("SERVER_NAME") },
  { STRING_WITH_LEN("SPECIFIC_NAME") },
  { STRING_WITH_LEN("TRIGGER_CATALOG") },
  { STRING_WITH_LEN("TRIGGER_NAME") },
  { STRING_WITH_LEN("TRIGGER_SCHEMA") }
};

static bool mylite_sql_diagnostics_unsupported(const char *statement)
{
  my_error(ER_NOT_SUPPORTED_YET, MYF(0), statement);
  return true;
}

Set_signal_information::Set_signal_information(
  const Set_signal_information& set)
{
  memcpy(m_item, set.m_item, sizeof(m_item));
}

void Set_signal_information::clear()
{
  memset(m_item, 0, sizeof(m_item));
}

bool Sql_cmd_get_diagnostics::execute(THD *)
{
  return mylite_sql_diagnostics_unsupported(
    "GET DIAGNOSTICS in the MyLite minsize profile");
}

bool Diagnostics_information_item::set_value(THD *, Item **)
{
  return mylite_sql_diagnostics_unsupported(
    "GET DIAGNOSTICS in the MyLite minsize profile");
}

Item *Statement_information_item::get_value(THD *, const Diagnostics_area *)
{
  mylite_sql_diagnostics_unsupported(
    "GET DIAGNOSTICS in the MyLite minsize profile");
  return NULL;
}

bool Statement_information::aggregate(THD *, const Diagnostics_area *)
{
  return mylite_sql_diagnostics_unsupported(
    "GET DIAGNOSTICS in the MyLite minsize profile");
}

Item *Condition_information_item::make_utf8_string_item(THD *, const String *)
{
  mylite_sql_diagnostics_unsupported(
    "GET DIAGNOSTICS in the MyLite minsize profile");
  return NULL;
}

Item *Condition_information_item::get_value(THD *, const Sql_condition *)
{
  mylite_sql_diagnostics_unsupported(
    "GET DIAGNOSTICS in the MyLite minsize profile");
  return NULL;
}

bool Condition_information::aggregate(THD *, const Diagnostics_area *)
{
  return mylite_sql_diagnostics_unsupported(
    "GET DIAGNOSTICS in the MyLite minsize profile");
}

int Sql_cmd_common_signal::eval_signal_informations(THD *, Sql_condition *)
{
  return mylite_sql_diagnostics_unsupported(
    "SIGNAL in the MyLite minsize profile");
}

bool Sql_cmd_common_signal::raise_condition(THD *, Sql_condition *)
{
  return mylite_sql_diagnostics_unsupported(
    "SIGNAL in the MyLite minsize profile");
}

bool Sql_cmd_signal::execute(THD *)
{
  return mylite_sql_diagnostics_unsupported(
    "SIGNAL in the MyLite minsize profile");
}

bool Sql_cmd_resignal::execute(THD *)
{
  return mylite_sql_diagnostics_unsupported(
    "RESIGNAL in the MyLite minsize profile");
}
