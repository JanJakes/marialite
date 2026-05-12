/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mariadb.h"
#include "sql_priv.h"
#include "spatial.h"
#include "mysqld_error.h"

Geometry::Class_info *Geometry::ci_collection[Geometry::wkb_last + 1];

Geometry *Geometry::construct(Geometry_buffer *, const char *, uint32)
{
  my_error(ER_CANT_CREATE_GEOMETRY_OBJECT, MYF(0));
  return nullptr;
}

uint Geometry::get_key_image_itMBR(LEX_CSTRING &, uchar *, uint)
{
  my_error(ER_CANT_CREATE_GEOMETRY_OBJECT, MYF(0));
  return 0;
}

int Geometry::as_wkt(String *, const char **end)
{
  if (end)
    *end= get_data_ptr();
  my_error(ER_CANT_CREATE_GEOMETRY_OBJECT, MYF(0));
  return 1;
}
