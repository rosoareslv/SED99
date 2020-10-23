/*
 *      Copyright (C) 2015-2016 Team Kodi
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

#include "ControllerLayout.h"
#include "Controller.h"
#include "ControllerDefinitions.h"
#include "guilib/LocalizeStrings.h"
#include "utils/log.h"
#include "utils/XMLUtils.h"

#include <algorithm>
#include <sstream>

using namespace GAME;
using namespace JOYSTICK;

// --- FeatureTypeEqual --------------------------------------------------------

struct FeatureTypeEqual
{
  FeatureTypeEqual(FEATURE_TYPE type, INPUT_TYPE inputType) : type(type), inputType(inputType) { }

  bool operator()(const CControllerFeature& feature) const
  {
    if (type == FEATURE_TYPE::UNKNOWN)
      return true; // Match all feature types

    if (type == FEATURE_TYPE::SCALAR && feature.Type() == FEATURE_TYPE::SCALAR)
    {
      if (inputType == INPUT_TYPE::UNKNOWN)
        return true; // Match all input types

      return inputType == feature.InputType();
    }

    return type == feature.Type();
  }

  const FEATURE_TYPE type;
  const INPUT_TYPE   inputType;
};

// --- CControllerLayout ---------------------------------------------------

void CControllerLayout::Reset(void)
{
  m_label = 0;
  m_strImage.clear();
  m_strOverlay.clear();
  m_width = 0;
  m_height = 0;
  m_features.clear();
}

unsigned int CControllerLayout::FeatureCount(FEATURE_TYPE type      /* = FEATURE_TYPE::UNKNOWN */,
                                             INPUT_TYPE   inputType /* = INPUT_TYPE::UNKNOWN */) const
{
  return std::count_if(m_features.begin(), m_features.end(), FeatureTypeEqual(type, inputType));
}

bool CControllerLayout::Deserialize(const TiXmlElement* pElement, const CController* controller)
{
  Reset();

  if (!pElement)
    return false;

  // Label
  std::string strLabel = XMLUtils::GetAttribute(pElement, LAYOUT_XML_ATTR_LAYOUT_LABEL);
  if (strLabel.empty())
  {
    CLog::Log(LOGERROR, "<%s> tag has no \"%s\" attribute", LAYOUT_XML_ROOT, LAYOUT_XML_ATTR_LAYOUT_LABEL);
    return false;
  }
  std::istringstream(strLabel) >> m_label;

  // Image
  m_strImage = XMLUtils::GetAttribute(pElement, LAYOUT_XML_ATTR_LAYOUT_IMAGE);
  if (m_strImage.empty())
    CLog::Log(LOGDEBUG, "<%s> tag has no \"%s\" attribute", LAYOUT_XML_ROOT, LAYOUT_XML_ATTR_LAYOUT_IMAGE);

  // Features
  for (const TiXmlElement* pGroup = pElement->FirstChildElement(); pGroup != nullptr; pGroup = pGroup->NextSiblingElement())
  {
    if (pGroup->ValueStr() != LAYOUT_XML_ELM_GROUP)
    {
      CLog::Log(LOGDEBUG, "<%s> tag is misnamed: <%s>", LAYOUT_XML_ELM_GROUP, pGroup->Value() ? pGroup->Value() : "");
      continue;
    }

    // Group
    std::string strGroup;

    std::string strGroupLabel = XMLUtils::GetAttribute(pGroup, LAYOUT_XML_ATTR_GROUP_LABEL);
    if (!strGroupLabel.empty())
    {
      unsigned int categoryId;
      std::istringstream(strGroupLabel) >> categoryId;
      strGroup = g_localizeStrings.GetAddonString(controller->ID(), categoryId);
      if (strGroup.empty())
        strGroup = g_localizeStrings.Get(categoryId);
    }

    for (const TiXmlElement* pFeature = pGroup->FirstChildElement(); pFeature != nullptr; pFeature = pFeature->NextSiblingElement())
    {
      CControllerFeature feature;

      if (!feature.Deserialize(pFeature, controller, strGroup))
        return false;

      m_features.push_back(feature);
    }
  }

  return true;
}
