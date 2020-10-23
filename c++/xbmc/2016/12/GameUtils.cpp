/*
 *      Copyright (C) 2012-2016 Team Kodi
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
 *  along with this Program; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "GameUtils.h"
#include "addons/Addon.h"
#include "addons/AddonManager.h"
#include "addons/BinaryAddonCache.h"
#include "dialogs/GUIDialogOK.h"
#include "games/addons/GameClient.h"
#include "games/dialogs/GUIDialogSelectGameClient.h"
#include "games/tags/GameInfoTag.h"
#include "filesystem/SpecialProtocol.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "FileItem.h"
#include "ServiceBroker.h"
#include "URL.h"

#include <algorithm>

using namespace GAME;

GameClientPtr CGameUtils::OpenGameClient(const CFileItem& file)
{
  using namespace ADDON;

  GameClientPtr gameClient;

  // Get the game client ID from the game info tag
  std::string gameClientId;
  if (file.HasGameInfoTag())
    gameClientId = file.GetGameInfoTag()->GetGameClient();

  // If the fileitem is an add-on, fall back to that
  if (gameClientId.empty())
  {
    if (file.HasAddonInfo() && file.GetAddonInfo()->Type() == ADDON::ADDON_GAMEDLL)
      gameClientId = file.GetAddonInfo()->ID();
  }

  // Get game client by ID
  if (!gameClientId.empty())
  {
    CBinaryAddonCache& addonCache = CServiceBroker::GetBinaryAddonCache();
    AddonPtr addon = addonCache.GetAddonInstance(gameClientId, ADDON_GAMEDLL);
    gameClient = std::static_pointer_cast<GAME::CGameClient>(addon);
  }

  // Need to prompt the user if no game client was found
  if (!gameClient)
  {
    GameClientVector candidates;
    GameClientVector installable;
    bool bHasVfsGameClient;
    GetGameClients(file, candidates, installable, bHasVfsGameClient);

    if (candidates.empty() && installable.empty())
    {
      int errorTextId = bHasVfsGameClient ?
          35214 : // "This game can only be played directly from a hard drive or partition. Compressed files must be extracted."
          35212;  // "This game isn't compatible with any available emulators."

      // "Failed to play game"
      CGUIDialogOK::ShowAndGetInput(CVariant{ 35210 }, CVariant{ errorTextId });
    }
    else if (candidates.size() == 1 && installable.empty())
    {
      // Only 1 option, avoid prompting the user
      gameClient = candidates[0];
    }
    else
    {
      CGUIDialogSelectGameClient::ShowAndGetGameClient(candidates, installable, gameClient);
    }
  }

  return gameClient;
}

void CGameUtils::GetGameClients(const CFileItem& file, GameClientVector& candidates, GameClientVector& installable, bool& bHasVfsGameClient)
{
  using namespace ADDON;

  bHasVfsGameClient = false;

  // Try to resolve path to a local file, as not all game clients support VFS
  CURL translatedUrl(CSpecialProtocol::TranslatePath(file.GetPath()));

  // Get local candidates
  VECADDONS localAddons;
  CBinaryAddonCache& addonCache = CServiceBroker::GetBinaryAddonCache();
  addonCache.GetAddons(localAddons, ADDON_GAMEDLL);

  bool bVfs = false;
  GetGameClients(localAddons, translatedUrl, candidates, bVfs);
  bHasVfsGameClient |= bVfs;

  // Get remote candidates
  VECADDONS remoteAddons;
  if (CAddonMgr::GetInstance().GetInstallableAddons(remoteAddons, ADDON_GAMEDLL))
  {
    GetGameClients(remoteAddons, translatedUrl, installable, bVfs);
    bHasVfsGameClient |= bVfs;
  }

  // Sort by name
  //! @todo Move to presentation code
  auto SortByName = [](const GameClientPtr& lhs, const GameClientPtr& rhs)
  {
    std::string lhsName = lhs->Name();
    std::string rhsName = rhs->Name();

    StringUtils::ToLower(lhsName);
    StringUtils::ToLower(rhsName);

    return lhsName < rhsName;
  };

  std::sort(candidates.begin(), candidates.end(), SortByName);
  std::sort(installable.begin(), installable.end(), SortByName);
}

void CGameUtils::GetGameClients(const ADDON::VECADDONS& addons, const CURL& translatedUrl, GameClientVector& candidates, bool& bHasVfsGameClient)
{
  bHasVfsGameClient = false;

  const std::string extension = URIUtils::GetExtension(translatedUrl.Get());

  const bool bIsLocalFile = (translatedUrl.GetProtocol() == "file" ||
                             translatedUrl.GetProtocol().empty());

  for (auto& addon : addons)
  {
    GameClientPtr gameClient = std::static_pointer_cast<CGameClient>(addon);

    // Filter by extension
    if (!gameClient->IsExtensionValid(extension))
      continue;

    // Filter by VFS
    if (!bIsLocalFile && !gameClient->SupportsVFS())
    {
      bHasVfsGameClient = true;
      continue;
    }

    candidates.push_back(gameClient);
  }
}

bool CGameUtils::HasGameExtension(const std::string &path)
{
  using namespace ADDON;

  // Get filename from CURL so that top-level zip directories will become
  // normal paths:
  //
  //   zip://%2Fpath_to_zip_file.zip/  ->  /path_to_zip_file.zip
  //
  std::string filename = CURL(path).GetFileNameWithoutPath();

  // Get the file extension
  std::string extension = URIUtils::GetExtension(filename);
  if (extension.empty())
    return false;

  StringUtils::ToLower(extension);

  // Look for a game client that supports this extension
  VECADDONS gameClients;
  CBinaryAddonCache& addonCache = CServiceBroker::GetBinaryAddonCache();
  addonCache.GetAddons(gameClients, ADDON_GAMEDLL);
  for (auto& gameClient : gameClients)
  {
    GameClientPtr gc(std::static_pointer_cast<CGameClient>(gameClient));
    if (gc->IsExtensionValid(extension))
      return true;
  }

  // Check remote add-ons
  gameClients.clear();
  if (CAddonMgr::GetInstance().GetInstallableAddons(gameClients, ADDON_GAMEDLL))
  {
    for (auto& gameClient : gameClients)
    {
      GameClientPtr gc(std::static_pointer_cast<CGameClient>(gameClient));
      if (gc->IsExtensionValid(extension))
        return true;
    }
  }

  return false;
}

std::set<std::string> CGameUtils::GetGameExtensions()
{
  using namespace ADDON;

  std::set<std::string> extensions;

  VECADDONS gameClients;
  CBinaryAddonCache& addonCache = CServiceBroker::GetBinaryAddonCache();
  addonCache.GetAddons(gameClients, ADDON_GAMEDLL);
  for (auto& gameClient : gameClients)
  {
    GameClientPtr gc(std::static_pointer_cast<CGameClient>(gameClient));
    extensions.insert(gc->GetExtensions().begin(), gc->GetExtensions().end());
  }

  // Check remote add-ons
  gameClients.clear();
  if (CAddonMgr::GetInstance().GetInstallableAddons(gameClients, ADDON_GAMEDLL))
  {
    for (auto& gameClient : gameClients)
    {
      GameClientPtr gc(std::static_pointer_cast<CGameClient>(gameClient));
      extensions.insert(gc->GetExtensions().begin(), gc->GetExtensions().end());
    }
  }

  return extensions;
}

bool CGameUtils::IsStandaloneGame(const ADDON::AddonPtr& addon)
{
  using namespace ADDON;

  switch (addon->Type())
  {
    case ADDON_GAMEDLL:
    {
      return std::static_pointer_cast<GAME::CGameClient>(addon)->SupportsStandalone();
    }
    case ADDON_SCRIPT:
    {
      return addon->IsType(ADDON_GAME);
    }
    default:
      break;
  }

  return false;
}
