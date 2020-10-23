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

#include "RenderSettings.h"

using namespace KODI;
using namespace RETRO;

void CRenderSettings::Reset()
{
  m_geometry.Reset();
  m_videoSettings.Reset();
}

bool CRenderSettings::operator==(const CRenderSettings &rhs) const
{
  return m_geometry == rhs.m_geometry &&
         m_videoSettings == rhs.m_videoSettings;
}

bool CRenderSettings::operator<(const CRenderSettings &rhs) const
{
  if (m_geometry < rhs.m_geometry) return true;
  if (m_geometry > rhs.m_geometry) return false;

  if (m_videoSettings < rhs.m_videoSettings) return true;
  if (m_videoSettings > rhs.m_videoSettings) return false;

  return false;
}
