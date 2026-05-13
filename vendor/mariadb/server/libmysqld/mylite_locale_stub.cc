/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mariadb.h"
#include "sql_priv.h"
#include "unireg.h"
#include "sql_locale.h"

MY_LOCALE_ERRMSGS global_errmsgs[]=
{
  {"english", nullptr},
  {nullptr, nullptr}
};

static const char *my_locale_month_names_en_US[13]=
  { "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December", NullS };
static const char *my_locale_ab_month_names_en_US[13]=
  { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
    "Oct", "Nov", "Dec", NullS };
static const char *my_locale_day_names_en_US[8]=
  { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
    "Sunday", NullS };
static const char *my_locale_ab_day_names_en_US[8]=
  { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun", NullS };

static TYPELIB my_locale_typelib_month_names_en_US=
  CREATE_TYPELIB_FOR(my_locale_month_names_en_US);
static TYPELIB my_locale_typelib_ab_month_names_en_US=
  CREATE_TYPELIB_FOR(my_locale_ab_month_names_en_US);
static TYPELIB my_locale_typelib_day_names_en_US=
  CREATE_TYPELIB_FOR(my_locale_day_names_en_US);
static TYPELIB my_locale_typelib_ab_day_names_en_US=
  CREATE_TYPELIB_FOR(my_locale_ab_day_names_en_US);

MY_LOCALE my_locale_en_US
(
  0,
  "en_US"_Lex_ident_locale,
  "English - United States",
  TRUE,
  &my_locale_typelib_month_names_en_US,
  &my_locale_typelib_ab_month_names_en_US,
  &my_locale_typelib_day_names_en_US,
  &my_locale_typelib_ab_day_names_en_US,
  9,
  9,
  '.',
  ',',
  "\x03\x03",
  &global_errmsgs[0]
);

MY_LOCALE *my_locales[]=
{
  &my_locale_en_US,
  nullptr
};

MY_LOCALE *my_locale_by_number(uint number)
{
  return number == 0 ? &my_locale_en_US : nullptr;
}

MY_LOCALE *my_locale_by_name(const LEX_CSTRING &name)
{
  return my_locale_en_US.name.streq(name) ? &my_locale_en_US : nullptr;
}

void cleanup_errmsgs()
{
  for (MY_LOCALE_ERRMSGS *msgs= global_errmsgs; msgs->language; msgs++)
    my_free(msgs->errmsgs);
}
