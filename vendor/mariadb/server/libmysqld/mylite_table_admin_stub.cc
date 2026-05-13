/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mariadb.h"
#include "sql_priv.h"
#include "sql_class.h"
#include "unireg.h"
#include "item.h"
#include "sql_admin.h"
#include "mysqld_error.h"

static bool mylite_table_admin_unsupported(const char *statement);

void fill_check_table_metadata_fields(THD *thd, List<Item>* fields)
{
  Item *item;

  item= new (thd->mem_root) Item_empty_string(thd, "Table",
                                              NAME_CHAR_LEN * 2);
  item->set_maybe_null();
  fields->push_back(item, thd->mem_root);

  item= new (thd->mem_root) Item_empty_string(thd, "Op", 10);
  item->set_maybe_null();
  fields->push_back(item, thd->mem_root);

  item= new (thd->mem_root) Item_empty_string(thd, "Msg_type", 10);
  item->set_maybe_null();
  fields->push_back(item, thd->mem_root);

  item= new (thd->mem_root) Item_empty_string(thd, "Msg_text",
                                              SQL_ADMIN_MSG_TEXT_SIZE);
  item->set_maybe_null();
  fields->push_back(item, thd->mem_root);
}

bool mysql_assign_to_keycache(THD *, TABLE_LIST *, const LEX_CSTRING *)
{
  return mylite_table_admin_unsupported("CACHE INDEX");
}

bool mysql_preload_keys(THD *, TABLE_LIST *)
{
  return mylite_table_admin_unsupported("LOAD INDEX INTO CACHE");
}

bool Sql_cmd_analyze_table::execute(THD *)
{
  return mylite_table_admin_unsupported("ANALYZE TABLE");
}

bool Sql_cmd_check_table::execute(THD *)
{
  return mylite_table_admin_unsupported("CHECK TABLE");
}

bool Sql_cmd_optimize_table::execute(THD *)
{
  return mylite_table_admin_unsupported("OPTIMIZE TABLE");
}

bool Sql_cmd_repair_table::execute(THD *)
{
  return mylite_table_admin_unsupported("REPAIR TABLE");
}

static bool mylite_table_admin_unsupported(const char *statement)
{
  my_error(ER_NOT_SUPPORTED_YET, MYF(0), statement);
  return true;
}
