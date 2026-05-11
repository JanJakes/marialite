/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include <my_global.h>
#include <mysql/plugin.h>
#include "ha_mylite.h"
#include "key.h"
#include "sql_class.h"
#include "table.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

struct Mylite_row
{
  uint64_t rowid;
  bool deleted;
  std::vector<uchar> record;
};

struct Mylite_table_definition
{
  std::string db;
  std::string table_name;
  std::string seed_sql;
  std::vector<uchar> frm_image;
  uint64_t next_rowid;
  uint64_t auto_increment_next;
  std::vector<Mylite_row> rows;
};

struct Mylite_index_entry
{
  uint64_t rowid;
  std::vector<uchar> key;
};

struct Mylite_catalog_header
{
  size_t slot;
  uint64_t generation;
  uint64_t payload_offset;
  uint64_t payload_length;
  uint64_t payload_checksum;
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
static int mylite_store_table_definition(const char *path, const TABLE *table,
                                         const HA_CREATE_INFO *create_info);
static int mylite_remove_table_definition(const char *path);
static int mylite_rename_table_definition(const char *from, const char *to);
static int mylite_store_row(const char *db, size_t db_length,
                            const char *table_name,
                            size_t table_name_length, TABLE *table,
                            const uchar *record, uint *duplicate_key);
static int mylite_update_row(const char *db, size_t db_length,
                             const char *table_name, size_t table_name_length,
                             TABLE *table, uint64_t rowid,
                             const uchar *record, uint *duplicate_key);
static int mylite_delete_row(const char *db, size_t db_length,
                             const char *table_name,
                             size_t table_name_length, uint64_t rowid);
static int mylite_read_row(const char *db, size_t db_length,
                           const char *table_name, size_t table_name_length,
                           const TABLE *table, size_t *scan_index,
                           uint64_t *rowid, uchar *record);
static int mylite_read_row_by_id(const char *db, size_t db_length,
                                 const char *table_name,
                                 size_t table_name_length, const TABLE *table,
                                 uint64_t rowid, uchar *record);
static int mylite_build_index_entries(
    const char *db, size_t db_length, const char *table_name,
    size_t table_name_length, TABLE *table, uint key_index,
    std::vector<Mylite_index_entry> *entries);
static ha_rows mylite_count_index_range(
    const char *db, size_t db_length, const char *table_name,
    size_t table_name_length, TABLE *table, uint key_index,
    const key_range *min_key, const key_range *max_key);
static size_t mylite_count_rows(const char *db, size_t db_length,
                                const char *table_name,
                                size_t table_name_length);
static bool mylite_reserve_auto_increment(
    const char *db, size_t db_length, const char *table_name,
    size_t table_name_length, const TABLE *table, ulonglong offset,
    ulonglong increment, ulonglong nb_desired_values, ulonglong *first_value,
    ulonglong *nb_reserved_values);
static int mylite_reset_auto_increment_value(const char *db, size_t db_length,
                                             const char *table_name,
                                             size_t table_name_length,
                                             ulonglong value);
static bool mylite_read_auto_increment_value(const char *db, size_t db_length,
                                             const char *table_name,
                                             size_t table_name_length,
                                             ulonglong *value);
static bool mylite_table_supports_row_storage(const TABLE *table);
static bool mylite_table_supports_key_storage(const TABLE *table);
static bool mylite_key_supports_storage(const KEY &key);
static bool mylite_key_part_supports_storage(const KEY_PART_INFO &key_part);
static bool mylite_table_uses_autoincrement(const TABLE *table);
static uint64_t mylite_initial_auto_increment_next(
    const TABLE *table, const HA_CREATE_INFO *create_info);
static int mylite_check_unique_constraints_locked(
    Mylite_table_definition *definition, TABLE *table, const uchar *record,
    uint64_t ignored_rowid, uint *duplicate_key);
static bool mylite_make_key_image(TABLE *table, uint key_index,
                                  const uchar *record,
                                  std::vector<uchar> *key_image);
static int mylite_compare_index_entry(const Mylite_index_entry &entry,
                                      KEY *key_info, const uchar *key,
                                      uint key_length);
static bool mylite_find_index_position(
    const std::vector<Mylite_index_entry> &entries, KEY *key_info,
    const uchar *key, uint key_length, enum ha_rkey_function find_flag,
    size_t *position);
static bool mylite_index_entry_in_range(const Mylite_index_entry &entry,
                                        KEY *key_info,
                                        const key_range *min_key,
                                        const key_range *max_key);
static bool mylite_advance_auto_increment_locked(
    Mylite_table_definition *definition, const TABLE *table,
    const uchar *record);
static bool mylite_next_auto_increment_value(uint64_t value, uint64_t offset,
                                             uint64_t increment,
                                             uint64_t *next_value);
static Mylite_row *mylite_find_row_locked(Mylite_table_definition *definition,
                                          uint64_t rowid);
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
static bool mylite_parse_catalog_payload_locked(const std::string &content);
static bool mylite_parse_table_payload_record_locked(
    const std::string &line, std::vector<Mylite_table_definition> *loaded);
static bool mylite_parse_next_rowid_payload_record_locked(
    const std::string &line, std::vector<Mylite_table_definition> *loaded);
static bool mylite_parse_autoincrement_payload_record_locked(
    const std::string &line, std::vector<Mylite_table_definition> *loaded);
static bool mylite_parse_row_payload_record_locked(
    const std::string &line, std::vector<Mylite_table_definition> *loaded);
static bool mylite_catalog_contains_definition(
    const std::vector<Mylite_table_definition> &catalog,
    const std::string &db, const std::string &table_name);
static Mylite_table_definition *mylite_find_table_definition_in_catalog(
    std::vector<Mylite_table_definition> *catalog, const std::string &db,
    const std::string &table_name);
static bool mylite_parse_decimal_uint64(const std::string &value,
                                        uint64_t *result);
static std::string mylite_format_decimal_uint64(uint64_t value);
static bool mylite_find_latest_catalog_header(int fd, off_t file_size,
                                              Mylite_catalog_header *latest);
static bool mylite_read_catalog_header(int fd, size_t slot, off_t file_size,
                                       Mylite_catalog_header *header);
static bool mylite_read_catalog_payload(int fd,
                                        const Mylite_catalog_header &header,
                                        std::string *content);
static bool mylite_write_catalog_payload(int fd, const std::string &content,
                                         uint64_t *payload_offset,
                                         uint64_t *payload_checksum);
static bool mylite_write_catalog_header(int fd,
                                        const Mylite_catalog_header &header);
static std::string mylite_hex_encode(const uchar *data, size_t length);
static bool mylite_hex_decode(const std::string &hex,
                              std::vector<uchar> *result);
static int mylite_hex_value(char c);
static bool mylite_read_all_at(int fd, uchar *data, size_t length,
                               uint64_t offset);
static bool mylite_write_all_at(int fd, const uchar *data, size_t length,
                                uint64_t offset);
static void mylite_store_le32(uchar *to, uint32_t value);
static void mylite_store_le64(uchar *to, uint64_t value);
static uint32_t mylite_read_le32(const uchar *from);
static uint64_t mylite_read_le64(const uchar *from);
static uint64_t mylite_checksum(const uchar *data, size_t length);
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
static const uchar mylite_catalog_file_magic[16]= {
  'M', 'Y', 'L', 'I', 'T', 'E', 'F', 'M',
  'T', 'C', 'A', 'T', '1', '\0', '\0', '\0'
};
static const uint32_t mylite_catalog_format_version= 1;
static const uint32_t mylite_catalog_page_size= 4096;
static const uint64_t mylite_catalog_payload_start=
  static_cast<uint64_t>(mylite_catalog_page_size) * 2;
static const size_t mylite_catalog_header_checksum_offset= 56;
static const uint64_t mylite_fnv1a_offset_basis= 14695981039346656037ULL;
static const uint64_t mylite_fnv1a_prime= 1099511628211ULL;
static std::mutex mylite_catalog_mutex;
static bool mylite_catalog_loaded= false;
static bool mylite_catalog_load_failed= false;
static std::vector<Mylite_table_definition> mylite_catalog= {
  { mylite_seed_db, mylite_seed_table, mylite_seed_sql, std::vector<uchar>(),
    1, 0, std::vector<Mylite_row>() }
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
{
  ref_length= sizeof(uint64_t);
}

int ha_mylite::open(const char *name, int, uint)
{
  DBUG_ENTER("ha_mylite::open");

  if (!mylite_parse_table_path(name, &db_name, &opened_table_name))
  {
    db_name.assign(table_share->db.str, table_share->db.length);
    opened_table_name.assign(table_share->table_name.str,
                             table_share->table_name.length);
  }

  if (!(share= get_share()))
    DBUG_RETURN(HA_ERR_OUT_OF_MEM);
  thr_lock_data_init(&share->lock, &lock, nullptr);

  DBUG_RETURN(0);
}

int ha_mylite::close()
{
  DBUG_ENTER("ha_mylite::close");
  share= nullptr;
  index_cursor_rowids.clear();
  DBUG_RETURN(0);
}

int ha_mylite::create(const char *name, TABLE *table_arg,
                      HA_CREATE_INFO *create_info)
{
  DBUG_ENTER("ha_mylite::create");
  if (!mylite_table_supports_row_storage(table_arg) ||
      !mylite_table_supports_key_storage(table_arg))
    DBUG_RETURN(HA_ERR_UNSUPPORTED);
  DBUG_RETURN(mylite_store_table_definition(name, table_arg, create_info));
}

int ha_mylite::delete_table(const char *name)
{
  DBUG_ENTER("ha_mylite::delete_table");
  DBUG_RETURN(mylite_remove_table_definition(name));
}

int ha_mylite::write_row(const uchar *buf)
{
  DBUG_ENTER("ha_mylite::write_row");
  lookup_errkey= static_cast<uint>(-1);
  if (table->next_number_field && buf == table->record[0])
  {
    const int error= update_auto_increment();
    if (error)
      DBUG_RETURN(error);
  }

  uint duplicate_key= static_cast<uint>(-1);
  const int error= mylite_store_row(db_name.c_str(), db_name.length(),
                                    opened_table_name.c_str(),
                                    opened_table_name.length(), table, buf,
                                    &duplicate_key);
  if (error == HA_ERR_FOUND_DUPP_KEY)
  {
    errkey= duplicate_key;
    lookup_errkey= duplicate_key;
  }
  index_cursor_rowids.clear();
  DBUG_RETURN(error);
}

int ha_mylite::update_row(const uchar *, const uchar *new_data)
{
  DBUG_ENTER("ha_mylite::update_row");
  lookup_errkey= static_cast<uint>(-1);
  uint duplicate_key= static_cast<uint>(-1);
  const int error= mylite_update_row(db_name.c_str(), db_name.length(),
                                     opened_table_name.c_str(),
                                     opened_table_name.length(), table,
                                     current_rowid, new_data,
                                     &duplicate_key);
  if (error == HA_ERR_FOUND_DUPP_KEY)
  {
    errkey= duplicate_key;
    lookup_errkey= duplicate_key;
  }
  index_cursor_rowids.clear();
  DBUG_RETURN(error);
}

int ha_mylite::delete_row(const uchar *)
{
  DBUG_ENTER("ha_mylite::delete_row");
  const int error= mylite_delete_row(db_name.c_str(), db_name.length(),
                                     opened_table_name.c_str(),
                                     opened_table_name.length(),
                                     current_rowid);
  index_cursor_rowids.clear();
  DBUG_RETURN(error);
}

int ha_mylite::index_read_map(uchar *buf, const uchar *key,
                              key_part_map keypart_map,
                              enum ha_rkey_function find_flag)
{
  DBUG_ENTER("ha_mylite::index_read_map");

  std::vector<Mylite_index_entry> entries;
  int error= mylite_build_index_entries(
    db_name.c_str(), db_name.length(), opened_table_name.c_str(),
    opened_table_name.length(), table, active_index, &entries);
  if (error)
  {
    index_cursor_rowids.clear();
    DBUG_RETURN(error);
  }

  const uint key_length= key
    ? calculate_key_len(table, active_index, key, keypart_map)
    : 0;
  size_t position= 0;
  if (!mylite_find_index_position(entries, table->key_info + active_index,
                                  key, key_length, find_flag, &position))
  {
    index_cursor_rowids.clear();
    DBUG_RETURN(HA_ERR_KEY_NOT_FOUND);
  }

  index_cursor_rowids.clear();
  index_cursor_rowids.reserve(entries.size());
  for (const Mylite_index_entry &entry : entries)
    index_cursor_rowids.push_back(entry.rowid);
  index_cursor_position= position;
  current_rowid= index_cursor_rowids[index_cursor_position];
  DBUG_RETURN(mylite_read_row_by_id(db_name.c_str(), db_name.length(),
                                    opened_table_name.c_str(),
                                    opened_table_name.length(), table,
                                    current_rowid, buf));
}

int ha_mylite::index_next(uchar *buf)
{
  DBUG_ENTER("ha_mylite::index_next");
  if (index_cursor_position + 1 >= index_cursor_rowids.size())
    DBUG_RETURN(HA_ERR_END_OF_FILE);

  current_rowid= index_cursor_rowids[++index_cursor_position];
  DBUG_RETURN(mylite_read_row_by_id(db_name.c_str(), db_name.length(),
                                    opened_table_name.c_str(),
                                    opened_table_name.length(), table,
                                    current_rowid, buf));
}

int ha_mylite::index_prev(uchar *buf)
{
  DBUG_ENTER("ha_mylite::index_prev");
  if (index_cursor_rowids.empty() || index_cursor_position == 0)
    DBUG_RETURN(HA_ERR_END_OF_FILE);

  current_rowid= index_cursor_rowids[--index_cursor_position];
  DBUG_RETURN(mylite_read_row_by_id(db_name.c_str(), db_name.length(),
                                    opened_table_name.c_str(),
                                    opened_table_name.length(), table,
                                    current_rowid, buf));
}

int ha_mylite::index_first(uchar *buf)
{
  DBUG_ENTER("ha_mylite::index_first");

  std::vector<Mylite_index_entry> entries;
  int error= mylite_build_index_entries(
    db_name.c_str(), db_name.length(), opened_table_name.c_str(),
    opened_table_name.length(), table, active_index, &entries);
  if (error)
  {
    index_cursor_rowids.clear();
    DBUG_RETURN(error);
  }
  if (entries.empty())
  {
    index_cursor_rowids.clear();
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  index_cursor_rowids.clear();
  index_cursor_rowids.reserve(entries.size());
  for (const Mylite_index_entry &entry : entries)
    index_cursor_rowids.push_back(entry.rowid);
  index_cursor_position= 0;
  current_rowid= index_cursor_rowids[index_cursor_position];
  DBUG_RETURN(mylite_read_row_by_id(db_name.c_str(), db_name.length(),
                                    opened_table_name.c_str(),
                                    opened_table_name.length(), table,
                                    current_rowid, buf));
}

int ha_mylite::index_last(uchar *buf)
{
  DBUG_ENTER("ha_mylite::index_last");

  std::vector<Mylite_index_entry> entries;
  int error= mylite_build_index_entries(
    db_name.c_str(), db_name.length(), opened_table_name.c_str(),
    opened_table_name.length(), table, active_index, &entries);
  if (error)
  {
    index_cursor_rowids.clear();
    DBUG_RETURN(error);
  }
  if (entries.empty())
  {
    index_cursor_rowids.clear();
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  index_cursor_rowids.clear();
  index_cursor_rowids.reserve(entries.size());
  for (const Mylite_index_entry &entry : entries)
    index_cursor_rowids.push_back(entry.rowid);
  index_cursor_position= index_cursor_rowids.size() - 1;
  current_rowid= index_cursor_rowids[index_cursor_position];
  DBUG_RETURN(mylite_read_row_by_id(db_name.c_str(), db_name.length(),
                                    opened_table_name.c_str(),
                                    opened_table_name.length(), table,
                                    current_rowid, buf));
}

int ha_mylite::rnd_init(bool)
{
  DBUG_ENTER("ha_mylite::rnd_init");
  scan_index= 0;
  current_rowid= 0;
  index_cursor_rowids.clear();
  DBUG_RETURN(0);
}

int ha_mylite::rnd_next(uchar *buf)
{
  DBUG_ENTER("ha_mylite::rnd_next");
  DBUG_RETURN(mylite_read_row(db_name.c_str(), db_name.length(),
                              opened_table_name.c_str(),
                              opened_table_name.length(), table, &scan_index,
                              &current_rowid, buf));
}

int ha_mylite::rnd_pos(uchar *buf, uchar *pos)
{
  DBUG_ENTER("ha_mylite::rnd_pos");
  current_rowid= mylite_read_le64(pos);
  DBUG_RETURN(mylite_read_row_by_id(db_name.c_str(), db_name.length(),
                                    opened_table_name.c_str(),
                                    opened_table_name.length(), table,
                                    current_rowid, buf));
}

void ha_mylite::position(const uchar *)
{
  DBUG_ENTER("ha_mylite::position");
  mylite_store_le64(ref, current_rowid);
  DBUG_VOID_RETURN;
}

int ha_mylite::info(uint)
{
  DBUG_ENTER("ha_mylite::info");
  stats.records= mylite_count_rows(db_name.c_str(), db_name.length(),
                                   opened_table_name.c_str(),
                                   opened_table_name.length());
  stats.deleted= 0;
  stats.data_file_length= stats.records * table_share->reclength;
  stats.index_file_length= 0;
  DBUG_RETURN(0);
}

ha_rows ha_mylite::records_in_range(uint inx, const key_range *min_key,
                                    const key_range *max_key, page_range *)
{
  DBUG_ENTER("ha_mylite::records_in_range");
  DBUG_RETURN(mylite_count_index_range(
    db_name.c_str(), db_name.length(), opened_table_name.c_str(),
    opened_table_name.length(), table, inx, min_key, max_key));
}

void ha_mylite::get_auto_increment(ulonglong offset, ulonglong increment,
                                   ulonglong nb_desired_values,
                                   ulonglong *first_value,
                                   ulonglong *nb_reserved_values)
{
  DBUG_ENTER("ha_mylite::get_auto_increment");
  if (!mylite_reserve_auto_increment(
        db_name.c_str(), db_name.length(), opened_table_name.c_str(),
        opened_table_name.length(), table, offset, increment,
        nb_desired_values, first_value, nb_reserved_values))
  {
    *first_value= ULONGLONG_MAX;
    *nb_reserved_values= 0;
  }
  DBUG_VOID_RETURN;
}

int ha_mylite::reset_auto_increment(ulonglong value)
{
  DBUG_ENTER("ha_mylite::reset_auto_increment");
  DBUG_RETURN(mylite_reset_auto_increment_value(
    db_name.c_str(), db_name.length(), opened_table_name.c_str(),
    opened_table_name.length(), value));
}

void ha_mylite::update_create_info(HA_CREATE_INFO *create_info)
{
  DBUG_ENTER("ha_mylite::update_create_info");
  ulonglong value= 0;
  if (create_info &&
      mylite_read_auto_increment_value(
        db_name.c_str(), db_name.length(), opened_table_name.c_str(),
        opened_table_name.length(), &value) &&
      value > 0)
    create_info->auto_increment_value= value;
  DBUG_VOID_RETURN;
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

static int mylite_store_table_definition(const char *path, const TABLE *table,
                                         const HA_CREATE_INFO *create_info)
{
  const TABLE_SHARE *share= table ? table->s : nullptr;
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
  definition.next_rowid= 1;
  definition.auto_increment_next=
    mylite_initial_auto_increment_next(table, create_info);
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

static int mylite_store_row(const char *db, size_t db_length,
                            const char *table_name,
                            size_t table_name_length, TABLE *table,
                            const uchar *record, uint *duplicate_key)
{
  if (!record || !mylite_table_supports_row_storage(table) ||
      !mylite_table_supports_key_storage(table))
    return HA_ERR_UNSUPPORTED;

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return HA_ERR_CRASHED;

  Mylite_table_definition *definition=
    mylite_find_table_definition_locked(db, db_length, table_name,
                                        table_name_length);
  if (!definition)
    return HA_ERR_NO_SUCH_TABLE;
  if (definition->next_rowid == ~static_cast<uint64_t>(0))
    return HA_ERR_RECORD_FILE_FULL;

  const int unique_error=
    mylite_check_unique_constraints_locked(definition, table, record, 0,
                                           duplicate_key);
  if (unique_error)
    return unique_error;

  const std::vector<Mylite_table_definition> before= mylite_catalog;
  if (!mylite_advance_auto_increment_locked(definition, table, record))
    return HA_ERR_RECORD_FILE_FULL;

  Mylite_row row;
  row.rowid= definition->next_rowid++;
  row.deleted= false;
  row.record.assign(record, record + table->s->reclength);
  definition->rows.push_back(row);

  const int error= mylite_flush_catalog_locked();
  if (error)
    mylite_catalog= before;
  return error;
}

static int mylite_update_row(const char *db, size_t db_length,
                             const char *table_name,
                             size_t table_name_length, TABLE *table,
                             uint64_t rowid, const uchar *record,
                             uint *duplicate_key)
{
  if (!record || rowid == 0 || !mylite_table_supports_row_storage(table) ||
      !mylite_table_supports_key_storage(table))
    return HA_ERR_UNSUPPORTED;

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return HA_ERR_CRASHED;

  Mylite_table_definition *definition=
    mylite_find_table_definition_locked(db, db_length, table_name,
                                        table_name_length);
  if (!definition)
    return HA_ERR_NO_SUCH_TABLE;

  Mylite_row *row= mylite_find_row_locked(definition, rowid);
  if (!row)
    return HA_ERR_KEY_NOT_FOUND;

  const int unique_error=
    mylite_check_unique_constraints_locked(definition, table, record, rowid,
                                           duplicate_key);
  if (unique_error)
    return unique_error;

  const std::vector<Mylite_table_definition> before= mylite_catalog;
  if (!mylite_advance_auto_increment_locked(definition, table, record))
    return HA_ERR_RECORD_FILE_FULL;
  row->record.assign(record, record + table->s->reclength);
  const int error= mylite_flush_catalog_locked();
  if (error)
    mylite_catalog= before;
  return error;
}

static int mylite_delete_row(const char *db, size_t db_length,
                             const char *table_name,
                             size_t table_name_length, uint64_t rowid)
{
  if (rowid == 0)
    return HA_ERR_KEY_NOT_FOUND;

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return HA_ERR_CRASHED;

  Mylite_table_definition *definition=
    mylite_find_table_definition_locked(db, db_length, table_name,
                                        table_name_length);
  if (!definition)
    return HA_ERR_NO_SUCH_TABLE;

  Mylite_row *row= mylite_find_row_locked(definition, rowid);
  if (!row)
    return HA_ERR_KEY_NOT_FOUND;

  const std::vector<Mylite_table_definition> before= mylite_catalog;
  row->deleted= true;
  const int error= mylite_flush_catalog_locked();
  if (error)
    mylite_catalog= before;
  return error;
}

static int mylite_read_row(const char *db, size_t db_length,
                           const char *table_name, size_t table_name_length,
                           const TABLE *table, size_t *scan_index,
                           uint64_t *rowid, uchar *record)
{
  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return HA_ERR_CRASHED;

  Mylite_table_definition *definition=
    mylite_find_table_definition_locked(db, db_length, table_name,
                                        table_name_length);
  if (!definition)
    return HA_ERR_NO_SUCH_TABLE;

  while (*scan_index < definition->rows.size())
  {
    const Mylite_row &row= definition->rows[(*scan_index)++];
    if (row.deleted)
      continue;
    if (row.record.size() != table->s->reclength)
      return HA_ERR_CRASHED;
    std::memcpy(record, row.record.data(), row.record.size());
    *rowid= row.rowid;
    return 0;
  }

  return HA_ERR_END_OF_FILE;
}

static int mylite_read_row_by_id(const char *db, size_t db_length,
                                 const char *table_name,
                                 size_t table_name_length, const TABLE *table,
                                 uint64_t rowid, uchar *record)
{
  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return HA_ERR_CRASHED;

  Mylite_table_definition *definition=
    mylite_find_table_definition_locked(db, db_length, table_name,
                                        table_name_length);
  if (!definition)
    return HA_ERR_NO_SUCH_TABLE;

  const Mylite_row *row= mylite_find_row_locked(definition, rowid);
  if (!row)
    return HA_ERR_KEY_NOT_FOUND;
  if (row->record.size() != table->s->reclength)
    return HA_ERR_CRASHED;

  std::memcpy(record, row->record.data(), row->record.size());
  return 0;
}

static int mylite_build_index_entries(
    const char *db, size_t db_length, const char *table_name,
    size_t table_name_length, TABLE *table, uint key_index,
    std::vector<Mylite_index_entry> *entries)
{
  if (!table || !table->s || key_index >= table->s->keys ||
      !mylite_key_supports_storage(table->key_info[key_index]))
    return HA_ERR_UNSUPPORTED;

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return HA_ERR_CRASHED;

  Mylite_table_definition *definition=
    mylite_find_table_definition_locked(db, db_length, table_name,
                                        table_name_length);
  if (!definition)
    return HA_ERR_NO_SUCH_TABLE;

  entries->clear();
  for (const Mylite_row &row : definition->rows)
  {
    if (row.deleted)
      continue;
    if (row.record.size() != table->s->reclength)
      return HA_ERR_CRASHED;

    Mylite_index_entry entry;
    entry.rowid= row.rowid;
    if (!mylite_make_key_image(table, key_index, row.record.data(),
                               &entry.key))
      return HA_ERR_CRASHED;
    entries->push_back(entry);
  }

  KEY *key_info= table->key_info + key_index;
  std::sort(entries->begin(), entries->end(),
            [key_info](const Mylite_index_entry &left,
                       const Mylite_index_entry &right) {
              const int cmp= key_tuple_cmp(key_info->key_part,
                                           left.key.data(),
                                           right.key.data(),
                                           key_info->key_length);
              return cmp != 0 ? cmp < 0 : left.rowid < right.rowid;
            });
  return 0;
}

static ha_rows mylite_count_index_range(
    const char *db, size_t db_length, const char *table_name,
    size_t table_name_length, TABLE *table, uint key_index,
    const key_range *min_key, const key_range *max_key)
{
  std::vector<Mylite_index_entry> entries;
  const int error= mylite_build_index_entries(db, db_length, table_name,
                                              table_name_length, table,
                                              key_index, &entries);
  if (error)
    return HA_POS_ERROR;

  KEY *key_info= table->key_info + key_index;
  ha_rows count= 0;
  for (const Mylite_index_entry &entry : entries)
  {
    if (mylite_index_entry_in_range(entry, key_info, min_key, max_key))
      ++count;
  }
  return count;
}

static size_t mylite_count_rows(const char *db, size_t db_length,
                                const char *table_name,
                                size_t table_name_length)
{
  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return 0;

  Mylite_table_definition *definition=
    mylite_find_table_definition_locked(db, db_length, table_name,
                                        table_name_length);
  if (!definition)
    return 0;

  size_t rows= 0;
  for (const Mylite_row &row : definition->rows)
  {
    if (!row.deleted)
      ++rows;
  }
  return rows;
}

static bool mylite_reserve_auto_increment(
    const char *db, size_t db_length, const char *table_name,
    size_t table_name_length, const TABLE *table, ulonglong offset,
    ulonglong increment, ulonglong nb_desired_values, ulonglong *first_value,
    ulonglong *nb_reserved_values)
{
  if (!first_value || !nb_reserved_values)
    return false;

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return false;

  Mylite_table_definition *definition=
    mylite_find_table_definition_locked(db, db_length, table_name,
                                        table_name_length);
  if (!definition)
    return false;
  const uint64_t current_next= definition->auto_increment_next == 0 &&
                              mylite_table_uses_autoincrement(table)
    ? 1
    : definition->auto_increment_next;
  if (current_next == 0)
    return false;

  const uint64_t count= nb_desired_values == 0 ? 1 : nb_desired_values;
  uint64_t first= 0;
  if (!mylite_next_auto_increment_value(current_next, offset, increment,
                                        &first))
    return false;

  uint64_t next= first;
  for (uint64_t i= 0; i < count; ++i)
  {
    if (next == ~static_cast<uint64_t>(0))
      return false;
    if (!mylite_next_auto_increment_value(next + 1, offset, increment, &next))
      return false;
  }

  const std::vector<Mylite_table_definition> before= mylite_catalog;
  definition->auto_increment_next= next;
  const int error= mylite_flush_catalog_locked();
  if (error)
  {
    mylite_catalog= before;
    return false;
  }

  *first_value= first;
  *nb_reserved_values= count;
  return true;
}

static int mylite_reset_auto_increment_value(const char *db, size_t db_length,
                                             const char *table_name,
                                             size_t table_name_length,
                                             ulonglong value)
{
  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return HA_ERR_CRASHED;

  Mylite_table_definition *definition=
    mylite_find_table_definition_locked(db, db_length, table_name,
                                        table_name_length);
  if (!definition)
    return HA_ERR_NO_SUCH_TABLE;
  if (definition->auto_increment_next == 0)
    return 0;

  const std::vector<Mylite_table_definition> before= mylite_catalog;
  definition->auto_increment_next= value > 0 ? value : 1;
  const int error= mylite_flush_catalog_locked();
  if (error)
    mylite_catalog= before;
  return error;
}

static bool mylite_read_auto_increment_value(const char *db, size_t db_length,
                                             const char *table_name,
                                             size_t table_name_length,
                                             ulonglong *value)
{
  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return false;

  Mylite_table_definition *definition=
    mylite_find_table_definition_locked(db, db_length, table_name,
                                        table_name_length);
  if (!definition)
    return false;

  *value= definition->auto_increment_next;
  return true;
}

static bool mylite_table_supports_row_storage(const TABLE *table)
{
  return table && table->s && table->s->blob_fields == 0;
}

static bool mylite_table_supports_key_storage(const TABLE *table)
{
  if (!table || !table->s)
    return false;

  for (uint key_index= 0; key_index < table->s->keys; ++key_index)
  {
    if (!mylite_key_supports_storage(table->key_info[key_index]))
      return false;
  }

  if (!mylite_table_uses_autoincrement(table))
    return true;

  return table->s->next_number_index < table->s->keys &&
         table->s->next_number_keypart == 0;
}

static bool mylite_key_supports_storage(const KEY &key)
{
  if (key.user_defined_key_parts == 0)
    return false;
  if (key.algorithm != HA_KEY_ALG_UNDEF && key.algorithm != HA_KEY_ALG_BTREE)
    return false;
  if (key.flags & (HA_FULLTEXT_legacy | HA_SPATIAL_legacy |
                   HA_NULL_PART_KEY | HA_GENERATED_KEY))
    return false;

  KEY_PART_INFO *key_part= key.key_part;
  KEY_PART_INFO *key_part_end= key_part + key.user_defined_key_parts;
  for (; key_part < key_part_end; ++key_part)
  {
    if (!mylite_key_part_supports_storage(*key_part))
      return false;
  }
  return true;
}

static bool mylite_key_part_supports_storage(const KEY_PART_INFO &key_part)
{
  return key_part.field &&
         !key_part.field->real_maybe_null() &&
         !(key_part.key_part_flag & (HA_BLOB_PART | HA_REVERSE_SORT));
}

static bool mylite_table_uses_autoincrement(const TABLE *table)
{
  return table && table->found_next_number_field &&
         table->next_number_field;
}

static uint64_t mylite_initial_auto_increment_next(
    const TABLE *table, const HA_CREATE_INFO *create_info)
{
  if (!mylite_table_uses_autoincrement(table))
    return 0;
  if (create_info && create_info->auto_increment_value > 0)
    return create_info->auto_increment_value;
  return 1;
}

static int mylite_check_unique_constraints_locked(
    Mylite_table_definition *definition, TABLE *table, const uchar *record,
    uint64_t ignored_rowid, uint *duplicate_key)
{
  for (uint key_index= 0; key_index < table->s->keys; ++key_index)
  {
    KEY *key_info= table->key_info + key_index;
    if (!(key_info->flags & HA_NOSAME))
      continue;

    std::vector<uchar> candidate_key;
    if (!mylite_make_key_image(table, key_index, record, &candidate_key))
      return HA_ERR_CRASHED;

    for (const Mylite_row &row : definition->rows)
    {
      if (row.deleted || row.rowid == ignored_rowid)
        continue;
      if (row.record.size() != table->s->reclength)
        return HA_ERR_CRASHED;

      std::vector<uchar> stored_key;
      if (!mylite_make_key_image(table, key_index, row.record.data(),
                                 &stored_key))
        return HA_ERR_CRASHED;
      if (key_tuple_cmp(key_info->key_part, candidate_key.data(),
                        stored_key.data(), key_info->key_length) == 0)
      {
        *duplicate_key= key_index;
        return HA_ERR_FOUND_DUPP_KEY;
      }
    }
  }
  return 0;
}

static bool mylite_make_key_image(TABLE *table, uint key_index,
                                  const uchar *record,
                                  std::vector<uchar> *key_image)
{
  if (!table || !table->s || key_index >= table->s->keys)
    return false;

  KEY *key_info= table->key_info + key_index;
  key_image->assign(key_info->key_length, 0);
  if (!key_image->empty())
    key_copy(key_image->data(), record, key_info, 0, false);
  return true;
}

static int mylite_compare_index_entry(const Mylite_index_entry &entry,
                                      KEY *key_info, const uchar *key,
                                      uint key_length)
{
  return key_tuple_cmp(key_info->key_part, entry.key.data(), key,
                       key_length);
}

static bool mylite_find_index_position(
    const std::vector<Mylite_index_entry> &entries, KEY *key_info,
    const uchar *key, uint key_length, enum ha_rkey_function find_flag,
    size_t *position)
{
  if (entries.empty())
    return false;
  if (!key || key_length == 0)
  {
    *position= 0;
    return true;
  }

  bool found= false;
  size_t found_position= 0;
  for (size_t i= 0; i < entries.size(); ++i)
  {
    const int cmp=
      mylite_compare_index_entry(entries[i], key_info, key, key_length);
    bool match= false;
    switch (find_flag) {
    case HA_READ_KEY_EXACT:
    case HA_READ_PREFIX:
      match= cmp == 0;
      break;
    case HA_READ_KEY_OR_NEXT:
      match= cmp >= 0;
      break;
    case HA_READ_AFTER_KEY:
      match= cmp > 0;
      break;
    case HA_READ_KEY_OR_PREV:
    case HA_READ_PREFIX_LAST_OR_PREV:
      if (cmp <= 0)
      {
        found= true;
        found_position= i;
      }
      continue;
    case HA_READ_BEFORE_KEY:
      if (cmp < 0)
      {
        found= true;
        found_position= i;
      }
      continue;
    case HA_READ_PREFIX_LAST:
      if (cmp == 0)
      {
        found= true;
        found_position= i;
      }
      continue;
    default:
      return false;
    }

    if (match)
    {
      *position= i;
      return true;
    }
  }

  if (found)
    *position= found_position;
  return found;
}

static bool mylite_index_entry_in_range(const Mylite_index_entry &entry,
                                        KEY *key_info,
                                        const key_range *min_key,
                                        const key_range *max_key)
{
  if (min_key && min_key->key && min_key->length > 0)
  {
    const int cmp= mylite_compare_index_entry(entry, key_info, min_key->key,
                                             min_key->length);
    if (min_key->flag == HA_READ_AFTER_KEY)
    {
      if (cmp <= 0)
        return false;
    }
    else if (cmp < 0)
      return false;
  }

  if (max_key && max_key->key && max_key->length > 0)
  {
    const int cmp= mylite_compare_index_entry(entry, key_info, max_key->key,
                                             max_key->length);
    if (max_key->flag == HA_READ_BEFORE_KEY)
    {
      if (cmp >= 0)
        return false;
    }
    else if (cmp > 0)
      return false;
  }

  return true;
}

static bool mylite_advance_auto_increment_locked(
    Mylite_table_definition *definition, const TABLE *table,
    const uchar *record)
{
  if (!mylite_table_uses_autoincrement(table))
    return true;

  const uint64_t current_next= definition->auto_increment_next == 0
    ? 1
    : definition->auto_increment_next;
  const my_ptrdiff_t offset= record - table->record[0];
  const ulonglong value= table->next_number_field->val_int_offset(offset);
  if (value == 0 || value < current_next)
    return true;
  if (value == ~static_cast<ulonglong>(0))
    return false;

  definition->auto_increment_next= value + 1;
  return definition->auto_increment_next > value;
}

static bool mylite_next_auto_increment_value(uint64_t value, uint64_t offset,
                                             uint64_t increment,
                                             uint64_t *next_value)
{
  if (increment == 0)
    return false;
  if (value < offset)
  {
    *next_value= offset;
    return true;
  }

  const uint64_t remainder= (value - offset) % increment;
  if (remainder == 0)
  {
    *next_value= value;
    return true;
  }

  const uint64_t delta= increment - remainder;
  if (value > ~static_cast<uint64_t>(0) - delta)
    return false;

  *next_value= value + delta;
  return true;
}

static Mylite_row *mylite_find_row_locked(Mylite_table_definition *definition,
                                          uint64_t rowid)
{
  for (Mylite_row &row : definition->rows)
  {
    if (!row.deleted && row.rowid == rowid)
      return &row;
  }
  return nullptr;
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

  const int fd= open(mylite_catalog_file, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
  {
    if (errno == ENOENT)
      return true;
    mylite_log_catalog_error("open", mylite_catalog_file);
    return false;
  }

  bool ok= true;
  struct stat st;
  Mylite_catalog_header header= { 0, 0, 0, 0, 0 };
  std::string content;
  if (fstat(fd, &st) != 0)
  {
    mylite_log_catalog_error("stat", mylite_catalog_file);
    ok= false;
    goto done;
  }
  if (st.st_size == 0)
    goto done;

  if (!mylite_find_latest_catalog_header(fd, st.st_size, &header))
  {
    sql_print_error("MyLite: no valid catalog header in %s",
                    mylite_catalog_file);
    ok= false;
    goto done;
  }

  if (!mylite_read_catalog_payload(fd, header, &content) ||
      !mylite_parse_catalog_payload_locked(content))
    ok= false;

done:
  if (close(fd) != 0)
  {
    mylite_log_catalog_error("close", mylite_catalog_file);
    ok= false;
  }
  return ok;
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
  const std::string content= mylite_serialize_catalog_locked();

  const int fd= open(catalog_path.c_str(),
                     O_RDWR | O_CREAT | O_CLOEXEC, 0666);
  if (fd < 0)
  {
    mylite_log_catalog_error("open", catalog_path);
    return false;
  }

  bool ok= true;
  struct stat st;
  Mylite_catalog_header latest= { 0, 0, 0, 0, 0 };
  Mylite_catalog_header next= { 0, 0, 0, 0, 0 };
  bool found= false;
  uint64_t payload_offset= 0;
  uint64_t payload_checksum= 0;
  if (fstat(fd, &st) != 0)
  {
    mylite_log_catalog_error("stat", catalog_path);
    ok= false;
    goto done;
  }

  found= mylite_find_latest_catalog_header(fd, st.st_size, &latest);
  if (!found && st.st_size != 0)
  {
    sql_print_error("MyLite: refusing to overwrite invalid catalog %s",
                    catalog_path.c_str());
    ok= false;
    goto done;
  }
  if (found && latest.generation == ~static_cast<uint64_t>(0))
  {
    sql_print_error("MyLite: catalog generation is exhausted in %s",
                    catalog_path.c_str());
    ok= false;
    goto done;
  }

  if (!mylite_write_catalog_payload(fd, content, &payload_offset,
                                    &payload_checksum))
  {
    mylite_log_catalog_error("write", catalog_path);
    ok= false;
    goto done;
  }
  if (fsync(fd) != 0)
  {
    mylite_log_catalog_error("fsync", catalog_path);
    ok= false;
    goto done;
  }

  next.slot= found && latest.slot == 0 ? 1 : 0;
  next.generation= found ? latest.generation + 1 : 1;
  next.payload_offset= payload_offset;
  next.payload_length= content.length();
  next.payload_checksum= payload_checksum;
  if (!mylite_write_catalog_header(fd, next))
  {
    mylite_log_catalog_error("write", catalog_path);
    ok= false;
    goto done;
  }
  if (fsync(fd) != 0)
  {
    mylite_log_catalog_error("fsync", catalog_path);
    ok= false;
    goto done;
  }

done:
  if (close(fd) != 0)
  {
    mylite_log_catalog_error("close", catalog_path);
    ok= false;
  }
  return ok;
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

    content.append("NEXTROWID\t");
    content.append(mylite_hex_encode(
      reinterpret_cast<const uchar *>(definition.db.data()),
      definition.db.length()));
    content.push_back('\t');
    content.append(mylite_hex_encode(
      reinterpret_cast<const uchar *>(definition.table_name.data()),
      definition.table_name.length()));
    content.push_back('\t');
    content.append(mylite_format_decimal_uint64(definition.next_rowid));
    content.push_back('\n');

    content.append("AUTOINC\t");
    content.append(mylite_hex_encode(
      reinterpret_cast<const uchar *>(definition.db.data()),
      definition.db.length()));
    content.push_back('\t');
    content.append(mylite_hex_encode(
      reinterpret_cast<const uchar *>(definition.table_name.data()),
      definition.table_name.length()));
    content.push_back('\t');
    content.append(mylite_format_decimal_uint64(
      definition.auto_increment_next));
    content.push_back('\n');

    for (const Mylite_row &row : definition.rows)
    {
      if (row.deleted)
        continue;

      content.append("ROW\t");
      content.append(mylite_hex_encode(
        reinterpret_cast<const uchar *>(definition.db.data()),
        definition.db.length()));
      content.push_back('\t');
      content.append(mylite_hex_encode(
        reinterpret_cast<const uchar *>(definition.table_name.data()),
        definition.table_name.length()));
      content.push_back('\t');
      content.append(mylite_format_decimal_uint64(row.rowid));
      content.push_back('\t');
      content.append(mylite_hex_encode(row.record.data(),
                                       row.record.size()));
      content.push_back('\n');
    }
  }

  return content;
}

static bool mylite_parse_catalog_payload_locked(const std::string &content)
{
  std::istringstream input(content);
  std::string line;
  if (!std::getline(input, line) || line != mylite_catalog_magic)
  {
    sql_print_error("MyLite: invalid catalog payload in %s",
                    mylite_catalog_file);
    return false;
  }

  std::vector<Mylite_table_definition> loaded;
  for (const Mylite_table_definition &definition : mylite_catalog)
  {
    if (!definition.seed_sql.empty())
      loaded.push_back(definition);
  }

  while (std::getline(input, line))
  {
    if (line.empty())
      continue;

    if (line.compare(0, 6, "TABLE\t") == 0)
    {
      if (!mylite_parse_table_payload_record_locked(line, &loaded))
        return false;
      continue;
    }
    if (line.compare(0, 10, "NEXTROWID\t") == 0)
    {
      if (!mylite_parse_next_rowid_payload_record_locked(line, &loaded))
        return false;
      continue;
    }
    if (line.compare(0, 8, "AUTOINC\t") == 0)
    {
      if (!mylite_parse_autoincrement_payload_record_locked(line, &loaded))
        return false;
      continue;
    }
    if (line.compare(0, 4, "ROW\t") == 0)
    {
      if (!mylite_parse_row_payload_record_locked(line, &loaded))
        return false;
      continue;
    }

    sql_print_error("MyLite: invalid catalog record in %s",
                    mylite_catalog_file);
    return false;
  }

  mylite_catalog.swap(loaded);
  return true;
}

static bool mylite_parse_table_payload_record_locked(
    const std::string &line, std::vector<Mylite_table_definition> *loaded)
{
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
  definition.next_rowid= 1;
  definition.auto_increment_next= 0;
  if (mylite_catalog_contains_definition(*loaded, definition.db,
                                         definition.table_name))
  {
    sql_print_error("MyLite: duplicate catalog table in %s",
                    mylite_catalog_file);
    return false;
  }

  loaded->push_back(definition);
  return true;
}

static bool mylite_parse_next_rowid_payload_record_locked(
    const std::string &line, std::vector<Mylite_table_definition> *loaded)
{
  const std::string::size_type first= line.find('\t');
  const std::string::size_type second= first == std::string::npos
    ? std::string::npos
    : line.find('\t', first + 1);
  const std::string::size_type third= second == std::string::npos
    ? std::string::npos
    : line.find('\t', second + 1);

  if (first == std::string::npos || second == std::string::npos ||
      third == std::string::npos || line.substr(0, first) != "NEXTROWID")
  {
    sql_print_error("MyLite: invalid catalog rowid record in %s",
                    mylite_catalog_file);
    return false;
  }

  std::vector<uchar> db_bytes;
  std::vector<uchar> table_bytes;
  uint64_t next_rowid= 0;
  if (!mylite_hex_decode(line.substr(first + 1, second - first - 1),
                         &db_bytes) ||
      !mylite_hex_decode(line.substr(second + 1, third - second - 1),
                         &table_bytes) ||
      !mylite_parse_decimal_uint64(line.substr(third + 1), &next_rowid) ||
      db_bytes.empty() || table_bytes.empty() || next_rowid == 0)
  {
    sql_print_error("MyLite: invalid catalog rowid encoding in %s",
                    mylite_catalog_file);
    return false;
  }

  const std::string db(reinterpret_cast<const char *>(db_bytes.data()),
                       db_bytes.size());
  const std::string table_name(
    reinterpret_cast<const char *>(table_bytes.data()), table_bytes.size());
  Mylite_table_definition *definition=
    mylite_find_table_definition_in_catalog(loaded, db, table_name);
  if (!definition)
  {
    sql_print_error("MyLite: rowid record before table in %s",
                    mylite_catalog_file);
    return false;
  }

  definition->next_rowid= next_rowid;
  return true;
}

static bool mylite_parse_autoincrement_payload_record_locked(
    const std::string &line, std::vector<Mylite_table_definition> *loaded)
{
  const std::string::size_type first= line.find('\t');
  const std::string::size_type second= first == std::string::npos
    ? std::string::npos
    : line.find('\t', first + 1);
  const std::string::size_type third= second == std::string::npos
    ? std::string::npos
    : line.find('\t', second + 1);

  if (first == std::string::npos || second == std::string::npos ||
      third == std::string::npos || line.substr(0, first) != "AUTOINC")
  {
    sql_print_error("MyLite: invalid catalog autoincrement record in %s",
                    mylite_catalog_file);
    return false;
  }

  std::vector<uchar> db_bytes;
  std::vector<uchar> table_bytes;
  uint64_t auto_increment_next= 0;
  if (!mylite_hex_decode(line.substr(first + 1, second - first - 1),
                         &db_bytes) ||
      !mylite_hex_decode(line.substr(second + 1, third - second - 1),
                         &table_bytes) ||
      !mylite_parse_decimal_uint64(line.substr(third + 1),
                                   &auto_increment_next) ||
      db_bytes.empty() || table_bytes.empty())
  {
    sql_print_error("MyLite: invalid catalog autoincrement encoding in %s",
                    mylite_catalog_file);
    return false;
  }

  const std::string db(reinterpret_cast<const char *>(db_bytes.data()),
                       db_bytes.size());
  const std::string table_name(
    reinterpret_cast<const char *>(table_bytes.data()), table_bytes.size());
  Mylite_table_definition *definition=
    mylite_find_table_definition_in_catalog(loaded, db, table_name);
  if (!definition)
  {
    sql_print_error("MyLite: autoincrement record before table in %s",
                    mylite_catalog_file);
    return false;
  }

  definition->auto_increment_next= auto_increment_next;
  return true;
}

static bool mylite_parse_row_payload_record_locked(
    const std::string &line, std::vector<Mylite_table_definition> *loaded)
{
  const std::string::size_type first= line.find('\t');
  const std::string::size_type second= first == std::string::npos
    ? std::string::npos
    : line.find('\t', first + 1);
  const std::string::size_type third= second == std::string::npos
    ? std::string::npos
    : line.find('\t', second + 1);
  const std::string::size_type fourth= third == std::string::npos
    ? std::string::npos
    : line.find('\t', third + 1);

  if (first == std::string::npos || second == std::string::npos ||
      third == std::string::npos || fourth == std::string::npos ||
      line.substr(0, first) != "ROW")
  {
    sql_print_error("MyLite: invalid catalog row record in %s",
                    mylite_catalog_file);
    return false;
  }

  std::vector<uchar> db_bytes;
  std::vector<uchar> table_bytes;
  Mylite_row row;
  if (!mylite_hex_decode(line.substr(first + 1, second - first - 1),
                         &db_bytes) ||
      !mylite_hex_decode(line.substr(second + 1, third - second - 1),
                         &table_bytes) ||
      !mylite_parse_decimal_uint64(
        line.substr(third + 1, fourth - third - 1), &row.rowid) ||
      !mylite_hex_decode(line.substr(fourth + 1), &row.record) ||
      db_bytes.empty() || table_bytes.empty() || row.rowid == 0 ||
      row.record.empty())
  {
    sql_print_error("MyLite: invalid catalog row encoding in %s",
                    mylite_catalog_file);
    return false;
  }

  const std::string db(reinterpret_cast<const char *>(db_bytes.data()),
                       db_bytes.size());
  const std::string table_name(
    reinterpret_cast<const char *>(table_bytes.data()), table_bytes.size());
  Mylite_table_definition *definition=
    mylite_find_table_definition_in_catalog(loaded, db, table_name);
  if (!definition || mylite_find_row_locked(definition, row.rowid))
  {
    sql_print_error("MyLite: invalid catalog row owner in %s",
                    mylite_catalog_file);
    return false;
  }

  row.deleted= false;
  if (row.rowid == ~static_cast<uint64_t>(0))
  {
    sql_print_error("MyLite: catalog row id is exhausted in %s",
                    mylite_catalog_file);
    return false;
  }
  if (row.rowid >= definition->next_rowid)
    definition->next_rowid= row.rowid + 1;
  definition->rows.push_back(row);
  return true;
}

static bool mylite_catalog_contains_definition(
    const std::vector<Mylite_table_definition> &catalog,
    const std::string &db, const std::string &table_name)
{
  for (const Mylite_table_definition &definition : catalog)
  {
    if (definition.db == db && definition.table_name == table_name)
      return true;
  }
  return false;
}

static Mylite_table_definition *mylite_find_table_definition_in_catalog(
    std::vector<Mylite_table_definition> *catalog, const std::string &db,
    const std::string &table_name)
{
  for (Mylite_table_definition &definition : *catalog)
  {
    if (definition.db == db && definition.table_name == table_name)
      return &definition;
  }
  return nullptr;
}

static bool mylite_parse_decimal_uint64(const std::string &value,
                                        uint64_t *result)
{
  if (value.empty())
    return false;

  uint64_t parsed= 0;
  for (char c : value)
  {
    if (c < '0' || c > '9')
      return false;
    const uint64_t digit= static_cast<uint64_t>(c - '0');
    if (parsed > (~static_cast<uint64_t>(0) - digit) / 10)
      return false;
    parsed= parsed * 10 + digit;
  }

  *result= parsed;
  return true;
}

static std::string mylite_format_decimal_uint64(uint64_t value)
{
  if (value == 0)
    return "0";

  std::string result;
  while (value > 0)
  {
    result.push_back(static_cast<char>('0' + (value % 10)));
    value/= 10;
  }
  std::reverse(result.begin(), result.end());
  return result;
}

static bool mylite_find_latest_catalog_header(int fd, off_t file_size,
                                              Mylite_catalog_header *latest)
{
  bool found= false;
  for (size_t slot= 0; slot < 2; ++slot)
  {
    Mylite_catalog_header header= { 0, 0, 0, 0, 0 };
    if (!mylite_read_catalog_header(fd, slot, file_size, &header))
      continue;

    std::string payload;
    if (!mylite_read_catalog_payload(fd, header, &payload))
      continue;

    if (!found || header.generation > latest->generation)
    {
      *latest= header;
      found= true;
    }
  }
  return found;
}

static bool mylite_read_catalog_header(int fd, size_t slot, off_t file_size,
                                       Mylite_catalog_header *header)
{
  const uint64_t header_offset=
    static_cast<uint64_t>(slot) * mylite_catalog_page_size;
  if (file_size < 0 ||
      static_cast<uint64_t>(file_size) <
        header_offset + mylite_catalog_page_size)
    return false;

  std::vector<uchar> page(mylite_catalog_page_size, 0);
  if (!mylite_read_all_at(fd, page.data(), page.size(), header_offset))
    return false;

  if (std::memcmp(page.data(), mylite_catalog_file_magic,
                  sizeof(mylite_catalog_file_magic)) != 0)
    return false;
  if (mylite_read_le32(page.data() + 16) != mylite_catalog_format_version ||
      mylite_read_le32(page.data() + 20) != mylite_catalog_page_size)
    return false;

  const uint64_t stored_header_checksum=
    mylite_read_le64(page.data() + mylite_catalog_header_checksum_offset);
  mylite_store_le64(page.data() + mylite_catalog_header_checksum_offset, 0);
  if (mylite_checksum(page.data(), page.size()) != stored_header_checksum)
    return false;

  header->slot= slot;
  header->generation= mylite_read_le64(page.data() + 24);
  header->payload_offset= mylite_read_le64(page.data() + 32);
  header->payload_length= mylite_read_le64(page.data() + 40);
  header->payload_checksum= mylite_read_le64(page.data() + 48);

  const uint64_t file_size_u= static_cast<uint64_t>(file_size);
  if (header->generation == 0 ||
      header->payload_offset < mylite_catalog_payload_start ||
      header->payload_length == 0 ||
      header->payload_offset > file_size_u ||
      header->payload_length > file_size_u - header->payload_offset)
    return false;

  return true;
}

static bool mylite_read_catalog_payload(int fd,
                                        const Mylite_catalog_header &header,
                                        std::string *content)
{
  if (header.payload_length > content->max_size())
    return false;

  content->assign(static_cast<size_t>(header.payload_length), '\0');
  if (!mylite_read_all_at(fd,
                          reinterpret_cast<uchar *>(&(*content)[0]),
                          content->length(), header.payload_offset))
    return false;

  return mylite_checksum(
           reinterpret_cast<const uchar *>(content->data()),
           content->length()) == header.payload_checksum;
}

static bool mylite_write_catalog_payload(int fd, const std::string &content,
                                         uint64_t *payload_offset,
                                         uint64_t *payload_checksum)
{
  const off_t file_end= lseek(fd, 0, SEEK_END);
  if (file_end < 0)
    return false;

  *payload_offset= static_cast<uint64_t>(file_end);
  if (*payload_offset < mylite_catalog_payload_start)
    *payload_offset= mylite_catalog_payload_start;
  *payload_checksum= mylite_checksum(
    reinterpret_cast<const uchar *>(content.data()), content.length());

  return mylite_write_all_at(
    fd, reinterpret_cast<const uchar *>(content.data()), content.length(),
    *payload_offset);
}

static bool mylite_write_catalog_header(int fd,
                                        const Mylite_catalog_header &header)
{
  std::vector<uchar> page(mylite_catalog_page_size, 0);
  std::memcpy(page.data(), mylite_catalog_file_magic,
              sizeof(mylite_catalog_file_magic));
  mylite_store_le32(page.data() + 16, mylite_catalog_format_version);
  mylite_store_le32(page.data() + 20, mylite_catalog_page_size);
  mylite_store_le64(page.data() + 24, header.generation);
  mylite_store_le64(page.data() + 32, header.payload_offset);
  mylite_store_le64(page.data() + 40, header.payload_length);
  mylite_store_le64(page.data() + 48, header.payload_checksum);
  mylite_store_le64(page.data() + mylite_catalog_header_checksum_offset, 0);
  mylite_store_le64(page.data() + mylite_catalog_header_checksum_offset,
                    mylite_checksum(page.data(), page.size()));

  return mylite_write_all_at(
    fd, page.data(), page.size(),
    static_cast<uint64_t>(header.slot) * mylite_catalog_page_size);
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

static bool mylite_read_all_at(int fd, uchar *data, size_t length,
                               uint64_t offset)
{
  uchar *ptr= data;
  size_t remaining= length;
  while (remaining > 0)
  {
    const ssize_t read_bytes=
      pread(fd, ptr, remaining, static_cast<off_t>(offset));
    if (read_bytes < 0)
    {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (read_bytes == 0)
      return false;
    ptr+= read_bytes;
    offset+= static_cast<uint64_t>(read_bytes);
    remaining-= static_cast<size_t>(read_bytes);
  }
  return true;
}

static bool mylite_write_all_at(int fd, const uchar *data, size_t length,
                                uint64_t offset)
{
  const uchar *ptr= data;
  size_t remaining= length;
  while (remaining > 0)
  {
    const ssize_t written=
      pwrite(fd, ptr, remaining, static_cast<off_t>(offset));
    if (written < 0)
    {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (written == 0)
      return false;
    ptr+= written;
    offset+= static_cast<uint64_t>(written);
    remaining-= static_cast<size_t>(written);
  }
  return true;
}

static void mylite_store_le32(uchar *to, uint32_t value)
{
  to[0]= static_cast<uchar>(value);
  to[1]= static_cast<uchar>(value >> 8);
  to[2]= static_cast<uchar>(value >> 16);
  to[3]= static_cast<uchar>(value >> 24);
}

static void mylite_store_le64(uchar *to, uint64_t value)
{
  for (size_t i= 0; i < 8; ++i)
    to[i]= static_cast<uchar>(value >> (i * 8));
}

static uint32_t mylite_read_le32(const uchar *from)
{
  return static_cast<uint32_t>(from[0]) |
         (static_cast<uint32_t>(from[1]) << 8) |
         (static_cast<uint32_t>(from[2]) << 16) |
         (static_cast<uint32_t>(from[3]) << 24);
}

static uint64_t mylite_read_le64(const uchar *from)
{
  uint64_t value= 0;
  for (size_t i= 0; i < 8; ++i)
    value|= static_cast<uint64_t>(from[i]) << (i * 8);
  return value;
}

static uint64_t mylite_checksum(const uchar *data, size_t length)
{
  uint64_t value= mylite_fnv1a_offset_basis;
  for (size_t i= 0; i < length; ++i)
  {
    value^= data[i];
    value*= mylite_fnv1a_prime;
  }
  return value;
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
