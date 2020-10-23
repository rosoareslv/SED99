/*
 *      Copyright (C) 2017 Team Kodi
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "GUIDialogPVRRecordingSettings.h"

#include "ServiceBroker.h"
#include "guilib/LocalizeStrings.h"
#include "messaging/helpers/DialogHelper.h"
#include "settings/lib/Setting.h"
#include "settings/lib/SettingsManager.h"
#include "utils/StringUtils.h"
#include "utils/Variant.h"
#include "utils/log.h"

#include "pvr/PVRManager.h"
#include "pvr/addons/PVRClients.h"
#include "pvr/recordings/PVRRecording.h"

using namespace PVR;
using namespace KODI::MESSAGING::HELPERS;

#define SETTING_RECORDING_NAME "recording.name"
#define SETTING_RECORDING_PLAYCOUNT "recording.playcount"
#define SETTING_RECORDING_LIFETIME "recording.lifetime"

CGUIDialogPVRRecordingSettings::CGUIDialogPVRRecordingSettings() :
  CGUIDialogSettingsManualBase(WINDOW_DIALOG_PVR_RECORDING_SETTING, "DialogSettings.xml"),
  m_iPlayCount(0),
  m_iLifetime(0)
{
  m_loadType = LOAD_EVERY_TIME;
}

void CGUIDialogPVRRecordingSettings::SetRecording(const CPVRRecordingPtr &recording)
{
  if (!recording)
  {
    CLog::Log(LOGERROR, "CGUIDialogPVRRecordingSettings::SetRecording - no recording given");
    return;
  }

  m_recording = recording;

  // Copy data we need from tag. Do not modify the tag itself until Save()!
  m_strTitle = m_recording->m_strTitle;
  m_iPlayCount = m_recording->GetLocalPlayCount();
  m_iLifetime = m_recording->m_iLifetime;
}

void CGUIDialogPVRRecordingSettings::SetupView()
{
  CGUIDialogSettingsManualBase::SetupView();
  SetHeading(19068); // Recording settings
  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_CUSTOM_BUTTON);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_OKAY_BUTTON, 186); // OK
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CANCEL_BUTTON, 222); // Cancel
}

void CGUIDialogPVRRecordingSettings::InitializeSettings()
{
  CGUIDialogSettingsManualBase::InitializeSettings();

  const std::shared_ptr<CSettingCategory> category = AddCategory("pvrrecordingsettings", -1);
  if (category == nullptr)
  {
    CLog::Log(LOGERROR, "CGUIDialogPVRRecordingSettings::InitializeSettings - Unable to add settings category");
    return;
  }

  const std::shared_ptr<CSettingGroup> group = AddGroup(category);
  if (group == nullptr)
  {
    CLog::Log(LOGERROR, "CGUIDialogPVRRecordingSettings::InitializeSettings - Unable to add settings group");
    return;
  }

  std::shared_ptr<CSetting> setting = nullptr;

  // Name
  setting = AddEdit(group, SETTING_RECORDING_NAME, 19075, SettingLevel::Basic, m_strTitle);
  setting->SetEnabled(CServiceBroker::GetPVRManager().Clients()->GetClientCapabilities(m_recording->ClientID()).SupportsRecordingsRename());

  // Play count
  if (CServiceBroker::GetPVRManager().Clients()->GetClientCapabilities(m_recording->ClientID()).SupportsRecordingsPlayCount())
    setting = AddEdit(group, SETTING_RECORDING_PLAYCOUNT, 567, SettingLevel::Basic, m_recording->GetLocalPlayCount());

  // Lifetime
  if (CServiceBroker::GetPVRManager().Clients()->GetClientCapabilities(m_recording->ClientID()).SupportsRecordingsLifetimeChange())
    setting = AddList(group, SETTING_RECORDING_LIFETIME, 19083, SettingLevel::Basic, m_iLifetime, LifetimesFiller, 19083);
}

bool CGUIDialogPVRRecordingSettings::OnSettingChanging(std::shared_ptr<const CSetting> setting)
{
  if (setting == nullptr)
  {
    CLog::Log(LOGERROR, "CGUIDialogPVRRecordingSettings::OnSettingChanging - No setting");
    return false;
  }

  const std::string &settingId = setting->GetId();

  if (settingId == SETTING_RECORDING_LIFETIME)
  {
    int iNewLifetime = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
    if (m_recording->WillBeExpiredWithNewLifetime(iNewLifetime))
    {
      if (ShowYesNoDialogText(CVariant{19068}, // "Recording settings"
                              StringUtils::Format(g_localizeStrings.Get(19147).c_str(), iNewLifetime)) // "Setting the lieftime..."
            != DialogResponse::YES)
        return false;
    }
  }

  return CGUIDialogSettingsManualBase::OnSettingChanging(setting);
}

void CGUIDialogPVRRecordingSettings::OnSettingChanged(std::shared_ptr<const CSetting> setting)
{
  if (setting == nullptr)
  {
    CLog::Log(LOGERROR, "CGUIDialogPVRRecordingSettings::OnSettingChanged - No setting");
    return;
  }

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);

  const std::string &settingId = setting->GetId();

  if (settingId == SETTING_RECORDING_NAME)
  {
    m_strTitle = std::static_pointer_cast<const CSettingString>(setting)->GetValue();
  }
  else if (settingId == SETTING_RECORDING_PLAYCOUNT)
  {
    m_iPlayCount = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  }
  else if (settingId == SETTING_RECORDING_LIFETIME)
  {
    m_iLifetime = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  }
}

void CGUIDialogPVRRecordingSettings::Save()
{
  // Name
  m_recording->m_strTitle = m_strTitle;

  // Play count
  m_recording->SetLocalPlayCount(m_iPlayCount);

  // Lifetime
  m_recording->m_iLifetime = m_iLifetime;
}

void CGUIDialogPVRRecordingSettings::LifetimesFiller(
  SettingConstPtr setting, std::vector< std::pair<std::string, int> > &list, int &current, void *data)
{
  CGUIDialogPVRRecordingSettings *pThis = static_cast<CGUIDialogPVRRecordingSettings*>(data);
  if (pThis)
  {
    list.clear();
    CServiceBroker::GetPVRManager().Clients()->GetClientCapabilities(pThis->m_recording->ClientID()).GetRecordingsLifetimeValues(list);
    current = pThis->m_iLifetime;

    auto it = list.begin();
    while (it != list.end())
    {
      if (it->second == current)
        break; // value already in list

      ++it;
    }

    if (it == list.end())
    {
      // PVR backend supplied value is not in the list of predefined values. Insert it.
      list.insert(it, std::make_pair(StringUtils::Format(g_localizeStrings.Get(17999).c_str(), current) /* %i days */, current));
    }
  }
  else
    CLog::Log(LOGERROR, "CGUIDialogPVRRecordingSettings::LifetimesFiller - No dialog");
}
