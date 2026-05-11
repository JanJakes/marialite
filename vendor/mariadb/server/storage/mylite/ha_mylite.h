/* Copyright (c) 2026, MyLite contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License. */

#ifndef HA_MYLITE_H
#define HA_MYLITE_H

#include "my_global.h"
#include "handler.h"
#include "my_base.h"
#include "thr_lock.h"

#include <cstdint>
#include <string>
#include <vector>

class Mylite_share : public Handler_share
{
public:
  THR_LOCK lock;

  Mylite_share()
  {
    thr_lock_init(&lock);
  }

  ~Mylite_share()
  {
    thr_lock_delete(&lock);
  }
};

class ha_mylite final : public handler
{
public:
  ha_mylite(handlerton *hton, TABLE_SHARE *table_arg);

  ulonglong table_flags() const override
  {
    return HA_BINLOG_STMT_CAPABLE | HA_REC_NOT_IN_SEQ |
           HA_STATS_RECORDS_IS_EXACT;
  }

  ulong index_flags(uint, uint, bool) const override
  {
    return HA_READ_NEXT | HA_READ_PREV | HA_READ_ORDER | HA_READ_RANGE;
  }

  uint max_supported_record_length() const override
  {
    return HA_MAX_REC_LENGTH;
  }

  uint max_supported_keys() const override
  {
    return MAX_KEY;
  }

  uint max_supported_key_parts() const override
  {
    return MAX_REF_PARTS;
  }

  uint max_supported_key_length() const override
  {
    return MAX_DATA_LENGTH_FOR_KEY;
  }

  IO_AND_CPU_COST scan_time() override
  {
    IO_AND_CPU_COST cost;
    cost.io= 0;
    cost.cpu= 0;
    return cost;
  }

  IO_AND_CPU_COST rnd_pos_time(ha_rows) override
  {
    IO_AND_CPU_COST cost;
    cost.io= 0;
    cost.cpu= 0;
    return cost;
  }

  int open(const char *name, int mode, uint test_if_locked) override;
  int close() override;
  int create(const char *name, TABLE *table_arg,
             HA_CREATE_INFO *create_info) override;
  int delete_table(const char *name) override;
  int write_row(const uchar *buf) override;
  int update_row(const uchar *old_data, const uchar *new_data) override;
  int delete_row(const uchar *buf) override;
  int index_read_map(uchar *buf, const uchar *key, key_part_map keypart_map,
                     enum ha_rkey_function find_flag) override;
  int index_next(uchar *buf) override;
  int index_prev(uchar *buf) override;
  int index_first(uchar *buf) override;
  int index_last(uchar *buf) override;
  int rnd_init(bool scan) override;
  int rnd_next(uchar *buf) override;
  int rnd_pos(uchar *buf, uchar *pos) override;
  void position(const uchar *record) override;
  int info(uint flag) override;
  ha_rows records_in_range(uint inx, const key_range *min_key,
                           const key_range *max_key,
                           page_range *pages) override;
  void get_auto_increment(ulonglong offset, ulonglong increment,
                          ulonglong nb_desired_values,
                          ulonglong *first_value,
                          ulonglong *nb_reserved_values) override;
  int reset_auto_increment(ulonglong value) override;
  void update_create_info(HA_CREATE_INFO *create_info) override;
  int external_lock(THD *thd, int lock_type) override;
  THR_LOCK_DATA **store_lock(THD *thd, THR_LOCK_DATA **to,
                             enum thr_lock_type lock_type) override;

protected:
  int rename_table(const char *from, const char *to) override;

private:
  Mylite_share *get_share();

  THR_LOCK_DATA lock;
  Mylite_share *share= nullptr;
  std::string db_name;
  std::string opened_table_name;
  size_t scan_index= 0;
  uint64_t current_rowid= 0;
  std::vector<uint64_t> index_cursor_rowids;
  size_t index_cursor_position= 0;
  std::vector<uchar> blob_read_buffer;
};

#endif
