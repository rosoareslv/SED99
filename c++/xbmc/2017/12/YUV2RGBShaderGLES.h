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

#include "guilib/TransformMatrix.h"
#include "ShaderFormats.h"

void CalculateYUVMatrix(TransformMatrix &matrix
                        , unsigned int  flags
                        , EShaderFormat format
                        , float         black
                        , float         contrast
                        , bool          limited);

#include "GLSLOutputGLES.h"

#ifndef __GNUC__
#pragma warning( push )
#pragma warning( disable : 4250 )
#endif

#include "guilib/Shader.h"

namespace Shaders {

  class BaseYUV2RGBShader : virtual public CShaderProgram
  {
  public:
    BaseYUV2RGBShader() : m_convertFullRange(false) {};
    ~BaseYUV2RGBShader() override = default;
    virtual void SetField(int field) {};
    virtual void SetWidth(int width) {};
    virtual void SetHeight(int width) {};

    virtual void SetBlack(float black) {};
    virtual void SetContrast(float contrast) {};
    virtual void SetNonLinStretch(float stretch) {};

    virtual GLint GetVertexLoc() { return 0; };
    virtual GLint GetYcoordLoc() { return 0; };
    virtual GLint GetUcoordLoc() { return 0; };
    virtual GLint GetVcoordLoc() { return 0; };

    virtual void SetMatrices(GLfloat *p, GLfloat *m) {};
    virtual void SetAlpha(GLfloat alpha) {};

    void SetConvertFullColorRange(bool convertFullRange) { m_convertFullRange = convertFullRange; }

  protected:
    bool m_convertFullRange;
  };


  class BaseYUV2RGBGLSLShader
    : public BaseYUV2RGBShader
    , public CGLSLShaderProgram
  {
  public:
    BaseYUV2RGBGLSLShader(unsigned flags, EShaderFormat format, bool stretch, GLSLOutput *output=NULL);
   ~BaseYUV2RGBGLSLShader() override;
    void SetField(int field) override { m_field  = field; }
    void SetWidth(int w) override { m_width  = w; }
    void SetHeight(int h) override { m_height = h; }

    void SetBlack(float black) override { m_black    = black; }
    void SetContrast(float contrast) override { m_contrast = contrast; }
    void SetNonLinStretch(float stretch) override { m_stretch = stretch; }

    GLint GetVertexLoc() override { return m_hVertex; }
    GLint GetYcoordLoc() override { return m_hYcoord; }
    GLint GetUcoordLoc() override { return m_hUcoord; }
    GLint GetVcoordLoc() override { return m_hVcoord; }

    void SetMatrices(GLfloat *p, GLfloat *m) override { m_proj = p; m_model = m; }
    void SetAlpha(GLfloat alpha) override { m_alpha = alpha; }

  protected:
    void OnCompiledAndLinked() override;
    bool OnEnabled() override;
    void OnDisabled() override;
    void Free();

    unsigned m_flags;
    EShaderFormat m_format;
    int m_width;
    int m_height;
    int m_field;

    float m_black;
    float m_contrast;
    float m_stretch;

    std::string m_defines;

    Shaders::GLSLOutput *m_glslOutput;

    // shader attribute handles
    GLint m_hYTex;
    GLint m_hUTex;
    GLint m_hVTex;
    GLint m_hMatrix;
    GLint m_hStretch;
    GLint m_hStep;

    GLint m_hVertex;
    GLint m_hYcoord;
    GLint m_hUcoord;
    GLint m_hVcoord;
    GLint m_hProj;
    GLint m_hModel;
    GLint m_hAlpha;

    GLfloat *m_proj;
    GLfloat *m_model;
    GLfloat  m_alpha;
  };

  class YUV2RGBProgressiveShader : public BaseYUV2RGBGLSLShader
  {
  public:
    YUV2RGBProgressiveShader(unsigned flags=0,
                             EShaderFormat format=SHADER_NONE,
                             bool stretch = false,
                             GLSLOutput *output=NULL);
  };

  class YUV2RGBBobShader : public BaseYUV2RGBGLSLShader
  {
  public:
    YUV2RGBBobShader(unsigned flags=0, EShaderFormat format=SHADER_NONE);
    void OnCompiledAndLinked() override;
    bool OnEnabled() override;

    GLint m_hStepX;
    GLint m_hStepY;
    GLint m_hField;
  };

} // end namespace

#ifndef __GNUC__
#pragma warning( pop )
#endif
