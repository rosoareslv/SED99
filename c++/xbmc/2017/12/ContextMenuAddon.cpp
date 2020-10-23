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

#include "ContextMenuAddon.h"

#include "AddonManager.h"
#include "ContextMenuManager.h"
#include "ContextMenuItem.h"
#include "ServiceBroker.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"

#include <sstream>

namespace ADDON
{

void CContextMenuAddon::ParseMenu(
    const CAddonInfo& addonInfo,
    cp_cfg_element_t* elem,
    const std::string& parent,
    int& anonGroupCount,
    std::vector<CContextMenuItem>& items)
{
  auto menuId = CServiceBroker::GetAddonMgr().GetExtValue(elem, "@id");
  auto menuLabel = CServiceBroker::GetAddonMgr().GetExtValue(elem, "label");
  if (StringUtils::IsNaturalNumber(menuLabel))
    menuLabel = g_localizeStrings.GetAddonString(addonInfo.ID(), atoi(menuLabel.c_str()));

  if (menuId.empty())
  {
    //anonymous group. create a new unique internal id.
    std::stringstream ss;
    ss << addonInfo.ID() << ++anonGroupCount;
    menuId = ss.str();
  }

  items.push_back(CContextMenuItem::CreateGroup(menuLabel, parent, menuId, addonInfo.ID()));

  ELEMENTS subMenus;
  if (CServiceBroker::GetAddonMgr().GetExtElements(elem, "menu", subMenus))
    for (const auto& subMenu : subMenus)
      ParseMenu(addonInfo, subMenu, menuId, anonGroupCount, items);

  ELEMENTS elems;
  if (CServiceBroker::GetAddonMgr().GetExtElements(elem, "item", elems))
  {
    for (const auto& elem : elems)
    {
      auto visCondition = CServiceBroker::GetAddonMgr().GetExtValue(elem, "visible");
      auto library = CServiceBroker::GetAddonMgr().GetExtValue(elem, "@library");
      auto label = CServiceBroker::GetAddonMgr().GetExtValue(elem, "label");
      if (StringUtils::IsNaturalNumber(label))
        label = g_localizeStrings.GetAddonString(addonInfo.ID(), atoi(label.c_str()));

      if (!label.empty() && !library.empty() && !visCondition.empty())
      {
        auto menu = CContextMenuItem::CreateItem(label, menuId,
            URIUtils::AddFileToFolder(addonInfo.Path(), library), visCondition, addonInfo.ID());
        items.push_back(menu);
      }
    }
  }
}

std::unique_ptr<CContextMenuAddon> CContextMenuAddon::FromExtension(CAddonInfo addonInfo, const cp_extension_t* ext)
{
  std::vector<CContextMenuItem> items;

  cp_cfg_element_t* menu = CServiceBroker::GetAddonMgr().GetExtElement(ext->configuration, "menu");
  if (menu)
  {
    int tmp = 0;
    ParseMenu(addonInfo, menu, "", tmp, items);
  }
  else
  {
    //backwards compatibility. add first item definition
    ELEMENTS elems;
    if (CServiceBroker::GetAddonMgr().GetExtElements(ext->configuration, "item", elems))
    {
      cp_cfg_element_t *elem = elems[0];

      std::string visCondition = CServiceBroker::GetAddonMgr().GetExtValue(elem, "visible");
      if (visCondition.empty())
        visCondition = "false";

      std::string parent = CServiceBroker::GetAddonMgr().GetExtValue(elem, "parent") == "kodi.core.manage"
          ? CContextMenuManager::MANAGE.m_groupId : CContextMenuManager::MAIN.m_groupId;

      auto label = CServiceBroker::GetAddonMgr().GetExtValue(elem, "label");
      if (StringUtils::IsNaturalNumber(label))
        label = g_localizeStrings.GetAddonString(addonInfo.ID(), atoi(label.c_str()));

      CContextMenuItem menuItem = CContextMenuItem::CreateItem(label, parent,
          URIUtils::AddFileToFolder(addonInfo.Path(), addonInfo.LibName()), visCondition, addonInfo.ID());

      items.push_back(menuItem);
    }
  }

  return std::unique_ptr<CContextMenuAddon>(new CContextMenuAddon(std::move(addonInfo), std::move(items)));
}

CContextMenuAddon::CContextMenuAddon(CAddonInfo addonInfo, std::vector<CContextMenuItem> items)
    : CAddon(std::move(addonInfo)), m_items(std::move(items))
{
}

}
