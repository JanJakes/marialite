/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mariadb.h"
#include "sql_class.h"
#include "sql_sequence.h"
#include "ha_sequence.h"
#include "sql_alter.h"
#include "mysqld_error.h"

handlerton *sql_sequence_hton;

static bool mylite_sql_sequence_unsupported()
{
  my_error(ER_NOT_SUPPORTED_YET, MYF(0), "SQL SEQUENCE");
  return true;
}

Type_handler const *sequence_definition::value_type_handler()
{
  return &type_handler_slonglong;
}

longlong sequence_definition::value_type_max()
{
  return LONGLONG_MAX - 1;
}

longlong sequence_definition::value_type_min()
{
  return LONGLONG_MIN + 1;
}

bool sequence_definition::check_and_adjust(THD *, bool)
{
  return mylite_sql_sequence_unsupported();
}

void sequence_definition::store_fields(TABLE *)
{
}

void sequence_definition::read_fields(TABLE *)
{
}

int sequence_definition::write_initial_sequence(TABLE *)
{
  return mylite_sql_sequence_unsupported();
}

int sequence_definition::write(TABLE *, bool)
{
  return mylite_sql_sequence_unsupported();
}

void sequence_definition::adjust_values(longlong)
{
}

longlong sequence_definition::truncate_value(const Longlong_hybrid& original)
{
  return original.value();
}

bool sequence_definition::is_allowed_value_type(enum_field_types)
{
  return false;
}

bool sequence_definition::prepare_sequence_fields(List<Create_field> *, bool)
{
  return mylite_sql_sequence_unsupported();
}

SEQUENCE::SEQUENCE()
  :all_values_used(true), initialized(SEQ_UNINTIALIZED)
{
}

SEQUENCE::~SEQUENCE()
{
}

longlong SEQUENCE::next_value(TABLE *, bool, int *error)
{
  if (error)
    *error= 1;
  mylite_sql_sequence_unsupported();
  return 0;
}

int SEQUENCE::set_value(TABLE *, longlong, ulonglong, bool)
{
  return mylite_sql_sequence_unsupported();
}

bool SEQUENCE_LAST_VALUE::check_version(TABLE *)
{
  return false;
}

void SEQUENCE_LAST_VALUE::set_version(TABLE *)
{
}

bool check_sequence_fields(LEX *, List<Create_field> *, const LEX_CSTRING,
                           const LEX_CSTRING)
{
  return mylite_sql_sequence_unsupported();
}

bool sequence_insert(THD *, LEX *, TABLE_LIST *)
{
  return mylite_sql_sequence_unsupported();
}

bool Sql_cmd_alter_sequence::execute(THD *)
{
  return mylite_sql_sequence_unsupported();
}
