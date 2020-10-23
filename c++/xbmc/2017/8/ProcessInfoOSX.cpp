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

#include "ProcessInfoOSX.h"
#include "cores/VideoPlayer/Process/ProcessInfo.h"
#include "threads/SingleLock.h"

using namespace VIDEOPLAYER;

CProcessInfo* CProcessInfoOSX::Create()
{
  return new CProcessInfoOSX();
}

void CProcessInfoOSX::Register()
{
  CProcessInfo::RegisterProcessControl("osx", CProcessInfoOSX::Create);
}

void CProcessInfoOSX::SetSwDeinterlacingMethods()
{
  // first populate with the defaults from base implementation
  CProcessInfo::SetSwDeinterlacingMethods();

  std::list<EINTERLACEMETHOD> methods;
  {
    // get the current methods
    CSingleLock lock(m_videoCodecSection);
    methods = m_deintMethods;
  }
  // add bob and blend deinterlacer for osx
  methods.push_back(EINTERLACEMETHOD::VS_INTERLACEMETHOD_RENDER_BOB);
  methods.push_back(EINTERLACEMETHOD::VS_INTERLACEMETHOD_RENDER_BLEND);

  // update with the new methods list
  UpdateDeinterlacingMethods(methods);
}

std::vector<AVPixelFormat> CProcessInfoOSX::GetRenderFormats()
{
  std::vector<AVPixelFormat> formats;
  formats.push_back(AV_PIX_FMT_YUV420P);
  formats.push_back(AV_PIX_FMT_YUV420P10);
  formats.push_back(AV_PIX_FMT_YUV420P16);
  formats.push_back(AV_PIX_FMT_NV12);
  formats.push_back(AV_PIX_FMT_YUYV422);
  formats.push_back(AV_PIX_FMT_UYVY422);

  return formats;
}

