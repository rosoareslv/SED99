/* Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

/**
  @file item_regexp_func.cc

  Implementation of the regular expression function object. This file deals
  only with the function objects and their place in the Item hierarchy. All
  interactions with the regular expression library are done through a facade
  object, which is instantiated in fix_fields() and deleted in cleanup().
*/

#include "sql/item_regexp_func.h"

#include "my_dbug.h"
#include "nullable.h"
#include "sql/item_func.h"  // agg_arg_charsets_for_comparison()
#include "sql/sql_class.h"  // THD
#include "sql/sql_lex.h"    // Disable_semijoin_flattening

/**
  Transforms a textual option string from the user to a bitmask of ICU flags.
  The match_parameter is a string of characters where each character controls
  some characteristic of the regular expression matching:

  - `'c'` means case-sensitive matching.
  - `'i'` means case-insensitive matching.
  - `'m'` activates multi-line matching.
  - `'n'` lets the period match the newline character.
  - `'u'` means that only UNIX line endings are recognized as line endings.

  @param options_string The options to parse.
  @param case_sensitive_is_default Whether case-sensitive matching is the
  default.
  @param[out] flags These flags will be updated (overridden) with the
  values of flags in options_string.

  @retval false String parsed successfully and *flags is updated accordingly.
  @retval true String could not be parsed, *flags is untouched.
*/
static bool ParseRegexpOptions(const std::string &options_string,
                               bool case_sensitive_is_default,
                               uint32_t *flags) {
  uint32_t result = case_sensitive_is_default ? 0 : UREGEX_CASE_INSENSITIVE;
  for (char c : options_string) {
    switch (c) {
      case 'c':
        result &= ~UREGEX_CASE_INSENSITIVE;
        break;
      case 'i':
        result |= UREGEX_CASE_INSENSITIVE;
        break;
      case 'm':
        result |= UREGEX_MULTILINE;
        break;
      case 'n':
        result |= UREGEX_DOTALL;
        break;
      case 'u':
        result |= UREGEX_UNIX_LINES;
        break;
      default:
        return true;
    }
  }
  *flags = result;
  return false;
}

bool Item_func_regexp::resolve_type(THD *) {
  return agg_arg_charsets_for_comparison(m_cmp_collation, args, 2);
}

bool Item_func_regexp::fix_fields(THD *thd, Item **arguments) {
  if (Item_func::fix_fields(thd, arguments)) return true;

  bool is_case_sensitive =
      ((m_cmp_collation.collation->state & MY_CS_CSSORT) != 0 ||
       (m_cmp_collation.collation->state & MY_CS_BINSORT) != 0);

  uint32_t icu_flags;
  // match_parameter overrides coercion type.
  if (ParseRegexpOptions(match_parameter(), is_case_sensitive, &icu_flags)) {
    my_error(ER_WRONG_ARGUMENTS, MYF(0), func_name());
    return true;
  }

  // Make sure that cleanup() deleted the facade in case of re-resolution.
  DBUG_ASSERT(m_facade.get() == nullptr);
  m_facade =
      make_unique_destroy_only<regexp::Regexp_facade>(*THR_MALLOC, icu_flags);

  fixed = true;

  // There may be errors evaluating arguments.
  return thd->is_error();
}

void Item_func_regexp::cleanup() {
  m_facade.reset();
  Item_func::cleanup();
}

bool Item_func_regexp_instr::fix_fields(THD *thd, Item **arguments) {
  if (Item_func_regexp::fix_fields(thd, arguments)) return true;

  if (return_option() != 0 && return_option() != 1) {
    my_error(ER_WRONG_ARGUMENTS, MYF(0),
             "regexp_instr: return_option must be 1 or 0.");
    return true;
  }

  return false;
}

longlong Item_func_regexp_instr::val_int() {
  DBUG_ENTER("Item_func_regexp_instr::val_int");
  DBUG_ASSERT(fixed);
  if (m_facade->SetPattern(pattern())) DBUG_RETURN(0);
  Mysql::Nullable<int32_t> result =
      m_facade->Find(subject(), position(), occurrence(), return_option());
  if (result.has_value()) DBUG_RETURN(result.value());
  null_value = true;
  DBUG_RETURN(0);
}

longlong Item_func_regexp_like::val_int() {
  DBUG_ASSERT(fixed);
  DBUG_ENTER("Item_func_regexp_like::val_int");
  if (m_facade->SetPattern(pattern())) DBUG_RETURN(0);
  Mysql::Nullable<bool> result =
      m_facade->Matches(subject(), position(), occurrence());
  null_value = !result.has_value();
  if (null_value) DBUG_RETURN(0);

  DBUG_RETURN(result.value());
}

bool Item_func_regexp_replace::resolve_type(THD *thd) {
  if (Item_func_regexp::resolve_type(thd)) return true;
  collation.collation = regexp::regexp_lib_charset;
  return false;
}

String *Item_func_regexp_replace::val_str(String *buf) {
  DBUG_ASSERT(fixed);
  if (m_facade->SetPattern(pattern())) {
    null_value = true;
    return nullptr;
  }

  if (position() < 1) {
    my_error(ER_WRONG_PARAMETERS_TO_NATIVE_FCT, MYF(0), func_name());
    null_value = true;
    return nullptr;
  }

  String *result = m_facade->Replace(subject(), replacement(), position(),
                                     occurrence(), buf);
  null_value = (result == nullptr);
  return result;
}

bool Item_func_regexp_substr::resolve_type(THD *thd) {
  if (Item_func_regexp::resolve_type(thd)) return true;
  collation.collation = regexp::regexp_lib_charset;
  return false;
}

String *Item_func_regexp_substr::val_str(String *buf) {
  DBUG_ASSERT(fixed);
  if (m_facade->SetPattern(pattern())) {
    null_value = true;
    return nullptr;
  }
  if (position() < 1) {
    my_error(ER_WRONG_PARAMETERS_TO_NATIVE_FCT, MYF(0), func_name());
    null_value = true;
    return nullptr;
  }
  String *result = m_facade->Substr(subject(), position(), occurrence(), buf);
  null_value = (result == nullptr);
  return result;
}

using regexp::icu_version_string;

Item_func_icu_version::Item_func_icu_version(const POS &pos)
    : Item_static_string_func(pos, NAME_STRING("icu_version()"),
                              icu_version_string(),
                              strlen(icu_version_string()), system_charset_info,
                              DERIVATION_SYSCONST) {}

bool Item_func_icu_version::itemize(Parse_context *pc, Item **res) {
  if (skip_itemize(res)) return false;
  if (super::itemize(pc, res)) return true;
  pc->thd->lex->set_stmt_unsafe(LEX::BINLOG_STMT_UNSAFE_SYSTEM_FUNCTION);
  return false;
}
