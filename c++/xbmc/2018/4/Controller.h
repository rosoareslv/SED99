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

#include "ControllerFeature.h"
#include "ControllerTypes.h"
#include "addons/Addon.h"
#include "input/joysticks/JoystickTypes.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace KODI
{
namespace GAME
{
class CControllerLayout;
class CControllerTopology;

using JOYSTICK::FEATURE_TYPE;

class CController : public ADDON::CAddon
{
public:
  static std::unique_ptr<CController> FromExtension(ADDON::CAddonInfo addonInfo, const cp_extension_t* ext);

  explicit CController(ADDON::CAddonInfo addonInfo);

  virtual ~CController();

  static const ControllerPtr EmptyPtr;

  /*!
   * \brief Get all controller features
   *
   * \return The features
   */
  const std::vector<CControllerFeature>& Features(void) const { return m_features; }

  /*!
   * \brief Get a feature by its name
   *
   * \param name The feature name
   *
   * \return The feature, or a feature of type FEATURE_TYPE::UNKNOWN if the name is invalid
   */
  const CControllerFeature& GetFeature(const std::string &name) const;

  /*!
   * \brief Get the count of controller features matching the specified types
   *
   * \param type The feature type, or FEATURE_TYPE::UNKNOWN to match all feature types
   * \param inputType The input type, or INPUT_TYPE::UNKNOWN to match all input types
   *
   * \return The feature count
   */
  unsigned int FeatureCount(FEATURE_TYPE type = FEATURE_TYPE::UNKNOWN,
                            JOYSTICK::INPUT_TYPE inputType = JOYSTICK::INPUT_TYPE::UNKNOWN) const;

  /*!
   * \brief Get the features matching the specified type
   *
   * \param type The feature type, or FEATURE_TYPE::UNKNOWN to get all features
   */
  void GetFeatures(std::vector<std::string>& features, FEATURE_TYPE type = FEATURE_TYPE::UNKNOWN) const;

  /*!
   * \brief Get the type of the specified feature
   *
   * \param feature The feature name to look up
   *
   * \return The feature type, or FEATURE_TYPE::UNKNOWN if an invalid feature was specified
   */
  FEATURE_TYPE FeatureType(const std::string &feature) const;

  /*!
   * \brief Get the input type of the specified feature
   *
   * \param feature The feature name to look up
   *
   * \return The input type of the feature, or INPUT_TYPE::UNKNOWN if unknown
   */
  JOYSTICK::INPUT_TYPE GetInputType(const std::string& feature) const;

  /*!
   * \brief Load the controller layout
   *
   * \return true if the layout is loaded or was already loaded, false otherwise
   */
  bool LoadLayout(void);

  /*!
   * \brief Get the controller layout
   */
  const CControllerLayout& Layout(void) const { return *m_layout; }

  /*!
   * \brief Get the controller's physical topology
   *
   * This defines how controllers physically connect to each other.
   *
   * \return The physical topology of the controller
   */
  const CControllerTopology& Topology() const;

private:
  std::unique_ptr<CControllerLayout> m_layout;
  std::vector<CControllerFeature> m_features;
  bool m_bLoaded = false;
};

}
}
