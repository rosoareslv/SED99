/*
 *      Copyright (C) 2005-2013 Team XBMC
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

#include "GLUtils.h"
#include "log.h"
#include "ServiceBroker.h"
#include "settings/AdvancedSettings.h"
#include "rendering/RenderSystem.h"

void _VerifyGLState(const char* szfile, const char* szfunction, int lineno){
#if defined(HAS_GL) && defined(_DEBUG)
#define printMatrix(matrix)                                             \
  {                                                                     \
    for (int ixx = 0 ; ixx<4 ; ixx++)                                   \
      {                                                                 \
        CLog::Log(LOGDEBUG, "% 3.3f % 3.3f % 3.3f % 3.3f ",             \
                  matrix[ixx*4], matrix[ixx*4+1], matrix[ixx*4+2],      \
                  matrix[ixx*4+3]);                                     \
      }                                                                 \
  }
  if (g_advancedSettings.m_logLevel < LOG_LEVEL_DEBUG_FREEMEM)
    return;
  GLenum err = glGetError();
  if (err==GL_NO_ERROR)
    return;
  CLog::Log(LOGERROR, "GL ERROR: %s\n", gluErrorString(err));
  if (szfile && szfunction)
      CLog::Log(LOGERROR, "In file:%s function:%s line:%d", szfile, szfunction, lineno);
  GLboolean bools[16];
  GLfloat matrix[16];
  glGetFloatv(GL_SCISSOR_BOX, matrix);
  CLog::Log(LOGDEBUG, "Scissor box: %f, %f, %f, %f", matrix[0], matrix[1], matrix[2], matrix[3]);
  glGetBooleanv(GL_SCISSOR_TEST, bools);
  CLog::Log(LOGDEBUG, "Scissor test enabled: %d", (int)bools[0]);
  glGetFloatv(GL_VIEWPORT, matrix);
  CLog::Log(LOGDEBUG, "Viewport: %f, %f, %f, %f", matrix[0], matrix[1], matrix[2], matrix[3]);
  glGetFloatv(GL_PROJECTION_MATRIX, matrix);
  CLog::Log(LOGDEBUG, "Projection Matrix:");
  printMatrix(matrix);
  glGetFloatv(GL_MODELVIEW_MATRIX, matrix);
  CLog::Log(LOGDEBUG, "Modelview Matrix:");
  printMatrix(matrix);
//  abort();
#endif
}

void LogGraphicsInfo()
{
#if defined(HAS_GL) || defined(HAS_GLES)
  const GLubyte *s;

  s = glGetString(GL_VENDOR);
  if (s)
    CLog::Log(LOGNOTICE, "GL_VENDOR = %s", s);
  else
    CLog::Log(LOGNOTICE, "GL_VENDOR = NULL");

  s = glGetString(GL_RENDERER);
  if (s)
    CLog::Log(LOGNOTICE, "GL_RENDERER = %s", s);
  else
    CLog::Log(LOGNOTICE, "GL_RENDERER = NULL");

  s = glGetString(GL_VERSION);
  if (s)
    CLog::Log(LOGNOTICE, "GL_VERSION = %s", s);
  else
    CLog::Log(LOGNOTICE, "GL_VERSION = NULL");

  s = glGetString(GL_SHADING_LANGUAGE_VERSION);
  if (s)
    CLog::Log(LOGNOTICE, "GL_SHADING_LANGUAGE_VERSION = %s", s);
  else
    CLog::Log(LOGNOTICE, "GL_SHADING_LANGUAGE_VERSION = NULL");

  //GL_NVX_gpu_memory_info extension
#define GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX          0x9047
#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX    0x9048
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX  0x9049
#define GL_GPU_MEMORY_INFO_EVICTION_COUNT_NVX            0x904A
#define GL_GPU_MEMORY_INFO_EVICTED_MEMORY_NVX            0x904B

  if (CServiceBroker::GetRenderSystem()->IsExtSupported("GL_NVX_gpu_memory_info"))
  {
    GLint mem = 0;

    glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &mem);
    CLog::Log(LOGNOTICE, "GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX = %i", mem);

    //this seems to be the amount of ram on the videocard
    glGetIntegerv(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &mem);
    CLog::Log(LOGNOTICE, "GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX = %i", mem);
  }

  std::string extensions;
#if defined(HAS_GL)
  unsigned int renderVersionMajor, renderVersionMinor;
  CServiceBroker::GetRenderSystem()->GetRenderVersion(renderVersionMajor, renderVersionMinor);
  if (renderVersionMajor > 3 ||
      (renderVersionMajor == 3 && renderVersionMinor >= 2))
  {
    GLint n;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    if (n > 0)
    {
      GLint i;
      for (i = 0; i < n; i++)
      {
        extensions += (const char*)glGetStringi(GL_EXTENSIONS, i);
        extensions += " ";
      }
    }
  }
  else
#endif
  {
    extensions += (const char*) glGetString(GL_EXTENSIONS);
  }

  if (!extensions.empty())
    CLog::Log(LOGNOTICE, "GL_EXTENSIONS = %s", extensions.c_str());
  else
    CLog::Log(LOGNOTICE, "GL_EXTENSIONS = NULL");


#else /* !HAS_GL */
  CLog::Log(LOGNOTICE,
            "Please define LogGraphicsInfo for your chosen graphics library");
#endif /* !HAS_GL */
}

int glFormatElementByteCount(GLenum format)
{
  switch (format)
  {
#ifdef HAS_GL
  case GL_BGRA:
    return 4;
  case GL_RED:
    return 1;
  case GL_GREEN:
    return 1;
  case GL_RG:
    return 2;
  case GL_BGR:
    return 3;
#endif
  case GL_RGBA:
    return 4;
  case GL_RGB:
    return 3;
  case GL_LUMINANCE_ALPHA:
    return 2;
  case GL_LUMINANCE:
  case GL_ALPHA:
    return 1;
  default:
    CLog::Log(LOGERROR, "glFormatElementByteCount - Unknown format %u", format);
    return 1;
  }
}
