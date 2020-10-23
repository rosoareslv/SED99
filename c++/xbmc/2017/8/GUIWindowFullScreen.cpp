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

#include "threads/SystemClock.h"
#include "system.h"
#include "GUIWindowFullScreen.h"
#include "Application.h"
#include "ServiceBroker.h"
#include "messaging/ApplicationMessenger.h"
#include "GUIInfoManager.h"
#include "guilib/GUIProgressControl.h"
#include "guilib/GUILabelControl.h"
#include "video/dialogs/GUIDialogVideoOSD.h"
#include "video/dialogs/GUIDialogAudioSubtitleSettings.h"
#include "guilib/GUIWindowManager.h"
#include "input/Key.h"
#include "pvr/PVRGUIActions.h"
#include "pvr/PVRManager.h"
#include "video/dialogs/GUIDialogFullScreenInfo.h"
#include "settings/DisplaySettings.h"
#include "settings/MediaSettings.h"
#include "settings/Settings.h"
#include "FileItem.h"
#include "utils/CPUInfo.h"
#include "guilib/LocalizeStrings.h"
#include "threads/SingleLock.h"
#include "utils/StringUtils.h"
#include "XBDateTime.h"
#include "input/InputManager.h"
#include "windowing/WindowingFactory.h"
#include "cores/IPlayer.h"
#include "guiinfo/GUIInfoLabels.h"
#include "video/ViewModeSettings.h"

#include <stdio.h>
#include <algorithm>
#if defined(TARGET_DARWIN)
#include "linux/LinuxResourceCounter.h"
#endif

using namespace KODI::MESSAGING;

#define BLUE_BAR                          0
#define LABEL_ROW1                       10
#define LABEL_ROW2                       11
#define LABEL_ROW3                       12

//Displays current position, visible after seek or when forced
//Alt, use conditional visibility Player.DisplayAfterSeek
#define LABEL_CURRENT_TIME               22

//Displays when video is rebuffering
//Alt, use conditional visibility Player.IsCaching
#define LABEL_BUFFERING                  24

//Progressbar used for buffering status and after seeking
#define CONTROL_PROGRESS                 23

#if defined(TARGET_DARWIN)
static CLinuxResourceCounter m_resourceCounter;
#endif

CGUIWindowFullScreen::CGUIWindowFullScreen(void)
    : CGUIWindow(WINDOW_FULLSCREEN_VIDEO, "VideoFullScreen.xml")
{
  m_viewModeChanged = true;
  m_dwShowViewModeTimeout = 0;
  m_bShowCurrentTime = false;
  m_loadType = KEEP_IN_MEMORY;
  // audio
  //  - language
  //  - volume
  //  - stream

  // video
  //  - Create Bookmark (294)
  //  - Cycle bookmarks (295)
  //  - Clear bookmarks (296)
  //  - jump to specific time
  //  - slider
  //  - av delay

  // subtitles
  //  - delay
  //  - language

  m_controlStats = new GUICONTROLSTATS;
}

CGUIWindowFullScreen::~CGUIWindowFullScreen(void)
{
  delete m_controlStats;
}

bool CGUIWindowFullScreen::OnAction(const CAction &action)
{
  if (CServiceBroker::GetSettings().GetBool(CSettings::SETTING_PVRPLAYBACK_CONFIRMCHANNELSWITCH) &&
      CServiceBroker::GetPVRManager().GUIActions()->GetChannelNavigator().IsPreview() &&
      (action.GetID() == ACTION_SELECT_ITEM ||
       CServiceBroker::GetInputManager().GetGlobalAction(action.GetButtonCode()).GetID() == ACTION_SELECT_ITEM))
  {
    // If confirm channel switch is active, channel preview is currently shown
    // and the button that caused this action matches (global) action "Select" (OK)
    // switch to the channel currently displayed within the preview.
    CServiceBroker::GetPVRManager().GUIActions()->GetChannelNavigator().SwitchToCurrentChannel();
    return true;
  }

  switch (action.GetID())
  {
  case ACTION_SHOW_OSD:
    ToggleOSD();
    return true;

  case ACTION_TRIGGER_OSD:
    TriggerOSD();
    return true;

  case ACTION_SHOW_GUI:
    {
      // switch back to the menu
      g_windowManager.PreviousWindow();
      return true;
    }
    break;

  case ACTION_SHOW_OSD_TIME:
    m_bShowCurrentTime = !m_bShowCurrentTime;
    g_infoManager.SetShowTime(m_bShowCurrentTime);
    return true;
    break;

  case ACTION_SHOW_INFO:
    {
      CGUIDialogFullScreenInfo* pDialog = g_windowManager.GetWindow<CGUIDialogFullScreenInfo>(WINDOW_DIALOG_FULLSCREEN_INFO);
      if (pDialog)
      {
        CFileItem item(g_application.CurrentFileItem());
        pDialog->Open();
        return true;
      }
      break;
    }

  case ACTION_ASPECT_RATIO:
    { // toggle the aspect ratio mode (only if the info is onscreen)
      if (m_dwShowViewModeTimeout)
      {
        g_application.m_pPlayer->SetRenderViewMode(CViewModeSettings::GetNextQuickCycleViewMode(CMediaSettings::GetInstance().GetCurrentVideoSettings().m_ViewMode));
      }
      else
        m_viewModeChanged = true;
      m_dwShowViewModeTimeout = XbmcThreads::SystemClockMillis();
    }
    return true;
    break;
  case ACTION_SHOW_PLAYLIST:
    {
      CFileItem item(g_application.CurrentFileItem());
      if (item.HasPVRChannelInfoTag())
        g_windowManager.ActivateWindow(WINDOW_DIALOG_PVR_OSD_CHANNELS);
      else if (item.HasVideoInfoTag())
        g_windowManager.ActivateWindow(WINDOW_VIDEO_PLAYLIST);
      else if (item.HasMusicInfoTag())
        g_windowManager.ActivateWindow(WINDOW_MUSIC_PLAYLIST);
    }
    return true;
    break;
  case ACTION_BROWSE_SUBTITLE:
    {
      std::string path = CGUIDialogAudioSubtitleSettings::BrowseForSubtitle();
      if (!path.empty())
        g_application.m_pPlayer->AddSubtitle(path);
      return true;
    }
  default:
      break;
  }

  return CGUIWindow::OnAction(action);
}

void CGUIWindowFullScreen::ClearBackground()
{
  if (g_application.m_pPlayer->IsRenderingVideoLayer())
#ifdef HAS_IMXVPU
    g_graphicsContext.Clear((16 << 16)|(8 << 8)|16);
#else
    g_graphicsContext.Clear(0);
#endif
}

void CGUIWindowFullScreen::OnWindowLoaded()
{
  CGUIWindow::OnWindowLoaded();
  // override the clear colour - we must never clear fullscreen
  m_clearBackground = 0;

  CGUIProgressControl* pProgress = dynamic_cast<CGUIProgressControl*>(GetControl(CONTROL_PROGRESS));
  if (pProgress)
  {
    if( pProgress->GetInfo() == 0 || !pProgress->HasVisibleCondition())
    {
      pProgress->SetInfo(PLAYER_PROGRESS);
      pProgress->SetVisibleCondition("player.displayafterseek");
      pProgress->SetVisible(true);
    }
  }

  CGUILabelControl* pLabel = dynamic_cast<CGUILabelControl*>(GetControl(LABEL_BUFFERING));
  if(pLabel && !pLabel->HasVisibleCondition())
  {
    pLabel->SetVisibleCondition("player.caching");
    pLabel->SetVisible(true);
  }

  pLabel = dynamic_cast<CGUILabelControl*>(GetControl(LABEL_CURRENT_TIME));
  if(pLabel && !pLabel->HasVisibleCondition())
  {
    pLabel->SetVisibleCondition("player.displayafterseek");
    pLabel->SetVisible(true);
    pLabel->SetLabel("$INFO(VIDEOPLAYER.TIME) / $INFO(VIDEOPLAYER.DURATION)");
  }
}

bool CGUIWindowFullScreen::OnMessage(CGUIMessage& message)
{
  switch (message.GetMessage())
  {
  case GUI_MSG_WINDOW_INIT:
    {
      // check whether we've come back here from a window during which time we've actually
      // stopped playing videos
      if (message.GetParam1() == WINDOW_INVALID && !g_application.m_pPlayer->IsPlayingVideo())
      { // why are we here if nothing is playing???
        g_windowManager.PreviousWindow();
        return true;
      }
      g_infoManager.SetShowInfo(false);
      m_bShowCurrentTime = false;
      g_infoManager.SetDisplayAfterSeek(0); // Make sure display after seek is off.

      // switch resolution
      g_graphicsContext.SetFullScreenVideo(true);

      // now call the base class to load our windows
      CGUIWindow::OnMessage(message);

      m_dwShowViewModeTimeout = 0;
      m_viewModeChanged = true;


      return true;
    }
  case GUI_MSG_WINDOW_DEINIT:
    {
      // close all active modal dialogs
      g_windowManager.CloseInternalModalDialogs(true);

      CGUIWindow::OnMessage(message);

      CServiceBroker::GetSettings().Save();

      CSingleLock lock (g_graphicsContext);
      g_graphicsContext.SetFullScreenVideo(false);
      lock.Leave();

      return true;
    }
  case GUI_MSG_SETFOCUS:
  case GUI_MSG_LOSTFOCUS:
    if (message.GetSenderId() != WINDOW_FULLSCREEN_VIDEO) return true;
    break;
  }

  return CGUIWindow::OnMessage(message);
}

EVENT_RESULT CGUIWindowFullScreen::OnMouseEvent(const CPoint &point, const CMouseEvent &event)
{
  if (event.m_id == ACTION_MOUSE_RIGHT_CLICK)
  { // no control found to absorb this click - go back to GUI
    OnAction(CAction(ACTION_SHOW_GUI));
    return EVENT_RESULT_HANDLED;
  }
  if (event.m_id == ACTION_MOUSE_WHEEL_UP)
  {
    return g_application.OnAction(CAction(ACTION_ANALOG_SEEK_FORWARD, 0.5f)) ? EVENT_RESULT_HANDLED : EVENT_RESULT_UNHANDLED;
  }
  if (event.m_id == ACTION_MOUSE_WHEEL_DOWN)
  {
    return g_application.OnAction(CAction(ACTION_ANALOG_SEEK_BACK, 0.5f)) ? EVENT_RESULT_HANDLED : EVENT_RESULT_UNHANDLED;
  }
  if (event.m_id >= ACTION_GESTURE_NOTIFY && event.m_id <= ACTION_GESTURE_END) // gestures
    return EVENT_RESULT_UNHANDLED;
  return EVENT_RESULT_UNHANDLED;
}

void CGUIWindowFullScreen::FrameMove()
{
  float playspeed = g_application.m_pPlayer->GetPlaySpeed();
  if (playspeed != 1.0 && !g_application.m_pPlayer->HasGame())
    g_infoManager.SetDisplayAfterSeek();

  if (!g_application.m_pPlayer->HasPlayer())
    return;

  //----------------------
  // ViewMode Information
  //----------------------
  if (m_dwShowViewModeTimeout && XbmcThreads::SystemClockMillis() - m_dwShowViewModeTimeout > 2500)
  {
    m_dwShowViewModeTimeout = 0;
    m_viewModeChanged = true;
  }

  if (m_dwShowViewModeTimeout)
  {
    RESOLUTION_INFO res = g_graphicsContext.GetResInfo();

    {
      // get the "View Mode" string
      std::string strTitle = g_localizeStrings.Get(629);
      const auto& settings = CMediaSettings::GetInstance().GetCurrentVideoSettings();
      int sId = CViewModeSettings::GetViewModeStringIndex(settings.m_ViewMode);
      std::string strMode = g_localizeStrings.Get(sId);
      std::string strInfo = StringUtils::Format("%s : %s", strTitle.c_str(), strMode.c_str());
      CGUIMessage msg(GUI_MSG_LABEL_SET, GetID(), LABEL_ROW1);
      msg.SetLabel(strInfo);
      OnMessage(msg);
    }
    // show sizing information
    SPlayerVideoStreamInfo info;
    g_application.m_pPlayer->GetVideoStreamInfo(CURRENT_STREAM,info);
    {
      // Splitres scaling factor
      float xscale = (float)res.iScreenWidth  / (float)res.iWidth;
      float yscale = (float)res.iScreenHeight / (float)res.iHeight;

      std::string strSizing = StringUtils::Format(g_localizeStrings.Get(245).c_str(),
                                                 (int)info.SrcRect.Width(),
                                                 (int)info.SrcRect.Height(),
                                                 (int)(info.DestRect.Width() * xscale),
                                                 (int)(info.DestRect.Height() * yscale),
                                                 CDisplaySettings::GetInstance().GetZoomAmount(),
                                                 info.videoAspectRatio*CDisplaySettings::GetInstance().GetPixelRatio(),
                                                 CDisplaySettings::GetInstance().GetPixelRatio(),
                                                 CDisplaySettings::GetInstance().GetVerticalShift());
      CGUIMessage msg(GUI_MSG_LABEL_SET, GetID(), LABEL_ROW2);
      msg.SetLabel(strSizing);
      OnMessage(msg);
    }
    // show resolution information
    {
      std::string strStatus;
      if (g_Windowing.IsFullScreen())
        strStatus = StringUtils::Format("%s %ix%i@%.2fHz - %s",
                                        g_localizeStrings.Get(13287).c_str(),
                                        res.iScreenWidth,
                                        res.iScreenHeight,
                                        res.fRefreshRate,
                                        g_localizeStrings.Get(244).c_str());
      else
        strStatus = StringUtils::Format("%s %ix%i - %s",
                                        g_localizeStrings.Get(13287).c_str(),
                                        res.iScreenWidth,
                                        res.iScreenHeight,
                                        g_localizeStrings.Get(242).c_str());

      CGUIMessage msg(GUI_MSG_LABEL_SET, GetID(), LABEL_ROW3);
      msg.SetLabel(strStatus);
      OnMessage(msg);
    }
  }

  if (m_viewModeChanged)
  {
    if (m_dwShowViewModeTimeout)
    {
      SET_CONTROL_VISIBLE(LABEL_ROW1);
      SET_CONTROL_VISIBLE(LABEL_ROW2);
      SET_CONTROL_VISIBLE(LABEL_ROW3);
      SET_CONTROL_VISIBLE(BLUE_BAR);
    }
    else
    {
      SET_CONTROL_HIDDEN(LABEL_ROW1);
      SET_CONTROL_HIDDEN(LABEL_ROW2);
      SET_CONTROL_HIDDEN(LABEL_ROW3);
      SET_CONTROL_HIDDEN(BLUE_BAR);
    }
    m_viewModeChanged = false;
  }
}

void CGUIWindowFullScreen::Process(unsigned int currentTime, CDirtyRegionList &dirtyregion)
{
  if (g_application.m_pPlayer->IsRenderingGuiLayer())
    MarkDirtyRegion();

  m_controlStats->Reset();

  CGUIWindow::Process(currentTime, dirtyregion);

  //! @todo This isn't quite optimal - ideally we'd only be dirtying up the actual video render rect
  //!       which is probably the job of the renderer as it can more easily track resizing etc.
  m_renderRegion.SetRect(0, 0, (float)g_graphicsContext.GetWidth(), (float)g_graphicsContext.GetHeight());
}

void CGUIWindowFullScreen::Render()
{
  g_graphicsContext.SetRenderingResolution(g_graphicsContext.GetVideoResolution(), false);
  g_application.m_pPlayer->Render(true, 255);
  g_graphicsContext.SetRenderingResolution(m_coordsRes, m_needsScaling);
  CGUIWindow::Render();
}

void CGUIWindowFullScreen::RenderEx()
{
  CGUIWindow::RenderEx();
  g_graphicsContext.SetRenderingResolution(g_graphicsContext.GetVideoResolution(), false);
  g_application.m_pPlayer->Render(false, 255, false);
  g_graphicsContext.SetRenderingResolution(m_coordsRes, m_needsScaling);
}

void CGUIWindowFullScreen::SeekChapter(int iChapter)
{
  g_application.m_pPlayer->SeekChapter(iChapter);

  // Make sure gui items are visible.
  g_infoManager.SetDisplayAfterSeek();
}

void CGUIWindowFullScreen::ToggleOSD()
{
  CGUIDialog *pOSD = GetOSD();
  if (pOSD)
  {
    if (pOSD->IsDialogRunning())
      pOSD->Close();
    else
      pOSD->Open();
  }

  MarkDirtyRegion();
}

void CGUIWindowFullScreen::TriggerOSD()
{
  CGUIDialog *pOSD = GetOSD();
  if (pOSD && !pOSD->IsDialogRunning())
  {
    if (!g_application.m_pPlayer->IsPlayingGame())
      pOSD->SetAutoClose(3000);
    pOSD->Open();
  }
}

bool CGUIWindowFullScreen::HasVisibleControls()
{
  return m_controlStats->nCountVisible > 0;
}

CGUIDialog *CGUIWindowFullScreen::GetOSD()
{
  if (g_application.m_pPlayer->IsPlayingGame())
    return g_windowManager.GetDialog(WINDOW_DIALOG_GAME_OSD);
  else
    return g_windowManager.GetDialog(WINDOW_DIALOG_VIDEO_OSD);
}
