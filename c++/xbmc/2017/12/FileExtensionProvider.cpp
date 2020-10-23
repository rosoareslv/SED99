/*
 *      Copyright (C) 2012-2017 Team Kodi
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "FileExtensionProvider.h"

#include <string>
#include <vector>

#include "addons/AddonManager.h"
#include "addons/binary-addons/BinaryAddonBase.h"
#include "ServiceBroker.h"

using namespace ADDON;

const std::vector<TYPE> ADDON_TYPES = {
  ADDON_VFS,
  ADDON_IMAGEDECODER,
  ADDON_AUDIODECODER
};

CFileExtensionProvider::CFileExtensionProvider()
{
  m_advancedSettings = g_advancedSettingsRef;

  SetAddonExtensions();

  if (CServiceBroker::IsBinaryAddonCacheUp())
    CServiceBroker::GetAddonMgr().Events().Subscribe(this, &CFileExtensionProvider::OnAddonEvent);
}

CFileExtensionProvider::~CFileExtensionProvider()
{
  if (CServiceBroker::IsBinaryAddonCacheUp())
    CServiceBroker::GetAddonMgr().Events().Unsubscribe(this);

  m_advancedSettings.reset();
  m_addonExtensions.clear();
}

std::string CFileExtensionProvider::GetDiscStubExtensions() const
{
  return m_advancedSettings->m_discStubExtensions;
}

std::string CFileExtensionProvider::GetMusicExtensions() const
{
  std::string extensions(m_advancedSettings->m_musicExtensions);
  extensions += '|' + GetAddonExtensions(ADDON_VFS);
  extensions += '|' + GetAddonExtensions(ADDON_AUDIODECODER);

  return extensions;
}

std::string CFileExtensionProvider::GetPictureExtensions() const
{
  std::string extensions(m_advancedSettings->m_pictureExtensions);
  extensions += '|' + GetAddonExtensions(ADDON_VFS);
  extensions += '|' + GetAddonExtensions(ADDON_IMAGEDECODER);

  return extensions;
}

std::string CFileExtensionProvider::GetSubtitleExtensions() const
{
  std::string extensions(m_advancedSettings->m_subtitlesExtensions);
  extensions += '|' + GetAddonExtensions(ADDON_VFS);

  return extensions;
}

std::string CFileExtensionProvider::GetVideoExtensions() const
{
  std::string extensions(m_advancedSettings->m_videoExtensions);
  if (!extensions.empty())
    extensions += '|';
  extensions += GetAddonExtensions(ADDON_VFS);

  return extensions;
}

std::string CFileExtensionProvider::GetFileFolderExtensions() const
{
  std::string extensions(GetAddonFileFolderExtensions(ADDON_VFS));
  if (!extensions.empty())
    extensions += '|';
  extensions += GetAddonFileFolderExtensions(ADDON_AUDIODECODER);

  return extensions;
}

std::string CFileExtensionProvider::GetAddonExtensions(const TYPE &type) const
{
  auto it = m_addonExtensions.find(type);
  if (it != m_addonExtensions.end())
    return it->second;

  return "";
}

std::string CFileExtensionProvider::GetAddonFileFolderExtensions(const TYPE &type) const
{
  auto it = m_addonExtensions.find(type);
  if (it != m_addonExtensions.end())
    return it->second;

  return "";
}

void CFileExtensionProvider::SetAddonExtensions()
{
  for (auto const type : ADDON_TYPES)
  {
    SetAddonExtensions(type);
  }
}

void CFileExtensionProvider::SetAddonExtensions(const TYPE& type)
{
  if (!CServiceBroker::IsBinaryAddonCacheUp())
    return;

  std::vector<std::string> extensions;
  std::vector<std::string> fileFolderExtensions;
  BinaryAddonBaseList addonInfos;
  CServiceBroker::GetBinaryAddonManager().GetAddonInfos(addonInfos, true, type);
  for (const auto& addonInfo : addonInfos)
  {
    std::string info = ADDON_VFS == type ? "@extensions" : "@extension";
    std::string ext = addonInfo->Type(type)->GetValue(info).asString();
    if (!ext.empty())
    {
      extensions.push_back(ext);
      if (type == ADDON_VFS || type == ADDON_AUDIODECODER)
      {
        std::string info2 = ADDON_VFS == type ? "@filedirectories" : "@tracks";
        if (addonInfo->Type(type)->GetValue(info2).asBoolean())
          fileFolderExtensions.push_back(ext);
      }
    }
  }

  m_addonExtensions.insert(make_pair(type, StringUtils::Join(extensions, "|")));
  if (!fileFolderExtensions.empty())
    m_addonFileFolderExtensions.insert(make_pair(type, StringUtils::Join(fileFolderExtensions, "|")));
}

void CFileExtensionProvider::OnAddonEvent(const AddonEvent& event)
{
  if (typeid(event) == typeid(AddonEvents::Enabled) ||
      typeid(event) == typeid(AddonEvents::Disabled) ||
      typeid(event) == typeid(AddonEvents::ReInstalled))
  {
    for (auto &type : ADDON_TYPES)
    {
      if (CServiceBroker::GetAddonMgr().HasType(event.id, type))
      {
        SetAddonExtensions(type);
        break;
      }
    }
  }
  else if (typeid(event) == typeid(AddonEvents::UnInstalled))
  {
    SetAddonExtensions();
  }
}
