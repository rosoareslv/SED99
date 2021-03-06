/* Copyright (c) 2016, 2017, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef TABLE_EES_BY_THREAD_BY_EVENT_NAME_H
#define TABLE_EES_BY_THREAD_BY_EVENT_NAME_H

/**
  @file storage/perfschema/table_ees_by_thread_by_error.h
  Table EVENTS_ERRORS_SUMMARY_BY_THREAD_BY_ERROR (declarations).
*/

#include <sys/types.h>

#include "my_inttypes.h"
#include "storage/perfschema/pfs_column_types.h"
#include "storage/perfschema/pfs_engine_table.h"
#include "storage/perfschema/pfs_error.h"
#include "storage/perfschema/pfs_instr.h"
#include "storage/perfschema/pfs_instr_class.h"
#include "storage/perfschema/table_helper.h"

/**
  @addtogroup Performance_schema_tables
  @{
*/

class PFS_index_ees_by_thread_by_error : public PFS_engine_index
{
public:
  PFS_index_ees_by_thread_by_error()
    : PFS_engine_index(&m_key_1, &m_key_2),
      m_key_1("THREAD_ID"),
      m_key_2("ERROR_NUMBER")
  {
  }

  ~PFS_index_ees_by_thread_by_error()
  {
  }

  virtual bool match(PFS_thread *pfs);
  virtual bool match_error_index(uint error_index);

private:
  PFS_key_thread_id m_key_1;
  PFS_key_error_number m_key_2;
};

/**
  A row of table
  PERFORMANCE_SCHEMA.EVENTS_ERRORS_SUMMARY_BY_THREAD_BY_ERROR.
*/
struct row_ees_by_thread_by_error
{
  /** Column THREAD_ID. */
  ulonglong m_thread_internal_id;
  /** Columns ERROR_NUMBER, ERROR_NAME, COUNT_STAR. */
  PFS_error_stat_row m_stat;
};

/**
  Position of a cursor on
  PERFORMANCE_SCHEMA.EVENTS_ERRORS_SUMMARY_BY_THREAD_BY_ERROR.
  Index 1 on thread (0 based).
  Index 2 on error (0 based)
*/
struct pos_ees_by_thread_by_error : public PFS_double_index
{
  pos_ees_by_thread_by_error() : PFS_double_index(0, 0)
  {
  }

  inline void
  reset(void)
  {
    m_index_1 = 0;
    m_index_2 = 0;
  }

  inline void
  next_thread(void)
  {
    m_index_1++;
    m_index_2 = 0;
  }

  inline bool
  has_more_error(void)
  {
    return (m_index_2 < max_server_errors);
  }

  inline void
  next_error(void)
  {
    m_index_2++;
  }
};

/** Table PERFORMANCE_SCHEMA.EVENTS_ERRORS_SUMMARY_BY_THREAD_BY_ERROR. */
class table_ees_by_thread_by_error : public PFS_engine_table
{
public:
  /** Table share */
  static PFS_engine_table_share m_share;
  static PFS_engine_table *create(PFS_engine_table_share *);
  static int delete_all_rows();
  static ha_rows get_row_count();

  virtual void reset_position(void);

  virtual int rnd_init(bool scan);
  virtual int rnd_next();
  virtual int rnd_pos(const void *pos);

  virtual int index_init(uint idx, bool sorted);
  virtual int index_next();

protected:
  virtual int read_row_values(TABLE *table,
                              unsigned char *buf,
                              Field **fields,
                              bool read_all);

  table_ees_by_thread_by_error();

public:
  ~table_ees_by_thread_by_error()
  {
  }

protected:
  int make_row(PFS_thread *thread, int error_index);

private:
  /** Table share lock. */
  static THR_LOCK m_table_lock;
  /** Table definition. */
  static Plugin_table m_table_def;

  /** Current row. */
  row_ees_by_thread_by_error m_row;
  /** Current position. */
  pos_ees_by_thread_by_error m_pos;
  /** Next position. */
  pos_ees_by_thread_by_error m_next_pos;

  PFS_index_ees_by_thread_by_error *m_opened_index;
};

/** @} */
#endif
