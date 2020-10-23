/*
 *      Copyright (C) 2015-2017 Team Kodi
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

#include "JoystickTranslator.h"
#include "guilib/LocalizeStrings.h"
#include "input/joysticks/DriverPrimitive.h"
#include "utils/StringUtils.h"

using namespace KODI;
using namespace JOYSTICK;

const char* CJoystickTranslator::HatStateToString(HAT_STATE state)
{
  switch (state)
  {
    case HAT_STATE::UP:        return "UP";
    case HAT_STATE::DOWN:      return "DOWN";
    case HAT_STATE::RIGHT:     return "RIGHT";
    case HAT_STATE::LEFT:      return "LEFT";
    case HAT_STATE::RIGHTUP:   return "UP RIGHT";
    case HAT_STATE::RIGHTDOWN: return "DOWN RIGHT";
    case HAT_STATE::LEFTUP:    return "UP LEFT";
    case HAT_STATE::LEFTDOWN:  return "DOWN LEFT";
    case HAT_STATE::UNPRESSED:
    default:
      break;
  }

  return "RELEASED";
}

const char* CJoystickTranslator::TranslateDirection(ANALOG_STICK_DIRECTION dir)
{
  switch (dir)
  {
  case ANALOG_STICK_DIRECTION::UP:    return "up";
  case ANALOG_STICK_DIRECTION::DOWN:  return "down";
  case ANALOG_STICK_DIRECTION::RIGHT: return "right";
  case ANALOG_STICK_DIRECTION::LEFT:  return "left";
  default:
    break;
  }

  return "";
}

ANALOG_STICK_DIRECTION CJoystickTranslator::TranslateDirection(const std::string &dir)
{
  if (dir == "up")    return ANALOG_STICK_DIRECTION::UP;
  if (dir == "down")  return ANALOG_STICK_DIRECTION::DOWN;
  if (dir == "right") return ANALOG_STICK_DIRECTION::RIGHT;
  if (dir == "left")  return ANALOG_STICK_DIRECTION::LEFT;

  return ANALOG_STICK_DIRECTION::UNKNOWN;
}

SEMIAXIS_DIRECTION CJoystickTranslator::PositionToSemiAxisDirection(float position)
{
  if      (position > 0) return SEMIAXIS_DIRECTION::POSITIVE;
  else if (position < 0) return SEMIAXIS_DIRECTION::NEGATIVE;

  return SEMIAXIS_DIRECTION::ZERO;
}

ANALOG_STICK_DIRECTION CJoystickTranslator::VectorToAnalogStickDirection(float x, float y)
{
  if      (y >= x && y >  -x) return ANALOG_STICK_DIRECTION::UP;
  else if (y <  x && y >= -x) return ANALOG_STICK_DIRECTION::RIGHT;
  else if (y <= x && y <  -x) return ANALOG_STICK_DIRECTION::DOWN;
  else if (y >  x && y <= -x) return ANALOG_STICK_DIRECTION::LEFT;

  return ANALOG_STICK_DIRECTION::UNKNOWN;
}

std::string CJoystickTranslator::GetPrimitiveName(const CDriverPrimitive& primitive)
{
  std::string primitiveTemplate;

  switch (primitive.Type())
  {
    case PRIMITIVE_TYPE::BUTTON:
      primitiveTemplate = g_localizeStrings.Get(35015); // "Button %d"
      break;
    case PRIMITIVE_TYPE::SEMIAXIS:
      primitiveTemplate = g_localizeStrings.Get(35016); // "Axis %d"
      break;
    default: break;
  }

  return StringUtils::Format(primitiveTemplate.c_str(), primitive.Index());
}
