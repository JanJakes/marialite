/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#ifndef MYLITE_H
#define MYLITE_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
# if defined(MYLITE_BUILD_SHARED)
#  define MYLITE_API __declspec(dllexport)
# elif defined(MYLITE_SHARED)
#  define MYLITE_API __declspec(dllimport)
# else
#  define MYLITE_API
# endif
#elif defined(__GNUC__)
# define MYLITE_API __attribute__((visibility("default")))
#else
# define MYLITE_API
#endif

typedef struct mylite_db mylite_db;

typedef int (*mylite_exec_callback)(
    void *ctx,
    int column_count,
    char **values,
    char **column_names);

typedef enum mylite_result {
  MYLITE_OK = 0,
  MYLITE_ERROR = 1,
  MYLITE_BUSY = 5,
  MYLITE_NOMEM = 7,
  MYLITE_READONLY = 8,
  MYLITE_IOERR = 10,
  MYLITE_CORRUPT = 11,
  MYLITE_NOTFOUND = 12,
  MYLITE_FULL = 13,
  MYLITE_CANTOPEN = 14,
  MYLITE_CONSTRAINT = 19,
  MYLITE_MISUSE = 21,
  MYLITE_ROW = 100,
  MYLITE_DONE = 101
} mylite_result;

#define MYLITE_OPEN_READONLY   0x00000001u
#define MYLITE_OPEN_READWRITE  0x00000002u
#define MYLITE_OPEN_CREATE     0x00000004u
#define MYLITE_OPEN_EXCLUSIVE  0x00000008u
#define MYLITE_OPEN_URI        0x00000010u

MYLITE_API int mylite_open(const char *filename, mylite_db **out_db);
MYLITE_API int mylite_open_v2(
    const char *filename,
    mylite_db **out_db,
    unsigned flags,
    const char *profile);
MYLITE_API int mylite_close(mylite_db *db);

MYLITE_API int mylite_exec(
    mylite_db *db,
    const char *sql,
    mylite_exec_callback callback,
    void *ctx,
    char **errmsg);
MYLITE_API void mylite_free(void *ptr);

MYLITE_API int mylite_errcode(mylite_db *db);
MYLITE_API int mylite_extended_errcode(mylite_db *db);
MYLITE_API unsigned mylite_mariadb_errno(mylite_db *db);
MYLITE_API const char *mylite_sqlstate(mylite_db *db);
MYLITE_API const char *mylite_errmsg(mylite_db *db);

#ifdef __cplusplus
}
#endif

#endif
