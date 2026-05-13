/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mariadb.h"
#include "sql_class.h"
#include "sql_lex.h"
#include "json_table.h"
#include "mysqld_error.h"

static bool mylite_json_table_unsupported();

TABLE *
create_table_for_function(THD *, TABLE_LIST *)
{
  mylite_json_table_unsupported();
  return nullptr;
}

table_map
add_table_function_dependencies(List<TABLE_LIST> *, table_map, bool *)
{
  return 0;
}

bool
push_table_function_arg_context(LEX *lex, MEM_ROOT *alloc)
{
  List_iterator<Name_resolution_context> it(lex->context_stack);
  Name_resolution_context *ctx;
  while ((ctx= it++))
  {
    if (ctx->select_lex && ctx == &ctx->select_lex->context)
      break;
  }
  DBUG_ASSERT(ctx);

  Name_resolution_context *new_ctx= new (alloc) Name_resolution_context;
  if (!new_ctx)
    return true;
  *new_ctx= *ctx;
  return lex->push_context(new_ctx);
}

int
Json_table_column::On_response::respond(Json_table_column *, Field *, uint)
{
  return mylite_json_table_unsupported();
}

int
Json_table_column::On_response::print(const char *, String *) const
{
  return mylite_json_table_unsupported();
}

int
Json_table_column::set(THD *, enum_type ctype, const LEX_CSTRING &,
                       CHARSET_INFO *cs)
{
  set(ctype);
  m_explicit_cs= cs;
  return mylite_json_table_unsupported();
}

int
Json_table_column::set(THD *, enum_type ctype, const LEX_CSTRING &,
                       const Lex_column_charset_collation_attrs_st &)
{
  set(ctype);
  return mylite_json_table_unsupported();
}

int
Json_table_column::print(THD *, Field **, String *)
{
  return mylite_json_table_unsupported();
}

void
Json_table_nested_path::scan_start(CHARSET_INFO *, const uchar *, const uchar *)
{
  m_null= true;
  m_ordinality_counter= 0;
}

int
Json_table_nested_path::scan_next()
{
  return 1;
}

bool
Json_table_nested_path::check_error(const char *)
{
  return false;
}

bool
Json_table_nested_path::column_in_this_or_nested(
    const Json_table_nested_path *, const Json_table_column *)
{
  return false;
}

int
Json_table_nested_path::print(THD *, Field ***, String *,
                              List_iterator_fast<Json_table_column> &,
                              Json_table_column **)
{
  return mylite_json_table_unsupported();
}

int
Json_table_nested_path::set_path(THD *, const LEX_CSTRING &)
{
  return mylite_json_table_unsupported();
}

int
Table_function_json_table::walk_items(Item_processor processor,
                                      bool walk_subquery, void *argument)
{
  return m_json ? m_json->walk(processor, walk_subquery, argument) : 0;
}

void
Table_function_json_table::get_estimates(ha_rows *out_rows,
                                         double *scan_time,
                                         double *startup_cost)
{
  *out_rows= 0;
  *scan_time= 0.0;
  *startup_cost= 0.0;
}

void
Table_function_json_table::end_nested_path()
{
  if (cur_parent && cur_parent->m_parent)
  {
    last_sibling_hook= &cur_parent->m_next_nested;
    cur_parent= cur_parent->m_parent;
  }
}

void
Table_function_json_table::fix_after_pullout(TABLE_LIST *, st_select_lex *,
                                             bool)
{
}

void
Table_function_json_table::start_nested_path(Json_table_nested_path *np)
{
  np->m_parent= cur_parent;
  *last_sibling_hook= np;
  cur_parent= np;
  last_sibling_hook= &np->m_nested;
}

int
Table_function_json_table::print(THD *, TABLE_LIST *, String *,
                                 enum_query_type)
{
  return mylite_json_table_unsupported();
}

bool
Table_function_json_table::setup(THD *, TABLE_LIST *, SELECT_LEX *)
{
  return mylite_json_table_unsupported();
}

static bool
mylite_json_table_unsupported()
{
  my_error(ER_NOT_SUPPORTED_YET, MYF(0),
           "JSON_TABLE in the MyLite minsize profile");
  return true;
}
