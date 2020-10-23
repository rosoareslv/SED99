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

#include "commons/ilog.h"
#include "guilib/GraphicContext.h"
#include "input/touch/generic/GenericTouchActionHandler.h"
#include "input/touch/generic/GenericTouchInputHandler.h"
#include "rendering/dx/DirectXHelper.h"
#include "rendering/dx/RenderContext.h"
#include "utils/SystemInfo.h"
#include "utils/win32/Win32Log.h"
#include "WinSystemWin10DX.h"

#include <agile.h>

std::unique_ptr<CWinSystemBase> CWinSystemBase::CreateWinSystem()
{
  auto winSysDX = new CWinSystemWin10DX();
  winSysDX->SetCoreWindow(DX::CoreWindowHolder::Get()->GetWindow());

  std::unique_ptr<CWinSystemBase> winSystem(winSysDX);
  return winSystem;
}

CWinSystemWin10DX::CWinSystemWin10DX() : CRenderSystemDX()
{
}

CWinSystemWin10DX::~CWinSystemWin10DX()
{
}

void CWinSystemWin10DX::PresentRenderImpl(bool rendered)
{
  if (rendered)
    m_deviceResources->Present();

  if (m_delayDispReset && m_dispResetTimer.IsTimePast())
  {
    m_delayDispReset = false;
    OnDisplayReset();
  }

  if (!rendered)
    Sleep(40);
}

bool CWinSystemWin10DX::CreateNewWindow(const std::string& name, bool fullScreen, RESOLUTION_INFO& res)
{
  const MONITOR_DETAILS* monitor = GetMonitor(res.iScreen);
  if (!monitor)
    return false;

  m_deviceResources = DX::DeviceResources::Get();
  m_deviceResources->SetWindow(m_coreWindow.Get());

  bool created = CWinSystemWin10::CreateNewWindow(name, fullScreen, res) && m_deviceResources->HasValidDevice();
  if (created)
  {
    CGenericTouchInputHandler::GetInstance().RegisterHandler(&CGenericTouchActionHandler::GetInstance());
    CGenericTouchInputHandler::GetInstance().SetScreenDPI(DX::DisplayMetrics::Dpi100);
  }
  return created;
}

void CWinSystemWin10DX::SetWindow(HWND hWnd) const
{
}

bool CWinSystemWin10DX::DestroyRenderSystem()
{
  CRenderSystemDX::DestroyRenderSystem();

  m_deviceResources->Release();
  m_deviceResources.reset();
  return true;
}

void CWinSystemWin10DX::UpdateMonitor() const
{
  //const MONITOR_DETAILS* monitor = GetMonitor(m_nScreen);
  //if (monitor)
  //  m_deviceResources->SetMonitor(monitor->hMonitor);
}

void CWinSystemWin10DX::SetDeviceFullScreen(bool fullScreen, RESOLUTION_INFO& res)
{
  m_deviceResources->SetFullScreen(fullScreen, res);
}

bool CWinSystemWin10DX::ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop)
{
  CWinSystemWin10::ResizeWindow(newWidth, newHeight, newLeft, newTop);
  CRenderSystemDX::OnResize();

  return true;
}

void CWinSystemWin10DX::OnMove(int x, int y)
{
}

bool CWinSystemWin10DX::DPIChanged(WORD dpi, RECT windowRect) const
{
  m_deviceResources->SetDpi(dpi);
  if (!IsAlteringWindow())
    return CWinSystemWin10::DPIChanged(dpi, windowRect);

  return true;
}

void CWinSystemWin10DX::ReleaseBackBuffer()
{
  m_deviceResources->ReleaseBackBuffer();
}

void CWinSystemWin10DX::CreateBackBuffer()
{
  m_deviceResources->CreateBackBuffer();
}

void CWinSystemWin10DX::ResizeDeviceBuffers()
{
  m_deviceResources->ResizeBuffers();
}

bool CWinSystemWin10DX::IsStereoEnabled()
{
  return m_deviceResources->IsStereoEnabled();
}

void CWinSystemWin10DX::OnResize(int width, int height)
{
  if (!m_IsAlteringWindow)
    ReleaseBackBuffer();

  m_deviceResources->SetLogicalSize(width, height);

  if (!m_IsAlteringWindow)
    CreateBackBuffer();
}

bool CWinSystemWin10DX::SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays)
{
  bool const result = CWinSystemWin10::SetFullScreen(fullScreen, res, blankOtherDisplays);
  CRenderSystemDX::OnResize();
  return result;
}

void CWinSystemWin10DX::UninitHooks()
{
}

void CWinSystemWin10DX::InitHooks(IDXGIOutput* pOutput)
{
}
