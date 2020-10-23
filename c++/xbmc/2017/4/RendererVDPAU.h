/*
 *      Copyright (C) 2007-2015 Team XBMC
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

#include "system.h"

#ifdef HAVE_LIBVDPAU

#include "cores/VideoPlayer/VideoRenderers/LinuxRendererGL.h"

class CRendererVDPAU : public CLinuxRendererGL
{
public:
  CRendererVDPAU();
  virtual ~CRendererVDPAU();

  // Player functions
  virtual void AddVideoPictureHW(DVDVideoPicture &picture, int index);
  virtual void ReleaseBuffer(int idx);
  virtual CRenderInfo GetRenderInfo();

  // Feature support
  virtual bool Supports(ERENDERFEATURE feature);
  virtual bool Supports(ESCALINGMETHOD method);

protected:
  virtual bool LoadShadersHook();
  virtual bool RenderHook(int idx);

  // textures
  virtual bool UploadTexture(int index);
  virtual void DeleteTexture(int index);
  virtual bool CreateTexture(int index);

  bool CreateVDPAUTexture(int index);
  void DeleteVDPAUTexture(int index);
  bool UploadVDPAUTexture(int index);

  bool CreateVDPAUTexture420(int index);
  void DeleteVDPAUTexture420(int index);
  bool UploadVDPAUTexture420(int index);
};

#endif

