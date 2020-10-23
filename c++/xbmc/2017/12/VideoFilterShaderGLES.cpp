/*
 *      Copyright (c) 2007 d4rk
 *      Copyright (C) 2007-2013 Team XBMC
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

#include <string>
#include <math.h>

#include "VideoFilterShaderGLES.h"
#include "ServiceBroker.h"
#include "utils/log.h"
#include "utils/GLUtils.h"
#include "ConvolutionKernels.h"
#include "rendering/gles/RenderSystemGLES.h"

#define TEXTARGET GL_TEXTURE_2D

using namespace Shaders;

//////////////////////////////////////////////////////////////////////
// BaseVideoFilterShader - base class for video filter shaders
//////////////////////////////////////////////////////////////////////

BaseVideoFilterShader::BaseVideoFilterShader()
{
  m_width = 1;
  m_height = 1;
  m_hStepXY = 0;
  m_stepX = 0;
  m_stepY = 0;
  m_sourceTexUnit = 0;
  m_hSourceTex = 0;

  m_stretch = 0.0f;

  m_hVertex = -1;
  m_hcoord = -1;
  m_hProj = -1;
  m_hModel = -1;
  m_hAlpha = -1;

  m_proj = nullptr;
  m_model = nullptr;
  m_alpha = -1;

  std::string shaderv =
    "attribute vec4 m_attrpos;"
    "attribute vec2 m_attrcord;"
    "varying vec2 cord;"
    "uniform mat4 m_proj;"
    "uniform mat4 m_model;"

    "void main ()"
    "{"
    "mat4 mvp = m_proj * m_model;"
    "gl_Position = mvp * m_attrpos;"
    "cord = m_attrcord.xy;"
    "}";
  VertexShader()->SetSource(shaderv);

  std::string shaderp =
    "precision mediump float;"
    "uniform sampler2D img;"
    "varying vec2 cord;"
    "void main()"
    "{"
    "gl_FragColor = texture2D(img, cord);"
    "}";
  PixelShader()->SetSource(shaderp);
}

void BaseVideoFilterShader::OnCompiledAndLinked()
{
  m_hVertex = glGetAttribLocation(ProgramHandle(),  "m_attrpos");
  m_hcoord = glGetAttribLocation(ProgramHandle(),  "m_attrcord");
  m_hAlpha  = glGetUniformLocation(ProgramHandle(), "m_alpha");
  m_hProj  = glGetUniformLocation(ProgramHandle(), "m_proj");
  m_hModel = glGetUniformLocation(ProgramHandle(), "m_model");
}

bool BaseVideoFilterShader::OnEnabled()
{
  glUniformMatrix4fv(m_hProj,  1, GL_FALSE, m_proj);
  glUniformMatrix4fv(m_hModel, 1, GL_FALSE, m_model);
  glUniform1f(m_hAlpha, m_alpha);
  return true;
}

ConvolutionFilterShader::ConvolutionFilterShader(ESCALINGMETHOD method, bool stretch, GLSLOutput *output)
{
  m_method = method;
  m_kernelTex1 = 0;
  m_hKernTex = -1;

  std::string shadername;
  std::string defines;

  if (CServiceBroker::GetRenderSystem().IsExtSupported("GL_EXT_color_buffer_float"))
  {
    m_floattex = true;
  }
  else
  {
    m_floattex = false;
  }

  if (m_method == VS_SCALINGMETHOD_CUBIC ||
      m_method == VS_SCALINGMETHOD_LANCZOS2 ||
      m_method == VS_SCALINGMETHOD_SPLINE36_FAST ||
      m_method == VS_SCALINGMETHOD_LANCZOS3_FAST)
  {
    shadername = "convolution-4x4.glsl";
  }
  else if (m_method == VS_SCALINGMETHOD_SPLINE36 ||
           m_method == VS_SCALINGMETHOD_LANCZOS3)
  {
    shadername = "convolution-6x6.glsl";
  }

  if (m_floattex)
  {
    m_internalformat = GL_RGBA16F_EXT;
    defines = "#define HAS_FLOAT_TEXTURE 1\n";
  }
  else
  {
    m_internalformat = GL_RGBA;
    defines = "#define HAS_FLOAT_TEXTURE 0\n";
  }

  //don't compile in stretch support when it's not needed
  if (stretch)
    defines += "#define XBMC_STRETCH 1\n";
  else
    defines += "#define XBMC_STRETCH 0\n";

  // get defines from the output stage if used
  m_glslOutput = output;
  if (m_glslOutput) {
    defines += m_glslOutput->GetDefines();
  }

  //tell shader if we're not using a 1D texture
  defines += "#define USE1DTEXTURE 0\n";

  CLog::Log(LOGDEBUG, "GL: ConvolutionFilterShader: using %s defines:\n%s", shadername.c_str(), defines.c_str());
  PixelShader()->LoadSource(shadername, defines);
  PixelShader()->AppendSource("output.glsl");
}

ConvolutionFilterShader::~ConvolutionFilterShader()
{
  Free();
  delete m_glslOutput;
}

void ConvolutionFilterShader::OnCompiledAndLinked()
{
  BaseVideoFilterShader::OnCompiledAndLinked();

  // obtain shader attribute handles on successful compilation
  m_hSourceTex = glGetUniformLocation(ProgramHandle(), "img");
  m_hStepXY    = glGetUniformLocation(ProgramHandle(), "stepxy");
  m_hKernTex   = glGetUniformLocation(ProgramHandle(), "kernelTex");
  m_hStretch   = glGetUniformLocation(ProgramHandle(), "m_stretch");

  CConvolutionKernel kernel(m_method, 256);

  if (m_kernelTex1)
  {
    glDeleteTextures(1, &m_kernelTex1);
    m_kernelTex1 = 0;
  }

  glGenTextures(1, &m_kernelTex1);

  if ((m_kernelTex1<=0))
  {
    CLog::Log(LOGERROR, "GL: ConvolutionFilterShader: Error creating kernel texture");
    return;
  }

  //make a kernel texture on GL_TEXTURE2 and set clamping and interpolation
  //TEXTARGET is set to GL_TEXTURE_1D or GL_TEXTURE_2D
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(TEXTARGET, m_kernelTex1);
  glTexParameteri(TEXTARGET, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(TEXTARGET, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(TEXTARGET, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(TEXTARGET, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  //if float textures are supported, we can load the kernel as a float texture
  //if not we load it as 8 bit unsigned which gets converted back to float in the shader
  GLenum  format;
  GLvoid* data;
  if (m_floattex)
  {
    format = GL_FLOAT;
    data   = (GLvoid*)kernel.GetFloatPixels();
  }
  else
  {
    format = GL_UNSIGNED_BYTE;
    data   = (GLvoid*)kernel.GetUint8Pixels();
  }

  //upload as 2D texture with height of 1
  glTexImage2D(TEXTARGET, 0, m_internalformat, kernel.GetSize(), 1, 0, GL_RGBA, format, data);

  glActiveTexture(GL_TEXTURE0);

  VerifyGLState();

  if (m_glslOutput) m_glslOutput->OnCompiledAndLinked(ProgramHandle());
}

bool ConvolutionFilterShader::OnEnabled()
{
  BaseVideoFilterShader::OnEnabled();

  // set shader attributes once enabled
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(TEXTARGET, m_kernelTex1);

  glActiveTexture(GL_TEXTURE0);
  glUniform1i(m_hSourceTex, m_sourceTexUnit);
  glUniform1i(m_hKernTex, 2);
  glUniform2f(m_hStepXY, m_stepX, m_stepY);
  glUniform1f(m_hStretch, m_stretch);
  VerifyGLState();
  if (m_glslOutput) m_glslOutput->OnEnabled();
  return true;
}

void ConvolutionFilterShader::OnDisabled()
{
  if (m_glslOutput) m_glslOutput->OnDisabled();
}

void ConvolutionFilterShader::Free()
{
  if (m_kernelTex1)
    glDeleteTextures(1, &m_kernelTex1);
  m_kernelTex1 = 0;
  if (m_glslOutput) m_glslOutput->Free();
  BaseVideoFilterShader::Free();
}

StretchFilterShader::StretchFilterShader()
{
  PixelShader()->LoadSource("stretch.glsl");
}

void StretchFilterShader::OnCompiledAndLinked()
{
  BaseVideoFilterShader::OnCompiledAndLinked();

  m_hSourceTex = glGetUniformLocation(ProgramHandle(), "img");
  m_hStretch   = glGetUniformLocation(ProgramHandle(), "m_stretch");
}

bool StretchFilterShader::OnEnabled()
{
  BaseVideoFilterShader::OnEnabled();

  glUniform1i(m_hSourceTex, m_sourceTexUnit);
  glUniform1f(m_hStretch, m_stretch);
  VerifyGLState();
  return true;
}

void DefaultFilterShader::OnCompiledAndLinked()
{
  BaseVideoFilterShader::OnCompiledAndLinked();

  m_hSourceTex = glGetUniformLocation(ProgramHandle(), "img");
}

bool DefaultFilterShader::OnEnabled()
{
  BaseVideoFilterShader::OnEnabled();

  glUniform1i(m_hSourceTex, m_sourceTexUnit);
  VerifyGLState();
  return true;
}
