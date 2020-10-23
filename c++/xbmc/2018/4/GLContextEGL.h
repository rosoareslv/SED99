/*
 *      Copyright (C) 2005-2014 Team XBMC
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

#pragma once

#include "GLContext.h"
#include "EGL/egl.h"
#include <X11/Xutil.h>

class CGLContextEGL : public CGLContext
{
public:
  explicit CGLContextEGL(Display *dpy);
  ~CGLContextEGL() override;
  bool Refresh(bool force, int screen, Window glWindow, bool &newContext) override;
  void Destroy() override;
  void Detach() override;
  void SetVSync(bool enable) override;
  void SwapBuffers() override;
  void QueryExtensions() override;
  EGLDisplay m_eglDisplay;
  EGLSurface m_eglSurface;
  EGLContext m_eglContext;
  EGLConfig m_eglConfig;
protected:
  bool IsSuitableVisual(XVisualInfo *vInfo);
  EGLConfig GetEGLConfig(EGLDisplay eglDisplay, XVisualInfo *vInfo);
};
