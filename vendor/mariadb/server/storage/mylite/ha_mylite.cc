/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include <my_global.h>
#include <mysql/plugin.h>
#include "ha_mylite.h"
#include "table.h"

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

struct Mylite_table_definition
{
  std::string db;
  std::string table_name;
  std::string seed_sql;
  std::vector<uchar> frm_image;
};

static handler *mylite_create_handler(handlerton *hton, TABLE_SHARE *table,
                                      MEM_ROOT *mem_root);
static int mylite_discover_table(handlerton *hton, THD *thd,
                                 TABLE_SHARE *share);
static int mylite_discover_table_names(handlerton *hton,
                                       const LEX_CSTRING *db, MY_DIR *dir,
                                       handlerton::discovered_list *result);
static int mylite_discover_table_existence(handlerton *hton, const char *db,
                                           const char *table_name);
static bool mylite_read_table_definition(const char *db, size_t db_length,
                                         const char *table_name,
                                         size_t table_name_length,
                                         std::string *seed_sql,
                                         std::vector<uchar> *frm_image);
static int mylite_store_table_definition(const char *path,
                                         const TABLE_SHARE *share);
static int mylite_remove_table_definition(const char *path);
static int mylite_rename_table_definition(const char *from, const char *to);
static bool mylite_table_definition_exists(const char *db, size_t db_length,
                                           const char *table_name,
                                           size_t table_name_length);
static Mylite_table_definition *mylite_find_table_definition_locked(
    const char *db, size_t db_length, const char *table_name,
    size_t table_name_length);
static bool mylite_parse_table_path(const char *path, std::string *db,
                                    std::string *table_name);

static handlerton *mylite_hton;
static const char mylite_seed_db[]= "mylite";
static const char mylite_seed_table[]= "probe";
static const char mylite_seed_sql[]=
  "CREATE TABLE probe (id INT) ENGINE=MYLITE";
static std::mutex mylite_catalog_mutex;
static std::vector<Mylite_table_definition> mylite_catalog= {
  { mylite_seed_db, mylite_seed_table, mylite_seed_sql, std::vector<uchar>() }
};

static const char *ha_mylite_exts[]= {
  NullS
};

static int mylite_init_func(void *p)
{
  DBUG_ENTER("mylite_init_func");

  mylite_hton= static_cast<handlerton *>(p);
  mylite_hton->create= mylite_create_handler;
  mylite_hton->flags= HTON_NO_PARTITION | HTON_TEMPORARY_NOT_SUPPORTED;
  mylite_hton->tablefile_extensions= ha_mylite_exts;
  mylite_hton->discover_table= mylite_discover_table;
  mylite_hton->discover_table_names= mylite_discover_table_names;
  mylite_hton->discover_table_existence= mylite_discover_table_existence;

  DBUG_RETURN(0);
}

static handler *mylite_create_handler(handlerton *hton, TABLE_SHARE *table,
                                      MEM_ROOT *mem_root)
{
  return new (mem_root) ha_mylite(hton, table);
}

static int mylite_discover_table(handlerton *, THD *thd, TABLE_SHARE *share)
{
  std::string seed_sql;
  std::vector<uchar> frm_image;

  DBUG_ENTER("mylite_discover_table");

  if (!mylite_read_table_definition(share->db.str, share->db.length,
                                    share->table_name.str,
                                    share->table_name.length,
                                    &seed_sql, &frm_image))
    DBUG_RETURN(HA_ERR_NO_SUCH_TABLE);

  if (!frm_image.empty())
    DBUG_RETURN(share->init_from_binary_frm_image(thd, false,
                                                  frm_image.data(),
                                                  frm_image.size()));

  DBUG_RETURN(share->init_from_sql_statement_string(thd, false,
                                                    seed_sql.c_str(),
                                                    seed_sql.length()));
}

static int mylite_discover_table_names(handlerton *, const LEX_CSTRING *db,
                                       MY_DIR *,
                                       handlerton::discovered_list *result)
{
  DBUG_ENTER("mylite_discover_table_names");

  if (!db || !db->str)
    DBUG_RETURN(0);

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  for (const Mylite_table_definition &definition : mylite_catalog)
  {
    if (definition.db.length() == db->length &&
        std::strncmp(definition.db.c_str(), db->str, db->length) == 0 &&
        result->add_table(definition.table_name.c_str(),
                          definition.table_name.length()))
      DBUG_RETURN(1);
  }

  DBUG_RETURN(0);
}

static int mylite_discover_table_existence(handlerton *, const char *db,
                                           const char *table_name)
{
  DBUG_ENTER("mylite_discover_table_existence");
  if (!db || !table_name)
    DBUG_RETURN(0);
  DBUG_RETURN(mylite_table_definition_exists(db, strlen(db), table_name,
                                             strlen(table_name)));
}

static bool mylite_read_table_definition(const char *db, size_t db_length,
                                         const char *table_name,
                                         size_t table_name_length,
                                         std::string *seed_sql,
                                         std::vector<uchar> *frm_image)
{
  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  const Mylite_table_definition *definition=
    mylite_find_table_definition_locked(db, db_length, table_name,
                                        table_name_length);
  if (!definition)
    return false;

  *seed_sql= definition->seed_sql;
  *frm_image= definition->frm_image;
  return true;
}

static bool mylite_table_definition_exists(const char *db, size_t db_length,
                                           const char *table_name,
                                           size_t table_name_length)
{
  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  return mylite_find_table_definition_locked(db, db_length, table_name,
                                             table_name_length) != nullptr;
}

ha_mylite::ha_mylite(handlerton *hton, TABLE_SHARE *table_arg)
  :handler(hton, table_arg)
{}

int ha_mylite::open(const char *, int, uint)
{
  DBUG_ENTER("ha_mylite::open");

  if (!(share= get_share()))
    DBUG_RETURN(HA_ERR_OUT_OF_MEM);
  thr_lock_data_init(&share->lock, &lock, nullptr);

  DBUG_RETURN(0);
}

int ha_mylite::close()
{
  DBUG_ENTER("ha_mylite::close");
  share= nullptr;
  DBUG_RETURN(0);
}

int ha_mylite::create(const char *name, TABLE *table_arg, HA_CREATE_INFO *)
{
  DBUG_ENTER("ha_mylite::create");
  DBUG_RETURN(mylite_store_table_definition(name, table_arg->s));
}

int ha_mylite::delete_table(const char *name)
{
  DBUG_ENTER("ha_mylite::delete_table");
  DBUG_RETURN(mylite_remove_table_definition(name));
}

int ha_mylite::rnd_init(bool)
{
  DBUG_ENTER("ha_mylite::rnd_init");
  DBUG_RETURN(0);
}

int ha_mylite::rnd_next(uchar *)
{
  DBUG_ENTER("ha_mylite::rnd_next");
  DBUG_RETURN(HA_ERR_END_OF_FILE);
}

int ha_mylite::rnd_pos(uchar *, uchar *)
{
  DBUG_ENTER("ha_mylite::rnd_pos");
  DBUG_RETURN(HA_ERR_WRONG_COMMAND);
}

void ha_mylite::position(const uchar *)
{
  DBUG_ENTER("ha_mylite::position");
  DBUG_VOID_RETURN;
}

int ha_mylite::info(uint)
{
  DBUG_ENTER("ha_mylite::info");
  stats.records= 0;
  stats.deleted= 0;
  stats.data_file_length= 0;
  stats.index_file_length= 0;
  DBUG_RETURN(0);
}

int ha_mylite::external_lock(THD *, int)
{
  DBUG_ENTER("ha_mylite::external_lock");
  DBUG_RETURN(0);
}

THR_LOCK_DATA **ha_mylite::store_lock(THD *, THR_LOCK_DATA **to,
                                      enum thr_lock_type lock_type)
{
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
    lock.type= lock_type;
  *to++= &lock;
  return to;
}

int ha_mylite::rename_table(const char *from, const char *to)
{
  DBUG_ENTER("ha_mylite::rename_table");
  DBUG_RETURN(mylite_rename_table_definition(from, to));
}

Mylite_share *ha_mylite::get_share()
{
  Mylite_share *tmp_share;

  DBUG_ENTER("ha_mylite::get_share");

  lock_shared_ha_data();
  if (!(tmp_share= static_cast<Mylite_share *>(get_ha_share_ptr())))
  {
    tmp_share= new Mylite_share;
    if (!tmp_share)
      goto err;
    set_ha_share_ptr(static_cast<Handler_share *>(tmp_share));
  }

err:
  unlock_shared_ha_data();
  DBUG_RETURN(tmp_share);
}

static int mylite_store_table_definition(const char *path,
                                         const TABLE_SHARE *share)
{
  if (!share || !share->frm_image || !share->frm_image->str ||
      !share->frm_image->length)
    return HA_ERR_WRONG_COMMAND;

  std::string db;
  std::string table_name;
  if (!mylite_parse_table_path(path, &db, &table_name))
  {
    db.assign(share->db.str, share->db.length);
    table_name.assign(share->table_name.str, share->table_name.length);
  }

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (mylite_find_table_definition_locked(db.c_str(), db.length(),
                                          table_name.c_str(),
                                          table_name.length()))
    return HA_ERR_TABLE_EXIST;

  Mylite_table_definition definition;
  definition.db= db;
  definition.table_name= table_name;
  definition.frm_image.assign(share->frm_image->str,
                              share->frm_image->str +
                                share->frm_image->length);
  mylite_catalog.push_back(definition);
  return 0;
}

static int mylite_remove_table_definition(const char *path)
{
  std::string db;
  std::string table_name;
  if (!mylite_parse_table_path(path, &db, &table_name))
    return HA_ERR_NO_SUCH_TABLE;

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  for (std::vector<Mylite_table_definition>::iterator it=
         mylite_catalog.begin(); it != mylite_catalog.end(); ++it)
  {
    if (it->db == db && it->table_name == table_name)
    {
      mylite_catalog.erase(it);
      return 0;
    }
  }

  return HA_ERR_NO_SUCH_TABLE;
}

static int mylite_rename_table_definition(const char *from, const char *to)
{
  std::string from_db;
  std::string from_table;
  std::string to_db;
  std::string to_table;

  if (!mylite_parse_table_path(from, &from_db, &from_table) ||
      !mylite_parse_table_path(to, &to_db, &to_table))
    return HA_ERR_NO_SUCH_TABLE;

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (mylite_find_table_definition_locked(to_db.c_str(), to_db.length(),
                                          to_table.c_str(), to_table.length()))
    return HA_ERR_TABLE_EXIST;

  Mylite_table_definition *definition=
    mylite_find_table_definition_locked(from_db.c_str(), from_db.length(),
                                        from_table.c_str(),
                                        from_table.length());
  if (!definition)
    return HA_ERR_NO_SUCH_TABLE;

  definition->db= to_db;
  definition->table_name= to_table;
  return 0;
}

static Mylite_table_definition *mylite_find_table_definition_locked(
    const char *db, size_t db_length, const char *table_name,
    size_t table_name_length)
{
  if (!db || !table_name)
    return nullptr;

  for (Mylite_table_definition &definition : mylite_catalog)
  {
    if (definition.db.length() == db_length &&
        definition.table_name.length() == table_name_length &&
        std::strncmp(definition.db.c_str(), db, db_length) == 0 &&
        std::strncmp(definition.table_name.c_str(), table_name,
                     table_name_length) == 0)
      return &definition;
  }

  return nullptr;
}

static bool mylite_parse_table_path(const char *path, std::string *db,
                                    std::string *table_name)
{
  if (!path || !path[0])
    return false;

  std::string value(path);
  while (!value.empty() && (value.back() == '/' || value.back() == '\\'))
    value.resize(value.length() - 1);

  const std::string separators= "/\\";
  const std::string::size_type table_pos= value.find_last_of(separators);
  if (table_pos == std::string::npos || table_pos + 1 >= value.length())
    return false;

  const std::string::size_type db_end= table_pos;
  const std::string::size_type db_pos= db_end == 0
    ? std::string::npos
    : value.find_last_of(separators, db_end - 1);

  *table_name= value.substr(table_pos + 1);
  *db= db_pos == std::string::npos
    ? value.substr(0, db_end)
    : value.substr(db_pos + 1, db_end - db_pos - 1);

  return !db->empty() && !table_name->empty();
}

struct st_mysql_storage_engine mylite_storage_engine=
{ MYSQL_HANDLERTON_INTERFACE_VERSION };

maria_declare_plugin(mylite)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,
  &mylite_storage_engine,
  "MYLITE",
  "MyLite contributors",
  "MyLite storage engine skeleton",
  PLUGIN_LICENSE_GPL,
  mylite_init_func,
  nullptr,
  0x0001,
  nullptr,
  nullptr,
  "0.1",
  MariaDB_PLUGIN_MATURITY_EXPERIMENTAL
}
maria_declare_plugin_end;
