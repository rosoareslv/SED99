/*!
 \file Key.h
 \brief
 */
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

//! @todo Remove dependence on CAction
#include "Action.h"
#include "ActionIDs.h"

#include <string>
#include <stdint.h>

// Reserved 0 - 255
//  XBIRRemote.h
//  XINPUT_IR_REMOTE-*

/*
 * EventServer "gamepad" keys based on original Xbox controller
 */
// Analogue - don't change order
#define KEY_BUTTON_A                        256
#define KEY_BUTTON_B                        257
#define KEY_BUTTON_X                        258
#define KEY_BUTTON_Y                        259
#define KEY_BUTTON_BLACK                    260
#define KEY_BUTTON_WHITE                    261
#define KEY_BUTTON_LEFT_TRIGGER             262
#define KEY_BUTTON_RIGHT_TRIGGER            263

#define KEY_BUTTON_LEFT_THUMB_STICK         264
#define KEY_BUTTON_RIGHT_THUMB_STICK        265

#define KEY_BUTTON_RIGHT_THUMB_STICK_UP     266 // right thumb stick directions
#define KEY_BUTTON_RIGHT_THUMB_STICK_DOWN   267 // for defining different actions per direction
#define KEY_BUTTON_RIGHT_THUMB_STICK_LEFT   268
#define KEY_BUTTON_RIGHT_THUMB_STICK_RIGHT  269

// Digital - don't change order
#define KEY_BUTTON_DPAD_UP                  270
#define KEY_BUTTON_DPAD_DOWN                271
#define KEY_BUTTON_DPAD_LEFT                272
#define KEY_BUTTON_DPAD_RIGHT               273

#define KEY_BUTTON_START                    274
#define KEY_BUTTON_BACK                     275

#define KEY_BUTTON_LEFT_THUMB_BUTTON        276
#define KEY_BUTTON_RIGHT_THUMB_BUTTON       277

#define KEY_BUTTON_LEFT_ANALOG_TRIGGER      278
#define KEY_BUTTON_RIGHT_ANALOG_TRIGGER     279

#define KEY_BUTTON_LEFT_THUMB_STICK_UP      280 // left thumb stick directions
#define KEY_BUTTON_LEFT_THUMB_STICK_DOWN    281 // for defining different actions per direction
#define KEY_BUTTON_LEFT_THUMB_STICK_LEFT    282
#define KEY_BUTTON_LEFT_THUMB_STICK_RIGHT   283

// 0xF000 -> 0xF200 is reserved for the keyboard; a keyboard press is either
#define KEY_VKEY            0xF000 // a virtual key/functional key e.g. cursor left
#define KEY_ASCII           0xF100 // a printable character in the range of TRUE ASCII (from 0 to 127) // FIXME make it clean and pure unicode! remove the need for KEY_ASCII
#define KEY_UNICODE         0xF200 // another printable character whose range is not included in this KEY code

// 0xE000 -> 0xEFFF is reserved for mouse actions
#define KEY_VMOUSE          0xEFFF

#define KEY_MOUSE_START            0xE000
#define KEY_MOUSE_CLICK            0xE000
#define KEY_MOUSE_RIGHTCLICK       0xE001
#define KEY_MOUSE_MIDDLECLICK      0xE002
#define KEY_MOUSE_DOUBLE_CLICK     0xE010
#define KEY_MOUSE_LONG_CLICK       0xE020
#define KEY_MOUSE_WHEEL_UP         0xE101
#define KEY_MOUSE_WHEEL_DOWN       0xE102
#define KEY_MOUSE_MOVE             0xE103
#define KEY_MOUSE_DRAG             0xE104
#define KEY_MOUSE_DRAG_START       0xE105
#define KEY_MOUSE_DRAG_END         0xE106
#define KEY_MOUSE_RDRAG            0xE107
#define KEY_MOUSE_RDRAG_START      0xE108
#define KEY_MOUSE_RDRAG_END        0xE109
#define KEY_MOUSE_NOOP             0xEFFF
#define KEY_MOUSE_END              0xEFFF

// 0xD000 -> 0xD0FF is reserved for WM_APPCOMMAND messages
#define KEY_APPCOMMAND      0xD000

#define KEY_INVALID         0xFFFF

#define ICON_TYPE_NONE          101
#define ICON_TYPE_PROGRAMS      102
#define ICON_TYPE_MUSIC         103
#define ICON_TYPE_PICTURES      104
#define ICON_TYPE_VIDEOS        105
#define ICON_TYPE_FILES         106
#define ICON_TYPE_WEATHER       107
#define ICON_TYPE_SETTINGS      109

#ifndef SWIG

/*!
  \ingroup actionkeys, mouse
  \brief Simple class for mouse events
  */
class CMouseEvent
{
public:
  CMouseEvent(int actionID, int state = 0, float offsetX = 0, float offsetY = 0)
  {
    m_id = actionID;
    m_state = state;
    m_offsetX = offsetX;
    m_offsetY = offsetY;
  };

  int    m_id;
  int    m_state;
  float  m_offsetX;
  float  m_offsetY;
};

/*!
  \ingroup actionkeys
  \brief
  */
class CKey
{
public:
  CKey(void);
  CKey(uint32_t buttonCode, uint8_t leftTrigger = 0, uint8_t rightTrigger = 0, float leftThumbX = 0.0f, float leftThumbY = 0.0f, float rightThumbX = 0.0f, float rightThumbY = 0.0f, float repeat = 0.0f);
  CKey(uint32_t buttonCode, unsigned int held);
  CKey(uint8_t vkey, wchar_t unicode, char ascii, uint32_t modifiers, unsigned int held);
  CKey(const CKey& key);
  void Reset();

  virtual ~CKey(void);
  CKey& operator=(const CKey& key);
  uint8_t GetLeftTrigger() const;
  uint8_t GetRightTrigger() const;
  float GetLeftThumbX() const;
  float GetLeftThumbY() const;
  float GetRightThumbX() const;
  float GetRightThumbY() const;
  float GetRepeat() const;
  bool FromKeyboard() const;
  bool IsAnalogButton() const;
  bool IsIRRemote() const;
  void SetFromService(bool fromService);
  bool GetFromService() const { return m_fromService; }

  inline uint32_t GetButtonCode() const { return m_buttonCode; }
  inline uint8_t  GetVKey() const       { return m_vkey; }
  inline wchar_t  GetUnicode() const    { return m_unicode; }
  inline char     GetAscii() const      { return m_ascii; }
  inline uint32_t GetModifiers() const  { return m_modifiers; };
  inline unsigned int GetHeld() const   { return m_held; }

  enum Modifier {
    MODIFIER_CTRL  = 0x00010000,
    MODIFIER_SHIFT = 0x00020000,
    MODIFIER_ALT   = 0x00040000,
    MODIFIER_RALT  = 0x00080000,
    MODIFIER_SUPER = 0x00100000,
    MODIFIER_META  = 0X00200000,
    MODIFIER_LONG  = 0X01000000
  };

private:
  uint32_t m_buttonCode;
  uint8_t  m_vkey;
  wchar_t  m_unicode;
  char     m_ascii;
  uint32_t m_modifiers;
  unsigned int m_held;

  uint8_t m_leftTrigger;
  uint8_t m_rightTrigger;
  float m_leftThumbX;
  float m_leftThumbY;
  float m_rightThumbX;
  float m_rightThumbY;
  float m_repeat; // time since last keypress
  bool m_fromService;
};
#endif //undef SWIG

