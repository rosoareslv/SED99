#pragma once

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

#include "settings/dialogs/GUIDialogSettingsManualBase.h"

class CGUIDialogNetworkSetup : public CGUIDialogSettingsManualBase
{
public:
  enum NET_PROTOCOL { NET_PROTOCOL_SMB = 0,
                      NET_PROTOCOL_XBMSP,
                      NET_PROTOCOL_FTP,
                      NET_PROTOCOL_HTTP,
                      NET_PROTOCOL_HTTPS,
                      NET_PROTOCOL_DAV,
                      NET_PROTOCOL_DAVS,
                      NET_PROTOCOL_UPNP,
                      NET_PROTOCOL_RSS,
                      NET_PROTOCOL_SFTP,
                      NET_PROTOCOL_NFS};
  CGUIDialogNetworkSetup(void);
  ~CGUIDialogNetworkSetup(void) override;
  bool OnMessage(CGUIMessage& message) override;
  bool OnBack(int actionID) override;
  void OnInitWindow() override;
  void OnDeinitWindow(int nextWindowID) override;

  static bool ShowAndGetNetworkAddress(std::string &path);

  std::string ConstructPath() const;
  void SetPath(const std::string &path);
  bool IsConfirmed() const override { return m_confirmed; };

protected:
  // implementations of ISettingCallback
  void OnSettingChanged(std::shared_ptr<const CSetting> setting) override;
  void OnSettingAction(std::shared_ptr<const CSetting> setting) override;

  // specialization of CGUIDialogSettingsBase
  bool AllowResettingSettings() const override { return false; }
  void Save() override { }
  void SetupView() override;

  // specialization of CGUIDialogSettingsManualBase
  void InitializeSettings() override;

  void OnProtocolChange();
  void OnServerBrowse();
  void OnOK();
  void OnCancel() override;
  void UpdateButtons();

  NET_PROTOCOL m_protocol;
  std::string m_server;
  std::string m_path;
  std::string m_username;
  std::string m_password;
  std::string m_port;

  bool m_confirmed;
};
