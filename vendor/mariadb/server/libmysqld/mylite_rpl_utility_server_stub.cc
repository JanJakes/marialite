/* Copyright (c) 2026, MyLite contributors.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

#include "mariadb.h"
#include "field.h"

static uint32 mylite_rpl_display_length(const Conv_source &src);
static void mylite_show_binlog_type(const Type_handler *handler, String *str);


uint32
Type_handler_newdecimal::max_display_length_for_field(const Conv_source &src)
                                                      const
{
  return src.metadata();
}


uint32
Type_handler_typelib::max_display_length_for_field(const Conv_source &src)
                                                   const
{
  return mylite_rpl_display_length(src);
}


uint32
Type_handler_string::max_display_length_for_field(const Conv_source &src)
                                                  const
{
  return mylite_rpl_display_length(src);
}


uint32
Type_handler_time2::max_display_length_for_field(const Conv_source &src)
                                                 const
{
  return mylite_rpl_display_length(src);
}


uint32
Type_handler_timestamp2::max_display_length_for_field(const Conv_source &src)
                                                      const
{
  return mylite_rpl_display_length(src);
}


uint32
Type_handler_datetime2::max_display_length_for_field(const Conv_source &src)
                                                     const
{
  return mylite_rpl_display_length(src);
}


uint32
Type_handler_bit::max_display_length_for_field(const Conv_source &src)
                                               const
{
  return mylite_rpl_display_length(src);
}


uint32
Type_handler_var_string::max_display_length_for_field(const Conv_source &src)
                                                      const
{
  return mylite_rpl_display_length(src);
}


uint32
Type_handler_varchar::max_display_length_for_field(const Conv_source &src)
                                                   const
{
  return mylite_rpl_display_length(src);
}


uint32
Type_handler_varchar_compressed::
  max_display_length_for_field(const Conv_source &src) const
{
  return mylite_rpl_display_length(src);
}


uint32
Type_handler_tiny_blob::max_display_length_for_field(const Conv_source &src)
                                                     const
{
  return mylite_rpl_display_length(src);
}


uint32
Type_handler_medium_blob::max_display_length_for_field(const Conv_source &src)
                                                       const
{
  return mylite_rpl_display_length(src);
}


uint32
Type_handler_blob::max_display_length_for_field(const Conv_source &src)
                                                const
{
  return mylite_rpl_display_length(src);
}


uint32
Type_handler_blob_compressed::max_display_length_for_field(const Conv_source &src)
                                                    const
{
  return mylite_rpl_display_length(src);
}


uint32
Type_handler_long_blob::max_display_length_for_field(const Conv_source &src)
                                                     const
{
  return mylite_rpl_display_length(src);
}


uint32
Type_handler_olddecimal::max_display_length_for_field(const Conv_source &src)
                                                      const
{
  return mylite_rpl_display_length(src);
}


void Type_handler::show_binlog_type(const Conv_source &, const Field &,
                                    String *str) const
{
  mylite_show_binlog_type(this, str);
}


void Type_handler_var_string::show_binlog_type(const Conv_source &,
                                               const Field &,
                                               String *str) const
{
  mylite_show_binlog_type(this, str);
}


void Type_handler_varchar::show_binlog_type(const Conv_source &,
                                            const Field &,
                                            String *str) const
{
  mylite_show_binlog_type(this, str);
}


void Type_handler_varchar_compressed::show_binlog_type(const Conv_source &,
                                                       const Field &,
                                                       String *str) const
{
  mylite_show_binlog_type(this, str);
}


void Type_handler_bit::show_binlog_type(const Conv_source &, const Field &,
                                        String *str) const
{
  mylite_show_binlog_type(this, str);
}


void Type_handler_olddecimal::show_binlog_type(const Conv_source &,
                                               const Field &,
                                               String *str) const
{
  mylite_show_binlog_type(this, str);
}


void Type_handler_newdecimal::show_binlog_type(const Conv_source &,
                                               const Field &,
                                               String *str) const
{
  mylite_show_binlog_type(this, str);
}


void Type_handler_blob_compressed::show_binlog_type(const Conv_source &,
                                                    const Field &,
                                                    String *str) const
{
  mylite_show_binlog_type(this, str);
}


void Type_handler_string::show_binlog_type(const Conv_source &,
                                           const Field &,
                                           String *str) const
{
  mylite_show_binlog_type(this, str);
}


enum_conv_type
Field::rpl_conv_type_from_same_data_type(uint16,
                                         const Relay_log_info *,
                                         const Conv_param &) const
{
  return CONV_TYPE_IMPOSSIBLE;
}


#define MYLITE_RPL_CONV_IMPOSSIBLE(FIELD_CLASS)                         \
enum_conv_type                                                           \
FIELD_CLASS::rpl_conv_type_from(const Conv_source &,                     \
                                const Relay_log_info *,                  \
                                const Conv_param &) const                \
{                                                                        \
  return CONV_TYPE_IMPOSSIBLE;                                           \
}

MYLITE_RPL_CONV_IMPOSSIBLE(Field_new_decimal)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_real)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_int)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_enum)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_longstr)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_newdate)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_time)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_timef)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_timestamp)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_timestampf)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_datetime)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_datetimef)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_date)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_bit)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_year)
MYLITE_RPL_CONV_IMPOSSIBLE(Field_null)

#undef MYLITE_RPL_CONV_IMPOSSIBLE


static uint32
mylite_rpl_display_length(const Conv_source &src)
{
  return src.metadata();
}


static void
mylite_show_binlog_type(const Type_handler *handler, String *str)
{
  str->set_ascii(handler->name().ptr(), handler->name().length());
}
