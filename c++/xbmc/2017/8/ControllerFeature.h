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
#pragma once

#include "ControllerTypes.h"
#include "input/joysticks/JoystickTypes.h"

#include <string>

class TiXmlElement;

namespace KODI
{
namespace GAME
{

class CControllerFeature
{
public:
  CControllerFeature(void) { Reset(); }
  CControllerFeature(const CControllerFeature& other) { *this = other; }

  void Reset(void);

  CControllerFeature& operator=(const CControllerFeature& rhs);

  JOYSTICK::FEATURE_TYPE Type(void) const { return m_type; }
  JOYSTICK::FEATURE_CATEGORY Category(void) const { return m_category; }
  std::string            CategoryLabel(void) const;
  const std::string&     Name(void) const       { return m_strName; }
  std::string            Label(void) const;
  int                    LabelID(void) const    { return m_labelId; }
  JOYSTICK::INPUT_TYPE InputType(void) const { return m_inputType; }

  bool Deserialize(const TiXmlElement* pElement,
                   const CController* controller,
                   JOYSTICK::FEATURE_CATEGORY category,
                   int categoryLabelId);

private:
  const CController *m_controller; // To get the controller ID for translating labels
  JOYSTICK::FEATURE_TYPE m_type;
  JOYSTICK::FEATURE_CATEGORY m_category;
  int m_categoryLabelId;
  std::string            m_strName;
  int                    m_labelId;
  JOYSTICK::INPUT_TYPE m_inputType;
};

}
}
