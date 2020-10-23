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

#include "DriverPrimitive.h"

using namespace KODI;
using namespace JOYSTICK;

CDriverPrimitive::CDriverPrimitive(void)
  : m_type(),
    m_driverIndex(0),
    m_hatDirection(),
    m_center(0),
    m_semiAxisDirection(),
    m_range(1)
{
}

CDriverPrimitive::CDriverPrimitive(PRIMITIVE_TYPE type, unsigned int index)
  : m_type(type),
    m_driverIndex(index),
    m_hatDirection(),
    m_center(0),
    m_semiAxisDirection(),
    m_range(1)
{
}

CDriverPrimitive::CDriverPrimitive(unsigned int hatIndex, HAT_DIRECTION direction)
  : m_type(HAT),
    m_driverIndex(hatIndex),
    m_hatDirection(direction),
    m_center(0),
    m_semiAxisDirection(),
    m_range(1)
{
}

CDriverPrimitive::CDriverPrimitive(unsigned int axisIndex, int center, SEMIAXIS_DIRECTION direction, unsigned int range)
  : m_type(SEMIAXIS),
    m_driverIndex(axisIndex),
    m_hatDirection(),
    m_center(center),
    m_semiAxisDirection(direction),
    m_range(range)
{
}

bool CDriverPrimitive::operator==(const CDriverPrimitive& rhs) const
{
  if (m_type == rhs.m_type)
  {
    switch (m_type)
    {
    case BUTTON:
    case MOTOR:
      return m_driverIndex == rhs.m_driverIndex;
    case HAT:
      return m_driverIndex == rhs.m_driverIndex && m_hatDirection == rhs.m_hatDirection;
    case SEMIAXIS:
      return m_driverIndex       == rhs.m_driverIndex &&
             m_center            == rhs.m_center &&
             m_semiAxisDirection == rhs.m_semiAxisDirection &&
             m_range             == rhs.m_range;
    default:
      return true;
    }
  }
  return false;
}

bool CDriverPrimitive::operator<(const CDriverPrimitive& rhs) const
{
  if (m_type < rhs.m_type) return true;
  if (m_type > rhs.m_type) return false;

  // Driver index is common to all valid primitives
  if (m_type != UNKNOWN)
  {
    if (m_driverIndex < rhs.m_driverIndex) return true;
    if (m_driverIndex > rhs.m_driverIndex) return false;
  }

  if (m_type == HAT)
  {
    if (m_hatDirection < rhs.m_hatDirection) return true;
    if (m_hatDirection > rhs.m_hatDirection) return false;
  }

  if (m_type == SEMIAXIS)
  {
    if (m_center < rhs.m_center) return true;
    if (m_center > rhs.m_center) return false;

    if (m_semiAxisDirection < rhs.m_semiAxisDirection) return true;
    if (m_semiAxisDirection > rhs.m_semiAxisDirection) return false;

    if (m_range < rhs.m_range) return true;
    if (m_range > rhs.m_range) return false;
  }

  return false;
}

bool CDriverPrimitive::IsValid(void) const
{
  if (m_type == BUTTON || m_type == MOTOR)
    return true;

  if (m_type == HAT)
  {
    return m_hatDirection == HAT_DIRECTION::UP    ||
           m_hatDirection == HAT_DIRECTION::DOWN  ||
           m_hatDirection == HAT_DIRECTION::RIGHT ||
           m_hatDirection == HAT_DIRECTION::LEFT;
  }

  if (m_type == SEMIAXIS)
  {
    unsigned int maxRange = 1;

    switch (m_center)
    {
    case -1:
    {
      if (m_semiAxisDirection != SEMIAXIS_DIRECTION::POSITIVE)
        return false;
      maxRange = 2;
      break;
    }
    case 0:
    {
      if (m_semiAxisDirection != SEMIAXIS_DIRECTION::POSITIVE &&
          m_semiAxisDirection != SEMIAXIS_DIRECTION::NEGATIVE)
        return false;
      break;
    }
    case 1:
    {
      if (m_semiAxisDirection != SEMIAXIS_DIRECTION::POSITIVE)
        return false;
      maxRange = 2;
      break;
    }
    default:
      break;
    }

    return 1 <= m_range && m_range <= maxRange;
  }

  return false;
}
