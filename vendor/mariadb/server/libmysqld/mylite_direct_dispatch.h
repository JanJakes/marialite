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

typedef struct st_mylite_embedded_direct_session
  MYLITE_EMBEDDED_DIRECT_SESSION;

#ifdef __cplusplus
extern "C" {
#endif

int mylite_embedded_direct_server_init(int argc, char **argv, char **groups);
void mylite_embedded_direct_server_end(void);
int mylite_embedded_direct_session_open(
    MYLITE_EMBEDDED_DIRECT_SESSION **session);
void mylite_embedded_direct_session_close(
    MYLITE_EMBEDDED_DIRECT_SESSION *session);
MYSQL *mylite_embedded_direct_session_mysql(
    MYLITE_EMBEDDED_DIRECT_SESSION *session);
void mylite_embedded_direct_result_init(
    MYLITE_EMBEDDED_DIRECT_RESULT *result);
void mylite_embedded_direct_result_free(
    MYLITE_EMBEDDED_DIRECT_RESULT *result);
int mylite_embedded_direct_query(MYLITE_EMBEDDED_DIRECT_SESSION *session,
                                 const char *query,
                                 unsigned long length,
                                 MYLITE_EMBEDDED_DIRECT_RESULT *result);

#ifdef __cplusplus
}
#endif

#endif /* MYLITE_DIRECT_DISPATCH_INCLUDED */
