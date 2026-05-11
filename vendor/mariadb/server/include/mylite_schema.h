/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#ifndef MYLITE_SCHEMA_INCLUDED
#define MYLITE_SCHEMA_INCLUDED

#include <stddef.h>

typedef bool (*mylite_name_callback)(const char *name, size_t length,
                                     void *ctx);

bool mylite_schema_namespace_active();
bool mylite_schema_exists(const char *name, size_t length);
int mylite_create_schema(const char *name, size_t length);
int mylite_drop_schema(const char *name, size_t length,
                       unsigned long *dropped_tables);
int mylite_for_each_schema(mylite_name_callback callback, void *ctx);
int mylite_for_each_schema_table(const char *schema_name,
                                 size_t schema_length,
                                 mylite_name_callback callback, void *ctx);

#endif
