/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mariadb.h"
#include "sql_priv.h"
#include "sql_class.h"
#include "sql_window.h"
#include "mysqld_error.h"

static bool mylite_window_functions_unsupported();

bool Window_spec::check_window_names(List_iterator_fast<Window_spec> &)
{
  return mylite_window_functions_unsupported();
}

void Window_spec::print(String *, enum_query_type)
{
}

void Window_spec::print_order(String *, enum_query_type)
{
}

void Window_spec::print_partition(String *, enum_query_type)
{
}

bool Window_frame::check_frame_bounds()
{
  return mylite_window_functions_unsupported();
}

void Window_frame::print(String *, enum_query_type)
{
}

void Window_frame_bound::print(String *, enum_query_type)
{
}

int setup_windows(THD *, Ref_ptr_array, TABLE_LIST *, List<Item> &,
                  List<Item> &, List<Window_spec> &win_specs,
                  List<Item_window_func> &win_funcs)
{
  if (win_specs.elements || win_funcs.elements)
    return mylite_window_functions_unsupported();
  return 0;
}

ORDER *st_select_lex::find_common_window_func_partition_fields(THD *)
{
  if (window_funcs.elements)
    mylite_window_functions_unsupported();
  return nullptr;
}

bool Window_func_runner::add_function_to_run(Item_window_func *)
{
  return mylite_window_functions_unsupported();
}

bool Window_func_runner::exec(THD *, TABLE *, SORT_INFO *)
{
  return mylite_window_functions_unsupported();
}

bool Window_funcs_sort::setup(THD *, SQL_SELECT *,
                              List_iterator<Item_window_func> &,
                              st_join_table *)
{
  return mylite_window_functions_unsupported();
}

bool Window_funcs_sort::exec(JOIN *, bool)
{
  return mylite_window_functions_unsupported();
}

bool Window_funcs_computation::setup(THD *, List<Item_window_func> *,
                                     st_join_table *)
{
  return mylite_window_functions_unsupported();
}

bool Window_funcs_computation::exec(JOIN *, bool)
{
  return mylite_window_functions_unsupported();
}

Explain_aggr_window_funcs *
Window_funcs_computation::save_explain_plan(MEM_ROOT *, bool)
{
  mylite_window_functions_unsupported();
  return nullptr;
}

void Window_funcs_computation::cleanup()
{
}

bool st_select_lex::add_window_func(Item_window_func *)
{
  return mylite_window_functions_unsupported();
}

static bool mylite_window_functions_unsupported()
{
  my_error(ER_NOT_SUPPORTED_YET, MYF(0),
           "window functions in the MyLite minsize profile");
  return true;
}
