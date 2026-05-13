/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mariadb.h"
#include "sql_priv.h"
#include "field.h"
#include "mysqld_error.h"
#include "structs.h"

bool Load_data_param::add_outvar_field(THD *, const Field *field)
{
  if (field->flags & BLOB_FLAG)
  {
    m_use_blobs= true;
    m_fixed_length+= 256;
  }
  else
    m_fixed_length+= field->field_length;
  return false;
}

bool Load_data_param::add_outvar_user_var(THD *)
{
  if (m_is_fixed_length)
  {
    my_error(ER_LOAD_FROM_FIXED_SIZE_ROWS_TO_VAR, MYF(0));
    return true;
  }
  return false;
}
