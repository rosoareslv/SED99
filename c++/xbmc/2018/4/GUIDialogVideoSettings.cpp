/*
 *      Copyright (C) 2005-2014 Team XBMC
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

#include "GUIDialogVideoSettings.h"

#include <utility>

#include "ServiceBroker.h"
#include "addons/Skin.h"
#include "dialogs/GUIDialogYesNo.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "GUIPassword.h"
#include "profiles/ProfilesManager.h"
#include "settings/lib/Setting.h"
#include "settings/lib/SettingsManager.h"
#include "settings/MediaSettings.h"
#include "settings/Settings.h"
#include "utils/log.h"
#include "utils/Variant.h"
#include "video/VideoDatabase.h"
#include "Application.h"
#include "utils/LangCodeExpander.h"
#include "utils/StringUtils.h"
#include "video/ViewModeSettings.h"

#define SETTING_VIDEO_VIEW_MODE           "video.viewmode"
#define SETTING_VIDEO_ZOOM                "video.zoom"
#define SETTING_VIDEO_PIXEL_RATIO         "video.pixelratio"
#define SETTING_VIDEO_BRIGHTNESS          "video.brightness"
#define SETTING_VIDEO_CONTRAST            "video.contrast"
#define SETTING_VIDEO_GAMMA               "video.gamma"
#define SETTING_VIDEO_NONLIN_STRETCH      "video.nonlinearstretch"
#define SETTING_VIDEO_POSTPROCESS         "video.postprocess"
#define SETTING_VIDEO_VERTICAL_SHIFT      "video.verticalshift"

#define SETTING_VIDEO_VDPAU_NOISE         "vdpau.noise"
#define SETTING_VIDEO_VDPAU_SHARPNESS     "vdpau.sharpness"

#define SETTING_VIDEO_INTERLACEMETHOD     "video.interlacemethod"
#define SETTING_VIDEO_SCALINGMETHOD       "video.scalingmethod"

#define SETTING_VIDEO_STEREOSCOPICMODE    "video.stereoscopicmode"
#define SETTING_VIDEO_STEREOSCOPICINVERT  "video.stereoscopicinvert"

#define SETTING_VIDEO_MAKE_DEFAULT        "video.save"
#define SETTING_VIDEO_CALIBRATION         "video.calibration"
#define SETTING_VIDEO_STREAM              "video.stream"

CGUIDialogVideoSettings::CGUIDialogVideoSettings()
    : CGUIDialogSettingsManualBase(WINDOW_DIALOG_VIDEO_OSD_SETTINGS, "DialogSettings.xml"),
      m_viewModeChanged(false)
{ }

CGUIDialogVideoSettings::~CGUIDialogVideoSettings() = default;

void CGUIDialogVideoSettings::OnSettingChanged(std::shared_ptr<const CSetting> setting)
{
  if (setting == NULL)
    return;

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);

  const std::string &settingId = setting->GetId();
  if (settingId == SETTING_VIDEO_INTERLACEMETHOD)
  {
    CVideoSettings vs = g_application.GetAppPlayer().GetVideoSettings();
    vs.m_InterlaceMethod = static_cast<EINTERLACEMETHOD>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
    g_application.GetAppPlayer().SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_SCALINGMETHOD)
  {
    CVideoSettings vs = g_application.GetAppPlayer().GetVideoSettings();
    vs.m_ScalingMethod = static_cast<ESCALINGMETHOD>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
    g_application.GetAppPlayer().SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_STREAM)
  {
    m_videoStream = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
    // only change the video stream if a different one has been asked for
    if (g_application.GetAppPlayer().GetVideoStream() != m_videoStream)
    {
      g_application.GetAppPlayer().SetVideoStream(m_videoStream);    // Set the video stream to the one selected
    }
  }
  else if (settingId == SETTING_VIDEO_VIEW_MODE)
  {
    int value = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
    const CVideoSettings vs = g_application.GetAppPlayer().GetVideoSettings();

    g_application.GetAppPlayer().SetRenderViewMode(value, vs.m_CustomZoomAmount,
                                               vs.m_CustomPixelRatio, vs.m_CustomVerticalShift,
                                               vs.m_CustomNonLinStretch);

    m_viewModeChanged = true;
    GetSettingsManager()->SetNumber(SETTING_VIDEO_ZOOM, vs.m_CustomZoomAmount);
    GetSettingsManager()->SetNumber(SETTING_VIDEO_PIXEL_RATIO, vs.m_CustomPixelRatio);
    GetSettingsManager()->SetNumber(SETTING_VIDEO_VERTICAL_SHIFT, vs.m_CustomVerticalShift);
    GetSettingsManager()->SetBool(SETTING_VIDEO_NONLIN_STRETCH, vs.m_CustomNonLinStretch);
    m_viewModeChanged = false;
  }
  else if (settingId == SETTING_VIDEO_ZOOM ||
           settingId == SETTING_VIDEO_VERTICAL_SHIFT ||
           settingId == SETTING_VIDEO_PIXEL_RATIO ||
           settingId == SETTING_VIDEO_NONLIN_STRETCH)
  {
    CVideoSettings vs = g_application.GetAppPlayer().GetVideoSettings();
    if (settingId == SETTING_VIDEO_ZOOM)
      vs.m_CustomZoomAmount = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
    else if (settingId == SETTING_VIDEO_VERTICAL_SHIFT)
      vs.m_CustomVerticalShift = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
    else if (settingId == SETTING_VIDEO_PIXEL_RATIO)
      vs.m_CustomPixelRatio = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
    else if (settingId == SETTING_VIDEO_NONLIN_STRETCH)
      vs.m_CustomNonLinStretch = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();

    // try changing the view mode to custom. If it already is set to custom
    // manually call the render manager
    if (GetSettingsManager()->GetInt(SETTING_VIDEO_VIEW_MODE) != ViewModeCustom)
      GetSettingsManager()->SetInt(SETTING_VIDEO_VIEW_MODE, ViewModeCustom);
    else
      g_application.GetAppPlayer().SetRenderViewMode(vs.m_ViewMode, vs.m_CustomZoomAmount,
                                                 vs.m_CustomPixelRatio, vs.m_CustomVerticalShift,
                                                 vs.m_CustomNonLinStretch);
  }
  else if (settingId == SETTING_VIDEO_POSTPROCESS)
  {
    CVideoSettings vs = g_application.GetAppPlayer().GetVideoSettings();
    vs.m_PostProcess = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
    g_application.GetAppPlayer().SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_BRIGHTNESS)
  {
    CVideoSettings vs = g_application.GetAppPlayer().GetVideoSettings();
    vs.m_Brightness = static_cast<float>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
    g_application.GetAppPlayer().SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_CONTRAST)
  {
    CVideoSettings vs = g_application.GetAppPlayer().GetVideoSettings();
    vs.m_Contrast = static_cast<float>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
    g_application.GetAppPlayer().SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_GAMMA)
  {
    CVideoSettings vs = g_application.GetAppPlayer().GetVideoSettings();
    vs.m_Gamma = static_cast<float>(std::static_pointer_cast<const CSettingInt>(setting)->GetValue());
    g_application.GetAppPlayer().SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_VDPAU_NOISE)
  {
    CVideoSettings vs = g_application.GetAppPlayer().GetVideoSettings();
    vs.m_NoiseReduction = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
    g_application.GetAppPlayer().SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_VDPAU_SHARPNESS)
  {
    CVideoSettings vs = g_application.GetAppPlayer().GetVideoSettings();
    vs.m_Sharpness = static_cast<float>(std::static_pointer_cast<const CSettingNumber>(setting)->GetValue());
    g_application.GetAppPlayer().SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_STEREOSCOPICMODE)
  {
    CVideoSettings vs = g_application.GetAppPlayer().GetVideoSettings();
    vs.m_StereoMode = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
    g_application.GetAppPlayer().SetVideoSettings(vs);
  }
  else if (settingId == SETTING_VIDEO_STEREOSCOPICINVERT)
  {
    CVideoSettings vs = g_application.GetAppPlayer().GetVideoSettings();
    vs.m_StereoInvert = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
    g_application.GetAppPlayer().SetVideoSettings(vs);
  }
}

void CGUIDialogVideoSettings::OnSettingAction(std::shared_ptr<const CSetting> setting)
{
  if (setting == NULL)
    return;

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);

  const std::string &settingId = setting->GetId();
  if (settingId == SETTING_VIDEO_CALIBRATION)
  {
    const CProfilesManager &profileManager = CServiceBroker::GetProfileManager();

    // launch calibration window
    if (profileManager.GetMasterProfile().getLockMode() != LOCK_MODE_EVERYONE  &&
        g_passwordManager.CheckSettingLevelLock(CServiceBroker::GetSettings().GetSetting(CSettings::SETTING_VIDEOSCREEN_GUICALIBRATION)->GetLevel()))
      return;
    CServiceBroker::GetGUI()->GetWindowManager().ForceActivateWindow(WINDOW_SCREEN_CALIBRATION);
  }
  //! @todo implement
  else if (settingId == SETTING_VIDEO_MAKE_DEFAULT)
    Save();
}

void CGUIDialogVideoSettings::Save()
{
  const CProfilesManager &profileManager = CServiceBroker::GetProfileManager();

  if (profileManager.GetMasterProfile().getLockMode() != LOCK_MODE_EVERYONE &&
      !g_passwordManager.CheckSettingLevelLock(::SettingLevel::Expert))
    return;

  // prompt user if they are sure
  if (CGUIDialogYesNo::ShowAndGetInput(CVariant(12376), CVariant(12377)))
  { // reset the settings
    CVideoDatabase db;
    if (!db.Open())
      return;
    db.EraseVideoSettings();
    db.Close();

    CMediaSettings::GetInstance().GetDefaultVideoSettings() = g_application.GetAppPlayer().GetVideoSettings();
    CMediaSettings::GetInstance().GetDefaultVideoSettings().m_SubtitleStream = -1;
    CMediaSettings::GetInstance().GetDefaultVideoSettings().m_AudioStream = -1;
    CServiceBroker::GetSettings().Save();
  }
}

void CGUIDialogVideoSettings::SetupView()
{
  CGUIDialogSettingsManualBase::SetupView();

  SetHeading(13395);
  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_OKAY_BUTTON);
  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_CUSTOM_BUTTON);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CANCEL_BUTTON, 15067);
}

void CGUIDialogVideoSettings::InitializeSettings()
{
  CGUIDialogSettingsManualBase::InitializeSettings();

  const std::shared_ptr<CSettingCategory> category = AddCategory("videosettings", -1);
  if (category == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogVideoSettings: unable to setup settings");
    return;
  }

  // get all necessary setting groups
  const std::shared_ptr<CSettingGroup> groupVideoStream = AddGroup(category);
  if (groupVideoStream == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogVideoSettings: unable to setup settings");
    return;
  }
  const std::shared_ptr<CSettingGroup> groupVideo = AddGroup(category);
  if (groupVideo == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogVideoSettings: unable to setup settings");
    return;
  }
  const std::shared_ptr<CSettingGroup> groupVideoPlayback = AddGroup(category);
  if (groupVideoPlayback == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogVideoSettings: unable to setup settings");
    return;
  }
  const std::shared_ptr<CSettingGroup> groupStereoscopic = AddGroup(category);
  if (groupStereoscopic == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogVideoSettings: unable to setup settings");
    return;
  }
  const std::shared_ptr<CSettingGroup> groupSaveAsDefault = AddGroup(category);
  if (groupSaveAsDefault == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogVideoSettings: unable to setup settings");
    return;
  }

  bool usePopup = g_SkinInfo->HasSkinFile("DialogSlider.xml");

  const CVideoSettings videoSettings = g_application.GetAppPlayer().GetVideoSettings();

  TranslatableIntegerSettingOptions entries;

  entries.clear();
  entries.push_back(std::make_pair(16039, VS_INTERLACEMETHOD_NONE));
  entries.push_back(std::make_pair(16019, VS_INTERLACEMETHOD_AUTO));
  entries.push_back(std::make_pair(20131, VS_INTERLACEMETHOD_RENDER_BLEND));
  entries.push_back(std::make_pair(20129, VS_INTERLACEMETHOD_RENDER_WEAVE));
  entries.push_back(std::make_pair(16021, VS_INTERLACEMETHOD_RENDER_BOB));
  entries.push_back(std::make_pair(16020, VS_INTERLACEMETHOD_DEINTERLACE));
  entries.push_back(std::make_pair(16036, VS_INTERLACEMETHOD_DEINTERLACE_HALF));
  entries.push_back(std::make_pair(16311, VS_INTERLACEMETHOD_VDPAU_TEMPORAL_SPATIAL));
  entries.push_back(std::make_pair(16310, VS_INTERLACEMETHOD_VDPAU_TEMPORAL));
  entries.push_back(std::make_pair(16325, VS_INTERLACEMETHOD_VDPAU_BOB));
  entries.push_back(std::make_pair(16318, VS_INTERLACEMETHOD_VDPAU_TEMPORAL_SPATIAL_HALF));
  entries.push_back(std::make_pair(16317, VS_INTERLACEMETHOD_VDPAU_TEMPORAL_HALF));
  entries.push_back(std::make_pair(16327, VS_INTERLACEMETHOD_VAAPI_BOB));
  entries.push_back(std::make_pair(16328, VS_INTERLACEMETHOD_VAAPI_MADI));
  entries.push_back(std::make_pair(16329, VS_INTERLACEMETHOD_VAAPI_MACI));
  entries.push_back(std::make_pair(16330, VS_INTERLACEMETHOD_MMAL_ADVANCED));
  entries.push_back(std::make_pair(16331, VS_INTERLACEMETHOD_MMAL_ADVANCED_HALF));
  entries.push_back(std::make_pair(16332, VS_INTERLACEMETHOD_MMAL_BOB));
  entries.push_back(std::make_pair(16333, VS_INTERLACEMETHOD_MMAL_BOB_HALF));
  entries.push_back(std::make_pair(16320, VS_INTERLACEMETHOD_DXVA_AUTO));

  /* remove unsupported methods */
  for (TranslatableIntegerSettingOptions::iterator it = entries.begin(); it != entries.end(); )
  {
    if (g_application.GetAppPlayer().Supports((EINTERLACEMETHOD)it->second))
      ++it;
    else
      it = entries.erase(it);
  }

  if (!entries.empty())
  {
    EINTERLACEMETHOD method = videoSettings.m_InterlaceMethod;
    if (!g_application.GetAppPlayer().Supports(method))
    {
      method = g_application.GetAppPlayer().GetDeinterlacingMethodDefault();
    }
    AddSpinner(groupVideo, SETTING_VIDEO_INTERLACEMETHOD, 16038, SettingLevel::Basic, static_cast<int>(method), entries);
  }

  entries.clear();
  entries.push_back(std::make_pair(16301, VS_SCALINGMETHOD_NEAREST));
  entries.push_back(std::make_pair(16302, VS_SCALINGMETHOD_LINEAR));
  entries.push_back(std::make_pair(16303, VS_SCALINGMETHOD_CUBIC ));
  entries.push_back(std::make_pair(16304, VS_SCALINGMETHOD_LANCZOS2));
  entries.push_back(std::make_pair(16323, VS_SCALINGMETHOD_SPLINE36_FAST));
  entries.push_back(std::make_pair(16315, VS_SCALINGMETHOD_LANCZOS3_FAST));
  entries.push_back(std::make_pair(16322, VS_SCALINGMETHOD_SPLINE36));
  entries.push_back(std::make_pair(16305, VS_SCALINGMETHOD_LANCZOS3));
  entries.push_back(std::make_pair(16306, VS_SCALINGMETHOD_SINC8));
//  entries.push_back(make_pair(?????, VS_SCALINGMETHOD_NEDI));
  entries.push_back(std::make_pair(16307, VS_SCALINGMETHOD_BICUBIC_SOFTWARE));
  entries.push_back(std::make_pair(16308, VS_SCALINGMETHOD_LANCZOS_SOFTWARE));
  entries.push_back(std::make_pair(16309, VS_SCALINGMETHOD_SINC_SOFTWARE));
  entries.push_back(std::make_pair(13120, VS_SCALINGMETHOD_VDPAU_HARDWARE));
  entries.push_back(std::make_pair(16319, VS_SCALINGMETHOD_DXVA_HARDWARE));
  entries.push_back(std::make_pair(16316, VS_SCALINGMETHOD_AUTO));

  /* remove unsupported methods */
  for(TranslatableIntegerSettingOptions::iterator it = entries.begin(); it != entries.end(); )
  {
    if (g_application.GetAppPlayer().Supports((ESCALINGMETHOD)it->second))
      ++it;
    else
      it = entries.erase(it);
  }

  AddSpinner(groupVideo, SETTING_VIDEO_SCALINGMETHOD, 16300, SettingLevel::Basic, static_cast<int>(videoSettings.m_ScalingMethod), entries);

  AddVideoStreams(groupVideoStream, SETTING_VIDEO_STREAM);

  if (g_application.GetAppPlayer().Supports(RENDERFEATURE_STRETCH) || g_application.GetAppPlayer().Supports(RENDERFEATURE_PIXEL_RATIO))
  {
    AddList(groupVideo, SETTING_VIDEO_VIEW_MODE, 629, SettingLevel::Basic, videoSettings.m_ViewMode, CViewModeSettings::ViewModesFiller, 629);
  }
  if (g_application.GetAppPlayer().Supports(RENDERFEATURE_ZOOM))
    AddSlider(groupVideo, SETTING_VIDEO_ZOOM, 216, SettingLevel::Basic, videoSettings.m_CustomZoomAmount, "%2.2f", 0.5f, 0.01f, 2.0f, 216, usePopup);
  if (g_application.GetAppPlayer().Supports(RENDERFEATURE_VERTICAL_SHIFT))
    AddSlider(groupVideo, SETTING_VIDEO_VERTICAL_SHIFT, 225, SettingLevel::Basic, videoSettings.m_CustomVerticalShift, "%2.2f", -2.0f, 0.01f, 2.0f, 225, usePopup);
  if (g_application.GetAppPlayer().Supports(RENDERFEATURE_PIXEL_RATIO))
    AddSlider(groupVideo, SETTING_VIDEO_PIXEL_RATIO, 217, SettingLevel::Basic, videoSettings.m_CustomPixelRatio, "%2.2f", 0.5f, 0.01f, 2.0f, 217, usePopup);
  if (g_application.GetAppPlayer().Supports(RENDERFEATURE_POSTPROCESS))
    AddToggle(groupVideo, SETTING_VIDEO_POSTPROCESS, 16400, SettingLevel::Basic, videoSettings.m_PostProcess);
  if (g_application.GetAppPlayer().Supports(RENDERFEATURE_BRIGHTNESS))
    AddPercentageSlider(groupVideoPlayback, SETTING_VIDEO_BRIGHTNESS, 464, SettingLevel::Basic, static_cast<int>(videoSettings.m_Brightness), 14047, 1, 464, usePopup);
  if (g_application.GetAppPlayer().Supports(RENDERFEATURE_CONTRAST))
    AddPercentageSlider(groupVideoPlayback, SETTING_VIDEO_CONTRAST, 465, SettingLevel::Basic, static_cast<int>(videoSettings.m_Contrast), 14047, 1, 465, usePopup);
  if (g_application.GetAppPlayer().Supports(RENDERFEATURE_GAMMA))
    AddPercentageSlider(groupVideoPlayback, SETTING_VIDEO_GAMMA, 466, SettingLevel::Basic, static_cast<int>(videoSettings.m_Gamma), 14047, 1, 466, usePopup);
  if (g_application.GetAppPlayer().Supports(RENDERFEATURE_NOISE))
    AddSlider(groupVideoPlayback, SETTING_VIDEO_VDPAU_NOISE, 16312, SettingLevel::Basic, videoSettings.m_NoiseReduction, "%2.2f", 0.0f, 0.01f, 1.0f, 16312, usePopup);
  if (g_application.GetAppPlayer().Supports(RENDERFEATURE_SHARPNESS))
    AddSlider(groupVideoPlayback, SETTING_VIDEO_VDPAU_SHARPNESS, 16313, SettingLevel::Basic, videoSettings.m_Sharpness, "%2.2f", -1.0f, 0.02f, 1.0f, 16313, usePopup);
  if (g_application.GetAppPlayer().Supports(RENDERFEATURE_NONLINSTRETCH))
    AddToggle(groupVideoPlayback, SETTING_VIDEO_NONLIN_STRETCH, 659, SettingLevel::Basic, videoSettings.m_CustomNonLinStretch);

  // stereoscopic settings
  entries.clear();
  entries.push_back(std::make_pair(16316, RENDER_STEREO_MODE_OFF));
  entries.push_back(std::make_pair(36503, RENDER_STEREO_MODE_SPLIT_HORIZONTAL));
  entries.push_back(std::make_pair(36504, RENDER_STEREO_MODE_SPLIT_VERTICAL));
  AddSpinner(groupStereoscopic, SETTING_VIDEO_STEREOSCOPICMODE, 36535, SettingLevel::Basic, videoSettings.m_StereoMode, entries);
  AddToggle(groupStereoscopic, SETTING_VIDEO_STEREOSCOPICINVERT, 36536, SettingLevel::Basic, videoSettings.m_StereoInvert);

  // general settings
  AddButton(groupSaveAsDefault, SETTING_VIDEO_MAKE_DEFAULT, 12376, SettingLevel::Basic);
  AddButton(groupSaveAsDefault, SETTING_VIDEO_CALIBRATION, 214, SettingLevel::Basic);
}

void CGUIDialogVideoSettings::AddVideoStreams(std::shared_ptr<CSettingGroup> group, const std::string &settingId)
{
  if (group == NULL || settingId.empty())
    return;

  m_videoStream = g_application.GetAppPlayer().GetVideoStream();
  if (m_videoStream < 0)
    m_videoStream = 0;

  AddList(group, settingId, 38031, SettingLevel::Basic, m_videoStream, VideoStreamsOptionFiller, 38031);
}

void CGUIDialogVideoSettings::VideoStreamsOptionFiller(std::shared_ptr<const CSetting> setting, std::vector< std::pair<std::string, int> > &list, int &current, void *data)
{
  int videoStreamCount = g_application.GetAppPlayer().GetVideoStreamCount();

  // cycle through each video stream and add it to our list control
  for (int i = 0; i < videoStreamCount; ++i)
  {
    std::string strItem;
    std::string strLanguage;

    VideoStreamInfo info;
    g_application.GetAppPlayer().GetVideoStreamInfo(i, info);

    g_LangCodeExpander.Lookup(info.language, strLanguage);

    if (!info.name.empty())
    {
      if (!strLanguage.empty())
        strItem = StringUtils::Format("%s - %s", strLanguage.c_str(), info.name.c_str());
      else
        strItem = info.name;
    }
    else if (!strLanguage.empty())
    {
        strItem = strLanguage;
    }

    if (info.codecName.empty())
      strItem += StringUtils::Format(" (%ix%i", info.width, info.height);
    else
      strItem += StringUtils::Format(" (%s, %ix%i", info.codecName.c_str(), info.width, info.height);

    if (info.bitrate)
      strItem += StringUtils::Format(", %i bps)", info.bitrate);
    else
      strItem += ")";

    strItem += FormatFlags(info.flags);
    strItem += StringUtils::Format(" (%i/%i)", i + 1, videoStreamCount);
    list.push_back(make_pair(strItem, i));
  }

  if (list.empty())
  {
    list.push_back(make_pair(g_localizeStrings.Get(231), -1));
    current = -1;
  }
}

std::string CGUIDialogVideoSettings::FormatFlags(StreamFlags flags)
{
  std::vector<std::string> localizedFlags;
  if (flags & StreamFlags::FLAG_DEFAULT)
    localizedFlags.emplace_back(g_localizeStrings.Get(39104));
  if (flags & StreamFlags::FLAG_FORCED)
    localizedFlags.emplace_back(g_localizeStrings.Get(39105));
  if (flags & StreamFlags::FLAG_HEARING_IMPAIRED)
    localizedFlags.emplace_back(g_localizeStrings.Get(39106));
  if (flags &  StreamFlags::FLAG_VISUAL_IMPAIRED)
    localizedFlags.emplace_back(g_localizeStrings.Get(39107));

  std::string formated = StringUtils::Join(localizedFlags, ", ");

  if (!formated.empty())
    formated = StringUtils::Format(" [%s]", formated);

  return formated;
}
