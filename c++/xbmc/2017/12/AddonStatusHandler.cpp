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
#include "AddonStatusHandler.h"
#include "ServiceBroker.h"
#include "addons/AddonManager.h"
#include "addons/settings/GUIDialogAddonSettings.h"
#include "threads/SingleLock.h"
#include "messaging/ApplicationMessenger.h"
#include "guilib/GUIWindowManager.h"
#include "dialogs/GUIDialogYesNo.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "messaging/helpers/DialogOKHelper.h"
#include "settings/Settings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/Variant.h"

using namespace KODI::MESSAGING;

namespace ADDON
{

/**********************************************************
 * CAddonStatusHandler - AddOn Status Report Class
 *
 * Used to inform the user about occurred errors and
 * changes inside Add-on's, and ask him what to do.
 *
 */

CCriticalSection CAddonStatusHandler::m_critSection;

CAddonStatusHandler::CAddonStatusHandler(const std::string &addonID, ADDON_STATUS status, std::string message, bool sameThread)
  : CThread(("AddonStatus " + addonID).c_str()),
    m_status(ADDON_STATUS_UNKNOWN)
{
  //! @todo The status handled CAddonStatusHandler by is related to the class, not the instance
  //! having CAddonMgr construct an instance makes no sense
  if (!CServiceBroker::GetAddonMgr().GetAddon(addonID, m_addon))
    return;

  CLog::Log(LOGINFO, "Called Add-on status handler for '%u' of clientName:%s, clientID:%s (same Thread=%s)", status, m_addon->Name().c_str(), m_addon->ID().c_str(), sameThread ? "yes" : "no");

  m_status  = status;
  m_message = message;

  if (sameThread)
  {
    Process();
  }
  else
  {
    Create(true, THREAD_MINSTACKSIZE);
  }
}

CAddonStatusHandler::~CAddonStatusHandler()
{
  StopThread();
}

void CAddonStatusHandler::OnStartup()
{
  SetPriority(GetMinPriority());
}

void CAddonStatusHandler::OnExit()
{
}

void CAddonStatusHandler::Process()
{
  CSingleLock lock(m_critSection);

  std::string heading = StringUtils::Format("%s: %s", CAddonInfo::TranslateType(m_addon->Type(), true).c_str(), m_addon->Name().c_str());

  /* Request to restart the AddOn and data structures need updated */
  if (m_status == ADDON_STATUS_NEED_RESTART)
  {
    HELPERS::ShowOKDialogLines(CVariant{heading}, CVariant{24074});
    CServiceBroker::GetAddonMgr().GetCallbackForType(m_addon->Type())->RequestRestart(m_addon, true);
  }
  /* Some required settings are missing/invalid */
  else if (m_status == ADDON_STATUS_NEED_SETTINGS)
  {
    CGUIDialogYesNo* pDialogYesNo = g_windowManager.GetWindow<CGUIDialogYesNo>(WINDOW_DIALOG_YES_NO);
    if (!pDialogYesNo) return;

    pDialogYesNo->SetHeading(CVariant{heading});
    pDialogYesNo->SetLine(1, CVariant{24070});
    pDialogYesNo->SetLine(2, CVariant{24072});
    pDialogYesNo->SetLine(3, CVariant{m_message});
    pDialogYesNo->Open();

    if (!pDialogYesNo->IsConfirmed()) return;

    if (!m_addon->HasSettings())
      return;

    if (CGUIDialogAddonSettings::ShowForAddon(m_addon))
    {
      //! @todo Doesn't dialogaddonsettings save these automatically? It should do this.
      m_addon->SaveSettings();
      CServiceBroker::GetAddonMgr().GetCallbackForType(m_addon->Type())->RequestRestart(m_addon, true);
    }
  }
}


} /*namespace ADDON*/

