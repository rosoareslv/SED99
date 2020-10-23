/*
 *  Copyright (C) 2007-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/VideoPlayer/Process/gbm/VideoBufferDRMPRIME.h"
#include "utils/EGLImage.h"
#include "utils/Geometry.h"

#include "system_gl.h"

class CDRMPRIMETexture
{
public:
  bool Map(IVideoBufferDRMPRIME* buffer);
  void Unmap();
  void Init(EGLDisplay eglDisplay);

  GLuint GetTexture() { return m_texture; }
  CSizeInt GetTextureSize() { return { m_texWidth, m_texHeight }; }

protected:
  IVideoBufferDRMPRIME* m_primebuffer{nullptr};
  std::unique_ptr<CEGLImage> m_eglImage;

  const GLenum m_textureTarget{GL_TEXTURE_EXTERNAL_OES};
  GLuint m_texture{0};
  int m_texWidth{0};
  int m_texHeight{0};

private:
  static int GetColorSpace(int colorSpace);
  static int GetColorRange(int colorRange);

};
