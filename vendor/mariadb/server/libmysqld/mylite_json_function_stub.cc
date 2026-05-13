/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mariadb.h"
#include "sql_priv.h"
#include "sql_class.h"
#include "item.h"

bool Item_func_json_valid::val_bool()
{
  String *js= args[0]->val_json(&tmp_value);

  if ((null_value= args[0]->null_value))
    return 0;

  return json_valid(js->ptr(), js->length(), js->charset());
}

bool is_json_type(const Item *item)
{
  for ( ; ; )
  {
    if (Type_handler_json_common::is_json_type_handler(item->type_handler()))
      return true;
    const Item_func_conv_charset *func;
    if (!(func= dynamic_cast<const Item_func_conv_charset *>(item->real_item())))
      return false;
    item= func->arguments()[0];
  }
  return false;
}

int Arg_comparator::compare_json_str_basic(Item *j, Item *s)
{
  String *js= j->val_str(&value1);
  String *str= s->val_str(&value2);
  if (js && str)
  {
    if (set_null)
      owner->null_value= 0;
    return sortcmp(js, str, compare_collation());
  }
  if (set_null)
    owner->null_value= 1;
  return -1;
}

int Arg_comparator::compare_e_json_str_basic(Item *j, Item *s)
{
  String *js= j->val_str(&value1);
  String *str= s->val_str(&value2);
  if (!js || !str)
    return MY_TEST(js == str);
  return MY_TEST(sortcmp(js, str, compare_collation()) == 0);
}

bool Item_func_json_arrayagg::fix_fields(THD *, Item **)
{
  my_error(ER_NOT_SUPPORTED_YET, MYF(0),
           "JSON_ARRAYAGG in the MyLite minsize profile");
  return true;
}

String *Item_func_json_arrayagg::get_str_from_item(Item *, String *)
{
  return NULL;
}

String *Item_func_json_arrayagg::get_str_from_field(Item *, Field *, String *,
                                                    const uchar *, size_t)
{
  return NULL;
}

void Item_func_json_arrayagg::cut_max_length(String *result,
                                             uint old_length,
                                             uint max_length) const
{
  Item_func_group_concat::cut_max_length(result, old_length, max_length);
}

Item *Item_func_json_arrayagg::copy_or_same(THD *thd)
{
  return new (thd->mem_root) Item_func_json_arrayagg(thd, this);
}

String *Item_func_json_arrayagg::val_str(String *)
{
  DBUG_ASSERT(0);
  return NULL;
}

Item_func_json_objectagg::
Item_func_json_objectagg(THD *thd, Item_func_json_objectagg *item)
  :Item_sum(thd, item)
{
  quick_group= FALSE;
  result.set_charset(collation.collation);
  result.append('{');
}

void Item_func_json_objectagg::cleanup()
{
  Item_sum::cleanup();
  result.length(1);
}

bool Item_func_json_objectagg::fix_fields(THD *, Item **)
{
  my_error(ER_NOT_SUPPORTED_YET, MYF(0),
           "JSON_OBJECTAGG in the MyLite minsize profile");
  return true;
}

Item *Item_func_json_objectagg::copy_or_same(THD *thd)
{
  return new (thd->mem_root) Item_func_json_objectagg(thd, this);
}

void Item_func_json_objectagg::clear()
{
  result.length(1);
  null_value= 1;
}

bool Item_func_json_objectagg::add()
{
  DBUG_ASSERT(0);
  return true;
}

String *Item_func_json_objectagg::val_str(String *)
{
  DBUG_ASSERT(0);
  return NULL;
}
