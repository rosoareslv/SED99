/*
 *      Copyright (C) 2005-2013 Team XBMC
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

#ifndef WINDOW_SYSTEM_WIN10_H
#define WINDOW_SYSTEM_WIN10_H

#pragma once

#include <agile.h>
#include <string>
#include <vector>

#include "guilib/DispResource.h"
#include "threads/CriticalSection.h"
#include "threads/SystemClock.h"
#include "windowing/WinSystem.h"

#pragma pack(push)
#pragma pack(8)

/* Controls the way the window appears and behaves. */
enum WINDOW_STATE
{
  WINDOW_STATE_FULLSCREEN = 1,    // Exclusive fullscreen
  WINDOW_STATE_FULLSCREEN_WINDOW, // Non-exclusive fullscreen window
  WINDOW_STATE_WINDOWED,          //Movable window with border
  WINDOW_STATE_BORDERLESS         //Non-movable window with no border
};

static const char* window_state_names[] =
{
  "unknown",
  "true fullscreen",
  "windowed fullscreen",
  "windowed",
  "borderless"
};

/* WINDOW_STATE restricted to fullscreen modes. */
enum WINDOW_FULLSCREEN_STATE
{
  WINDOW_FULLSCREEN_STATE_FULLSCREEN = WINDOW_STATE_FULLSCREEN,
  WINDOW_FULLSCREEN_STATE_FULLSCREEN_WINDOW = WINDOW_STATE_FULLSCREEN_WINDOW
};

/* WINDOW_STATE restricted to windowed modes. */
enum WINDOW_WINDOW_STATE
{
  WINDOW_WINDOW_STATE_WINDOWED = WINDOW_STATE_WINDOWED,
  WINDOW_WINDOW_STATE_BORDERLESS = WINDOW_STATE_BORDERLESS
};

struct MONITOR_DETAILS
{
  // Windows desktop info
  int       ScreenWidth;
  int       ScreenHeight;
  float     RefreshRate;
  int       Bpp;
  bool      Interlaced;

  //HMONITOR  hMonitor;
  //std::wstring MonitorNameW;
  //std::wstring CardNameW;
  //std::wstring DeviceNameW;
  int       ScreenNumber; // XBMC POV, not Windows. Windows primary is XBMC #0, then each secondary is +1.
};

class CWinSystemWin10 : public CWinSystemBase
{
public:
  CWinSystemWin10();
  virtual ~CWinSystemWin10();

  // CWinSystemBase overrides
  bool InitWindowSystem() override;
  bool DestroyWindowSystem() override;
  bool ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop) override;
  void FinishWindowResize(int newWidth, int newHeight) override;
  void UpdateResolutions() override;
  bool CenterWindow() override;
  virtual void NotifyAppFocusChange(bool bGaining) override;
  int  GetNumScreens() override { return m_MonitorsInfo.size(); };
  int  GetCurrentScreen() override;
  void ShowOSMouse(bool show) override;
  bool HasInertialGestures() override { return true; }//if win32 has touchscreen - it uses the win32 gesture api for inertial scrolling
  bool Minimize() override;
  bool Restore() override;
  bool Hide() override;
  bool Show(bool raise = true) override;
  std::string GetClipboardText() override;
  // videosync
  std::unique_ptr<CVideoSync> GetVideoSync(void *clock) override;

  bool WindowedMode() const { return m_state != WINDOW_STATE_FULLSCREEN; }
  bool SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays) override;

  // CWinSystemWin10
  HWND GetHwnd() const { return nullptr; }
  bool IsAlteringWindow() const { return m_IsAlteringWindow; }
  virtual bool DPIChanged(WORD dpi, RECT windowRect) const;
  bool IsMinimized() const { return m_bMinimized; }
  void SetMinimized(bool minimized) { m_bMinimized = minimized; }

  // UWP
  void SetCoreWindow(Windows::UI::Core::CoreWindow^ window);
  Windows::UI::Core::CoreWindow^ GetCoreWindow() { return m_coreWindow.Get(); }

  bool CanDoWindowed() override;

protected:
  bool CreateNewWindow(const std::string& name, bool fullScreen, RESOLUTION_INFO& res) override = 0;
  virtual void UpdateStates(bool fullScreen);
  WINDOW_STATE GetState(bool fullScreen) const;
  virtual void SetDeviceFullScreen(bool fullScreen, RESOLUTION_INFO& res) = 0;
  virtual void ReleaseBackBuffer() = 0;
  virtual void CreateBackBuffer() = 0;
  virtual void ResizeDeviceBuffers() = 0;
  virtual bool IsStereoEnabled() = 0;
  virtual void AdjustWindow(bool forceResize = false);
  void CenterCursor() const;

  virtual void Register(IDispResource *resource);
  virtual void Unregister(IDispResource *resource);

  bool ChangeResolution(const RESOLUTION_INFO& res, bool forceChange = false);
  virtual bool UpdateResolutionsInternal();
  const MONITOR_DETAILS* GetMonitor(int screen) const;
  void RestoreDesktopResolution(int screen);
  RECT ScreenRect(int screen) const;

  /*!
  \brief Adds a resolution to the list of resolutions if we don't already have it
  \param res resolution to add.
  */
  static void AddResolution(const RESOLUTION_INFO &res);

  void OnDisplayLost();
  void OnDisplayReset();
  void OnDisplayBack();
  void ResolutionChanged();

  std::vector<MONITOR_DETAILS> m_MonitorsInfo;
  int m_nPrimary;
  bool m_ValidWindowedPosition;
  bool m_IsAlteringWindow;

  CCriticalSection m_resourceSection;
  std::vector<IDispResource*> m_resources;
  bool m_delayDispReset;
  XbmcThreads::EndTime m_dispResetTimer;

  WINDOW_STATE m_state;                       // the state of the window
  WINDOW_FULLSCREEN_STATE m_fullscreenState;  // the state of the window when in fullscreen
  WINDOW_WINDOW_STATE m_windowState;          // the state of the window when in windowed
  bool m_inFocus;
  bool m_bMinimized;

  Platform::Agile<Windows::UI::Core::CoreWindow> m_coreWindow;
};

#pragma pack(pop)

#endif // WINDOW_SYSTEM_WIN10_H
