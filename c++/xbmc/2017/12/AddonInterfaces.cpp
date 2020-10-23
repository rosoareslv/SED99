/*
 *      Copyright (C) 2012-2013 Team XBMC
 *      Copyright (C) 2015-2016 Team KODI
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
 *  along with KODI; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "AddonInterfaces.h"

#include "addons/Addon.h"
#include "addons/PVRClient.h"
#include "games/addons/GameClient.h"

#include "addons/interfaces/Addon/AddonCallbacksAddon.h"
#include "addons/interfaces/GUI/AddonCallbacksGUI.h"
#include "addons/interfaces/GUI/Window.h"
#include "addons/interfaces/GUI/AddonGUIWindow.h"
#include "filesystem/SpecialProtocol.h"
#include "messaging/ApplicationMessenger.h"
#include "peripherals/addons/PeripheralAddon.h"
#include "utils/log.h"

using namespace KODI;
using namespace MESSAGING;

namespace ADDON
{

CAddonInterfaces::CAddonInterfaces(CAddon* addon)
  : m_callbacks(new AddonCB),
    m_addon(addon),
    m_helperAddOn(nullptr),
    m_helperGUI(nullptr)
{
  m_callbacks->libBasePath                  = strdup(CSpecialProtocol::TranslatePath("special://xbmcbinaddons").c_str());
  m_callbacks->addonData                    = this;

  m_callbacks->AddOnLib_RegisterMe          = CAddonInterfaces::AddOnLib_RegisterMe;
  m_callbacks->AddOnLib_UnRegisterMe        = CAddonInterfaces::AddOnLib_UnRegisterMe;
  m_callbacks->GUILib_RegisterMe            = CAddonInterfaces::GUILib_RegisterMe;
  m_callbacks->GUILib_UnRegisterMe          = CAddonInterfaces::GUILib_UnRegisterMe;
  m_callbacks->PVRLib_RegisterMe            = CAddonInterfaces::PVRLib_RegisterMe;
  m_callbacks->PVRLib_UnRegisterMe          = CAddonInterfaces::PVRLib_UnRegisterMe;
  m_callbacks->GameLib_RegisterMe           = CAddonInterfaces::GameLib_RegisterMe;
  m_callbacks->GameLib_UnRegisterMe         = CAddonInterfaces::GameLib_UnRegisterMe;
}

CAddonInterfaces::~CAddonInterfaces()
{
  delete static_cast<KodiAPI::AddOn::CAddonCallbacksAddon*>(m_helperAddOn);
  delete static_cast<KodiAPI::GUI::CAddonCallbacksGUI*>(m_helperGUI);

  free((char*)m_callbacks->libBasePath);
  delete m_callbacks;
}

/*\_____________________________________________________________________________
\*/

void* CAddonInterfaces::AddOnLib_RegisterMe(void *addonData)
{
  CAddonInterfaces* addon = static_cast<CAddonInterfaces*>(addonData);
  if (addon == nullptr)
  {
    CLog::Log(LOGERROR, "CAddonInterfaces - %s - called with a null pointer", __FUNCTION__);
    return nullptr;
  }

  addon->m_helperAddOn = new KodiAPI::AddOn::CAddonCallbacksAddon(addon->m_addon);
  return static_cast<KodiAPI::AddOn::CAddonCallbacksAddon*>(addon->m_helperAddOn)->GetCallbacks();
}

void CAddonInterfaces::AddOnLib_UnRegisterMe(void *addonData, void *cbTable)
{
  CAddonInterfaces* addon = static_cast<CAddonInterfaces*>(addonData);
  if (addon == nullptr)
  {
    CLog::Log(LOGERROR, "CAddonInterfaces - %s - called with a null pointer", __FUNCTION__);
    return;
  }

  delete static_cast<KodiAPI::AddOn::CAddonCallbacksAddon*>(addon->m_helperAddOn);
  addon->m_helperAddOn = nullptr;
}
/*\_____________________________________________________________________________
\*/
void* CAddonInterfaces::GUILib_RegisterMe(void *addonData)
{
  CAddonInterfaces* addon = static_cast<CAddonInterfaces*>(addonData);
  if (addon == nullptr)
  {
    CLog::Log(LOGERROR, "CAddonInterfaces - %s - called with a null pointer", __FUNCTION__);
    return nullptr;
  }

  addon->m_helperGUI = new KodiAPI::GUI::CAddonCallbacksGUI(addon->m_addon);
  return static_cast<KodiAPI::GUI::CAddonCallbacksGUI*>(addon->m_helperGUI)->GetCallbacks();
}

void CAddonInterfaces::GUILib_UnRegisterMe(void *addonData, void *cbTable)
{
  CAddonInterfaces* addon = static_cast<CAddonInterfaces*>(addonData);
  if (addon == nullptr)
  {
    CLog::Log(LOGERROR, "CAddonInterfaces - %s - called with a null pointer", __FUNCTION__);
    return;
  }

  delete static_cast<KodiAPI::GUI::CAddonCallbacksGUI*>(addon->m_helperGUI);
  addon->m_helperGUI = nullptr;
}
/*\_____________________________________________________________________________
\*/
void* CAddonInterfaces::PVRLib_RegisterMe(void *addonData)
{
  CAddonInterfaces* addon = static_cast<CAddonInterfaces*>(addonData);
  if (addon == nullptr)
  {
    CLog::Log(LOGERROR, "CAddonInterfaces - %s - called with a null pointer", __FUNCTION__);
    return nullptr;
  }

  return dynamic_cast<PVR::CPVRClient*>(addon->m_addon)->GetInstanceInterface();
}

void CAddonInterfaces::PVRLib_UnRegisterMe(void *addonData, void *cbTable)
{
}
/*\_____________________________________________________________________________
\*/
void* CAddonInterfaces::GameLib_RegisterMe(void *addonData)
{
  CAddonInterfaces* addon = static_cast<CAddonInterfaces*>(addonData);
  if (addon == nullptr)
  {
    CLog::Log(LOGERROR, "CAddonInterfaces - %s - called with a null pointer", __FUNCTION__);
    return nullptr;
  }

  return dynamic_cast<GAME::CGameClient*>(addon->m_addon)->GetInstanceInterface();
}

void CAddonInterfaces::GameLib_UnRegisterMe(void *addonData, void *cbTable)
{
}
/*\_____________________________________________________________________________
\*/
void CAddonInterfaces::OnApplicationMessage(ThreadMessage* pMsg)
{
  switch (pMsg->dwMessage)
  {
  case TMSG_GUI_ADDON_DIALOG:
  {
    if (pMsg->lpVoid)
    { //! @todo This is ugly - really these binary add-on dialogs should just be normal Kodi dialogs
      switch (pMsg->param1)
      {
      case 0:
        static_cast<ADDON::CGUIAddonWindowDialog*>(pMsg->lpVoid)->Show_Internal(pMsg->param2 > 0);
        break;
      case 1:
        static_cast<KodiAPI::GUI::CGUIAddonWindowDialog*>(pMsg->lpVoid)->Show_Internal(pMsg->param2 > 0);
        break;
      };
    }
  }
  break;
  }
}

} /* namespace ADDON */
