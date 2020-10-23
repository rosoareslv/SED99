/*
 *      Copyright (C) 2017 Team Kodi
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

#include "RPRenderManager.h"
#include "settings/MediaSettings.h"
#include "settings/VideoSettings.h"

using namespace KODI;
using namespace RETRO;

CRPRenderManager::CRPRenderManager(CDVDClock &clock, IRenderMsg *player) :
  CRenderManager(clock, player)
{
}

bool CRPRenderManager::SupportsRenderFeature(ERENDERFEATURE feature)
{
  return Supports(feature);
}

bool CRPRenderManager::SupportsScalingMethod(ESCALINGMETHOD method)
{
  return Supports(method);
}

ESCALINGMETHOD CRPRenderManager::GetScalingMethod()
{
  //! @todo
  CVideoSettings &videoSettings = CMediaSettings::GetInstance().GetCurrentVideoSettings();
  return videoSettings.m_ScalingMethod;
}

void CRPRenderManager::SetScalingMethod(ESCALINGMETHOD method)
{
  //! @todo
  CVideoSettings &videoSettings = CMediaSettings::GetInstance().GetCurrentVideoSettings();
  videoSettings.m_ScalingMethod = method;
}

ViewMode CRPRenderManager::GetRenderViewMode()
{
  //! @todo
  CVideoSettings &videoSettings = CMediaSettings::GetInstance().GetCurrentVideoSettings();
  return static_cast<ViewMode>(videoSettings.m_ViewMode);
}

void CRPRenderManager::SetRenderViewMode(ViewMode mode)
{
  SetViewMode(mode);
}
