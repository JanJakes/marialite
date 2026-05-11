/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#include <my_global.h>
#include <my_sys.h>
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
#include <new>
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

struct Mylite_overflow_row
{
  uint64_t rowid;
  uint64_t total_length;
  std::vector<uchar> record;
};

struct Mylite_index_entry
{
  uint64_t rowid;
  std::vector<uchar> key;
};

struct Mylite_index_root
{
  uint32_t key_index;
  uint32_t key_length;
  uint64_t payload_offset;
  uint64_t payload_length;
  uint64_t payload_checksum;
  bool payload_ref_seen;
  std::vector<Mylite_index_entry> entries;
};

struct Mylite_free_page_range
{
  uint64_t page_id;
  uint64_t page_count;
};

struct Mylite_table_definition
{
  std::string db;
  std::string table_name;
  std::string seed_sql;
  std::vector<uchar> frm_image;
  uint64_t next_rowid;
  uint64_t auto_increment_next;
  uint64_t rows_payload_offset;
  uint64_t rows_payload_length;
  uint64_t rows_payload_checksum;
  bool rows_payload_ref_seen;
  std::vector<Mylite_row> rows;
  std::vector<Mylite_index_root> index_roots;
};

struct Mylite_transaction_snapshot
{
  bool active;
  std::vector<Mylite_table_definition> catalog;
  std::vector<Mylite_free_page_range> pending_free_page_ranges;
};

struct Mylite_savepoint_snapshot
{
  uint64_t id;
  Mylite_transaction_snapshot snapshot;
};

struct Mylite_transaction_context
{
  bool dirty;
  uint64_t next_savepoint_id;
  Mylite_transaction_snapshot statement;
  Mylite_transaction_snapshot transaction;
  std::vector<Mylite_savepoint_snapshot> savepoints;
};

struct Mylite_catalog_header
{
  size_t slot;
  uint32_t format_version;
  uint64_t generation;
  uint64_t payload_offset;
  uint64_t payload_length;
  uint64_t payload_checksum;
  uint64_t free_payload_offset;
  uint64_t free_payload_length;
  uint64_t free_payload_checksum;
};

struct Mylite_page_header
{
  uint32_t type;
  uint64_t page_id;
  uint64_t next_page_id;
  uint32_t payload_length;
};

static handler *mylite_create_handler(handlerton *hton, TABLE_SHARE *table,
                                      MEM_ROOT *mem_root);
static int mylite_commit(THD *thd, bool all);
static int mylite_rollback(THD *thd, bool all);
static int mylite_savepoint_set(THD *thd, void *sv);
static int mylite_savepoint_rollback(THD *thd, void *sv);
static int mylite_savepoint_release(THD *thd, void *sv);
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
                            size_t table_name_length, THD *thd,
                            TABLE *table, const uchar *record,
                            uint *duplicate_key);
static int mylite_update_row(const char *db, size_t db_length,
                             const char *table_name, size_t table_name_length,
                             THD *thd, TABLE *table, uint64_t rowid,
                             const uchar *record, uint *duplicate_key);
static int mylite_delete_row(const char *db, size_t db_length,
                             const char *table_name,
                             size_t table_name_length, THD *thd,
                             TABLE *table, uint64_t rowid);
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
    size_t table_name_length, THD *thd, const TABLE *table, ulonglong offset,
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
static bool mylite_refresh_index_roots_locked(
    Mylite_table_definition *definition, TABLE *table);
static bool mylite_index_root_matches_table_locked(
    Mylite_table_definition *definition, const Mylite_index_root &root,
    TABLE *table, uint key_index);
static int mylite_build_index_entries_from_rows_locked(
    Mylite_table_definition *definition, TABLE *table, uint key_index,
    std::vector<Mylite_index_entry> *entries);
static Mylite_index_root *mylite_find_index_root_locked(
    Mylite_table_definition *definition, uint32_t key_index);
static const Mylite_index_root *mylite_find_index_root_locked(
    const Mylite_table_definition *definition, uint32_t key_index);
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
static int mylite_register_transaction(THD *thd, bool write);
static int mylite_prepare_dml_mutation_locked(THD *thd);
static int mylite_check_transaction_access_locked(THD *thd);
static int mylite_flush_catalog_for_thd_locked(THD *thd);
static Mylite_transaction_context *mylite_get_transaction_context(
    THD *thd, bool create);
static void mylite_clear_transaction_context_locked(
    THD *thd, Mylite_transaction_context *context);
static bool mylite_capture_snapshot_locked(
    Mylite_transaction_snapshot *snapshot);
static void mylite_restore_snapshot_locked(
    const Mylite_transaction_snapshot &snapshot);
static void mylite_clear_snapshot(Mylite_transaction_snapshot *snapshot);
static void mylite_store_savepoint_id(void *sv, uint64_t id);
static uint64_t mylite_load_savepoint_id(const void *sv);
static Mylite_savepoint_snapshot *mylite_find_savepoint_snapshot(
    Mylite_transaction_context *context, uint64_t id);
static void mylite_discard_savepoints_newer_than(
    Mylite_transaction_context *context, uint64_t id);
static void mylite_discard_savepoints_from(Mylite_transaction_context *context,
                                           uint64_t id);
static bool mylite_thd_in_explicit_transaction(THD *thd);
static void mylite_clear_catalog_error();
static void mylite_set_catalog_error(int error);
static void mylite_set_catalog_errno_error();
static int mylite_catalog_error_code();
static void mylite_clear_frm_definitions_locked();
static bool mylite_load_catalog_locked();
static bool mylite_ensure_catalog_file_locked();
static void mylite_release_catalog_file_locked();
static bool mylite_load_catalog_generation_locked(
    int fd, const Mylite_catalog_header &header,
    std::vector<Mylite_table_definition> *loaded,
    std::vector<Mylite_free_page_range> *free_ranges);
static bool mylite_load_row_payloads_locked(
    int fd, std::vector<Mylite_table_definition> *catalog);
static bool mylite_load_row_payload_locked(
    int fd, Mylite_table_definition *definition);
static bool mylite_parse_legacy_rows_payload_locked(
    const std::string &content, Mylite_table_definition *definition);
static bool mylite_parse_row_payload_pages_locked(
    int fd, Mylite_table_definition *definition);
static bool mylite_parse_row_slot_page_payload_locked(
    const uchar *payload, size_t payload_length,
    Mylite_table_definition *definition,
    const std::vector<Mylite_overflow_row> &overflow_rows);
static bool mylite_parse_row_overflow_page_payload_locked(
    const uchar *payload, size_t payload_length,
    Mylite_table_definition *definition,
    std::vector<Mylite_overflow_row> *overflow_rows);
static Mylite_overflow_row *mylite_find_overflow_row_locked(
    std::vector<Mylite_overflow_row> *overflow_rows, uint64_t rowid);
static bool mylite_load_index_payloads_locked(
    int fd, std::vector<Mylite_table_definition> *catalog);
static bool mylite_parse_index_payload_locked(
    const std::string &content, Mylite_table_definition *definition,
    Mylite_index_root *root);
static int mylite_flush_catalog_locked();
static bool mylite_write_catalog_locked();
static bool mylite_write_row_payloads_locked(
    int fd, std::vector<Mylite_free_page_range> *allocator);
static bool mylite_write_row_slot_pages_locked(
    int fd, const Mylite_table_definition &definition,
    std::vector<Mylite_free_page_range> *allocator,
    uint64_t *payload_offset, uint64_t *payload_length,
    uint64_t *payload_checksum);
static bool mylite_pack_row_slot_page_payloads_locked(
    const Mylite_table_definition &definition,
    std::vector<std::vector<uchar>> *page_payloads,
    std::string *logical_payload);
static void mylite_append_row_slot_page_payload(
    const std::vector<const Mylite_row *> &rows,
    std::vector<std::vector<uchar>> *page_payloads,
    std::string *logical_payload);
static void mylite_append_row_overflow_page_payloads(
    const Mylite_row &row, std::vector<std::vector<uchar>> *page_payloads,
    std::string *logical_payload);
static bool mylite_write_index_payloads_locked(
    int fd, std::vector<Mylite_free_page_range> *allocator);
static std::string mylite_serialize_index_payload_locked(
    const Mylite_index_root &root);
static std::string mylite_serialize_catalog_with_free_ranges_locked(
    const std::vector<Mylite_free_page_range> &free_ranges);
static std::string mylite_serialize_catalog_locked();
static std::string mylite_serialize_free_page_payload_locked(
    const std::vector<Mylite_free_page_range> &free_ranges);
static bool mylite_parse_catalog_payload_locked(
    const std::string &content, std::vector<Mylite_table_definition> *loaded,
    std::vector<Mylite_free_page_range> *free_ranges);
static bool mylite_parse_free_page_payload_locked(
    const std::string &content,
    std::vector<Mylite_free_page_range> *free_ranges);
static bool mylite_parse_freepage_payload_record_locked(
    const std::string &line, std::vector<Mylite_free_page_range> *free_ranges);
static bool mylite_parse_table_payload_record_locked(
    const std::string &line, std::vector<Mylite_table_definition> *loaded);
static bool mylite_parse_next_rowid_payload_record_locked(
    const std::string &line, std::vector<Mylite_table_definition> *loaded);
static bool mylite_parse_autoincrement_payload_record_locked(
    const std::string &line, std::vector<Mylite_table_definition> *loaded);
static bool mylite_parse_rowpage_payload_record_locked(
    const std::string &line, std::vector<Mylite_table_definition> *loaded);
static bool mylite_parse_indexpage_payload_record_locked(
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
                                         std::vector<Mylite_free_page_range>
                                           *allocator,
                                         uint64_t *payload_offset,
                                         uint64_t *payload_checksum);
static bool mylite_read_free_page_payload(
    int fd, const Mylite_catalog_header &header,
    std::vector<Mylite_free_page_range> *free_ranges);
static bool mylite_write_free_page_payload(
    int fd, const std::vector<Mylite_free_page_range> &free_ranges,
    uint64_t *payload_offset, uint64_t *payload_length,
    uint64_t *payload_checksum);
static bool mylite_read_page_chain(int fd, uint32_t page_type,
                                   uint64_t payload_offset,
                                   uint64_t payload_length,
                                   uint64_t payload_checksum,
                                   std::string *content);
static bool mylite_write_page_chain(int fd, uint32_t page_type,
                                    const std::string &content,
                                    std::vector<Mylite_free_page_range>
                                      *allocator,
                                    uint64_t *payload_offset,
                                    uint64_t *payload_checksum);
static bool mylite_write_page_chain_at(int fd, uint32_t page_type,
                                       const std::string &content,
                                       uint64_t page_id,
                                       uint64_t *payload_offset,
                                       uint64_t *payload_checksum);
static bool mylite_allocate_page_range_locked(
    int fd, uint64_t page_count,
    std::vector<Mylite_free_page_range> *allocator, uint64_t *page_id);
static bool mylite_write_catalog_header(int fd,
                                        const Mylite_catalog_header &header);
static bool mylite_read_page(int fd, uint64_t page_id,
                             std::vector<uchar> *page,
                             Mylite_page_header *header);
static bool mylite_write_page(int fd, uint32_t type, uint64_t page_id,
                              uint64_t next_page_id, const uchar *payload,
                              size_t payload_length);
static bool mylite_page_offset_is_valid(uint64_t offset);
static uint64_t mylite_payload_page_count(uint64_t payload_length);
static bool mylite_add_payload_free_range_locked(
    uint64_t payload_offset, uint64_t payload_length,
    std::vector<Mylite_free_page_range> *ranges);
static bool mylite_add_free_page_range_locked(
    uint64_t page_id, uint64_t page_count,
    std::vector<Mylite_free_page_range> *ranges);
static bool mylite_collect_catalog_payload_ranges_locked(
    const std::vector<Mylite_table_definition> &catalog,
    std::vector<Mylite_free_page_range> *ranges);
static bool mylite_collect_definition_payload_ranges_locked(
    const Mylite_table_definition &definition,
    std::vector<Mylite_free_page_range> *ranges);
static bool mylite_collect_index_payload_ranges_locked(
    const Mylite_table_definition &definition,
    std::vector<Mylite_free_page_range> *ranges);
static bool mylite_normalize_free_page_ranges_locked(
    std::vector<Mylite_free_page_range> *ranges);
static bool mylite_validate_free_page_ranges_locked(
    int fd, const Mylite_catalog_header &header,
    const std::vector<Mylite_table_definition> &catalog,
    std::vector<Mylite_free_page_range> *free_ranges);
static bool mylite_reclaim_orphan_pages_locked(
    int fd, const Mylite_catalog_header &header,
    const std::vector<Mylite_table_definition> &catalog,
    std::vector<Mylite_free_page_range> *free_ranges);
static bool mylite_page_ranges_overlap(
    const Mylite_free_page_range &left,
    const Mylite_free_page_range &right);
static bool mylite_page_id_in_ranges(
    uint64_t page_id, const std::vector<Mylite_free_page_range> &ranges);
static bool mylite_page_range_fits_file(
    const Mylite_free_page_range &range, uint64_t file_size);
static uint64_t mylite_page_offset(uint64_t page_id);
static uint64_t mylite_align_to_page(uint64_t offset);
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
static const char mylite_rows_payload_magic[]= "MYLITE ROWS 1";
static const uchar mylite_row_slot_magic[16]= {
  'M', 'Y', 'L', 'I', 'T', 'E', 'R', 'O',
  'W', 'S', 'L', 'O', 'T', '2', '\0', '\0'
};
static const uchar mylite_row_overflow_magic[16]= {
  'M', 'Y', 'L', 'I', 'T', 'E', 'R', 'O',
  'W', 'O', 'V', 'F', '3', '\0', '\0', '\0'
};
static const uchar mylite_index_payload_magic[16]= {
  'M', 'Y', 'L', 'I', 'T', 'E', 'I', 'N',
  'D', 'E', 'X', 'P', 'G', '1', '\0', '\0'
};
static const uchar mylite_catalog_file_magic[16]= {
  'M', 'Y', 'L', 'I', 'T', 'E', 'F', 'M',
  'T', 'P', 'A', 'G', 'E', '2', '\0', '\0'
};
static const uchar mylite_page_magic[16]= {
  'M', 'Y', 'L', 'I', 'T', 'E', 'P', 'A',
  'G', 'E', 'S', 'T', 'O', 'R', 'E', '\0'
};
static const uint32_t mylite_catalog_format_version_v2= 2;
static const uint32_t mylite_catalog_format_version= 3;
static const uint32_t mylite_page_format_version= 1;
static const uint32_t mylite_page_type_catalog_payload= 1;
static const uint32_t mylite_page_type_row_payload= 2;
static const uint32_t mylite_page_type_index_payload= 3;
static const uint32_t mylite_page_type_free_payload= 4;
static const uint32_t mylite_catalog_page_size= 4096;
static const uint64_t mylite_catalog_payload_start=
  static_cast<uint64_t>(mylite_catalog_page_size) * 2;
static const size_t mylite_catalog_header_checksum_offset= 56;
static const size_t mylite_page_payload_offset= 64;
static const size_t mylite_page_checksum_offset= 56;
static const size_t mylite_page_payload_capacity=
  mylite_catalog_page_size - mylite_page_payload_offset;
static const uint32_t mylite_row_slot_format_version= 2;
static const size_t mylite_row_slot_header_size= 32;
static const size_t mylite_row_slot_entry_size= 16;
static const size_t mylite_row_slot_max_record_length=
  mylite_page_payload_capacity - mylite_row_slot_header_size -
  mylite_row_slot_entry_size;
static const uint32_t mylite_row_overflow_format_version= 3;
static const size_t mylite_row_overflow_header_size= 52;
static const size_t mylite_row_overflow_segment_capacity=
  mylite_page_payload_capacity - mylite_row_overflow_header_size;
static const uint32_t mylite_index_payload_format_version= 1;
static const size_t mylite_index_payload_header_size= 36;
static const char mylite_free_page_payload_magic[]= "MYLITE FREE LIST 1";
static const uint64_t mylite_fnv1a_offset_basis= 14695981039346656037ULL;
static const uint64_t mylite_fnv1a_prime= 1099511628211ULL;
static std::mutex mylite_catalog_mutex;
static bool mylite_catalog_loaded= false;
static bool mylite_catalog_load_failed= false;
static bool mylite_loaded_catalog_header_valid= false;
static int mylite_catalog_fd= -1;
static thread_local int mylite_catalog_last_error= 0;
static THD *mylite_dirty_writer_thd= nullptr;
static std::string mylite_catalog_locked_path;
static Mylite_catalog_header mylite_loaded_catalog_header=
  { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static std::vector<Mylite_free_page_range> mylite_free_page_ranges;
static std::vector<Mylite_free_page_range> mylite_pending_free_page_ranges;
static std::vector<Mylite_table_definition> mylite_catalog= {
  { mylite_seed_db, mylite_seed_table, mylite_seed_sql, std::vector<uchar>(),
    1, 0, 0, 0, 0, false, std::vector<Mylite_row>(),
    std::vector<Mylite_index_root>() }
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
  mylite_hton->commit= mylite_commit;
  mylite_hton->rollback= mylite_rollback;
  mylite_hton->savepoint_offset= sizeof(uint64_t);
  mylite_hton->savepoint_set= mylite_savepoint_set;
  mylite_hton->savepoint_rollback= mylite_savepoint_rollback;
  mylite_hton->savepoint_release= mylite_savepoint_release;
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

static int mylite_commit(THD *thd, bool all)
{
  DBUG_ENTER("mylite_commit");
  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  Mylite_transaction_context *context=
    mylite_get_transaction_context(thd, false);
  if (!context)
    DBUG_RETURN(0);

  if (!context->dirty)
  {
    if (!all && mylite_thd_in_explicit_transaction(thd) &&
        !context->savepoints.empty())
      DBUG_RETURN(0);
    mylite_clear_transaction_context_locked(thd, context);
    DBUG_RETURN(0);
  }

  if (!all && mylite_thd_in_explicit_transaction(thd))
  {
    mylite_clear_snapshot(&context->statement);
    DBUG_RETURN(0);
  }

  const int error= mylite_flush_catalog_locked();
  if (error)
    DBUG_RETURN(error);

  mylite_clear_transaction_context_locked(thd, context);
  DBUG_RETURN(0);
}

static int mylite_rollback(THD *thd, bool all)
{
  DBUG_ENTER("mylite_rollback");
  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  Mylite_transaction_context *context=
    mylite_get_transaction_context(thd, false);
  if (!context)
    DBUG_RETURN(0);

  if (all)
  {
    if (context->transaction.active)
      mylite_restore_snapshot_locked(context->transaction);
    else if (context->statement.active)
      mylite_restore_snapshot_locked(context->statement);
    mylite_clear_transaction_context_locked(thd, context);
    DBUG_RETURN(0);
  }

  if (context->statement.active)
    mylite_restore_snapshot_locked(context->statement);
  mylite_clear_snapshot(&context->statement);
  if (!mylite_thd_in_explicit_transaction(thd))
    mylite_clear_transaction_context_locked(thd, context);
  DBUG_RETURN(0);
}

static int mylite_savepoint_set(THD *thd, void *sv)
{
  DBUG_ENTER("mylite_savepoint_set");
  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  Mylite_transaction_context *context=
    mylite_get_transaction_context(thd, true);
  if (!context)
    DBUG_RETURN(HA_ERR_OUT_OF_MEM);

  Mylite_savepoint_snapshot savepoint;
  savepoint.id= ++context->next_savepoint_id;
  if (!mylite_capture_snapshot_locked(&savepoint.snapshot))
    DBUG_RETURN(HA_ERR_OUT_OF_MEM);

  try
  {
    context->savepoints.push_back(savepoint);
  }
  catch (const std::bad_alloc &)
  {
    mylite_clear_snapshot(&savepoint.snapshot);
    DBUG_RETURN(HA_ERR_OUT_OF_MEM);
  }

  mylite_store_savepoint_id(sv, savepoint.id);
  DBUG_RETURN(0);
}

static int mylite_savepoint_rollback(THD *thd, void *sv)
{
  DBUG_ENTER("mylite_savepoint_rollback");
  const uint64_t id= mylite_load_savepoint_id(sv);
  if (id == 0)
    DBUG_RETURN(0);

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  Mylite_transaction_context *context=
    mylite_get_transaction_context(thd, false);
  if (!context)
    DBUG_RETURN(HA_ERR_NO_SAVEPOINT);

  Mylite_savepoint_snapshot *savepoint=
    mylite_find_savepoint_snapshot(context, id);
  if (!savepoint)
    DBUG_RETURN(HA_ERR_NO_SAVEPOINT);

  mylite_restore_snapshot_locked(savepoint->snapshot);
  mylite_discard_savepoints_newer_than(context, id);
  context->dirty= true;
  mylite_dirty_writer_thd= thd;
  DBUG_RETURN(0);
}

static int mylite_savepoint_release(THD *thd, void *sv)
{
  DBUG_ENTER("mylite_savepoint_release");
  const uint64_t id= mylite_load_savepoint_id(sv);
  if (id == 0)
    DBUG_RETURN(0);

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  Mylite_transaction_context *context=
    mylite_get_transaction_context(thd, false);
  if (!context)
    DBUG_RETURN(HA_ERR_NO_SAVEPOINT);

  mylite_discard_savepoints_from(context, id);
  DBUG_RETURN(0);
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
                                    opened_table_name.length(), ha_thd(),
                                    table, buf, &duplicate_key);
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
                                     opened_table_name.length(), ha_thd(),
                                     table, current_rowid, new_data,
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
                                     opened_table_name.length(), ha_thd(),
                                     table, current_rowid);
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
        opened_table_name.length(), ha_thd(), table, offset, increment,
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

int ha_mylite::external_lock(THD *thd, int lock_type)
{
  DBUG_ENTER("ha_mylite::external_lock");
  if (lock_type != F_UNLCK)
  {
    const int error= mylite_register_transaction(thd, lock_type == F_WRLCK);
    if (error)
      DBUG_RETURN(error);

    std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
    DBUG_RETURN(mylite_check_transaction_access_locked(thd));
  }
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
    return mylite_catalog_error_code();

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
  definition.rows_payload_offset= 0;
  definition.rows_payload_length= 0;
  definition.rows_payload_checksum= 0;
  definition.rows_payload_ref_seen= false;
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
    return mylite_catalog_error_code();

  for (std::vector<Mylite_table_definition>::iterator it=
         mylite_catalog.begin(); it != mylite_catalog.end(); ++it)
  {
    if (it->db == db && it->table_name == table_name)
    {
      const std::vector<Mylite_table_definition> before= mylite_catalog;
      const std::vector<Mylite_free_page_range> pending_before=
        mylite_pending_free_page_ranges;
      if (!mylite_collect_definition_payload_ranges_locked(
            *it, &mylite_pending_free_page_ranges))
        return HA_ERR_CRASHED;
      mylite_catalog.erase(it);
      const int error= mylite_flush_catalog_locked();
      if (error)
      {
        mylite_catalog= before;
        mylite_pending_free_page_ranges= pending_before;
      }
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
    return mylite_catalog_error_code();

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
                            size_t table_name_length, THD *thd,
                            TABLE *table, const uchar *record,
                            uint *duplicate_key)
{
  if (!record || !mylite_table_supports_row_storage(table) ||
      !mylite_table_supports_key_storage(table))
    return HA_ERR_UNSUPPORTED;

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return mylite_catalog_error_code();

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

  int error= mylite_prepare_dml_mutation_locked(thd);
  if (error)
    return error;

  const std::vector<Mylite_table_definition> before= mylite_catalog;
  const std::vector<Mylite_free_page_range> pending_before=
    mylite_pending_free_page_ranges;
  if (!mylite_collect_index_payload_ranges_locked(
        *definition, &mylite_pending_free_page_ranges))
    return HA_ERR_CRASHED;
  if (!mylite_advance_auto_increment_locked(definition, table, record))
  {
    mylite_pending_free_page_ranges= pending_before;
    return HA_ERR_RECORD_FILE_FULL;
  }

  Mylite_row row;
  row.rowid= definition->next_rowid++;
  row.deleted= false;
  row.record.assign(record, record + table->s->reclength);
  definition->rows.push_back(row);
  if (!mylite_refresh_index_roots_locked(definition, table))
  {
    mylite_catalog= before;
    mylite_pending_free_page_ranges= pending_before;
    return HA_ERR_CRASHED;
  }

  error= mylite_flush_catalog_for_thd_locked(thd);
  if (error)
  {
    mylite_catalog= before;
    mylite_pending_free_page_ranges= pending_before;
  }
  return error;
}

static int mylite_update_row(const char *db, size_t db_length,
                             const char *table_name,
                             size_t table_name_length, THD *thd,
                             TABLE *table, uint64_t rowid, const uchar *record,
                             uint *duplicate_key)
{
  if (!record || rowid == 0 || !mylite_table_supports_row_storage(table) ||
      !mylite_table_supports_key_storage(table))
    return HA_ERR_UNSUPPORTED;

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return mylite_catalog_error_code();

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

  int error= mylite_prepare_dml_mutation_locked(thd);
  if (error)
    return error;

  const std::vector<Mylite_table_definition> before= mylite_catalog;
  const std::vector<Mylite_free_page_range> pending_before=
    mylite_pending_free_page_ranges;
  if (!mylite_collect_index_payload_ranges_locked(
        *definition, &mylite_pending_free_page_ranges))
    return HA_ERR_CRASHED;
  if (!mylite_advance_auto_increment_locked(definition, table, record))
  {
    mylite_pending_free_page_ranges= pending_before;
    return HA_ERR_RECORD_FILE_FULL;
  }
  row->record.assign(record, record + table->s->reclength);
  if (!mylite_refresh_index_roots_locked(definition, table))
  {
    mylite_catalog= before;
    mylite_pending_free_page_ranges= pending_before;
    return HA_ERR_CRASHED;
  }
  error= mylite_flush_catalog_for_thd_locked(thd);
  if (error)
  {
    mylite_catalog= before;
    mylite_pending_free_page_ranges= pending_before;
  }
  return error;
}

static int mylite_delete_row(const char *db, size_t db_length,
                             const char *table_name,
                             size_t table_name_length, THD *thd,
                             TABLE *table, uint64_t rowid)
{
  if (rowid == 0)
    return HA_ERR_KEY_NOT_FOUND;
  if (!mylite_table_supports_key_storage(table))
    return HA_ERR_UNSUPPORTED;

  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return mylite_catalog_error_code();

  Mylite_table_definition *definition=
    mylite_find_table_definition_locked(db, db_length, table_name,
                                        table_name_length);
  if (!definition)
    return HA_ERR_NO_SUCH_TABLE;

  Mylite_row *row= mylite_find_row_locked(definition, rowid);
  if (!row)
    return HA_ERR_KEY_NOT_FOUND;

  int error= mylite_prepare_dml_mutation_locked(thd);
  if (error)
    return error;

  const std::vector<Mylite_table_definition> before= mylite_catalog;
  const std::vector<Mylite_free_page_range> pending_before=
    mylite_pending_free_page_ranges;
  if (!mylite_collect_index_payload_ranges_locked(
        *definition, &mylite_pending_free_page_ranges))
    return HA_ERR_CRASHED;
  row->deleted= true;
  if (!mylite_refresh_index_roots_locked(definition, table))
  {
    mylite_catalog= before;
    mylite_pending_free_page_ranges= pending_before;
    return HA_ERR_CRASHED;
  }
  error= mylite_flush_catalog_for_thd_locked(thd);
  if (error)
  {
    mylite_catalog= before;
    mylite_pending_free_page_ranges= pending_before;
  }
  return error;
}

static int mylite_read_row(const char *db, size_t db_length,
                           const char *table_name, size_t table_name_length,
                           const TABLE *table, size_t *scan_index,
                           uint64_t *rowid, uchar *record)
{
  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  if (!mylite_ensure_catalog_loaded_locked())
    return mylite_catalog_error_code();

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
    return mylite_catalog_error_code();

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
    return mylite_catalog_error_code();

  Mylite_table_definition *definition=
    mylite_find_table_definition_locked(db, db_length, table_name,
                                        table_name_length);
  if (!definition)
    return HA_ERR_NO_SUCH_TABLE;

  const Mylite_index_root *root=
    mylite_find_index_root_locked(definition, key_index);
  if (root && root->key_length == table->key_info[key_index].key_length)
  {
    if (!mylite_index_root_matches_table_locked(definition, *root, table,
                                                key_index))
      return HA_ERR_CRASHED;
    *entries= root->entries;
    return 0;
  }

  return mylite_build_index_entries_from_rows_locked(definition, table,
                                                    key_index, entries);
}

static int mylite_build_index_entries_from_rows_locked(
    Mylite_table_definition *definition, TABLE *table, uint key_index,
    std::vector<Mylite_index_entry> *entries)
{
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
    size_t table_name_length, THD *thd, const TABLE *table, ulonglong offset,
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

  if (mylite_prepare_dml_mutation_locked(thd))
    return false;

  const std::vector<Mylite_table_definition> before= mylite_catalog;
  definition->auto_increment_next= next;
  const int error= mylite_flush_catalog_for_thd_locked(thd);
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
    return mylite_catalog_error_code();

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

static bool mylite_refresh_index_roots_locked(
    Mylite_table_definition *definition, TABLE *table)
{
  if (!table || !table->s)
    return false;

  definition->index_roots.clear();
  for (uint key_index= 0; key_index < table->s->keys; ++key_index)
  {
    if (!mylite_key_supports_storage(table->key_info[key_index]))
      return false;

    Mylite_index_root root;
    root.key_index= key_index;
    root.key_length= table->key_info[key_index].key_length;
    root.payload_offset= 0;
    root.payload_length= 0;
    root.payload_checksum= 0;
    root.payload_ref_seen= false;
    const int error=
      mylite_build_index_entries_from_rows_locked(definition, table,
                                                  key_index, &root.entries);
    if (error)
      return false;
    if (!root.entries.empty())
      definition->index_roots.push_back(root);
  }

  return true;
}

static bool mylite_index_root_matches_table_locked(
    Mylite_table_definition *definition, const Mylite_index_root &root,
    TABLE *table, uint key_index)
{
  if (!table || !table->s || key_index >= table->s->keys ||
      root.key_index != key_index ||
      root.key_length != table->key_info[key_index].key_length)
    return false;

  KEY *key_info= table->key_info + key_index;
  const Mylite_index_entry *previous= nullptr;
  for (const Mylite_index_entry &entry : root.entries)
  {
    if (entry.rowid == 0 || entry.key.size() != root.key_length ||
        !mylite_find_row_locked(definition, entry.rowid))
      return false;

    if (previous)
    {
      const int cmp= key_tuple_cmp(key_info->key_part,
                                   previous->key.data(), entry.key.data(),
                                   key_info->key_length);
      if (cmp > 0 || (cmp == 0 && previous->rowid >= entry.rowid))
        return false;
    }
    previous= &entry;
  }

  return true;
}

static Mylite_index_root *mylite_find_index_root_locked(
    Mylite_table_definition *definition, uint32_t key_index)
{
  for (Mylite_index_root &root : definition->index_roots)
  {
    if (root.key_index == key_index)
      return &root;
  }
  return nullptr;
}

static const Mylite_index_root *mylite_find_index_root_locked(
    const Mylite_table_definition *definition, uint32_t key_index)
{
  for (const Mylite_index_root &root : definition->index_roots)
  {
    if (root.key_index == key_index)
      return &root;
  }
  return nullptr;
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
  {
    if (mylite_catalog_load_failed)
      mylite_set_catalog_error(HA_ERR_CRASHED);
    return !mylite_catalog_load_failed;
  }

  mylite_clear_catalog_error();

  if (!mylite_catalog_file || !mylite_catalog_file[0])
  {
    mylite_catalog_loaded= true;
    return true;
  }

  if (!mylite_ensure_catalog_file_locked())
    return false;

  mylite_catalog_load_failed= !mylite_load_catalog_locked();
  if (mylite_catalog_load_failed && !mylite_catalog_last_error)
    mylite_set_catalog_error(HA_ERR_CRASHED);
  mylite_catalog_loaded= true;
  return !mylite_catalog_load_failed;
}

static int mylite_register_transaction(THD *thd, bool write)
{
  if (!thd || !mylite_hton)
    return HA_ERR_INTERNAL_ERROR;

  trans_register_ha(thd, false, mylite_hton, 0);
  if (write && mylite_thd_in_explicit_transaction(thd))
    trans_register_ha(thd, true, mylite_hton, 0);
  return 0;
}

static int mylite_prepare_dml_mutation_locked(THD *thd)
{
  if (!mylite_ensure_catalog_loaded_locked())
    return mylite_catalog_error_code();

  int error= mylite_register_transaction(thd, true);
  if (error)
    return error;

  error= mylite_check_transaction_access_locked(thd);
  if (error)
    return error;

  Mylite_transaction_context *context=
    mylite_get_transaction_context(thd, true);
  if (!context)
    return HA_ERR_OUT_OF_MEM;

  if (!context->statement.active &&
      !mylite_capture_snapshot_locked(&context->statement))
  {
    mylite_clear_transaction_context_locked(thd, context);
    return HA_ERR_OUT_OF_MEM;
  }

  if (mylite_thd_in_explicit_transaction(thd) &&
      !context->transaction.active &&
      !mylite_capture_snapshot_locked(&context->transaction))
  {
    mylite_clear_transaction_context_locked(thd, context);
    return HA_ERR_OUT_OF_MEM;
  }

  context->dirty= true;
  mylite_dirty_writer_thd= thd;
  return 0;
}

static int mylite_check_transaction_access_locked(THD *thd)
{
  if (!mylite_dirty_writer_thd || mylite_dirty_writer_thd == thd)
    return 0;
  return HA_ERR_LOCK_WAIT_TIMEOUT;
}

static int mylite_flush_catalog_for_thd_locked(THD *thd)
{
  Mylite_transaction_context *context=
    mylite_get_transaction_context(thd, false);
  if (context && context->dirty)
    return 0;
  return mylite_flush_catalog_locked();
}

static Mylite_transaction_context *mylite_get_transaction_context(
    THD *thd, bool create)
{
  if (!thd || !mylite_hton)
    return nullptr;

  Mylite_transaction_context *context=
    static_cast<Mylite_transaction_context *>(
      thd_get_ha_data(thd, mylite_hton));
  if (context || !create)
    return context;

  context= new (std::nothrow) Mylite_transaction_context();
  if (!context)
    return nullptr;
  thd_set_ha_data(thd, mylite_hton, context);
  return context;
}

static void mylite_clear_transaction_context_locked(
    THD *thd, Mylite_transaction_context *context)
{
  if (mylite_dirty_writer_thd == thd)
    mylite_dirty_writer_thd= nullptr;
  mylite_clear_snapshot(&context->statement);
  mylite_clear_snapshot(&context->transaction);
  thd_set_ha_data(thd, mylite_hton, nullptr);
  delete context;
}

static bool mylite_capture_snapshot_locked(
    Mylite_transaction_snapshot *snapshot)
{
  try
  {
    snapshot->catalog= mylite_catalog;
    snapshot->pending_free_page_ranges= mylite_pending_free_page_ranges;
    snapshot->active= true;
  }
  catch (const std::bad_alloc &)
  {
    mylite_clear_snapshot(snapshot);
    return false;
  }
  return true;
}

static void mylite_restore_snapshot_locked(
    const Mylite_transaction_snapshot &snapshot)
{
  if (!snapshot.active)
    return;
  mylite_catalog= snapshot.catalog;
  mylite_pending_free_page_ranges= snapshot.pending_free_page_ranges;
}

static void mylite_clear_snapshot(Mylite_transaction_snapshot *snapshot)
{
  snapshot->active= false;
  snapshot->catalog.clear();
  snapshot->pending_free_page_ranges.clear();
}

static void mylite_store_savepoint_id(void *sv, uint64_t id)
{
  std::memcpy(sv, &id, sizeof(id));
}

static uint64_t mylite_load_savepoint_id(const void *sv)
{
  uint64_t id= 0;
  std::memcpy(&id, sv, sizeof(id));
  return id;
}

static Mylite_savepoint_snapshot *mylite_find_savepoint_snapshot(
    Mylite_transaction_context *context, uint64_t id)
{
  for (Mylite_savepoint_snapshot &savepoint : context->savepoints)
    if (savepoint.id == id)
      return &savepoint;
  return nullptr;
}

static void mylite_discard_savepoints_newer_than(
    Mylite_transaction_context *context, uint64_t id)
{
  std::vector<Mylite_savepoint_snapshot>::iterator first=
    context->savepoints.begin();
  for (; first != context->savepoints.end(); ++first)
    if (first->id > id)
      break;
  context->savepoints.erase(first, context->savepoints.end());
}

static void mylite_discard_savepoints_from(Mylite_transaction_context *context,
                                           uint64_t id)
{
  std::vector<Mylite_savepoint_snapshot>::iterator first=
    context->savepoints.begin();
  for (; first != context->savepoints.end(); ++first)
    if (first->id >= id)
      break;
  context->savepoints.erase(first, context->savepoints.end());
}

static bool mylite_thd_in_explicit_transaction(THD *thd)
{
  return thd && thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN);
}

static void mylite_clear_catalog_error()
{
  mylite_catalog_last_error= 0;
}

static void mylite_set_catalog_error(int error)
{
  mylite_catalog_last_error= error > 0 ? error : HA_ERR_INTERNAL_ERROR;
}

static void mylite_set_catalog_errno_error()
{
  mylite_set_catalog_error(errno > 0 ? errno : HA_ERR_INTERNAL_ERROR);
}

static int mylite_catalog_error_code()
{
  return mylite_catalog_last_error ? mylite_catalog_last_error :
                                    HA_ERR_CRASHED;
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

static bool mylite_ensure_catalog_file_locked()
{
  const std::string path(mylite_catalog_file ? mylite_catalog_file : "");
  if (path.empty())
    return true;

  if (mylite_catalog_fd >= 0)
  {
    if (mylite_catalog_locked_path == path)
      return true;

    sql_print_error("MyLite: catalog path changed from %s to %s",
                    mylite_catalog_locked_path.c_str(), path.c_str());
    mylite_set_catalog_error(HA_ERR_INTERNAL_ERROR);
    return false;
  }

  const int fd= open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
  if (fd < 0)
  {
    mylite_set_catalog_errno_error();
    mylite_log_catalog_error("open", path);
    return false;
  }

  if (my_lock(fd, F_WRLCK, 0, F_TO_EOF,
              MYF(MY_FORCE_LOCK | MY_NO_WAIT)) != 0)
  {
    const int saved_errno= my_errno > 0 ? my_errno :
                           errno > 0 ? errno : EAGAIN;
    mylite_set_catalog_error(saved_errno == EAGAIN ?
                             HA_ERR_LOCK_WAIT_TIMEOUT : saved_errno);
    sql_print_error("MyLite: catalog lock failed for %s: %s",
                    path.c_str(), strerror(saved_errno));
    if (close(fd) != 0)
      mylite_log_catalog_error("close", path);
    return false;
  }

  mylite_catalog_fd= fd;
  mylite_catalog_locked_path= path;
  return true;
}

static bool mylite_load_catalog_locked()
{
  mylite_clear_frm_definitions_locked();
  mylite_free_page_ranges.clear();
  mylite_pending_free_page_ranges.clear();
  mylite_loaded_catalog_header_valid= false;
  mylite_loaded_catalog_header= { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

  const int fd= mylite_catalog_fd;
  if (fd < 0)
  {
    mylite_set_catalog_error(HA_ERR_INTERNAL_ERROR);
    sql_print_error("MyLite: catalog file is not locked for %s",
                    mylite_catalog_file);
    return false;
  }

  bool ok= true;
  struct stat st;
  if (fstat(fd, &st) != 0)
  {
    mylite_set_catalog_errno_error();
    mylite_log_catalog_error("stat", mylite_catalog_file);
    ok= false;
    goto done;
  }
  if (st.st_size == 0)
    goto done;

  {
    std::vector<Mylite_catalog_header> headers;
    for (size_t slot= 0; slot < 2; ++slot)
    {
      Mylite_catalog_header header= { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
      if (mylite_read_catalog_header(fd, slot, st.st_size, &header))
        headers.push_back(header);
    }
    std::sort(headers.begin(), headers.end(),
              [](const Mylite_catalog_header &left,
                 const Mylite_catalog_header &right) {
                return left.generation > right.generation;
              });

    bool found= false;
    for (const Mylite_catalog_header &header : headers)
    {
      std::vector<Mylite_table_definition> loaded;
      std::vector<Mylite_free_page_range> free_ranges;
      if (!mylite_load_catalog_generation_locked(fd, header, &loaded,
                                                 &free_ranges))
        continue;

      mylite_catalog.swap(loaded);
      mylite_free_page_ranges.swap(free_ranges);
      mylite_pending_free_page_ranges.clear();
      mylite_loaded_catalog_header= header;
      mylite_loaded_catalog_header_valid= true;
      found= true;
      break;
    }
    if (!found)
    {
      mylite_set_catalog_error(HA_ERR_CRASHED);
      sql_print_error("MyLite: no valid catalog generation in %s",
                      mylite_catalog_file);
      ok= false;
    }
  }

done:
  return ok;
}

static bool mylite_load_catalog_generation_locked(
    int fd, const Mylite_catalog_header &header,
    std::vector<Mylite_table_definition> *loaded,
    std::vector<Mylite_free_page_range> *free_ranges)
{
  std::string content;
  if (!mylite_read_catalog_payload(fd, header, &content) ||
      !mylite_parse_catalog_payload_locked(content, loaded, free_ranges))
    return false;

  if (header.format_version >= mylite_catalog_format_version)
  {
    if (!free_ranges->empty() ||
        !mylite_read_free_page_payload(fd, header, free_ranges))
      return false;
  }

  return mylite_load_row_payloads_locked(fd, loaded) &&
         mylite_load_index_payloads_locked(fd, loaded) &&
         mylite_validate_free_page_ranges_locked(fd, header, *loaded,
                                                 free_ranges) &&
         mylite_reclaim_orphan_pages_locked(fd, header, *loaded, free_ranges);
}

static bool mylite_load_row_payloads_locked(
    int fd, std::vector<Mylite_table_definition> *catalog)
{
  for (Mylite_table_definition &definition : *catalog)
  {
    if (definition.rows_payload_length == 0)
    {
      if (definition.rows_payload_offset != 0 ||
          definition.rows_payload_checksum != 0)
        return false;
      continue;
    }

    if (!definition.rows.empty())
      return false;

    if (!mylite_load_row_payload_locked(fd, &definition))
      return false;
  }

  return true;
}

static bool mylite_load_row_payload_locked(
    int fd, Mylite_table_definition *definition)
{
  std::vector<uchar> page;
  Mylite_page_header page_header;
  if (!mylite_read_page(fd,
                        definition->rows_payload_offset /
                          mylite_catalog_page_size,
                        &page, &page_header) ||
      page_header.type != mylite_page_type_row_payload)
    return false;

  const uchar *payload= page.data() + mylite_page_payload_offset;
  const size_t legacy_magic_length= sizeof(mylite_rows_payload_magic) - 1;
  if (page_header.payload_length >= legacy_magic_length &&
      std::memcmp(payload, mylite_rows_payload_magic,
                  legacy_magic_length) == 0)
  {
    std::string content;
    return mylite_read_page_chain(
             fd, mylite_page_type_row_payload,
             definition->rows_payload_offset,
             definition->rows_payload_length,
             definition->rows_payload_checksum, &content) &&
           mylite_parse_legacy_rows_payload_locked(content, definition);
  }

  if (page_header.payload_length >= sizeof(mylite_row_slot_magic) &&
      std::memcmp(payload, mylite_row_slot_magic,
                  sizeof(mylite_row_slot_magic)) == 0)
    return mylite_parse_row_payload_pages_locked(fd, definition);

  if (page_header.payload_length >= sizeof(mylite_row_overflow_magic) &&
      std::memcmp(payload, mylite_row_overflow_magic,
                  sizeof(mylite_row_overflow_magic)) == 0)
    return mylite_parse_row_payload_pages_locked(fd, definition);

  sql_print_error("MyLite: invalid row payload in %s",
                  mylite_catalog_file);
  return false;
}

static bool mylite_parse_legacy_rows_payload_locked(
    const std::string &content, Mylite_table_definition *definition)
{
  std::istringstream input(content);
  std::string line;
  if (!std::getline(input, line) || line != mylite_rows_payload_magic)
  {
    sql_print_error("MyLite: invalid row payload in %s",
                    mylite_catalog_file);
    return false;
  }

  bool found_row= false;
  while (std::getline(input, line))
  {
    if (line.empty())
      continue;

    const std::string::size_type first= line.find('\t');
    const std::string::size_type second= first == std::string::npos
      ? std::string::npos
      : line.find('\t', first + 1);

    if (first == std::string::npos || second == std::string::npos ||
        line.substr(0, first) != "ROW")
    {
      sql_print_error("MyLite: invalid row payload record in %s",
                      mylite_catalog_file);
      return false;
    }

    Mylite_row row;
    if (!mylite_parse_decimal_uint64(
          line.substr(first + 1, second - first - 1), &row.rowid) ||
        !mylite_hex_decode(line.substr(second + 1), &row.record) ||
        row.rowid == 0 || row.record.empty() ||
        mylite_find_row_locked(definition, row.rowid))
    {
      sql_print_error("MyLite: invalid row payload encoding in %s",
                      mylite_catalog_file);
      return false;
    }

    row.deleted= false;
    if (row.rowid == ~static_cast<uint64_t>(0))
    {
      sql_print_error("MyLite: row payload id is exhausted in %s",
                      mylite_catalog_file);
      return false;
    }
    if (row.rowid >= definition->next_rowid)
      definition->next_rowid= row.rowid + 1;
    definition->rows.push_back(row);
    found_row= true;
  }

  if (!found_row)
  {
    sql_print_error("MyLite: empty row payload in %s",
                    mylite_catalog_file);
    return false;
  }

  return true;
}

static bool mylite_parse_row_payload_pages_locked(
    int fd, Mylite_table_definition *definition)
{
  std::string logical_payload;
  std::vector<Mylite_overflow_row> overflow_rows;
  if (definition->rows_payload_length > logical_payload.max_size())
    return false;
  logical_payload.reserve(static_cast<size_t>(
    definition->rows_payload_length));

  uint64_t page_id= definition->rows_payload_offset /
                    mylite_catalog_page_size;
  uint64_t remaining= definition->rows_payload_length;
  for (uint64_t page_count= 0; remaining > 0; ++page_count)
  {
    if (page_count >= definition->rows_payload_length || page_id < 2)
      return false;

    std::vector<uchar> page;
    Mylite_page_header page_header;
    if (!mylite_read_page(fd, page_id, &page, &page_header) ||
        page_header.type != mylite_page_type_row_payload ||
        page_header.payload_length > remaining ||
        page_header.payload_length > mylite_page_payload_capacity)
      return false;

    const uchar *payload= page.data() + mylite_page_payload_offset;
    logical_payload.append(reinterpret_cast<const char *>(payload),
                           page_header.payload_length);
    if (page_header.payload_length >= sizeof(mylite_row_slot_magic) &&
        std::memcmp(payload, mylite_row_slot_magic,
                    sizeof(mylite_row_slot_magic)) == 0)
    {
      if (!mylite_parse_row_slot_page_payload_locked(
            payload, page_header.payload_length, definition, overflow_rows))
        return false;
    }
    else if (page_header.payload_length >= sizeof(mylite_row_overflow_magic) &&
             std::memcmp(payload, mylite_row_overflow_magic,
                         sizeof(mylite_row_overflow_magic)) == 0)
    {
      if (!mylite_parse_row_overflow_page_payload_locked(
            payload, page_header.payload_length, definition, &overflow_rows))
        return false;
    }
    else
      return false;

    remaining-= page_header.payload_length;
    if (remaining == 0)
    {
      if (page_header.next_page_id != 0)
        return false;
      break;
    }
    if (page_header.next_page_id != page_id + 1)
      return false;
    page_id= page_header.next_page_id;
  }

  if (logical_payload.length() != definition->rows_payload_length)
    return false;

  return overflow_rows.empty() &&
         mylite_checksum(
           reinterpret_cast<const uchar *>(logical_payload.data()),
           logical_payload.length()) == definition->rows_payload_checksum;
}

static bool mylite_parse_row_slot_page_payload_locked(
    const uchar *payload, size_t payload_length,
    Mylite_table_definition *definition,
    const std::vector<Mylite_overflow_row> &overflow_rows)
{
  if (!overflow_rows.empty())
  {
    sql_print_error("MyLite: interrupted row overflow payload in %s",
                    mylite_catalog_file);
    return false;
  }

  if (payload_length < mylite_row_slot_header_size ||
      std::memcmp(payload, mylite_row_slot_magic,
                  sizeof(mylite_row_slot_magic)) != 0 ||
      mylite_read_le32(payload + 16) != mylite_row_slot_format_version)
  {
    sql_print_error("MyLite: invalid row slot page in %s",
                    mylite_catalog_file);
    return false;
  }

  const uint32_t row_count= mylite_read_le32(payload + 20);
  const uint32_t data_offset= mylite_read_le32(payload + 24);
  const size_t expected_data_offset=
    mylite_row_slot_header_size +
    static_cast<size_t>(row_count) * mylite_row_slot_entry_size;
  if (row_count == 0 || data_offset != expected_data_offset ||
      data_offset > payload_length)
  {
    sql_print_error("MyLite: invalid row slot directory in %s",
                    mylite_catalog_file);
    return false;
  }

  size_t previous_end= data_offset;
  for (uint32_t i= 0; i < row_count; ++i)
  {
    const size_t slot_offset= mylite_row_slot_header_size +
                              static_cast<size_t>(i) *
                                mylite_row_slot_entry_size;
    const uint64_t rowid= mylite_read_le64(payload + slot_offset);
    const uint32_t record_offset=
      mylite_read_le32(payload + slot_offset + 8);
    const uint32_t record_length=
      mylite_read_le32(payload + slot_offset + 12);
    if (rowid == 0 || rowid == ~static_cast<uint64_t>(0) ||
        record_length == 0 ||
        record_length > mylite_row_slot_max_record_length ||
        record_offset != previous_end ||
        record_offset > payload_length ||
        record_length > payload_length - record_offset ||
        mylite_find_row_locked(definition, rowid))
    {
      sql_print_error("MyLite: invalid row slot entry in %s",
                      mylite_catalog_file);
      return false;
    }

    Mylite_row row;
    row.rowid= rowid;
    row.deleted= false;
    row.record.assign(payload + record_offset,
                      payload + record_offset + record_length);
    if (row.rowid >= definition->next_rowid)
      definition->next_rowid= row.rowid + 1;
    definition->rows.push_back(row);
    previous_end= record_offset + record_length;
  }

  if (previous_end != payload_length)
  {
    sql_print_error("MyLite: invalid row slot payload length in %s",
                    mylite_catalog_file);
    return false;
  }

  return true;
}

static bool mylite_parse_row_overflow_page_payload_locked(
    const uchar *payload, size_t payload_length,
    Mylite_table_definition *definition,
    std::vector<Mylite_overflow_row> *overflow_rows)
{
  if (payload_length < mylite_row_overflow_header_size ||
      std::memcmp(payload, mylite_row_overflow_magic,
                  sizeof(mylite_row_overflow_magic)) != 0 ||
      mylite_read_le32(payload + 16) != mylite_row_overflow_format_version)
  {
    sql_print_error("MyLite: invalid row overflow page in %s",
                    mylite_catalog_file);
    return false;
  }

  const uint64_t rowid= mylite_read_le64(payload + 20);
  const uint64_t total_length= mylite_read_le64(payload + 28);
  const uint64_t segment_offset= mylite_read_le64(payload + 36);
  const uint32_t segment_length= mylite_read_le32(payload + 44);
  const uint32_t reserved= mylite_read_le32(payload + 48);
  if (rowid == 0 || rowid == ~static_cast<uint64_t>(0) ||
      total_length == 0 ||
      total_length <= mylite_row_slot_max_record_length ||
      segment_length == 0 ||
      segment_length > mylite_row_overflow_segment_capacity ||
      payload_length != mylite_row_overflow_header_size + segment_length ||
      segment_offset > total_length ||
      segment_length > total_length - segment_offset ||
      reserved != 0 ||
      mylite_find_row_locked(definition, rowid))
  {
    sql_print_error("MyLite: invalid row overflow segment in %s",
                    mylite_catalog_file);
    return false;
  }

  Mylite_overflow_row *overflow_row=
    mylite_find_overflow_row_locked(overflow_rows, rowid);
  if (!overflow_row)
  {
    if (segment_offset != 0 || !overflow_rows->empty())
    {
      sql_print_error("MyLite: invalid row overflow start in %s",
                      mylite_catalog_file);
      return false;
    }

    Mylite_overflow_row row;
    row.rowid= rowid;
    row.total_length= total_length;
    overflow_rows->push_back(row);
    overflow_row= &overflow_rows->back();
  }
  else if (overflow_row->total_length != total_length ||
           segment_offset != overflow_row->record.size())
  {
    sql_print_error("MyLite: invalid row overflow order in %s",
                    mylite_catalog_file);
    return false;
  }

  const uchar *segment= payload + mylite_row_overflow_header_size;
  overflow_row->record.insert(overflow_row->record.end(), segment,
                              segment + segment_length);
  if (overflow_row->record.size() == overflow_row->total_length)
  {
    Mylite_row row;
    row.rowid= overflow_row->rowid;
    row.deleted= false;
    row.record.swap(overflow_row->record);
    if (row.rowid >= definition->next_rowid)
      definition->next_rowid= row.rowid + 1;
    definition->rows.push_back(row);

    for (std::vector<Mylite_overflow_row>::iterator it=
           overflow_rows->begin(); it != overflow_rows->end(); ++it)
    {
      if (it->rowid == rowid)
      {
        overflow_rows->erase(it);
        break;
      }
    }
  }

  return true;
}

static Mylite_overflow_row *mylite_find_overflow_row_locked(
    std::vector<Mylite_overflow_row> *overflow_rows, uint64_t rowid)
{
  for (Mylite_overflow_row &row : *overflow_rows)
  {
    if (row.rowid == rowid)
      return &row;
  }
  return nullptr;
}

static bool mylite_load_index_payloads_locked(
    int fd, std::vector<Mylite_table_definition> *catalog)
{
  for (Mylite_table_definition &definition : *catalog)
  {
    for (Mylite_index_root &root : definition.index_roots)
    {
      if (root.payload_length == 0 ||
          !mylite_page_offset_is_valid(root.payload_offset) ||
          root.key_length == 0)
        return false;

      std::string content;
      if (!mylite_read_page_chain(fd, mylite_page_type_index_payload,
                                  root.payload_offset, root.payload_length,
                                  root.payload_checksum, &content) ||
          !mylite_parse_index_payload_locked(content, &definition, &root))
        return false;
    }
  }

  return true;
}

static bool mylite_parse_index_payload_locked(
    const std::string &content, Mylite_table_definition *definition,
    Mylite_index_root *root)
{
  if (content.length() < mylite_index_payload_header_size ||
      std::memcmp(content.data(), mylite_index_payload_magic,
                  sizeof(mylite_index_payload_magic)) != 0)
    return false;

  const uchar *data= reinterpret_cast<const uchar *>(content.data());
  const uint32_t format_version= mylite_read_le32(data + 16);
  const uint32_t key_index= mylite_read_le32(data + 20);
  const uint32_t key_length= mylite_read_le32(data + 24);
  const uint64_t entry_count= mylite_read_le64(data + 28);
  if (format_version != mylite_index_payload_format_version ||
      key_index != root->key_index || key_length != root->key_length ||
      key_length == 0)
    return false;

  const uint64_t entry_length= 8 + static_cast<uint64_t>(key_length);
  if (entry_count >
        (content.length() - mylite_index_payload_header_size) /
          entry_length)
    return false;
  if (mylite_index_payload_header_size + entry_count * entry_length !=
      content.length())
    return false;

  root->entries.clear();
  root->entries.reserve(static_cast<size_t>(entry_count));
  size_t offset= mylite_index_payload_header_size;
  for (uint64_t i= 0; i < entry_count; ++i)
  {
    Mylite_index_entry entry;
    entry.rowid= mylite_read_le64(data + offset);
    if (entry.rowid == 0 ||
        !mylite_find_row_locked(definition, entry.rowid))
      return false;
    for (const Mylite_index_entry &loaded : root->entries)
    {
      if (loaded.rowid == entry.rowid)
        return false;
    }

    offset+= 8;
    entry.key.assign(data + offset, data + offset + key_length);
    offset+= key_length;
    root->entries.push_back(entry);
  }

  return !root->entries.empty();
}

static int mylite_flush_catalog_locked()
{
  if (!mylite_catalog_file || !mylite_catalog_file[0])
    return 0;

  return mylite_write_catalog_locked() ? 0 : mylite_catalog_error_code();
}

static bool mylite_write_catalog_locked()
{
  const std::string catalog_path(mylite_catalog_file);
  std::string content;

  mylite_clear_catalog_error();
  if (!mylite_ensure_catalog_file_locked())
    return false;

  bool ok= true;
  const int fd= mylite_catalog_fd;
  struct stat st;
  Mylite_catalog_header latest= { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  Mylite_catalog_header next= { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  bool found= false;
  uint64_t payload_offset= 0;
  uint64_t payload_checksum= 0;
  uint64_t free_payload_offset= 0;
  uint64_t free_payload_length= 0;
  uint64_t free_payload_checksum= 0;
  std::vector<Mylite_free_page_range> free_ranges_before=
    mylite_free_page_ranges;
  std::vector<Mylite_free_page_range> allocator= mylite_free_page_ranges;
  std::vector<Mylite_free_page_range> obsolete_ranges=
    mylite_pending_free_page_ranges;
  std::vector<Mylite_free_page_range> next_free_ranges;
  Mylite_catalog_header published_header= { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  bool has_published_header= false;
  if (fstat(fd, &st) != 0)
  {
    mylite_log_catalog_error("stat", catalog_path);
    ok= false;
    goto done;
  }

  found= mylite_find_latest_catalog_header(fd, st.st_size, &latest);
  if (mylite_loaded_catalog_header_valid)
  {
    latest= mylite_loaded_catalog_header;
    found= true;
  }
  else if (!found && st.st_size != 0)
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

  if (!mylite_normalize_free_page_ranges_locked(&allocator) ||
      !mylite_normalize_free_page_ranges_locked(&obsolete_ranges) ||
      !mylite_collect_catalog_payload_ranges_locked(mylite_catalog,
                                                    &obsolete_ranges))
  {
    ok= false;
    goto done;
  }

  if (!mylite_write_row_payloads_locked(fd, &allocator))
  {
    mylite_log_catalog_error("write", catalog_path);
    ok= false;
    goto done;
  }

  if (!mylite_write_index_payloads_locked(fd, &allocator))
  {
    mylite_log_catalog_error("write", catalog_path);
    ok= false;
    goto done;
  }

  if (found &&
      (!mylite_add_payload_free_range_locked(latest.payload_offset,
                                             latest.payload_length,
                                             &obsolete_ranges) ||
       !mylite_add_payload_free_range_locked(latest.free_payload_offset,
                                             latest.free_payload_length,
                                             &obsolete_ranges)))
  {
    ok= false;
    goto done;
  }

  content= mylite_serialize_catalog_locked();
  if (!mylite_write_catalog_payload(fd, content, &allocator, &payload_offset,
                                    &payload_checksum))
  {
    mylite_log_catalog_error("write", catalog_path);
    ok= false;
    goto done;
  }

  next_free_ranges= allocator;
  next_free_ranges.insert(next_free_ranges.end(), obsolete_ranges.begin(),
                          obsolete_ranges.end());
  if (!mylite_normalize_free_page_ranges_locked(&next_free_ranges))
  {
    ok= false;
    goto done;
  }

  mylite_free_page_ranges= next_free_ranges;
  if (!mylite_write_free_page_payload(fd, next_free_ranges,
                                      &free_payload_offset,
                                      &free_payload_length,
                                      &free_payload_checksum))
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
  next.format_version= mylite_catalog_format_version;
  next.generation= found ? latest.generation + 1 : 1;
  next.payload_offset= payload_offset;
  next.payload_length= content.length();
  next.payload_checksum= payload_checksum;
  next.free_payload_offset= free_payload_offset;
  next.free_payload_length= free_payload_length;
  next.free_payload_checksum= free_payload_checksum;
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
  published_header= next;
  has_published_header= true;

  mylite_pending_free_page_ranges.clear();

done:
  bool restore_free_ranges= !ok;
  if (restore_free_ranges)
    mylite_free_page_ranges= free_ranges_before;
  else if (has_published_header)
  {
    mylite_loaded_catalog_header= published_header;
    mylite_loaded_catalog_header_valid= true;
  }
  return ok;
}

static bool mylite_write_row_payloads_locked(
    int fd, std::vector<Mylite_free_page_range> *allocator)
{
  for (Mylite_table_definition &definition : mylite_catalog)
  {
    if (definition.frm_image.empty())
      continue;

    bool has_rows= false;
    for (const Mylite_row &row : definition.rows)
    {
      if (!row.deleted)
      {
        has_rows= true;
        break;
      }
    }

    if (!has_rows)
    {
      definition.rows_payload_offset= 0;
      definition.rows_payload_length= 0;
      definition.rows_payload_checksum= 0;
      definition.rows_payload_ref_seen= false;
      continue;
    }

    uint64_t payload_offset= 0;
    uint64_t payload_length= 0;
    uint64_t payload_checksum= 0;
    if (!mylite_write_row_slot_pages_locked(fd, definition, allocator,
                                            &payload_offset,
                                            &payload_length,
                                            &payload_checksum))
      return false;

    definition.rows_payload_offset= payload_offset;
    definition.rows_payload_length= payload_length;
    definition.rows_payload_checksum= payload_checksum;
    definition.rows_payload_ref_seen= true;
  }

  return true;
}

static bool mylite_write_row_slot_pages_locked(
    int fd, const Mylite_table_definition &definition,
    std::vector<Mylite_free_page_range> *allocator,
    uint64_t *payload_offset, uint64_t *payload_length,
    uint64_t *payload_checksum)
{
  std::vector<std::vector<uchar>> page_payloads;
  std::string logical_payload;
  if (!mylite_pack_row_slot_page_payloads_locked(
        definition, &page_payloads, &logical_payload) ||
      page_payloads.empty())
    return false;

  uint64_t page_id= 0;
  if (!mylite_allocate_page_range_locked(fd, page_payloads.size(), allocator,
                                         &page_id))
    return false;
  *payload_offset= mylite_page_offset(page_id);
  *payload_length= logical_payload.length();
  *payload_checksum= mylite_checksum(
    reinterpret_cast<const uchar *>(logical_payload.data()),
    logical_payload.length());

  for (size_t i= 0; i < page_payloads.size(); ++i)
  {
    const uint64_t next_page_id= i + 1 < page_payloads.size()
      ? page_id + 1
      : 0;
    const std::vector<uchar> &payload= page_payloads[i];
    if (!mylite_write_page(fd, mylite_page_type_row_payload, page_id,
                           next_page_id, payload.data(), payload.size()))
      return false;
    ++page_id;
  }

  return true;
}

static bool mylite_pack_row_slot_page_payloads_locked(
    const Mylite_table_definition &definition,
    std::vector<std::vector<uchar>> *page_payloads,
    std::string *logical_payload)
{
  page_payloads->clear();
  logical_payload->clear();

  std::vector<const Mylite_row *> page_rows;
  size_t page_record_bytes= 0;
  for (const Mylite_row &row : definition.rows)
  {
    if (row.deleted)
      continue;
    if (row.record.empty())
      return false;

    if (row.record.size() > mylite_row_slot_max_record_length)
    {
      if (!page_rows.empty())
      {
        mylite_append_row_slot_page_payload(page_rows, page_payloads,
                                            logical_payload);
        page_rows.clear();
        page_record_bytes= 0;
      }
      mylite_append_row_overflow_page_payloads(row, page_payloads,
                                               logical_payload);
      continue;
    }

    const size_t next_row_count= page_rows.size() + 1;
    const size_t next_payload_length=
      mylite_row_slot_header_size +
      next_row_count * mylite_row_slot_entry_size +
      page_record_bytes + row.record.size();
    if (!page_rows.empty() &&
        next_payload_length > mylite_page_payload_capacity)
    {
      mylite_append_row_slot_page_payload(page_rows, page_payloads,
                                          logical_payload);
      page_rows.clear();
      page_record_bytes= 0;
    }

    page_rows.push_back(&row);
    page_record_bytes+= row.record.size();
  }

  if (!page_rows.empty())
    mylite_append_row_slot_page_payload(page_rows, page_payloads,
                                        logical_payload);

  return !page_payloads->empty();
}

static void mylite_append_row_slot_page_payload(
    const std::vector<const Mylite_row *> &rows,
    std::vector<std::vector<uchar>> *page_payloads,
    std::string *logical_payload)
{
  size_t record_bytes= 0;
  for (const Mylite_row *row : rows)
    record_bytes+= row->record.size();

  const size_t data_offset=
    mylite_row_slot_header_size +
    rows.size() * mylite_row_slot_entry_size;
  const size_t payload_length= data_offset + record_bytes;
  std::vector<uchar> payload(payload_length, 0);
  std::memcpy(payload.data(), mylite_row_slot_magic,
              sizeof(mylite_row_slot_magic));
  mylite_store_le32(payload.data() + 16, mylite_row_slot_format_version);
  mylite_store_le32(payload.data() + 20,
                    static_cast<uint32_t>(rows.size()));
  mylite_store_le32(payload.data() + 24,
                    static_cast<uint32_t>(data_offset));

  size_t record_offset= data_offset;
  for (size_t i= 0; i < rows.size(); ++i)
  {
    const Mylite_row *row= rows[i];
    const size_t slot_offset=
      mylite_row_slot_header_size + i * mylite_row_slot_entry_size;
    mylite_store_le64(payload.data() + slot_offset, row->rowid);
    mylite_store_le32(payload.data() + slot_offset + 8,
                      static_cast<uint32_t>(record_offset));
    mylite_store_le32(payload.data() + slot_offset + 12,
                      static_cast<uint32_t>(row->record.size()));
    std::memcpy(payload.data() + record_offset, row->record.data(),
                row->record.size());
    record_offset+= row->record.size();
  }

  logical_payload->append(reinterpret_cast<const char *>(payload.data()),
                          payload.size());
  page_payloads->push_back(payload);
}

static void mylite_append_row_overflow_page_payloads(
    const Mylite_row &row, std::vector<std::vector<uchar>> *page_payloads,
    std::string *logical_payload)
{
  size_t segment_offset= 0;
  while (segment_offset < row.record.size())
  {
    const size_t remaining= row.record.size() - segment_offset;
    const size_t segment_length=
      remaining < mylite_row_overflow_segment_capacity
        ? remaining
        : mylite_row_overflow_segment_capacity;
    std::vector<uchar> payload(
      mylite_row_overflow_header_size + segment_length, 0);
    std::memcpy(payload.data(), mylite_row_overflow_magic,
                sizeof(mylite_row_overflow_magic));
    mylite_store_le32(payload.data() + 16,
                      mylite_row_overflow_format_version);
    mylite_store_le64(payload.data() + 20, row.rowid);
    mylite_store_le64(payload.data() + 28, row.record.size());
    mylite_store_le64(payload.data() + 36, segment_offset);
    mylite_store_le32(payload.data() + 44,
                      static_cast<uint32_t>(segment_length));
    std::memcpy(payload.data() + mylite_row_overflow_header_size,
                row.record.data() + segment_offset, segment_length);

    logical_payload->append(reinterpret_cast<const char *>(payload.data()),
                            payload.size());
    page_payloads->push_back(payload);
    segment_offset+= segment_length;
  }
}

static bool mylite_write_index_payloads_locked(
    int fd, std::vector<Mylite_free_page_range> *allocator)
{
  for (Mylite_table_definition &definition : mylite_catalog)
  {
    if (definition.frm_image.empty())
      continue;

    for (Mylite_index_root &root : definition.index_roots)
    {
      if (root.entries.empty())
      {
        root.payload_offset= 0;
        root.payload_length= 0;
        root.payload_checksum= 0;
        root.payload_ref_seen= false;
        continue;
      }

      if (root.key_length == 0)
        return false;
      const size_t entry_length= 8 + static_cast<size_t>(root.key_length);
      if (root.entries.size() >
            (static_cast<size_t>(-1) - mylite_index_payload_header_size) /
              entry_length)
        return false;
      for (const Mylite_index_entry &entry : root.entries)
      {
        if (entry.rowid == 0 || entry.key.size() != root.key_length)
          return false;
      }

      const std::string content= mylite_serialize_index_payload_locked(root);
      uint64_t payload_offset= 0;
      uint64_t payload_checksum= 0;
      if (!mylite_write_page_chain(fd, mylite_page_type_index_payload,
                                   content, allocator, &payload_offset,
                                   &payload_checksum))
        return false;
      root.payload_offset= payload_offset;
      root.payload_length= content.length();
      root.payload_checksum= payload_checksum;
      root.payload_ref_seen= true;
    }
  }

  return true;
}

static std::string mylite_serialize_index_payload_locked(
    const Mylite_index_root &root)
{
  const size_t entry_length= 8 + static_cast<size_t>(root.key_length);
  std::vector<uchar> payload(
    mylite_index_payload_header_size + root.entries.size() * entry_length, 0);
  std::memcpy(payload.data(), mylite_index_payload_magic,
              sizeof(mylite_index_payload_magic));
  mylite_store_le32(payload.data() + 16,
                    mylite_index_payload_format_version);
  mylite_store_le32(payload.data() + 20, root.key_index);
  mylite_store_le32(payload.data() + 24, root.key_length);
  mylite_store_le64(payload.data() + 28, root.entries.size());

  size_t offset= mylite_index_payload_header_size;
  for (const Mylite_index_entry &entry : root.entries)
  {
    mylite_store_le64(payload.data() + offset, entry.rowid);
    offset+= 8;
    std::memcpy(payload.data() + offset, entry.key.data(),
                entry.key.size());
    offset+= entry.key.size();
  }

  return std::string(reinterpret_cast<const char *>(payload.data()),
                     payload.size());
}

static std::string mylite_serialize_catalog_with_free_ranges_locked(
    const std::vector<Mylite_free_page_range> &free_ranges)
{
  std::string content;
  content.append(mylite_catalog_magic);
  content.push_back('\n');

  for (const Mylite_free_page_range &range : free_ranges)
  {
    content.append("FREEPAGE\t");
    content.append(mylite_format_decimal_uint64(range.page_id));
    content.push_back('\t');
    content.append(mylite_format_decimal_uint64(range.page_count));
    content.push_back('\n');
  }

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

    content.append("ROWPAGE\t");
    content.append(mylite_hex_encode(
      reinterpret_cast<const uchar *>(definition.db.data()),
      definition.db.length()));
    content.push_back('\t');
    content.append(mylite_hex_encode(
      reinterpret_cast<const uchar *>(definition.table_name.data()),
      definition.table_name.length()));
    content.push_back('\t');
    content.append(mylite_format_decimal_uint64(
      definition.rows_payload_offset));
    content.push_back('\t');
    content.append(mylite_format_decimal_uint64(
      definition.rows_payload_length));
    content.push_back('\t');
    content.append(mylite_format_decimal_uint64(
      definition.rows_payload_checksum));
    content.push_back('\n');

    for (const Mylite_index_root &root : definition.index_roots)
    {
      if (root.entries.empty() || root.payload_length == 0)
        continue;

      content.append("INDEXPAGE\t");
      content.append(mylite_hex_encode(
        reinterpret_cast<const uchar *>(definition.db.data()),
        definition.db.length()));
      content.push_back('\t');
      content.append(mylite_hex_encode(
        reinterpret_cast<const uchar *>(definition.table_name.data()),
        definition.table_name.length()));
      content.push_back('\t');
      content.append(mylite_format_decimal_uint64(root.key_index));
      content.push_back('\t');
      content.append(mylite_format_decimal_uint64(root.key_length));
      content.push_back('\t');
      content.append(mylite_format_decimal_uint64(root.payload_offset));
      content.push_back('\t');
      content.append(mylite_format_decimal_uint64(root.payload_length));
      content.push_back('\t');
      content.append(mylite_format_decimal_uint64(root.payload_checksum));
      content.push_back('\n');
    }
  }

  return content;
}

static std::string mylite_serialize_catalog_locked()
{
  std::vector<Mylite_free_page_range> no_free_ranges;
  return mylite_serialize_catalog_with_free_ranges_locked(no_free_ranges);
}

static std::string mylite_serialize_free_page_payload_locked(
    const std::vector<Mylite_free_page_range> &free_ranges)
{
  std::string content;
  content.append(mylite_free_page_payload_magic);
  content.push_back('\n');

  for (const Mylite_free_page_range &range : free_ranges)
  {
    content.append("FREEPAGE\t");
    content.append(mylite_format_decimal_uint64(range.page_id));
    content.push_back('\t');
    content.append(mylite_format_decimal_uint64(range.page_count));
    content.push_back('\n');
  }

  return content;
}

static bool mylite_parse_free_page_payload_locked(
    const std::string &content,
    std::vector<Mylite_free_page_range> *free_ranges)
{
  std::istringstream input(content);
  std::string line;
  if (!std::getline(input, line) || line != mylite_free_page_payload_magic)
  {
    sql_print_error("MyLite: invalid free page payload in %s",
                    mylite_catalog_file);
    return false;
  }

  free_ranges->clear();
  while (std::getline(input, line))
  {
    if (line.empty())
      continue;

    if (line.compare(0, 9, "FREEPAGE\t") == 0)
    {
      if (!mylite_parse_freepage_payload_record_locked(line, free_ranges))
        return false;
      continue;
    }

    sql_print_error("MyLite: invalid free page record in %s",
                    mylite_catalog_file);
    return false;
  }

  return true;
}

static bool mylite_parse_catalog_payload_locked(
    const std::string &content, std::vector<Mylite_table_definition> *loaded,
    std::vector<Mylite_free_page_range> *free_ranges)
{
  std::istringstream input(content);
  std::string line;
  if (!std::getline(input, line) || line != mylite_catalog_magic)
  {
    sql_print_error("MyLite: invalid catalog payload in %s",
                    mylite_catalog_file);
    return false;
  }

  loaded->clear();
  free_ranges->clear();
  for (const Mylite_table_definition &definition : mylite_catalog)
  {
    if (!definition.seed_sql.empty())
      loaded->push_back(definition);
  }

  while (std::getline(input, line))
  {
    if (line.empty())
      continue;

    if (line.compare(0, 9, "FREEPAGE\t") == 0)
    {
      if (!mylite_parse_freepage_payload_record_locked(line, free_ranges))
        return false;
      continue;
    }
    if (line.compare(0, 6, "TABLE\t") == 0)
    {
      if (!mylite_parse_table_payload_record_locked(line, loaded))
        return false;
      continue;
    }
    if (line.compare(0, 10, "NEXTROWID\t") == 0)
    {
      if (!mylite_parse_next_rowid_payload_record_locked(line, loaded))
        return false;
      continue;
    }
    if (line.compare(0, 8, "AUTOINC\t") == 0)
    {
      if (!mylite_parse_autoincrement_payload_record_locked(line, loaded))
        return false;
      continue;
    }
    if (line.compare(0, 8, "ROWPAGE\t") == 0)
    {
      if (!mylite_parse_rowpage_payload_record_locked(line, loaded))
        return false;
      continue;
    }
    if (line.compare(0, 10, "INDEXPAGE\t") == 0)
    {
      if (!mylite_parse_indexpage_payload_record_locked(line, loaded))
        return false;
      continue;
    }
    if (line.compare(0, 4, "ROW\t") == 0)
    {
      if (!mylite_parse_row_payload_record_locked(line, loaded))
        return false;
      continue;
    }

    sql_print_error("MyLite: invalid catalog record in %s",
                    mylite_catalog_file);
    return false;
  }

  return true;
}

static bool mylite_parse_freepage_payload_record_locked(
    const std::string &line, std::vector<Mylite_free_page_range> *free_ranges)
{
  const std::string::size_type first= line.find('\t');
  const std::string::size_type second= first == std::string::npos
    ? std::string::npos
    : line.find('\t', first + 1);

  if (first == std::string::npos || second == std::string::npos ||
      line.substr(0, first) != "FREEPAGE")
  {
    sql_print_error("MyLite: invalid free page record in %s",
                    mylite_catalog_file);
    return false;
  }

  uint64_t page_id= 0;
  uint64_t page_count= 0;
  if (!mylite_parse_decimal_uint64(line.substr(first + 1,
                                               second - first - 1),
                                   &page_id) ||
      !mylite_parse_decimal_uint64(line.substr(second + 1), &page_count) ||
      !mylite_add_free_page_range_locked(page_id, page_count, free_ranges))
  {
    sql_print_error("MyLite: invalid free page encoding in %s",
                    mylite_catalog_file);
    return false;
  }

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
  definition.rows_payload_offset= 0;
  definition.rows_payload_length= 0;
  definition.rows_payload_checksum= 0;
  definition.rows_payload_ref_seen= false;
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

static bool mylite_parse_rowpage_payload_record_locked(
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
  const std::string::size_type fifth= fourth == std::string::npos
    ? std::string::npos
    : line.find('\t', fourth + 1);

  if (first == std::string::npos || second == std::string::npos ||
      third == std::string::npos || fourth == std::string::npos ||
      fifth == std::string::npos || line.substr(0, first) != "ROWPAGE")
  {
    sql_print_error("MyLite: invalid catalog row page record in %s",
                    mylite_catalog_file);
    return false;
  }

  std::vector<uchar> db_bytes;
  std::vector<uchar> table_bytes;
  uint64_t payload_offset= 0;
  uint64_t payload_length= 0;
  uint64_t payload_checksum= 0;
  if (!mylite_hex_decode(line.substr(first + 1, second - first - 1),
                         &db_bytes) ||
      !mylite_hex_decode(line.substr(second + 1, third - second - 1),
                         &table_bytes) ||
      !mylite_parse_decimal_uint64(
        line.substr(third + 1, fourth - third - 1), &payload_offset) ||
      !mylite_parse_decimal_uint64(
        line.substr(fourth + 1, fifth - fourth - 1), &payload_length) ||
      !mylite_parse_decimal_uint64(line.substr(fifth + 1),
                                   &payload_checksum) ||
      db_bytes.empty() || table_bytes.empty())
  {
    sql_print_error("MyLite: invalid catalog row page encoding in %s",
                    mylite_catalog_file);
    return false;
  }

  const std::string db(reinterpret_cast<const char *>(db_bytes.data()),
                       db_bytes.size());
  const std::string table_name(
    reinterpret_cast<const char *>(table_bytes.data()), table_bytes.size());
  Mylite_table_definition *definition=
    mylite_find_table_definition_in_catalog(loaded, db, table_name);
  if (!definition || definition->rows_payload_ref_seen)
  {
    sql_print_error("MyLite: invalid catalog row page owner in %s",
                    mylite_catalog_file);
    return false;
  }

  if (payload_length == 0)
  {
    if (payload_offset != 0 || payload_checksum != 0)
    {
      sql_print_error("MyLite: invalid empty row page in %s",
                      mylite_catalog_file);
      return false;
    }
  }
  else if (!mylite_page_offset_is_valid(payload_offset))
  {
    sql_print_error("MyLite: invalid row page offset in %s",
                    mylite_catalog_file);
    return false;
  }

  definition->rows_payload_offset= payload_offset;
  definition->rows_payload_length= payload_length;
  definition->rows_payload_checksum= payload_checksum;
  definition->rows_payload_ref_seen= true;
  return true;
}

static bool mylite_parse_indexpage_payload_record_locked(
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
  const std::string::size_type fifth= fourth == std::string::npos
    ? std::string::npos
    : line.find('\t', fourth + 1);
  const std::string::size_type sixth= fifth == std::string::npos
    ? std::string::npos
    : line.find('\t', fifth + 1);
  const std::string::size_type seventh= sixth == std::string::npos
    ? std::string::npos
    : line.find('\t', sixth + 1);

  if (first == std::string::npos || second == std::string::npos ||
      third == std::string::npos || fourth == std::string::npos ||
      fifth == std::string::npos || sixth == std::string::npos ||
      seventh == std::string::npos ||
      line.substr(0, first) != "INDEXPAGE")
  {
    sql_print_error("MyLite: invalid catalog index page record in %s",
                    mylite_catalog_file);
    return false;
  }

  std::vector<uchar> db_bytes;
  std::vector<uchar> table_bytes;
  uint64_t key_index= 0;
  uint64_t key_length= 0;
  uint64_t payload_offset= 0;
  uint64_t payload_length= 0;
  uint64_t payload_checksum= 0;
  if (!mylite_hex_decode(line.substr(first + 1, second - first - 1),
                         &db_bytes) ||
      !mylite_hex_decode(line.substr(second + 1, third - second - 1),
                         &table_bytes) ||
      !mylite_parse_decimal_uint64(
        line.substr(third + 1, fourth - third - 1), &key_index) ||
      !mylite_parse_decimal_uint64(
        line.substr(fourth + 1, fifth - fourth - 1), &key_length) ||
      !mylite_parse_decimal_uint64(
        line.substr(fifth + 1, sixth - fifth - 1), &payload_offset) ||
      !mylite_parse_decimal_uint64(
        line.substr(sixth + 1, seventh - sixth - 1), &payload_length) ||
      !mylite_parse_decimal_uint64(line.substr(seventh + 1),
                                   &payload_checksum) ||
      db_bytes.empty() || table_bytes.empty() || key_length == 0 ||
      key_index > static_cast<uint64_t>(~static_cast<uint32_t>(0)) ||
      key_length > static_cast<uint64_t>(~static_cast<uint32_t>(0)) ||
      payload_length == 0 || !mylite_page_offset_is_valid(payload_offset))
  {
    sql_print_error("MyLite: invalid catalog index page encoding in %s",
                    mylite_catalog_file);
    return false;
  }

  const std::string db(reinterpret_cast<const char *>(db_bytes.data()),
                       db_bytes.size());
  const std::string table_name(
    reinterpret_cast<const char *>(table_bytes.data()), table_bytes.size());
  Mylite_table_definition *definition=
    mylite_find_table_definition_in_catalog(loaded, db, table_name);
  if (!definition ||
      mylite_find_index_root_locked(
        definition, static_cast<uint32_t>(key_index)))
  {
    sql_print_error("MyLite: invalid catalog index page owner in %s",
                    mylite_catalog_file);
    return false;
  }

  Mylite_index_root root;
  root.key_index= static_cast<uint32_t>(key_index);
  root.key_length= static_cast<uint32_t>(key_length);
  root.payload_offset= payload_offset;
  root.payload_length= payload_length;
  root.payload_checksum= payload_checksum;
  root.payload_ref_seen= true;
  definition->index_roots.push_back(root);
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
    Mylite_catalog_header header= { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (!mylite_read_catalog_header(fd, slot, file_size, &header))
      continue;

    std::string payload;
    if (!mylite_read_catalog_payload(fd, header, &payload))
      continue;
    if (header.free_payload_length != 0)
    {
      std::vector<Mylite_free_page_range> free_ranges;
      if (!mylite_read_free_page_payload(fd, header, &free_ranges))
        continue;
    }

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
  const uint32_t format_version= mylite_read_le32(page.data() + 16);
  if ((format_version != mylite_catalog_format_version_v2 &&
       format_version != mylite_catalog_format_version) ||
      mylite_read_le32(page.data() + 20) != mylite_catalog_page_size)
    return false;

  const uint64_t stored_header_checksum=
    mylite_read_le64(page.data() + mylite_catalog_header_checksum_offset);
  mylite_store_le64(page.data() + mylite_catalog_header_checksum_offset, 0);
  if (mylite_checksum(page.data(), page.size()) != stored_header_checksum)
    return false;

  header->slot= slot;
  header->format_version= format_version;
  header->generation= mylite_read_le64(page.data() + 24);
  header->payload_offset= mylite_read_le64(page.data() + 32);
  header->payload_length= mylite_read_le64(page.data() + 40);
  header->payload_checksum= mylite_read_le64(page.data() + 48);
  header->free_payload_offset= 0;
  header->free_payload_length= 0;
  header->free_payload_checksum= 0;

  const uint64_t file_size_u= static_cast<uint64_t>(file_size);
  if (header->generation == 0 ||
      !mylite_page_offset_is_valid(header->payload_offset) ||
      header->payload_length == 0 ||
      header->payload_offset > file_size_u ||
      header->payload_offset + mylite_catalog_page_size > file_size_u)
    return false;

  if (format_version == mylite_catalog_format_version)
  {
    header->free_payload_offset= mylite_read_le64(page.data() + 64);
    header->free_payload_length= mylite_read_le64(page.data() + 72);
    header->free_payload_checksum= mylite_read_le64(page.data() + 80);
    if (!mylite_page_offset_is_valid(header->free_payload_offset) ||
        header->free_payload_length == 0 ||
        header->free_payload_offset > file_size_u ||
        header->free_payload_offset + mylite_catalog_page_size > file_size_u)
      return false;
  }

  return true;
}

static bool mylite_read_catalog_payload(int fd,
                                        const Mylite_catalog_header &header,
                                        std::string *content)
{
  return mylite_read_page_chain(fd, mylite_page_type_catalog_payload,
                                header.payload_offset,
                                header.payload_length,
                                header.payload_checksum, content);
}

static bool mylite_write_catalog_payload(int fd, const std::string &content,
                                         std::vector<Mylite_free_page_range>
                                           *allocator,
                                         uint64_t *payload_offset,
                                         uint64_t *payload_checksum)
{
  return mylite_write_page_chain(fd, mylite_page_type_catalog_payload,
                                 content, allocator, payload_offset,
                                 payload_checksum);
}

static bool mylite_read_free_page_payload(
    int fd, const Mylite_catalog_header &header,
    std::vector<Mylite_free_page_range> *free_ranges)
{
  if (header.free_payload_length == 0)
    return false;

  std::string content;
  return mylite_read_page_chain(fd, mylite_page_type_free_payload,
                                header.free_payload_offset,
                                header.free_payload_length,
                                header.free_payload_checksum, &content) &&
         mylite_parse_free_page_payload_locked(content, free_ranges);
}

static bool mylite_write_free_page_payload(
    int fd, const std::vector<Mylite_free_page_range> &free_ranges,
    uint64_t *payload_offset, uint64_t *payload_length,
    uint64_t *payload_checksum)
{
  const std::string content=
    mylite_serialize_free_page_payload_locked(free_ranges);
  if (!mylite_write_page_chain(fd, mylite_page_type_free_payload, content,
                               nullptr, payload_offset, payload_checksum))
    return false;

  *payload_length= content.length();
  return true;
}

static bool mylite_read_page_chain(int fd, uint32_t page_type,
                                   uint64_t payload_offset,
                                   uint64_t payload_length,
                                   uint64_t payload_checksum,
                                   std::string *content)
{
  if (!mylite_page_offset_is_valid(payload_offset) ||
      payload_length == 0 || payload_length > content->max_size())
    return false;

  content->clear();
  content->reserve(static_cast<size_t>(payload_length));

  uint64_t page_id= payload_offset / mylite_catalog_page_size;
  uint64_t remaining= payload_length;
  const uint64_t max_pages=
    (payload_length + mylite_page_payload_capacity - 1) /
    mylite_page_payload_capacity;
  for (uint64_t page_count= 0; remaining > 0; ++page_count)
  {
    if (page_count >= max_pages || page_id < 2)
      return false;

    std::vector<uchar> page;
    Mylite_page_header page_header;
    if (!mylite_read_page(fd, page_id, &page, &page_header))
      return false;
    if (page_header.type != page_type)
      return false;

    const uint32_t expected_payload_length=
      static_cast<uint32_t>(
        remaining < mylite_page_payload_capacity
          ? remaining
          : mylite_page_payload_capacity);
    if (page_header.payload_length != expected_payload_length)
      return false;

    content->append(
      reinterpret_cast<const char *>(page.data() + mylite_page_payload_offset),
      page_header.payload_length);
    remaining-= page_header.payload_length;

    if (remaining == 0)
    {
      if (page_header.next_page_id != 0)
        return false;
      break;
    }
    if (page_header.next_page_id != page_id + 1)
      return false;
    page_id= page_header.next_page_id;
  }

  if (content->length() != payload_length)
    return false;

  return mylite_checksum(
           reinterpret_cast<const uchar *>(content->data()),
           content->length()) == payload_checksum;
}

static bool mylite_write_page_chain(int fd, uint32_t page_type,
                                    const std::string &content,
                                    std::vector<Mylite_free_page_range>
                                      *allocator,
                                    uint64_t *payload_offset,
                                    uint64_t *payload_checksum)
{
  if (content.empty())
    return false;

  const uint64_t page_count= mylite_payload_page_count(content.length());
  uint64_t page_id= 0;
  if (!mylite_allocate_page_range_locked(fd, page_count, allocator, &page_id))
    return false;

  return mylite_write_page_chain_at(fd, page_type, content, page_id,
                                    payload_offset, payload_checksum);
}

static bool mylite_write_page_chain_at(int fd, uint32_t page_type,
                                       const std::string &content,
                                       uint64_t page_id,
                                       uint64_t *payload_offset,
                                       uint64_t *payload_checksum)
{
  if (content.empty() || page_id < 2)
    return false;

  *payload_offset= mylite_page_offset(page_id);
  *payload_checksum= mylite_checksum(
    reinterpret_cast<const uchar *>(content.data()), content.length());

  size_t written= 0;
  while (written < content.length())
  {
    const size_t remaining= content.length() - written;
    const size_t chunk= remaining < mylite_page_payload_capacity
      ? remaining
      : mylite_page_payload_capacity;
    const uint64_t next_page_id= written + chunk < content.length()
      ? page_id + 1
      : 0;

    if (!mylite_write_page(
          fd, page_type, page_id, next_page_id,
          reinterpret_cast<const uchar *>(content.data() + written), chunk))
      return false;

    written+= chunk;
    ++page_id;
  }

  return true;
}

static bool mylite_allocate_page_range_locked(
    int fd, uint64_t page_count,
    std::vector<Mylite_free_page_range> *allocator, uint64_t *page_id)
{
  if (page_count == 0)
    return false;

  if (allocator)
  {
    for (std::vector<Mylite_free_page_range>::iterator it= allocator->begin();
         it != allocator->end(); ++it)
    {
      if (it->page_count < page_count)
        continue;

      *page_id= it->page_id;
      it->page_id+= page_count;
      it->page_count-= page_count;
      if (it->page_count == 0)
        allocator->erase(it);
      return true;
    }
  }

  const off_t file_end= lseek(fd, 0, SEEK_END);
  if (file_end < 0)
    return false;

  uint64_t payload_offset= mylite_align_to_page(
    static_cast<uint64_t>(file_end));
  if (payload_offset < mylite_catalog_payload_start)
    payload_offset= mylite_catalog_payload_start;
  *page_id= payload_offset / mylite_catalog_page_size;
  return *page_id >= 2;
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
  mylite_store_le64(page.data() + 64, header.free_payload_offset);
  mylite_store_le64(page.data() + 72, header.free_payload_length);
  mylite_store_le64(page.data() + 80, header.free_payload_checksum);
  mylite_store_le64(page.data() + mylite_catalog_header_checksum_offset, 0);
  mylite_store_le64(page.data() + mylite_catalog_header_checksum_offset,
                    mylite_checksum(page.data(), page.size()));

  return mylite_write_all_at(
    fd, page.data(), page.size(),
    static_cast<uint64_t>(header.slot) * mylite_catalog_page_size);
}

static bool mylite_read_page(int fd, uint64_t page_id,
                             std::vector<uchar> *page,
                             Mylite_page_header *header)
{
  if (page_id < 2)
    return false;

  page->assign(mylite_catalog_page_size, 0);
  if (!mylite_read_all_at(fd, page->data(), page->size(),
                          mylite_page_offset(page_id)))
    return false;

  if (std::memcmp(page->data(), mylite_page_magic,
                  sizeof(mylite_page_magic)) != 0)
    return false;
  if (mylite_read_le32(page->data() + 16) != mylite_page_format_version)
    return false;

  const uint64_t stored_page_checksum=
    mylite_read_le64(page->data() + mylite_page_checksum_offset);
  mylite_store_le64(page->data() + mylite_page_checksum_offset, 0);
  if (mylite_checksum(page->data(), page->size()) != stored_page_checksum)
    return false;

  header->type= mylite_read_le32(page->data() + 20);
  header->page_id= mylite_read_le64(page->data() + 24);
  header->next_page_id= mylite_read_le64(page->data() + 32);
  header->payload_length= mylite_read_le32(page->data() + 40);
  if (header->page_id != page_id ||
      header->payload_length == 0 ||
      header->payload_length > mylite_page_payload_capacity)
    return false;

  return true;
}

static bool mylite_write_page(int fd, uint32_t type, uint64_t page_id,
                              uint64_t next_page_id, const uchar *payload,
                              size_t payload_length)
{
  if (page_id < 2 || payload_length == 0 ||
      payload_length > mylite_page_payload_capacity)
    return false;

  std::vector<uchar> page(mylite_catalog_page_size, 0);
  std::memcpy(page.data(), mylite_page_magic, sizeof(mylite_page_magic));
  mylite_store_le32(page.data() + 16, mylite_page_format_version);
  mylite_store_le32(page.data() + 20, type);
  mylite_store_le64(page.data() + 24, page_id);
  mylite_store_le64(page.data() + 32, next_page_id);
  mylite_store_le32(page.data() + 40,
                    static_cast<uint32_t>(payload_length));
  std::memcpy(page.data() + mylite_page_payload_offset, payload,
              payload_length);
  mylite_store_le64(page.data() + mylite_page_checksum_offset, 0);
  mylite_store_le64(page.data() + mylite_page_checksum_offset,
                    mylite_checksum(page.data(), page.size()));

  return mylite_write_all_at(fd, page.data(), page.size(),
                             mylite_page_offset(page_id));
}

static bool mylite_page_offset_is_valid(uint64_t offset)
{
  return offset >= mylite_catalog_payload_start &&
         offset % mylite_catalog_page_size == 0;
}

static uint64_t mylite_payload_page_count(uint64_t payload_length)
{
  return payload_length == 0
    ? 0
    : (payload_length - 1) / mylite_page_payload_capacity + 1;
}

static bool mylite_add_payload_free_range_locked(
    uint64_t payload_offset, uint64_t payload_length,
    std::vector<Mylite_free_page_range> *ranges)
{
  if (payload_length == 0)
    return payload_offset == 0;
  if (!mylite_page_offset_is_valid(payload_offset))
    return false;

  const uint64_t page_id= payload_offset / mylite_catalog_page_size;
  return mylite_add_free_page_range_locked(
    page_id, mylite_payload_page_count(payload_length), ranges);
}

static bool mylite_add_free_page_range_locked(
    uint64_t page_id, uint64_t page_count,
    std::vector<Mylite_free_page_range> *ranges)
{
  if (page_id < 2 || page_count == 0 ||
      page_count > ~static_cast<uint64_t>(0) - page_id)
    return false;

  Mylite_free_page_range range;
  range.page_id= page_id;
  range.page_count= page_count;
  ranges->push_back(range);
  return true;
}

static bool mylite_collect_catalog_payload_ranges_locked(
    const std::vector<Mylite_table_definition> &catalog,
    std::vector<Mylite_free_page_range> *ranges)
{
  for (const Mylite_table_definition &definition : catalog)
  {
    if (!mylite_collect_definition_payload_ranges_locked(definition, ranges))
      return false;
  }
  return true;
}

static bool mylite_collect_definition_payload_ranges_locked(
    const Mylite_table_definition &definition,
    std::vector<Mylite_free_page_range> *ranges)
{
  if (!mylite_add_payload_free_range_locked(definition.rows_payload_offset,
                                            definition.rows_payload_length,
                                            ranges))
    return false;

  for (const Mylite_index_root &root : definition.index_roots)
  {
    if (!mylite_add_payload_free_range_locked(root.payload_offset,
                                              root.payload_length, ranges))
      return false;
  }
  return true;
}

static bool mylite_collect_index_payload_ranges_locked(
    const Mylite_table_definition &definition,
    std::vector<Mylite_free_page_range> *ranges)
{
  for (const Mylite_index_root &root : definition.index_roots)
  {
    if (!mylite_add_payload_free_range_locked(root.payload_offset,
                                              root.payload_length, ranges))
      return false;
  }
  return true;
}

static bool mylite_normalize_free_page_ranges_locked(
    std::vector<Mylite_free_page_range> *ranges)
{
  std::sort(ranges->begin(), ranges->end(),
            [](const Mylite_free_page_range &left,
               const Mylite_free_page_range &right) {
              return left.page_id < right.page_id;
            });

  std::vector<Mylite_free_page_range> normalized;
  normalized.reserve(ranges->size());
  for (const Mylite_free_page_range &range : *ranges)
  {
    if (range.page_id < 2 || range.page_count == 0 ||
        range.page_count > ~static_cast<uint64_t>(0) - range.page_id)
      return false;

    if (normalized.empty())
    {
      normalized.push_back(range);
      continue;
    }

    Mylite_free_page_range &previous= normalized.back();
    const uint64_t previous_end= previous.page_id + previous.page_count;
    if (range.page_id < previous_end)
      return false;
    if (range.page_id == previous_end)
    {
      if (range.page_count >
          ~static_cast<uint64_t>(0) - previous.page_count)
        return false;
      previous.page_count+= range.page_count;
    }
    else
      normalized.push_back(range);
  }

  ranges->swap(normalized);
  return true;
}

static bool mylite_validate_free_page_ranges_locked(
    int fd, const Mylite_catalog_header &header,
    const std::vector<Mylite_table_definition> &catalog,
    std::vector<Mylite_free_page_range> *free_ranges)
{
  if (!mylite_normalize_free_page_ranges_locked(free_ranges))
    return false;

  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size < 0)
    return false;
  const uint64_t file_size= static_cast<uint64_t>(st.st_size);

  std::vector<Mylite_free_page_range> used_ranges;
  if (!mylite_add_payload_free_range_locked(header.payload_offset,
                                            header.payload_length,
                                            &used_ranges) ||
      !mylite_add_payload_free_range_locked(header.free_payload_offset,
                                            header.free_payload_length,
                                            &used_ranges) ||
      !mylite_collect_catalog_payload_ranges_locked(catalog, &used_ranges) ||
      !mylite_normalize_free_page_ranges_locked(&used_ranges))
    return false;

  for (const Mylite_free_page_range &free_range : *free_ranges)
  {
    if (!mylite_page_range_fits_file(free_range, file_size))
      return false;

    for (const Mylite_free_page_range &used_range : used_ranges)
    {
      if (mylite_page_ranges_overlap(free_range, used_range))
        return false;
    }
  }

  return true;
}

static bool mylite_reclaim_orphan_pages_locked(
    int fd, const Mylite_catalog_header &header,
    const std::vector<Mylite_table_definition> &catalog,
    std::vector<Mylite_free_page_range> *free_ranges)
{
  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size < 0)
    return false;
  const uint64_t complete_pages=
    static_cast<uint64_t>(st.st_size) / mylite_catalog_page_size;
  if (complete_pages <= 2)
    return true;

  std::vector<Mylite_free_page_range> protected_ranges= *free_ranges;
  if (!mylite_add_payload_free_range_locked(header.payload_offset,
                                            header.payload_length,
                                            &protected_ranges) ||
      !mylite_add_payload_free_range_locked(header.free_payload_offset,
                                            header.free_payload_length,
                                            &protected_ranges) ||
      !mylite_collect_catalog_payload_ranges_locked(catalog,
                                                    &protected_ranges) ||
      !mylite_normalize_free_page_ranges_locked(&protected_ranges))
    return false;

  bool in_orphan_range= false;
  uint64_t orphan_start= 0;
  uint64_t orphan_count= 0;
  for (uint64_t page_id= 2; page_id < complete_pages; ++page_id)
  {
    if (mylite_page_id_in_ranges(page_id, protected_ranges))
    {
      if (in_orphan_range &&
          !mylite_add_free_page_range_locked(orphan_start, orphan_count,
                                             free_ranges))
        return false;
      in_orphan_range= false;
      orphan_count= 0;
      continue;
    }

    if (!in_orphan_range)
    {
      in_orphan_range= true;
      orphan_start= page_id;
      orphan_count= 1;
    }
    else
      ++orphan_count;
  }

  if (in_orphan_range &&
      !mylite_add_free_page_range_locked(orphan_start, orphan_count,
                                         free_ranges))
    return false;

  return mylite_normalize_free_page_ranges_locked(free_ranges);
}

static bool mylite_page_ranges_overlap(
    const Mylite_free_page_range &left,
    const Mylite_free_page_range &right)
{
  const uint64_t left_end= left.page_id + left.page_count;
  const uint64_t right_end= right.page_id + right.page_count;
  return left.page_id < right_end && right.page_id < left_end;
}

static bool mylite_page_id_in_ranges(
    uint64_t page_id, const std::vector<Mylite_free_page_range> &ranges)
{
  for (const Mylite_free_page_range &range : ranges)
  {
    if (page_id < range.page_id)
      return false;
    if (page_id < range.page_id + range.page_count)
      return true;
  }
  return false;
}

static bool mylite_page_range_fits_file(
    const Mylite_free_page_range &range, uint64_t file_size)
{
  if (range.page_id < 2 || range.page_count == 0 ||
      range.page_count > ~static_cast<uint64_t>(0) - range.page_id)
    return false;

  const uint64_t end_page_id= range.page_id + range.page_count;
  if (end_page_id >
      ~static_cast<uint64_t>(0) / mylite_catalog_page_size)
    return false;

  return end_page_id * mylite_catalog_page_size <= file_size;
}

static uint64_t mylite_page_offset(uint64_t page_id)
{
  return page_id * mylite_catalog_page_size;
}

static uint64_t mylite_align_to_page(uint64_t offset)
{
  const uint64_t remainder= offset % mylite_catalog_page_size;
  return remainder == 0 ? offset : offset + mylite_catalog_page_size -
                                   remainder;
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
  mylite_set_catalog_errno_error();
  sql_print_error("MyLite: catalog %s failed for %s: %s", operation,
                  path.c_str(), strerror(errno));
}

static int mylite_deinit_func(void *)
{
  std::lock_guard<std::mutex> guard(mylite_catalog_mutex);
  mylite_clear_frm_definitions_locked();
  mylite_catalog_loaded= false;
  mylite_catalog_load_failed= false;
  mylite_loaded_catalog_header_valid= false;
  mylite_loaded_catalog_header= { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  mylite_free_page_ranges.clear();
  mylite_pending_free_page_ranges.clear();
  mylite_dirty_writer_thd= nullptr;
  mylite_release_catalog_file_locked();
  return 0;
}

static void mylite_release_catalog_file_locked()
{
  if (mylite_catalog_fd < 0)
    return;

  const int fd= mylite_catalog_fd;
  const std::string path= mylite_catalog_locked_path;
  mylite_catalog_fd= -1;
  mylite_catalog_locked_path.clear();

  if (my_lock(fd, F_UNLCK, 0, F_TO_EOF,
              MYF(MY_FORCE_LOCK | MY_NO_WAIT)) != 0)
  {
    const int saved_errno= my_errno > 0 ? my_errno :
                           errno > 0 ? errno : EAGAIN;
    sql_print_error("MyLite: catalog unlock failed for %s: %s",
                    path.c_str(), strerror(saved_errno));
  }
  if (close(fd) != 0)
    mylite_log_catalog_error("close", path);
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
