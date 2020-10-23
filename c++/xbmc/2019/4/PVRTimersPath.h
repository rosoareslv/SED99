/*
 *  Copyright (C) 2012-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <string>

namespace PVR
{
  class CPVRTimersPath
  {
  public:
    static const std::string PATH_ADDTIMER;
    static const std::string PATH_NEW;

    explicit CPVRTimersPath(const std::string& strPath);
    CPVRTimersPath(const std::string& strPath, int iClientId, unsigned int iParentId);
    CPVRTimersPath(bool bRadio, bool bTimerRules);

    bool IsValid() const { return m_bValid; }

    const std::string& GetPath() const { return m_path; }
    bool IsTimersRoot() const { return m_bRoot; }
    bool IsTimerRule() const { return !IsTimersRoot(); }
    bool IsRadio() const { return m_bRadio; }
    bool IsRules() const { return m_bTimerRules; }
    int GetClientId() const { return m_iClientId; }
    unsigned int GetParentId() const { return m_iParentId; }

  private:
    bool Init(const std::string &strPath);

    std::string m_path;
    bool m_bValid = false;
    bool m_bRoot = false;
    bool m_bRadio = false;
    bool m_bTimerRules = false;
    int m_iClientId = -1;
    unsigned int m_iParentId = 0;
  };
}
