/*
 *      Copyright (C) 2013-2015 Team XBMC
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

#include "ContextMenuManager.h"
#include "ContextMenuItem.h"
#include "addons/Addon.h"
#include "addons/ContextMenuAddon.h"
#include "addons/ContextMenus.h"
#include "addons/IAddon.h"
#include "favourites/ContextMenus.h"
#include "music/ContextMenus.h"
#include "pvr/PVRContextMenus.h"
#include "video/ContextMenus.h"
#include "utils/log.h"
#include "ServiceBroker.h"

#include <iterator>
#include "ContextMenus.h"

using namespace ADDON;
using namespace PVR;

const CContextMenuItem CContextMenuManager::MAIN = CContextMenuItem::CreateGroup("", "", "kodi.core.main", "");
const CContextMenuItem CContextMenuManager::MANAGE = CContextMenuItem::CreateGroup("", "", "kodi.core.manage", "");


CContextMenuManager::CContextMenuManager(CAddonMgr& addonMgr)
  : m_addonMgr(addonMgr) {}

CContextMenuManager::~CContextMenuManager()
{
  Deinit();
}

void CContextMenuManager::Deinit()
{
  m_addonMgr.Events().Unsubscribe(this);
  m_items.clear();
}

CContextMenuManager& CContextMenuManager::GetInstance()
{
  return CServiceBroker::GetContextMenuManager();
}

void CContextMenuManager::Init()
{
  m_addonMgr.Events().Subscribe(this, &CContextMenuManager::OnEvent);

  CSingleLock lock(m_criticalSection);
  m_items = {
      std::make_shared<CONTEXTMENU::CResume>(),
      std::make_shared<CONTEXTMENU::CPlay>(),
      std::make_shared<CONTEXTMENU::CAddonInfo>(),
      std::make_shared<CONTEXTMENU::CEnableAddon>(),
      std::make_shared<CONTEXTMENU::CDisableAddon>(),
      std::make_shared<CONTEXTMENU::CAddonSettings>(),
      std::make_shared<CONTEXTMENU::CCheckForUpdates>(),
      std::make_shared<CONTEXTMENU::CEpisodeInfo>(),
      std::make_shared<CONTEXTMENU::CMovieInfo>(),
      std::make_shared<CONTEXTMENU::CMusicVideoInfo>(),
      std::make_shared<CONTEXTMENU::CTVShowInfo>(),
      std::make_shared<CONTEXTMENU::CAlbumInfo>(),
      std::make_shared<CONTEXTMENU::CArtistInfo>(),
      std::make_shared<CONTEXTMENU::CSongInfo>(),
      std::make_shared<CONTEXTMENU::CMarkWatched>(),
      std::make_shared<CONTEXTMENU::CMarkUnWatched>(),
      std::make_shared<CONTEXTMENU::CRemoveResumePoint>(),
      std::make_shared<CONTEXTMENU::CEjectDisk>(),
      std::make_shared<CONTEXTMENU::CEjectDrive>(),
      std::make_shared<CONTEXTMENU::CRemoveFavourite>(),
      std::make_shared<CONTEXTMENU::CRenameFavourite>(),
      std::make_shared<CONTEXTMENU::CChooseThumbnailForFavourite>(),
  };

  ReloadAddonItems();

  const std::vector<std::shared_ptr<IContextMenuItem>> pvrItems(CPVRContextMenuManager::GetInstance().GetMenuItems());
  for (const auto &item : pvrItems)
    m_items.emplace_back(item);
}

void CContextMenuManager::ReloadAddonItems()
{
  VECADDONS addons;
  m_addonMgr.GetAddons(addons, ADDON_CONTEXT_ITEM);

  std::vector<CContextMenuItem> addonItems;
  for (const auto& addon : addons)
  {
    auto items = std::static_pointer_cast<CContextMenuAddon>(addon)->GetItems();
    for (auto& item : items)
    {
      auto it = std::find(addonItems.begin(), addonItems.end(), item);
      if (it == addonItems.end())
        addonItems.push_back(item);
    }
  }

  CSingleLock lock(m_criticalSection);
  m_addonItems = std::move(addonItems);

  CLog::Log(LOGDEBUG, "ContextMenuManager: addon menus reloaded.");
}

void CContextMenuManager::OnEvent(const ADDON::AddonEvent& event)
{
  if (typeid(event) == typeid(AddonEvents::ReInstalled) ||
      typeid(event) == typeid(AddonEvents::UnInstalled))
  {
    ReloadAddonItems();
  }
  else if (typeid(event) == typeid(AddonEvents::Enabled))
  {
    AddonPtr addon;
    if (m_addonMgr.GetAddon(event.id, addon, ADDON_CONTEXT_ITEM))
    {
      CSingleLock lock(m_criticalSection);
      auto items = std::static_pointer_cast<CContextMenuAddon>(addon)->GetItems();
      for (auto& item : items)
      {
        auto it = std::find(m_addonItems.begin(), m_addonItems.end(), item);
        if (it == m_addonItems.end())
          m_addonItems.push_back(item);
      }
      CLog::Log(LOGDEBUG, "ContextMenuManager: loaded %s.", event.id.c_str());
    }
  }
  else if (typeid(event) == typeid(AddonEvents::Disabled))
  {
    if (m_addonMgr.HasType(event.id, ADDON_CONTEXT_ITEM))
    {
      ReloadAddonItems();
    }
  }
}

bool CContextMenuManager::IsVisible(
  const CContextMenuItem& menuItem, const CContextMenuItem& root, const CFileItem& fileItem) const
{
  if (menuItem.GetLabel(fileItem).empty() || !root.IsParentOf(menuItem))
    return false;

  if (menuItem.IsGroup())
  {
    CSingleLock lock(m_criticalSection);
    return std::any_of(m_addonItems.begin(), m_addonItems.end(),
        [&](const CContextMenuItem& other){ return menuItem.IsParentOf(other) && other.IsVisible(fileItem); });
  }

  return menuItem.IsVisible(fileItem);
}

ContextMenuView CContextMenuManager::GetItems(const CFileItem& fileItem, const CContextMenuItem& root /*= MAIN*/) const
{
  ContextMenuView result;
  //! @todo implement group support
  if (&root == &MAIN)
  {
    CSingleLock lock(m_criticalSection);
    std::copy_if(m_items.begin(), m_items.end(), std::back_inserter(result),
        [&](const std::shared_ptr<IContextMenuItem>& menu){ return menu->IsVisible(fileItem); });
  }
  return result;
}

ContextMenuView CContextMenuManager::GetAddonItems(const CFileItem& fileItem, const CContextMenuItem& root /*= MAIN*/) const
{
  ContextMenuView result;
  {
    CSingleLock lock(m_criticalSection);
    for (const auto& menu : m_addonItems)
      if (IsVisible(menu, root, fileItem))
        result.emplace_back(new CContextMenuItem(menu));
  }

  if (&root == &MAIN || &root == &MANAGE)
  {
    std::sort(result.begin(), result.end(),
        [&](const ContextMenuView::value_type& lhs, const ContextMenuView::value_type& rhs)
        {
          return lhs->GetLabel(fileItem) < rhs->GetLabel(fileItem);
        }
    );
  }
  return result;
}

bool CONTEXTMENU::ShowFor(const CFileItemPtr& fileItem, const CContextMenuItem& root)
{
  if (!fileItem)
    return false;

  auto menuItems = CContextMenuManager::GetInstance().GetItems(*fileItem, root);
  for (auto&& item : CContextMenuManager::GetInstance().GetAddonItems(*fileItem, root))
    menuItems.emplace_back(std::move(item));

  if (menuItems.empty())
    return true;

  CContextButtons buttons;
  for (size_t i = 0; i < menuItems.size(); ++i)
    buttons.Add(i, menuItems[i]->GetLabel(*fileItem));

  int selected = CGUIDialogContextMenu::Show(buttons);
  if (selected < 0 || selected >= static_cast<int>(menuItems.size()))
    return false;

  return menuItems[selected]->IsGroup() ?
         ShowFor(fileItem, static_cast<const CContextMenuItem&>(*menuItems[selected])) :
         menuItems[selected]->Execute(fileItem);
}

bool CONTEXTMENU::LoopFrom(const IContextMenuItem& menu, const CFileItemPtr& fileItem)
{
  if (!fileItem)
    return false;
  if (menu.IsGroup())
    return ShowFor(fileItem, static_cast<const CContextMenuItem&>(menu));
  return menu.Execute(fileItem);
}
