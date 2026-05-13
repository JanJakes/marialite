/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include "mariadb.h"
#include "my_md5.h"
#include "sql_string.h"
#include "sql_class.h"
#include "sql_lex.h"
#include "sp_pcontext.h"
#include "sql_digest.h"
#include "sql_get_diagnostics.h"

#include "yy_mariadb.hh"
#ifdef LEX_YYSTYPE
#undef LEX_YYSTYPE
#endif
#define LEX_YYSTYPE YYSTYPE*

void compute_digest_md5(const sql_digest_storage *, unsigned char *md5)
{
  compute_md5_hash(md5, "", 0);
}


void compute_digest_text(const sql_digest_storage *, String *digest_text)
{
  digest_text->length(0);
}


sql_digest_state *digest_add_token(sql_digest_state *state, uint, LEX_YYSTYPE)
{
  return state;
}


sql_digest_state *digest_reduce_token(sql_digest_state *state, uint, uint)
{
  return state;
}
