/*
   Copyright (c) 2015, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/


#include "sql/ndb_name_util.h"

#include "m_string.h"       // strend()
#include "sql/sql_table.h"  // filename_to_table_name()
#include "sql/table.h"      // tmp_file_prefix
/**
  Set a given location from full pathname to database name.
*/

void ndb_set_dbname(const char *path_name, char *dbname)
{
  char *end, *ptr, *tmp_name;
  char tmp_buff[FN_REFLEN + 1];

  tmp_name= tmp_buff;
  /* Scan name from the end */
  ptr= strend(path_name)-1;
  while (ptr >= path_name && *ptr != '\\' && *ptr != '/') {
    ptr--;
  }
  ptr--;
  end= ptr;
  while (ptr >= path_name && *ptr != '\\' && *ptr != '/') {
    ptr--;
  }
  uint name_len= (uint)(end - ptr);
  memcpy(tmp_name, ptr + 1, name_len);
  tmp_name[name_len]= '\0';
  filename_to_tablename(tmp_name, dbname, sizeof(tmp_buff) - 1);
}



/**
  Set a given location from full pathname to table file.
*/

void
ndb_set_tabname(const char *path_name, char * tabname)
{
  char *end, *ptr, *tmp_name;
  char tmp_buff[FN_REFLEN + 1];

  tmp_name= tmp_buff;
  /* Scan name from the end */
  end= strend(path_name)-1;
  ptr= end;
  while (ptr >= path_name && *ptr != '\\' && *ptr != '/') {
    ptr--;
  }
  uint name_len= (uint)(end - ptr);
  memcpy(tmp_name, ptr + 1, end - ptr);
  tmp_name[name_len]= '\0';
  filename_to_tablename(tmp_name, tabname, sizeof(tmp_buff) - 1);
}

bool ndb_name_is_temp(const char *name)
{
  return is_prefix(name, tmp_file_prefix) == 1;
}


bool ndb_name_is_blob_prefix(const char* name)
{
  return is_prefix(name, "NDB$BLOB");
}
