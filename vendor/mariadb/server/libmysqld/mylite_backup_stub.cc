/* Copyright (c) 2026 MyLite contributors.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335 USA */

#include "mariadb.h"
#include "sql_class.h"
#include "backup.h"

static const char *stage_names[]=
{"START", "FLUSH", "BLOCK_DDL", "BLOCK_COMMIT", "END", 0};

TYPELIB backup_stage_names= CREATE_TYPELIB_FOR(stage_names);

void
backup_init()
{
}


bool
run_backup_stage(THD *thd, backup_stages stage)
{
  (void) thd;
  (void) stage;
  my_error(ER_OPTION_PREVENTS_STATEMENT, MYF(0), "embedded");
  return true;
}


bool
backup_end(THD *thd)
{
  thd->current_backup_stage= BACKUP_FINISHED;
  return false;
}


void
backup_set_alter_copy_lock(THD *thd, TABLE *table)
{
  (void) thd;
  (void) table;
}


bool
backup_reset_alter_copy_lock(THD *thd)
{
  (void) thd;
  return false;
}


bool
backup_lock(THD *thd, TABLE_LIST *table)
{
  (void) thd;
  (void) table;
  my_error(ER_OPTION_PREVENTS_STATEMENT, MYF(0), "embedded");
  return true;
}


void
backup_unlock(THD *thd)
{
  if (thd->mdl_backup_lock)
    thd->mdl_context.release_lock(thd->mdl_backup_lock);
  thd->mdl_backup_lock= nullptr;
}


void
backup_log_ddl(const backup_log_info *info)
{
  (void) info;
}
