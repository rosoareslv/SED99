/*
 *      Copyright (C) 2014-2017 Team Kodi
 *      http://kodi.tv
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
 *  along with this Program; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */
#pragma once

/*!
 \file
 \ingroup joystick
 */

#include <set>
#include <string>

namespace KODI
{
namespace JOYSTICK
{
  /*!
   * \brief Name of a physical feature belonging to the joystick
   */
  using FeatureName = std::string;

  /*!
   * \brief Types of features used in the joystick library
   *
   * Available types:
   *
   *   1) scalar[*]
   *   2) analog stick
   *   3) accelerometer
   *   4) rumble motor
   *   5) relative pointer
   *   6) absolute pointer
   *
   * [*] All three driver primitives (buttons, hats and axes) have a state that
   *     can be represented using a single scalar value. For this reason,
   *     features that map to a single primitive are called "scalar features".
   */
  enum class FEATURE_TYPE
  {
    UNKNOWN,
    SCALAR,
    ANALOG_STICK,
    ACCELEROMETER,
    MOTOR,
    RELPOINTER,
    ABSPOINTER,
  };

  /*!
   * \brief Categories of features used in the joystick library
   */
  enum class FEATURE_CATEGORY
  {
    UNKNOWN,
    FACE,
    SHOULDER,
    TRIGGER,
    ANALOG_STICK,
    ACCELEROMETER,
    HAPTICS,
    MOUSE_BUTTON,
    POINTER,
    LIGHTGUN,
    OFFSCREEN, // Virtual button to shoot light gun offscreen
    KEY, // A keyboard key
    KEYPAD, // A key on a numeric keymap, including star and pound
    HARDWARE, // A button or functionality on the console
  };

  /*!
   * \brief Direction arrows on the hat (directional pad)
   */
  enum class HAT_DIRECTION
  {
    UNKNOWN = 0x0,
    UP      = 0x1,
    DOWN    = 0x2,
    RIGHT   = 0x4,
    LEFT    = 0x8,
  };

  /*!
   * \brief Typedef for analog stick directions
   */
  using ANALOG_STICK_DIRECTION = HAT_DIRECTION;

  /*!
   * \brief States in which a hat can be
   */
  enum class HAT_STATE
  {
    UNPRESSED = 0x0,    /*!< @brief no directions are pressed */
    UP        = 0x1,    /*!< @brief only up is pressed */
    DOWN      = 0x2,    /*!< @brief only down is pressed */
    RIGHT     = 0x4,    /*!< @brief only right is pressed */
    LEFT      = 0x8,    /*!< @brief only left is pressed */
    RIGHTUP   = RIGHT | UP,
    RIGHTDOWN = RIGHT | DOWN,
    LEFTUP    = LEFT  | UP,
    LEFTDOWN  = LEFT  | DOWN,
  };

  /*!
   * \brief Directions in which a semiaxis can point
   */
  enum class SEMIAXIS_DIRECTION
  {
    NEGATIVE = -1,  // semiaxis lies in the interval [-1.0, 0.0]
    ZERO     =  0,  // semiaxis is unknown or invalid
    POSITIVE =  1,  // semiaxis lies in the interval [0.0, 1.0]
  };

  /*!
   * \brief Types of input available for scalar features
   */
  enum class INPUT_TYPE
  {
    UNKNOWN,
    DIGITAL,
    ANALOG,
  };

  /*!
  * \brief Type of driver primitive
  */
  enum PRIMITIVE_TYPE
  {
    UNKNOWN = 0, // primitive has no type (invalid)
    BUTTON,      // a digital button
    HAT,         // one of the four direction arrows on a D-pad
    SEMIAXIS,    // the positive or negative half of an axis
    MOTOR,       // a rumble motor
  };
  
  /*!
   * \ingroup joystick
   * \brief Action entry in joystick.xml
   */
  struct KeymapAction
  {
    unsigned int actionId;
    std::string actionString;
    unsigned int holdTimeMs;
    std::set<std::string> hotkeys;

    bool operator<(const KeymapAction &rhs) const
    {
      return holdTimeMs < rhs.holdTimeMs;
    }
  };

  /*!
   * \ingroup joystick
   * \brief Container that sorts action entries by their holdtime
   */
  using KeymapActions = std::set<KeymapAction>;
}
}
