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
    return HA_BINLOG_STMT_CAPABLE;
  }

  ulong index_flags(uint, uint, bool) const override
  {
    return 0;
  }

  uint max_supported_record_length() const override
  {
    return HA_MAX_REC_LENGTH;
  }

  uint max_supported_keys() const override
  {
    return 0;
  }

  uint max_supported_key_parts() const override
  {
    return 0;
  }

  uint max_supported_key_length() const override
  {
    return 0;
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
  int rnd_init(bool scan) override;
  int rnd_next(uchar *buf) override;
  int rnd_pos(uchar *buf, uchar *pos) override;
  void position(const uchar *record) override;
  int info(uint flag) override;
  int external_lock(THD *thd, int lock_type) override;
  THR_LOCK_DATA **store_lock(THD *thd, THR_LOCK_DATA **to,
                             enum thr_lock_type lock_type) override;

protected:
  int rename_table(const char *from, const char *to) override;

private:
  Mylite_share *get_share();

  THR_LOCK_DATA lock;
  Mylite_share *share= nullptr;
};

#endif
