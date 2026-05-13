/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#ifndef MYLITE_DIRECT_DISPATCH_INCLUDED
#define MYLITE_DIRECT_DISPATCH_INCLUDED

#include <mysql.h>

typedef struct st_mylite_embedded_direct_result
{
  MYSQL_DATA *data;
  MYSQL_FIELD *fields;
  unsigned int field_count;
  my_ulonglong affected_rows;
  my_ulonglong insert_id;
  unsigned int server_status;
  unsigned int warning_count;
  unsigned int last_errno;
  char sqlstate[SQLSTATE_LENGTH + 1];
  char message[MYSQL_ERRMSG_SIZE];
} MYLITE_EMBEDDED_DIRECT_RESULT;

#ifdef __cplusplus
extern "C" {
#endif

void mylite_embedded_direct_result_init(
    MYLITE_EMBEDDED_DIRECT_RESULT *result);
void mylite_embedded_direct_result_free(
    MYLITE_EMBEDDED_DIRECT_RESULT *result);
int mylite_embedded_direct_query(MYSQL *mysql, const char *query,
                                 unsigned long length,
                                 MYLITE_EMBEDDED_DIRECT_RESULT *result);

#ifdef __cplusplus
}
#endif

#endif /* MYLITE_DIRECT_DISPATCH_INCLUDED */
