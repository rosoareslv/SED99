/*
 *      Copyright (C) 2005-2013 Team XBMC
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


#include "system.h"

#include "RenderSystemGL.h"
#include "filesystem/File.h"
#include "guilib/GraphicContext.h"
#include "settings/AdvancedSettings.h"
#include "guilib/MatrixGLES.h"
#include "settings/DisplaySettings.h"
#include "utils/log.h"
#include "utils/GLUtils.h"
#include "utils/TimeUtils.h"
#include "utils/SystemInfo.h"
#include "utils/MathUtils.h"
#include "utils/StringUtils.h"
#ifdef TARGET_POSIX
#include "platform/linux/XTimeUtils.h"
#endif

CRenderSystemGL::CRenderSystemGL() : CRenderSystemBase()
{
  m_enumRenderingSystem = RENDERING_SYSTEM_OPENGL;
  m_pShader.reset(new CGLShader*[SM_MAX]);
}

CRenderSystemGL::~CRenderSystemGL() = default;

void CRenderSystemGL::CheckOpenGLQuirks()

{
#ifdef TARGET_DARWIN_OSX
  if (m_RenderVendor.find("NVIDIA") != std::string::npos)
  {
    // Nvidia 7300 (AppleTV) and 7600 cannot do DXT with NPOT under OSX
    // Nvidia 9400M is slow as a dog
    if (m_renderCaps & RENDER_CAPS_DXT_NPOT)
    {
      const char *arr[3]= { "7300","7600","9400M" };
      for(int j = 0; j < 3; j++)
      {
        if((int(m_RenderRenderer.find(arr[j])) > -1))
        {
          m_renderCaps &= ~ RENDER_CAPS_DXT_NPOT;
          break;
        }
      }
    }
  }
#ifdef __ppc__
  // ATI Radeon 9600 on osx PPC cannot do NPOT
  if (m_RenderRenderer.find("ATI Radeon 9600") != std::string::npos)
  {
    m_renderCaps &= ~ RENDER_CAPS_NPOT;
    m_renderCaps &= ~ RENDER_CAPS_DXT_NPOT;
  }
#endif
#endif
  if (StringUtils::EqualsNoCase(m_RenderVendor, "nouveau"))
    m_renderQuirks |= RENDER_QUIRKS_YV12_PREFERED;

  if (StringUtils::EqualsNoCase(m_RenderVendor, "Tungsten Graphics, Inc.")
  ||  StringUtils::EqualsNoCase(m_RenderVendor, "Tungsten Graphics, Inc"))
  {
    unsigned major, minor, micro;
    if (sscanf(m_RenderVersion.c_str(), "%*s Mesa %u.%u.%u", &major, &minor, &micro) == 3)
    {

      if((major  < 7)
      || (major == 7 && minor  < 7)
      || (major == 7 && minor == 7 && micro < 1))
        m_renderQuirks |= RENDER_QUIRKS_MAJORMEMLEAK_OVERLAYRENDERER;
    }
    else
      CLog::Log(LOGNOTICE, "CRenderSystemGL::CheckOpenGLQuirks - unable to parse mesa version string");

    if(m_RenderRenderer.find("Poulsbo") != std::string::npos)
      m_renderCaps &= ~RENDER_CAPS_DXT_NPOT;

    m_renderQuirks |= RENDER_QUIRKS_BROKEN_OCCLUSION_QUERY;
  }
}

bool CRenderSystemGL::InitRenderSystem()
{
  m_bVSync = false;
  m_bVsyncInit = false;
  m_maxTextureSize = 2048;
  m_renderCaps = 0;

  // Get the GL version number
  m_RenderVersionMajor = 0;
  m_RenderVersionMinor = 0;
  const char* ver = (const char*)glGetString(GL_VERSION);
  if (ver != 0)
  {
    sscanf(ver, "%d.%d", &m_RenderVersionMajor, &m_RenderVersionMinor);
    m_RenderVersion = ver;
  }

  m_RenderExtensions  = " ";
  if (m_RenderVersionMajor > 3 ||
      (m_RenderVersionMajor == 3 && m_RenderVersionMinor >= 2))
  {
    GLint n;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    if (n > 0)
    {
      GLint i;
      for (i = 0; i < n; i++)
      {
        m_RenderExtensions += (const char*)glGetStringi(GL_EXTENSIONS, i);
        m_RenderExtensions += " ";
      }
    }
  }
  else
  {
    m_RenderExtensions += (const char*) glGetString(GL_EXTENSIONS);
  }
  m_RenderExtensions += " ";

  ver = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
  if (ver)
  {
    sscanf(ver, "%d.%d", &m_glslMajor, &m_glslMinor);
  }
  else
  {
    m_glslMajor = 1;
    m_glslMinor = 0;
  }

  LogGraphicsInfo();

  // Get our driver vendor and renderer
  const char* tmpVendor = (const char*) glGetString(GL_VENDOR);
  m_RenderVendor.clear();
  if (tmpVendor != NULL)
    m_RenderVendor = tmpVendor;

  const char* tmpRenderer = (const char*) glGetString(GL_RENDERER);
  m_RenderRenderer.clear();
  if (tmpRenderer != NULL)
    m_RenderRenderer = tmpRenderer;

  // grab our capabilities
  if (IsExtSupported("GL_EXT_texture_compression_s3tc"))
    m_renderCaps |= RENDER_CAPS_DXT;

  if (IsExtSupported("GL_ARB_texture_non_power_of_two"))
  {
    m_renderCaps |= RENDER_CAPS_NPOT;
    if (m_renderCaps & RENDER_CAPS_DXT)
      m_renderCaps |= RENDER_CAPS_DXT_NPOT;
  }
  //Check OpenGL quirks and revert m_renderCaps as needed
  CheckOpenGLQuirks();

  m_bRenderCreated = true;

  if (m_RenderVersionMajor > 3 ||
      (m_RenderVersionMajor == 3 && m_RenderVersionMinor >= 2))
  {
    glGenVertexArrays(1, &m_vertexArray);
    glBindVertexArray(m_vertexArray);
  }

  InitialiseShader();

  return true;
}

bool CRenderSystemGL::ResetRenderSystem(int width, int height)
{
  m_width = width;
  m_height = height;

  glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );

  CalculateMaxTexturesize();

  CRect rect( 0, 0, width, height );
  SetViewPort( rect );

  glEnable(GL_SCISSOR_TEST);

  glMatrixProject.Clear();
  glMatrixProject->LoadIdentity();
  glMatrixProject->Ortho(0.0f, width-1, height-1, 0.0f, -1.0f, 1.0f);
  glMatrixProject.Load();

  glMatrixModview.Clear();
  glMatrixModview->LoadIdentity();
  glMatrixModview.Load();

  glMatrixTexture.Clear();
  glMatrixTexture->LoadIdentity();
  glMatrixTexture.Load();

  if (IsExtSupported("GL_ARB_multitexture"))
  {
    //clear error flags
    ResetGLErrors();

    GLint maxtex;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS_ARB, &maxtex);

    //some sanity checks
    GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
      CLog::Log(LOGERROR, "ResetRenderSystem() GL_MAX_TEXTURE_IMAGE_UNITS_ARB returned error %i", (int)error);
      maxtex = 3;
    }
    else if (maxtex < 1 || maxtex > 32)
    {
      CLog::Log(LOGERROR, "ResetRenderSystem() GL_MAX_TEXTURE_IMAGE_UNITS_ARB returned invalid value %i", (int)maxtex);
      maxtex = 3;
    }

    //reset texture matrix for all textures
    for (GLint i = 0; i < maxtex; i++)
    {
      glActiveTextureARB(GL_TEXTURE0 + i);
      glMatrixTexture.Load();
    }
    glActiveTextureARB(GL_TEXTURE0);
  }

  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glEnable(GL_BLEND);          // Turn Blending On
  glDisable(GL_DEPTH_TEST);

  return true;
}

bool CRenderSystemGL::DestroyRenderSystem()
{
  if (m_vertexArray != GL_NONE)
  {
    glDeleteVertexArrays(1, &m_vertexArray);
  }

  m_bRenderCreated = false;

  return true;
}

bool CRenderSystemGL::BeginRender()
{
  if (!m_bRenderCreated)
    return false;

  return true;
}

bool CRenderSystemGL::EndRender()
{
  if (!m_bRenderCreated)
    return false;

  return true;
}

bool CRenderSystemGL::ClearBuffers(color_t color)
{
  if (!m_bRenderCreated)
    return false;

  /* clear is not affected by stipple pattern, so we can only clear on first frame */
  if(m_stereoMode == RENDER_STEREO_MODE_INTERLACED && m_stereoView == RENDER_STEREO_VIEW_RIGHT)
    return true;

  float r = GET_R(color) / 255.0f;
  float g = GET_G(color) / 255.0f;
  float b = GET_B(color) / 255.0f;
  float a = GET_A(color) / 255.0f;

  glClearColor(r, g, b, a);

  GLbitfield flags = GL_COLOR_BUFFER_BIT;
  glClear(flags);

  return true;
}

bool CRenderSystemGL::IsExtSupported(const char* extension)
{
  if (m_RenderVersionMajor > 3 ||
      (m_RenderVersionMajor == 3 && m_RenderVersionMinor >= 2))
  {
    if (strcmp( extension, "GL_EXT_framebuffer_object") == 0)
    {
      return true;
    }
    if (strcmp( extension, "GL_ARB_texture_non_power_of_two") == 0)
    {
      return true;
    }
  }

  std::string name;
  name  = " ";
  name += extension;
  name += " ";

  return m_RenderExtensions.find(name) != std::string::npos;
}

void CRenderSystemGL::PresentRender(bool rendered, bool videoLayer)
{
  SetVSync(true);

  if (!m_bRenderCreated)
    return;

  PresentRenderImpl(rendered);

  if (!rendered)
    Sleep(40);
}

void CRenderSystemGL::SetVSync(bool enable)
{
  if (m_bVSync == enable && m_bVsyncInit == true)
    return;

  if (!m_bRenderCreated)
    return;

  if (enable)
    CLog::Log(LOGINFO, "GL: Enabling VSYNC");
  else
    CLog::Log(LOGINFO, "GL: Disabling VSYNC");

  m_bVSync = enable;
  m_bVsyncInit = true;

  SetVSyncImpl(enable);
}

void CRenderSystemGL::CaptureStateBlock()
{
  if (!m_bRenderCreated)
    return;

  glMatrixProject.Push();
  glMatrixModview.Push();
  glMatrixTexture.Push();

  glDisable(GL_SCISSOR_TEST); // fixes FBO corruption on Macs
  glActiveTexture(GL_TEXTURE0);
}

void CRenderSystemGL::ApplyStateBlock()
{
  if (!m_bRenderCreated)
    return;

  glBindVertexArray(m_vertexArray);

  glViewport(m_viewPort[0], m_viewPort[1], m_viewPort[2], m_viewPort[3]);

  glMatrixProject.PopLoad();
  glMatrixModview.PopLoad();
  glMatrixTexture.PopLoad();

  glActiveTexture(GL_TEXTURE0);
  glEnable(GL_BLEND);
  glEnable(GL_SCISSOR_TEST);
}

void CRenderSystemGL::SetCameraPosition(const CPoint &camera, int screenWidth, int screenHeight, float stereoFactor)
{
  if (!m_bRenderCreated)
    return;

  CPoint offset = camera - CPoint(screenWidth*0.5f, screenHeight*0.5f);


  float w = (float)m_viewPort[2]*0.5f;
  float h = (float)m_viewPort[3]*0.5f;

  glMatrixModview->LoadIdentity();
  glMatrixModview->Translatef(-(w + offset.x - stereoFactor), +(h + offset.y), 0);
  glMatrixModview->LookAt(0.0, 0.0, -2.0*h, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0);
  glMatrixModview.Load();

  glMatrixProject->LoadIdentity();
  glMatrixProject->Frustum( (-w - offset.x)*0.5f, (w - offset.x)*0.5f, (-h + offset.y)*0.5f, (h + offset.y)*0.5f, h, 100*h);
  glMatrixProject.Load();
}

void CRenderSystemGL::Project(float &x, float &y, float &z)
{
  GLfloat coordX, coordY, coordZ;
  if (CMatrixGL::Project(x, y, z, glMatrixModview.Get(), glMatrixProject.Get(), m_viewPort, &coordX, &coordY, &coordZ))
  {
    x = coordX;
    y = (float)(m_viewPort[1] + m_viewPort[3] - coordY);
    z = 0;
  }
}

bool CRenderSystemGL::TestRender()
{
  static float theta = 0.0;

  glPushMatrix();
  glRotatef( theta, 0.0f, 0.0f, 1.0f );
  glBegin( GL_TRIANGLES );
  glColor3f( 1.0f, 0.0f, 0.0f ); glVertex2f( 0.0f, 1.0f );
  glColor3f( 0.0f, 1.0f, 0.0f ); glVertex2f( 0.87f, -0.5f );
  glColor3f( 0.0f, 0.0f, 1.0f ); glVertex2f( -0.87f, -0.5f );
  glEnd();
  glPopMatrix();

  theta += 1.0f;

  return true;
}

void CRenderSystemGL::ApplyHardwareTransform(const TransformMatrix &finalMatrix)
{
  if (!m_bRenderCreated)
    return;

  glMatrixModview.Push();
  GLfloat matrix[4][4];

  for(int i = 0; i < 3; i++)
    for(int j = 0; j < 4; j++)
      matrix[j][i] = finalMatrix.m[i][j];

  matrix[0][3] = 0.0f;
  matrix[1][3] = 0.0f;
  matrix[2][3] = 0.0f;
  matrix[3][3] = 1.0f;

  glMatrixModview->MultMatrixf(&matrix[0][0]);
  glMatrixModview.Load();
}

void CRenderSystemGL::RestoreHardwareTransform()
{
  if (!m_bRenderCreated)
    return;

  glMatrixModview.PopLoad();
}

void CRenderSystemGL::CalculateMaxTexturesize()
{
  GLint width = 256;

  // reset any previous GL errors
  ResetGLErrors();

  // max out at 2^(8+8)
  for (int i = 0 ; i<8 ; i++)
  {
    glTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_RGBA, width, width, 0, GL_BGRA,
                 GL_UNSIGNED_BYTE, NULL);
    glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,
                             &width);

    // GMA950 on OS X sets error instead
    if (width == 0 || (glGetError() != GL_NO_ERROR) )
      break;

    m_maxTextureSize = width;
    width *= 2;
    if (width > 65536) // have an upper limit in case driver acts stupid
    {
      CLog::Log(LOGERROR, "GL: Could not determine maximum texture width, falling back to 2048");
      m_maxTextureSize = 2048;
      break;
    }
  }

#ifdef TARGET_DARWIN_OSX
  // Max Texture size reported on some apple machines seems incorrect
  // Displaying a picture with that resolution results in a corrupted output
  // So force it to a lower value
  // Problem noticed on:
  // iMac with ATI Radeon X1600, both on 10.5.8 (GL_VERSION: 2.0 ATI-1.5.48)
  // and 10.6.2 (GL_VERSION: 2.0 ATI-1.6.6)
  if (m_RenderRenderer == "ATI Radeon X1600 OpenGL Engine")
    m_maxTextureSize = 2048;
  // Mac mini G4 with ATI Radeon 9200 (GL_VERSION: 1.3 ATI-1.5.48)
  else if (m_RenderRenderer == "ATI Radeon 9200 OpenGL Engine")
    m_maxTextureSize = 1024;
#endif

  CLog::Log(LOGINFO, "GL: Maximum texture width: %u", m_maxTextureSize);
}

void CRenderSystemGL::GetViewPort(CRect& viewPort)
{
  if (!m_bRenderCreated)
    return;

  viewPort.x1 = m_viewPort[0];
  viewPort.y1 = m_height - m_viewPort[1] - m_viewPort[3];
  viewPort.x2 = m_viewPort[0] + m_viewPort[2];
  viewPort.y2 = viewPort.y1 + m_viewPort[3];
}

void CRenderSystemGL::SetViewPort(const CRect& viewPort)
{
  if (!m_bRenderCreated)
    return;

  glScissor((GLint) viewPort.x1, (GLint) (m_height - viewPort.y1 - viewPort.Height()), (GLsizei) viewPort.Width(), (GLsizei) viewPort.Height());
  glViewport((GLint) viewPort.x1, (GLint) (m_height - viewPort.y1 - viewPort.Height()), (GLsizei) viewPort.Width(), (GLsizei) viewPort.Height());
  m_viewPort[0] = viewPort.x1;
  m_viewPort[1] = m_height - viewPort.y1 - viewPort.Height();
  m_viewPort[2] = viewPort.Width();
  m_viewPort[3] = viewPort.Height();
}

bool CRenderSystemGL::ScissorsCanEffectClipping()
{
  if (m_pShader[m_method])
    return m_pShader[m_method]->HardwareClipIsPossible();

  return false;
}

CRect CRenderSystemGL::ClipRectToScissorRect(const CRect &rect)
{
  if (!m_pShader[m_method])
    return CRect();
  float xFactor = m_pShader[m_method]->GetClipXFactor();
  float xOffset = m_pShader[m_method]->GetClipXOffset();
  float yFactor = m_pShader[m_method]->GetClipYFactor();
  float yOffset = m_pShader[m_method]->GetClipYOffset();
  return CRect(rect.x1 * xFactor + xOffset,
               rect.y1 * yFactor + yOffset,
               rect.x2 * xFactor + xOffset,
               rect.y2 * yFactor + yOffset);
}

void CRenderSystemGL::SetScissors(const CRect &rect)
{
  if (!m_bRenderCreated)
    return;
  GLint x1 = MathUtils::round_int(rect.x1);
  GLint y1 = MathUtils::round_int(rect.y1);
  GLint x2 = MathUtils::round_int(rect.x2);
  GLint y2 = MathUtils::round_int(rect.y2);
  glScissor(x1, m_height - y2, x2-x1, y2-y1);
}

void CRenderSystemGL::ResetScissors()
{
  SetScissors(CRect(0, 0, (float)m_width, (float)m_height));
}

void CRenderSystemGL::GetGLSLVersion(int& major, int& minor)
{
  major = m_glslMajor;
  minor = m_glslMinor;
}

void CRenderSystemGL::ResetGLErrors()
{
  int count = 0;
  while (glGetError() != GL_NO_ERROR)
  {
    count++;
    if (count >= 100)
    {
      CLog::Log(LOGWARNING, "CRenderSystemGL::ResetGLErrors glGetError didn't return GL_NO_ERROR after %i iterations", count);
      break;
    }
  }
}
static const GLubyte stipple_3d[] = {
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00,
};

void CRenderSystemGL::SetStereoMode(RENDER_STEREO_MODE mode, RENDER_STEREO_VIEW view)
{
  CRenderSystemBase::SetStereoMode(mode, view);

  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDrawBuffer(GL_BACK);

  if(m_stereoMode == RENDER_STEREO_MODE_ANAGLYPH_RED_CYAN)
  {
    if(m_stereoView == RENDER_STEREO_VIEW_LEFT)
      glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
    else if(m_stereoView == RENDER_STEREO_VIEW_RIGHT)
      glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_TRUE);
  }
  if(m_stereoMode == RENDER_STEREO_MODE_ANAGLYPH_GREEN_MAGENTA)
  {
    if(m_stereoView == RENDER_STEREO_VIEW_LEFT)
      glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    else if(m_stereoView == RENDER_STEREO_VIEW_RIGHT)
      glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_TRUE);
  }
  if(m_stereoMode == RENDER_STEREO_MODE_ANAGLYPH_YELLOW_BLUE)
  {
    if(m_stereoView == RENDER_STEREO_VIEW_LEFT)
      glColorMask(GL_TRUE, GL_TRUE, GL_FALSE, GL_TRUE);
    else if(m_stereoView == RENDER_STEREO_VIEW_RIGHT)
      glColorMask(GL_FALSE, GL_FALSE, GL_TRUE, GL_TRUE);
  }

  if(m_stereoMode == RENDER_STEREO_MODE_INTERLACED)
  {
    glEnable(GL_POLYGON_STIPPLE);
    if(m_stereoView == RENDER_STEREO_VIEW_LEFT)
      glPolygonStipple(stipple_3d);
    else if(m_stereoView == RENDER_STEREO_VIEW_RIGHT)
      glPolygonStipple(stipple_3d+4);
  }

  if(m_stereoMode == RENDER_STEREO_MODE_HARDWAREBASED)
  {
    if(m_stereoView == RENDER_STEREO_VIEW_LEFT)
      glDrawBuffer(GL_BACK_LEFT);
    else if(m_stereoView == RENDER_STEREO_VIEW_RIGHT)
      glDrawBuffer(GL_BACK_RIGHT);
  }

}

bool CRenderSystemGL::SupportsStereo(RENDER_STEREO_MODE mode) const
{
  switch(mode)
  {
    case RENDER_STEREO_MODE_ANAGLYPH_RED_CYAN:
    case RENDER_STEREO_MODE_ANAGLYPH_GREEN_MAGENTA:
    case RENDER_STEREO_MODE_ANAGLYPH_YELLOW_BLUE:
    case RENDER_STEREO_MODE_INTERLACED:
      return true;
    case RENDER_STEREO_MODE_HARDWAREBASED: {
      //This is called by setting init, at which point GL is not inited
      //luckily if GL doesn't support this, it will just behave as if
      //it was not in effect.
      //GLboolean stereo = GL_FALSE;
      //glGetBooleanv(GL_STEREO, &stereo);
      //return stereo == GL_TRUE ? true : false;
      return true;
    }
    default:
      return CRenderSystemBase::SupportsStereo(mode);
  }
}

// -----------------------------------------------------------------------------
// shaders
// -----------------------------------------------------------------------------
void CRenderSystemGL::InitialiseShader()
{
  m_pShader[SM_DEFAULT] = new CGLShader("gl_shader_vert_default.glsl", "gl_shader_frag_default.glsl");
  if (!m_pShader[SM_DEFAULT]->CompileAndLink())
  {
    m_pShader[SM_DEFAULT]->Free();
    delete m_pShader[SM_DEFAULT];
    m_pShader[SM_DEFAULT] = nullptr;
    CLog::Log(LOGERROR, "GUI Shader gl_shader_frag_default.glsl - compile and link failed");
  }

  m_pShader[SM_TEXTURE] = new CGLShader("gl_shader_frag_texture.glsl");
  if (!m_pShader[SM_TEXTURE]->CompileAndLink())
  {
    m_pShader[SM_TEXTURE]->Free();
    delete m_pShader[SM_TEXTURE];
    m_pShader[SM_TEXTURE] = nullptr;
    CLog::Log(LOGERROR, "GUI Shader gl_shader_frag_texture.glsl - compile and link failed");
  }

  m_pShader[SM_MULTI] = new CGLShader("gl_shader_frag_multi.glsl");
  if (!m_pShader[SM_MULTI]->CompileAndLink())
  {
    m_pShader[SM_MULTI]->Free();
    delete m_pShader[SM_MULTI];
    m_pShader[SM_MULTI] = nullptr;
    CLog::Log(LOGERROR, "GUI Shader gl_shader_frag_multi.glsl - compile and link failed");
  }

  m_pShader[SM_FONTS] = new CGLShader("gl_shader_frag_fonts.glsl");
  if (!m_pShader[SM_FONTS]->CompileAndLink())
  {
    m_pShader[SM_FONTS]->Free();
    delete m_pShader[SM_FONTS];
    m_pShader[SM_FONTS] = nullptr;
    CLog::Log(LOGERROR, "GUI Shader gl_shader_frag_fonts.glsl - compile and link failed");
  }

  m_pShader[SM_TEXTURE_NOBLEND] = new CGLShader("gl_shader_frag_texture_noblend.glsl");
  if (!m_pShader[SM_TEXTURE_NOBLEND]->CompileAndLink())
  {
    m_pShader[SM_TEXTURE_NOBLEND]->Free();
    delete m_pShader[SM_TEXTURE_NOBLEND];
    m_pShader[SM_TEXTURE_NOBLEND] = nullptr;
    CLog::Log(LOGERROR, "GUI Shader gl_shader_frag_texture_noblend.glsl - compile and link failed");
  }

  m_pShader[SM_MULTI_BLENDCOLOR] = new CGLShader("gl_shader_frag_multi_blendcolor.glsl");
  if (!m_pShader[SM_MULTI_BLENDCOLOR]->CompileAndLink())
  {
    m_pShader[SM_MULTI_BLENDCOLOR]->Free();
    delete m_pShader[SM_MULTI_BLENDCOLOR];
    m_pShader[SM_MULTI_BLENDCOLOR] = nullptr;
    CLog::Log(LOGERROR, "GUI Shader gl_shader_frag_multi_blendcolor.glsl - compile and link failed");
  }
}

void CRenderSystemGL::EnableShader(ESHADERMETHOD method)
{
  m_method = method;
  if (m_pShader[m_method])
  {
    m_pShader[m_method]->Enable();
  }
  else
  {
    CLog::Log(LOGERROR, "Invalid GUI Shader selected %d", method);
  }
}

void CRenderSystemGL::DisableShader()
{
  if (m_pShader[m_method])
  {
    m_pShader[m_method]->Disable();
  }
  m_method = SM_DEFAULT;
}

GLint CRenderSystemGL::ShaderGetPos()
{
  if (m_pShader[m_method])
    return m_pShader[m_method]->GetPosLoc();

  return -1;
}

GLint CRenderSystemGL::ShaderGetCol()
{
  if (m_pShader[m_method])
    return m_pShader[m_method]->GetColLoc();

  return -1;
}

GLint CRenderSystemGL::ShaderGetCoord0()
{
  if (m_pShader[m_method])
    return m_pShader[m_method]->GetCord0Loc();

  return -1;
}

GLint CRenderSystemGL::ShaderGetCoord1()
{
  if (m_pShader[m_method])
    return m_pShader[m_method]->GetCord1Loc();

  return -1;
}

GLint CRenderSystemGL::ShaderGetUniCol()
{
  if (m_pShader[m_method])
    return m_pShader[m_method]->GetUniColLoc();

  return -1;
}

GLint CRenderSystemGL::ShaderGetModel()
{
  if (m_pShader[m_method])
    return m_pShader[m_method]->GetModelLoc();

  return -1;
}

std::string CRenderSystemGL::GetShaderPath(const std::string &filename)
{
  std::string path = "GL/1.2/";

  if (m_glslMajor >= 4)
  {
    std::string file = "special://xbmc/system/shaders/GL/4.0/" + filename;
    const CURL pathToUrl(file);
    if (XFILE::CFile::Exists(pathToUrl))
      return "GL/4.0/";
  }
  if (m_glslMajor >= 2 || (m_glslMajor == 1 && m_glslMinor >= 50))
    path = "GL/1.5/";

  return path;
}
