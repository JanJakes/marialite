/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include <my_global.h>
#include <mysql/plugin.h>
#include "ha_mylite.h"
#include "sql_class.h"
#include "table.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

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
static bool mylite_ensure_catalog_loaded_locked();
static void mylite_clear_frm_definitions_locked();
static bool mylite_load_catalog_locked();
static int mylite_flush_catalog_locked();
static bool mylite_write_catalog_locked();
static std::string mylite_serialize_catalog_locked();
static std::string mylite_hex_encode(const uchar *data, size_t length);
static bool mylite_hex_decode(const std::string &hex,
                              std::vector<uchar> *result);
static int mylite_hex_value(char c);
static bool mylite_write_all(int fd, const std::string &content);
static void mylite_log_catalog_error(const char *operation,
                                     const std::string &path);
static int mylite_deinit_func(void *p);

static handlerton *mylite_hton;
static char *mylite_catalog_file;
static const char mylite_seed_db[]= "mylite";
static const char mylite_seed_table[]= "probe";
static const char mylite_seed_sql[]=
  "CREATE TABLE probe (id INT) ENGINE=MYLITE";
static const char mylite_catalog_magic[]= "MYLITE CATALOG 1";
static std::mutex mylite_catalog_mutex;
static bool mylite_catalog_loaded= false;
static bool mylite_catalog_load_failed= false;
static std::vector<Mylite_table_definition> mylite_catalog= {
  { mylite_seed_db, mylite_seed_table, mylite_seed_sql, std::vector<uchar>() }
};

static MYSQL_SYSVAR_STR(
  catalog_file,
  mylite_catalog_file,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "Path to the primary MyLite catalog file.",
  nullptr,
  nullptr,
  nullptr);

static struct st_mysql_sys_var *mylite_system_variables[]= {
  MYSQL_SYSVAR(catalog_file),
  nullptr
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
  if (!mylite_ensure_catalog_loaded_locked())
    DBUG_RETURN(1);

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
  if (!mylite_ensure_catalog_loaded_locked())
    return false;

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
  if (!mylite_ensure_catalog_loaded_locked())
    return false;

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
  if (!mylite_ensure_catalog_loaded_locked())
    return HA_ERR_CRASHED;

  if (mylite_find_table_definition_locked(db.c_str(), db.length(),
                                          table_name.c_str(),
                                          table_name.length()))
    return HA_ERR_TABLE_EXIST;

  const std::vector<Mylite_table_definition> before= mylite_catalog;
  Mylite_table_definition definition;
  definition.db= db;
  definition.table_name= table_name;
  definition.frm_image.assign(share->frm_image->str,
                              share->frm_image->str +
                                share->frm_image->length);
  mylite_catalog.push_back(definition);
  const int error= mylite_flush_catalog_locked();
  if (error)
    mylite_catalog= before;
  return error;
}

static int mylite_remove_table_definition(const char *path)
{
  std::string db;
  std::string table_name;
  if (!mylite_parse_table_path(path, &db, &table_name))
    return HA_ERR_NO_SUCH_TABLE;

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return HA_ERR_CRASHED;

  for (std::vector<Mylite_table_definition>::iterator it=
         mylite_catalog.begin(); it != mylite_catalog.end(); ++it)
  {
    if (it->db == db && it->table_name == table_name)
    {
      const std::vector<Mylite_table_definition> before= mylite_catalog;
      mylite_catalog.erase(it);
      const int error= mylite_flush_catalog_locked();
      if (error)
        mylite_catalog= before;
      return error;
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
  if (!mylite_ensure_catalog_loaded_locked())
    return HA_ERR_CRASHED;

  if (mylite_find_table_definition_locked(to_db.c_str(), to_db.length(),
                                          to_table.c_str(), to_table.length()))
    return HA_ERR_TABLE_EXIST;

  Mylite_table_definition *definition=
    mylite_find_table_definition_locked(from_db.c_str(), from_db.length(),
                                        from_table.c_str(),
                                        from_table.length());
  if (!definition)
    return HA_ERR_NO_SUCH_TABLE;

  const std::vector<Mylite_table_definition> before= mylite_catalog;
  definition->db= to_db;
  definition->table_name= to_table;
  const int error= mylite_flush_catalog_locked();
  if (error)
    mylite_catalog= before;
  return error;
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

static bool mylite_ensure_catalog_loaded_locked()
{
  if (mylite_catalog_loaded)
    return !mylite_catalog_load_failed;

  mylite_catalog_loaded= true;
  if (!mylite_catalog_file || !mylite_catalog_file[0])
    return true;

  mylite_catalog_load_failed= !mylite_load_catalog_locked();
  return !mylite_catalog_load_failed;
}

static void mylite_clear_frm_definitions_locked()
{
  for (std::vector<Mylite_table_definition>::iterator it=
         mylite_catalog.begin(); it != mylite_catalog.end();)
  {
    if (it->seed_sql.empty())
      it= mylite_catalog.erase(it);
    else
      ++it;
  }
}

static bool mylite_load_catalog_locked()
{
  mylite_clear_frm_definitions_locked();

  struct stat st;
  if (stat(mylite_catalog_file, &st) != 0)
  {
    if (errno == ENOENT)
      return true;
    mylite_log_catalog_error("stat", mylite_catalog_file);
    return false;
  }
  if (st.st_size == 0)
    return true;

  std::ifstream input(mylite_catalog_file, std::ios::binary);
  if (!input)
  {
    mylite_log_catalog_error("open", mylite_catalog_file);
    return false;
  }

  std::string line;
  if (!std::getline(input, line) || line != mylite_catalog_magic)
  {
    sql_print_error("MyLite: invalid catalog header in %s",
                    mylite_catalog_file);
    return false;
  }

  while (std::getline(input, line))
  {
    if (line.empty())
      continue;

    const std::string::size_type first= line.find('\t');
    const std::string::size_type second= first == std::string::npos
      ? std::string::npos
      : line.find('\t', first + 1);
    const std::string::size_type third= second == std::string::npos
      ? std::string::npos
      : line.find('\t', second + 1);

    if (first == std::string::npos || second == std::string::npos ||
        third == std::string::npos || line.substr(0, first) != "TABLE")
    {
      sql_print_error("MyLite: invalid catalog record in %s",
                      mylite_catalog_file);
      return false;
    }

    std::vector<uchar> db_bytes;
    std::vector<uchar> table_bytes;
    Mylite_table_definition definition;
    if (!mylite_hex_decode(line.substr(first + 1, second - first - 1),
                           &db_bytes) ||
        !mylite_hex_decode(line.substr(second + 1, third - second - 1),
                           &table_bytes) ||
        !mylite_hex_decode(line.substr(third + 1), &definition.frm_image) ||
        db_bytes.empty() || table_bytes.empty() ||
        definition.frm_image.empty())
    {
      sql_print_error("MyLite: invalid catalog encoding in %s",
                      mylite_catalog_file);
      return false;
    }

    definition.db.assign(reinterpret_cast<const char *>(db_bytes.data()),
                         db_bytes.size());
    definition.table_name.assign(
      reinterpret_cast<const char *>(table_bytes.data()), table_bytes.size());
    if (mylite_find_table_definition_locked(definition.db.c_str(),
                                            definition.db.length(),
                                            definition.table_name.c_str(),
                                            definition.table_name.length()))
    {
      sql_print_error("MyLite: duplicate catalog table in %s",
                      mylite_catalog_file);
      return false;
    }

    mylite_catalog.push_back(definition);
  }

  return true;
}

static int mylite_flush_catalog_locked()
{
  if (!mylite_catalog_file || !mylite_catalog_file[0])
    return 0;

  return mylite_write_catalog_locked() ? 0 : HA_ERR_CRASHED;
}

static bool mylite_write_catalog_locked()
{
  const std::string catalog_path(mylite_catalog_file);
  const std::string tmp_path= catalog_path + ".tmp";
  const std::string content= mylite_serialize_catalog_locked();

  const int fd= open(tmp_path.c_str(),
                     O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  if (fd < 0)
  {
    mylite_log_catalog_error("open", tmp_path);
    return false;
  }

  bool ok= mylite_write_all(fd, content);
  if (ok && fsync(fd) != 0)
  {
    mylite_log_catalog_error("fsync", tmp_path);
    ok= false;
  }
  if (close(fd) != 0)
  {
    mylite_log_catalog_error("close", tmp_path);
    ok= false;
  }

  if (!ok)
  {
    unlink(tmp_path.c_str());
    return false;
  }

  if (rename(tmp_path.c_str(), catalog_path.c_str()) != 0)
  {
    mylite_log_catalog_error("rename", catalog_path);
    unlink(tmp_path.c_str());
    return false;
  }

  return true;
}

static std::string mylite_serialize_catalog_locked()
{
  std::string content;
  content.append(mylite_catalog_magic);
  content.push_back('\n');

  for (const Mylite_table_definition &definition : mylite_catalog)
  {
    if (definition.frm_image.empty())
      continue;

    content.append("TABLE\t");
    content.append(mylite_hex_encode(
      reinterpret_cast<const uchar *>(definition.db.data()),
      definition.db.length()));
    content.push_back('\t');
    content.append(mylite_hex_encode(
      reinterpret_cast<const uchar *>(definition.table_name.data()),
      definition.table_name.length()));
    content.push_back('\t');
    content.append(mylite_hex_encode(definition.frm_image.data(),
                                     definition.frm_image.size()));
    content.push_back('\n');
  }

  return content;
}

static std::string mylite_hex_encode(const uchar *data, size_t length)
{
  static const char digits[]= "0123456789abcdef";
  std::string result;
  result.reserve(length * 2);
  for (size_t i= 0; i < length; ++i)
  {
    result.push_back(digits[data[i] >> 4]);
    result.push_back(digits[data[i] & 0x0f]);
  }
  return result;
}

static bool mylite_hex_decode(const std::string &hex,
                              std::vector<uchar> *result)
{
  if (hex.length() % 2 != 0)
    return false;

  result->clear();
  result->reserve(hex.length() / 2);
  for (size_t i= 0; i < hex.length(); i += 2)
  {
    const int high= mylite_hex_value(hex[i]);
    const int low= mylite_hex_value(hex[i + 1]);
    if (high < 0 || low < 0)
      return false;
    result->push_back(static_cast<uchar>((high << 4) | low));
  }
  return true;
}

static int mylite_hex_value(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static bool mylite_write_all(int fd, const std::string &content)
{
  const char *ptr= content.data();
  size_t remaining= content.length();
  while (remaining > 0)
  {
    const ssize_t written= write(fd, ptr, remaining);
    if (written < 0)
    {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (written == 0)
      return false;
    ptr+= written;
    remaining-= static_cast<size_t>(written);
  }
  return true;
}

static void mylite_log_catalog_error(const char *operation,
                                     const std::string &path)
{
  sql_print_error("MyLite: catalog %s failed for %s: %s", operation,
                  path.c_str(), strerror(errno));
}

static int mylite_deinit_func(void *)
{
  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  mylite_clear_frm_definitions_locked();
  mylite_catalog_loaded= false;
  mylite_catalog_load_failed= false;
  return 0;
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
  mylite_deinit_func,
  0x0001,
  nullptr,
  mylite_system_variables,
  "0.1",
  MariaDB_PLUGIN_MATURITY_EXPERIMENTAL
}
maria_declare_plugin_end;
