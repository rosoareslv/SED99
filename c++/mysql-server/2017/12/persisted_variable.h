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

#ifndef PERSISTED_VARIABLE_H_INCLUDED
#define PERSISTED_VARIABLE_H_INCLUDED

#include <stddef.h>
#include <map>
#include <string>
#include <vector>

#include "my_alloc.h"
#include "my_inttypes.h"
#include "my_psi_config.h"
#include "mysql/components/services/mysql_mutex_bits.h"
#include "mysql/psi/psi_base.h"
#include "sql_string.h"
#include "sql/json_dom.h"

class THD;
class set_var;
class sys_var;
struct MYSQL_FILE;

/**
  STRUCT st_persist_var

  This structure represents information of a variable which is to
  be persisted in mysql-auto.cnf file.
*/
struct st_persist_var
{
  std::string key;
  std::string value;
  ulonglong timestamp;
  std::string user;
  std::string host;
  st_persist_var();
  st_persist_var(THD *thd);
  st_persist_var(const st_persist_var& var);
  st_persist_var(const std::string key, const std::string value,
                 const ulonglong timestamp, const std::string user,
                 const std::string host);
};

/**
  CLASS Persisted_variables_cache
    Holds <name,value> pair of all options which needs to be persisted
    to a file.

  OVERVIEW
  --------
  When first SET PERSIST statement is executed we instantiate
  Persisted_variables_cache which loads the config file if present into
  m_persist_variables vector. This is a singleton operation. m_persist_variables
  is an in-memory copy of config file itself. If the SET statement passes then
  this in-memory is updated and flushed to file as an atomic operation.

  Next SET PERSIST statement would only update the in-memory copy and sync
  to config file instead of loading the file again.
*/

#ifdef HAVE_PSI_INTERFACE
void my_init_persist_psi_keys(void);
#endif /* HAVE_PSI_INTERFACE */

class Persisted_variables_cache
{
public:
  int init(int *argc, char ***argv);
  static Persisted_variables_cache* get_instance();
  /**
    Update in-memory copy for every SET PERSIST statement
  */
  void set_variable(THD *thd, set_var *system_var);
  /**
    Flush in-memory copy to persistent file
  */
  bool flush_to_file();
  /**
    Read options from persistent file
  */
  int read_persist_file();
  /**
    Search for persisted config file and if found read persistent options
  */
  bool load_persist_file();
  /**
    Set persisted options
  */
  bool set_persist_options(bool plugin_options= false);
  /**
    Reset persisted options
  */
  bool reset_persisted_variables(THD *thd, const char* name, bool if_exists);
  /**
    Get persisted variables
  */
  std::vector<st_persist_var>* get_persisted_variables();
  /**
    Get persisted static variables
  */
  std::map<std::string, st_persist_var>* get_persist_ro_variables();
  /**
    append read only persisted variables to command line options with a
    separator.
  */
  bool append_read_only_variables(int *argc, char ***argv,
    bool plugin_options= false);
  void cleanup();

private:
  /* Helper function to get variable value */
  static String* get_variable_value(THD *thd,
    sys_var *system_var, String *str);
  /* Helper function to get variable name */
  static const char* get_variable_name(sys_var *system_var);
  /* Helper function to construct json formatted string */
  static String* construct_json_string(std::string name, std::string value,
    ulonglong timestamp, std::string user, std::string host, String *dest);
  /* Helper function to extract variables from json formatted string */
  bool extract_variables_from_json(Json_dom *dom, bool is_read_only= false);


private:
  /* Helper functions for file IO */
  bool open_persist_file(int flag);
  void close_persist_file();

private:
  /* In memory copy of persistent config file */
  std::vector<st_persist_var> m_persist_variables;
  /* copy of plugin variables whose plugin is not yet installed */
  std::vector<st_persist_var> m_persist_plugin_variables;
  /* In memory copy of read only persistent variables */
  std::map<std::string, st_persist_var> m_persist_ro_variables;

  mysql_mutex_t m_LOCK_persist_variables;
  static Persisted_variables_cache* m_instance;

  /* File handler members */
  MYSQL_FILE *m_fd;
  std::string m_persist_filename;
  mysql_mutex_t m_LOCK_persist_file;
  /* Memory for read only persisted options */
  MEM_ROOT ro_persisted_argv_alloc{PSI_NOT_INSTRUMENTED, 512};
  /* Memory for read only persisted plugin options */
  MEM_ROOT ro_persisted_plugin_argv_alloc{PSI_NOT_INSTRUMENTED, 512};
};

#endif /* PERSISTED_VARIABLE_H_INCLUDED */
