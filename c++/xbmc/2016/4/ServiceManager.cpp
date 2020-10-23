/*
 *      Copyright (C) 2005-2016 Team XBMC
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

#include "ServiceManager.h"
#include "cores/AudioEngine/DSPAddons/ActiveAEDSP.h"
#include "utils/log.h"
#include "interfaces/AnnouncementManager.h"
#include "interfaces/generic/ScriptInvocationManager.h"

bool CServiceManager::Init1()
{
  m_announcementManager.reset(new ANNOUNCEMENT::CAnnouncementManager());
  m_announcementManager->Start();

  m_XBPython.reset(new XBPython());
  CScriptInvocationManager::GetInstance().RegisterLanguageInvocationHandler(m_XBPython.get(), ".py");

  return true;
}

bool CServiceManager::Init2()
{
  m_addonMgr.reset(new ADDON::CAddonMgr());
  if (!m_addonMgr->Init())
  {
    CLog::Log(LOGFATAL, "CServiceManager::Init: Unable to start CAddonMgr");
    return false;
  }

  m_ADSPManager.reset(new ActiveAE::CActiveAEDSP());
  m_PVRManager.reset(new PVR::CPVRManager());

  m_binaryAddonCache.reset( new ADDON::CBinaryAddonCache());
  m_binaryAddonCache->Init();

  return true;
}

bool CServiceManager::Init3()
{
  m_ADSPManager->Init();
  m_PVRManager->Init();

  return true;
}

void CServiceManager::Deinit()
{
  m_binaryAddonCache.reset();
  m_PVRManager.reset();
  m_ADSPManager.reset();
  m_addonMgr.reset();
  CScriptInvocationManager::GetInstance().UnregisterLanguageInvocationHandler(m_XBPython.get());
  m_XBPython.reset();
  m_announcementManager.reset();
}

ADDON::CAddonMgr &CServiceManager::GetAddonMgr()
{
  return *m_addonMgr.get();
}

ADDON::CBinaryAddonCache &CServiceManager::GetBinaryAddonCache()
{
  return *m_binaryAddonCache.get();
}

ANNOUNCEMENT::CAnnouncementManager& CServiceManager::GetAnnouncementManager()
{
  return *m_announcementManager;
}

XBPython& CServiceManager::GetXBPython()
{
  return *m_XBPython;
}

PVR::CPVRManager& CServiceManager::GetPVRManager()
{
  return *m_PVRManager;
}

ActiveAE::CActiveAEDSP& CServiceManager::GetADSPManager()
{
  return *m_ADSPManager;
}
