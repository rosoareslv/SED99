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

#pragma once

#include <memory>
#include "platform/Platform.h"

namespace ADDON {
class CAddonMgr;
class CBinaryAddonCache;
}

namespace ActiveAE {
class CActiveAE;
}

namespace ANNOUNCEMENT
{
class CAnnouncementManager;
}

namespace PVR
{
class CPVRManager;
}

namespace PLAYLIST
{
  class CPlayListPlayer;
}

class CContextMenuManager;
#ifdef HAS_PYTHON
class XBPython;
#endif
class CDataCacheCore;
class CSettings;
class IAE;
class CFavouritesService;

namespace GAME
{
  class CGameServices;
}

namespace PERIPHERALS
{
  class CPeripherals;
}

class CServiceManager
{
public:
  CServiceManager();
  ~CServiceManager();

  bool Init1();
  bool Init2();
  bool CreateAudioEngine();
  bool DestroyAudioEngine();
  bool StartAudioEngine();
  bool Init3();
  void Deinit();
  ADDON::CAddonMgr& GetAddonMgr();
  ADDON::CBinaryAddonCache& GetBinaryAddonCache();
  ANNOUNCEMENT::CAnnouncementManager& GetAnnouncementManager();
#ifdef HAS_PYTHON
  XBPython& GetXBPython();
#endif
  PVR::CPVRManager& GetPVRManager();
  IAE& GetActiveAE();
  CContextMenuManager& GetContextMenuManager();
  CDataCacheCore& GetDataCacheCore();
  /**\brief Get the platform object. This is save to be called after Init1() was called
   */
  CPlatform& GetPlatform();
  GAME::CGameServices& GetGameServices();
  PERIPHERALS::CPeripherals& GetPeripherals();

  PLAYLIST::CPlayListPlayer& GetPlaylistPlayer();
  int init_level = 0;

  CSettings& GetSettings();
  CFavouritesService& GetFavouritesService();

protected:
  struct delete_dataCacheCore
  {
    void operator()(CDataCacheCore *p) const;
  };

  struct delete_contextMenuManager
  {
    void operator()(CContextMenuManager *p) const;
  };

  struct delete_activeAE
  {
    void operator()(ActiveAE::CActiveAE *p) const;
  };

  struct delete_favouritesService
  {
    void operator()(CFavouritesService *p) const;
  };

  std::unique_ptr<ADDON::CAddonMgr> m_addonMgr;
  std::unique_ptr<ADDON::CBinaryAddonCache> m_binaryAddonCache;
  std::unique_ptr<ANNOUNCEMENT::CAnnouncementManager> m_announcementManager;
#ifdef HAS_PYTHON
  std::unique_ptr<XBPython> m_XBPython;
#endif
  std::unique_ptr<PVR::CPVRManager> m_PVRManager;
  std::unique_ptr<ActiveAE::CActiveAE, delete_activeAE> m_ActiveAE;
  std::unique_ptr<CContextMenuManager, delete_contextMenuManager> m_contextMenuManager;
  std::unique_ptr<CDataCacheCore, delete_dataCacheCore> m_dataCacheCore;
  std::unique_ptr<CPlatform> m_Platform;
  std::unique_ptr<PLAYLIST::CPlayListPlayer> m_playlistPlayer;
  std::unique_ptr<CSettings> m_settings;
  std::unique_ptr<GAME::CGameServices> m_gameServices;
  std::unique_ptr<PERIPHERALS::CPeripherals> m_peripherals;
  std::unique_ptr<CFavouritesService, delete_favouritesService> m_favouritesService;
};
