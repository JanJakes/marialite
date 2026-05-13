/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mariadb.h"
#include "sql_priv.h"
#include "sql_handler.h"
#include "mysqld_error.h"

static bool mylite_sql_handler_unsupported();

bool mysql_ha_open(THD *, TABLE_LIST *, SQL_HANDLER *)
{
  return mylite_sql_handler_unsupported();
}

bool mysql_ha_close(THD *, TABLE_LIST *)
{
  return mylite_sql_handler_unsupported();
}

bool mysql_ha_read(THD *, TABLE_LIST *, enum enum_ha_read_modes,
                   const char *, List<Item> *, enum ha_rkey_function,
                   Item *, ha_rows, ha_rows)
{
  return mylite_sql_handler_unsupported();
}

SQL_HANDLER *mysql_ha_read_prepare(THD *, TABLE_LIST *,
                                   enum enum_ha_read_modes, const char *,
                                   List<Item> *, enum ha_rkey_function,
                                   Item *)
{
  mylite_sql_handler_unsupported();
  return nullptr;
}

void mysql_ha_flush(THD *)
{
}

void mysql_ha_flush_tables(THD *, TABLE_LIST *)
{
}

void mysql_ha_rm_tables(THD *, TABLE_LIST *)
{
}

void mysql_ha_cleanup_no_free(THD *)
{
}

void mysql_ha_cleanup(THD *)
{
}

void mysql_ha_set_explicit_lock_duration(THD *)
{
}

void mysql_ha_rm_temporary_tables(THD *)
{
}

static bool mylite_sql_handler_unsupported()
{
  my_error(ER_NOT_SUPPORTED_YET, MYF(0),
           "SQL HANDLER commands in the MyLite minsize profile");
  return true;
}
