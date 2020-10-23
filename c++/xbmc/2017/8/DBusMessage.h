#pragma once
/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */
#include "system.h"
#ifdef HAS_DBUS
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

#include <dbus/dbus.h>

class CDBusError;

template<typename T>
struct ToDBusType;
template<> struct ToDBusType<bool> { static constexpr int TYPE = DBUS_TYPE_BOOLEAN; };
template<> struct ToDBusType<char*> { static constexpr int TYPE = DBUS_TYPE_STRING; };
template<> struct ToDBusType<const char*> { static constexpr int TYPE = DBUS_TYPE_STRING; };
template<> struct ToDBusType<std::uint8_t> { static constexpr int TYPE = DBUS_TYPE_BYTE; };
template<> struct ToDBusType<std::int16_t> { static constexpr int TYPE = DBUS_TYPE_INT16; };
template<> struct ToDBusType<std::uint16_t> { static constexpr int TYPE = DBUS_TYPE_UINT16; };
template<> struct ToDBusType<std::int32_t> { static constexpr int TYPE = DBUS_TYPE_INT32; };
template<> struct ToDBusType<std::uint32_t> { static constexpr int TYPE = DBUS_TYPE_UINT32; };
template<> struct ToDBusType<std::int64_t> { static constexpr int TYPE = DBUS_TYPE_INT64; };
template<> struct ToDBusType<std::uint64_t> { static constexpr int TYPE = DBUS_TYPE_UINT64; };
template<> struct ToDBusType<double> { static constexpr int TYPE = DBUS_TYPE_DOUBLE; };

template<typename T>
using ToDBusTypeFromPointer = ToDBusType<typename std::remove_pointer<T>::type>;

struct DBusMessageDeleter
{
  void operator()(DBusMessage* message) const;
};
using DBusMessagePtr = std::unique_ptr<DBusMessage, DBusMessageDeleter>;

class CDBusMessage
{
public:
  CDBusMessage(const char *destination, const char *object, const char *interface, const char *method);
  CDBusMessage(std::string const& destination, std::string const& object, std::string const& interface, std::string const& method);

  void AppendObjectPath(const char *object);

  template<typename T>
  void AppendArgument(const T arg)
  {
    AppendWithType(ToDBusType<T>::TYPE, &arg);
  }
  void AppendArgument(const char **arrayString, unsigned int length);

  template<typename TFirst>
  void AppendArguments(const TFirst first)
  {
    AppendArgument(first);
    // Recursion end
  }
  template<typename TFirst, typename... TArgs>
  void AppendArguments(const TFirst first, const TArgs... args)
  {
    AppendArgument(first);
    AppendArguments(args...);
  }

  /**
   * Retrieve simple arguments from DBus reply message
   *
   * You MUST use the correct fixed-width integer typedefs (e.g. std::uint16_t)
   * corresponding to the DBus types for the variables or you will get potentially
   * differing behavior between architectures since the DBus argument type detection
   * is based on the width of the type.
   *
   * Complex arguments (arrays, structs) are not supported.
   *
   * Returned pointers for strings are only valid until the instance of this class
   * is deleted.
   *
   * \throw std::logic_error if the message has no reply
   * \return whether all arguments could be retrieved (false on argument type
   *         mismatch or when more arguments were to be retrieved than there are
   *         in the message)
   */
  template<typename... TArgs>
  bool GetReplyArguments(TArgs*... args)
  {
    DBusMessageIter iter;
    if (!InitializeReplyIter(&iter))
    {
      return false;
    }
    return GetReplyArgumentsWithIter(&iter, args...);
  }

  DBusMessage *SendSystem();
  DBusMessage *SendSession();
  DBusMessage *SendSystem(CDBusError& error);
  DBusMessage *SendSession(CDBusError& error);

  bool SendAsyncSystem();
  bool SendAsyncSession();

  DBusMessage *Send(DBusBusType type);
  DBusMessage *Send(DBusBusType type, CDBusError& error);
  DBusMessage *Send(DBusConnection *con, CDBusError& error);
private:
  void AppendWithType(int type, const void* value);
  bool SendAsync(DBusBusType type);

  void PrepareArgument();

  bool InitializeReplyIter(DBusMessageIter* iter);
  bool CheckTypeAndGetValue(DBusMessageIter* iter, int expectType, void* dest);
  template<typename TFirst>
  bool GetReplyArgumentsWithIter(DBusMessageIter* iter, TFirst* first)
  {
    // Recursion end
    return CheckTypeAndGetValue(iter, ToDBusTypeFromPointer<TFirst>::TYPE, first);
  }
  template<typename TFirst, typename... TArgs>
  bool GetReplyArgumentsWithIter(DBusMessageIter* iter, TFirst* first, TArgs*... args)
  {
    if (!CheckTypeAndGetValue(iter, ToDBusTypeFromPointer<TFirst>::TYPE, first))
    {
      return false;
    }
    // Ignore return value, if we try to read past the end of the message this
    // will be catched by the type check (past-end type is DBUS_TYPE_INVALID)
    dbus_message_iter_next(iter);
    return GetReplyArgumentsWithIter(iter, args...);
  }

  DBusMessagePtr m_message;
  DBusMessagePtr m_reply;
  DBusMessageIter m_args;
  bool m_haveArgs;
};

template<>
void CDBusMessage::AppendArgument<bool>(const bool arg);
template<>
void CDBusMessage::AppendArgument<std::string>(const std::string arg);
#endif
