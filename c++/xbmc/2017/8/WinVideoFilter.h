#pragma once

/*
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


#include "cores/IPlayer.h"
#include "guilib/Geometry.h"
#include "guilib/D3DResource.h"

#include <DirectXMath.h>
#include <vector>
#include <wrl/client.h>

enum EBufferFormat;
class CRenderBuffer;

using namespace DirectX;

class CYUV2RGBMatrix
{
public:
  CYUV2RGBMatrix();
  void SetParameters(float contrast, float blacklevel, unsigned int flags, EBufferFormat format);
  XMFLOAT4X4* Matrix();

private:
  bool         m_NeedRecalc;
  float        m_contrast;
  float        m_blacklevel;
  unsigned int m_flags;
  bool         m_limitedRange;
  EBufferFormat m_format;
  XMFLOAT4X4   m_mat;
};

class CWinShader
{
protected:
  CWinShader() :
    m_vbsize(0),
    m_vertsize(0),
    m_inputLayout(nullptr)
  {}
  virtual ~CWinShader();
  virtual bool CreateVertexBuffer(unsigned int vertCount, unsigned int vertSize);
  virtual bool LockVertexBuffer(void **data);
  virtual bool UnlockVertexBuffer();
  virtual bool LoadEffect(const std::string& filename, DefinesMap* defines);
  virtual bool Execute(const std::vector<CD3DTexture*> &targets, unsigned int vertexIndexStep);
  virtual void SetStepParams(UINT stepIndex) { }
  virtual bool CreateInputLayout(D3D11_INPUT_ELEMENT_DESC *layout, unsigned numElements);

  CD3DEffect   m_effect;
  CD3DTexture* m_target{ nullptr };

private:
  void SetTarget(CD3DTexture* target);
  CD3DBuffer m_vb;
  CD3DBuffer m_ib;
  unsigned int m_vbsize;
  unsigned int m_vertsize;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
};

class COutputShader : public CWinShader
{
public:
  virtual ~COutputShader();

  void ApplyEffectParameters(CD3DEffect &effect, unsigned sourceWidth, unsigned sourceHeight);
  void GetDefines(DefinesMap &map) const;
  bool Create(bool useCLUT, bool useDithering, int ditherDepth);
  void Render(CD3DTexture &sourceTexture, unsigned sourceWidth, unsigned sourceHeight, CRect sourceRect, const CPoint points[4]
            , CD3DTexture *target, unsigned range = 0, float contrast = 0.5f, float brightness = 0.5f);
  void Render(CD3DTexture &sourceTexture, unsigned sourceWidth, unsigned sourceHeight, CRect sourceRect, CRect destRect
            , CD3DTexture *target, unsigned range = 0, float contrast = 0.5f, float brightness = 0.5f);
  void SetCLUT(int clutSize, ID3D11ShaderResourceView *pCLUTView);

  static bool CreateCLUTView(int clutSize, uint16_t* clutData, bool isRGB, ID3D11ShaderResourceView** ppCLUTView);

private:
  bool HasCLUT() const { return m_clutSize && m_pCLUTView; }
  void PrepareParameters(unsigned sourceWidth, unsigned sourceHeight, CRect sourceRect, const CPoint points[4]);
  void SetShaderParameters(CD3DTexture &sourceTexture, unsigned range, float contrast, float brightness);
  void CreateDitherView();

  bool m_useCLUT{ false };
  unsigned m_sourceWidth{ 0 };
  unsigned m_sourceHeight{ 0 };
  CRect m_sourceRect{ 0.f, 0.f, 0.f, 0.f };
  CPoint m_destPoints[4] = 
  {
    { 0.f, 0.f },
    { 0.f, 0.f },
    { 0.f, 0.f },
    { 0.f, 0.f },
  };
  int m_clutSize{ 0 };
  ID3D11ShaderResourceView *m_pCLUTView{ nullptr };
  bool m_useDithering{ false };
  int m_ditherDepth{ 0 };
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_pDitherView;

  struct CUSTOMVERTEX {
    FLOAT x, y, z;
    FLOAT tu, tv;
  };
};

class CYUV2RGBShader : public CWinShader
{
public:
  CYUV2RGBShader();
  virtual ~CYUV2RGBShader();
  virtual bool Create(EBufferFormat fmt, COutputShader *pOutShader = nullptr);
  virtual void Render(CRect sourceRect, CPoint dest[], float contrast, float brightness, CRenderBuffer* videoBuffer, CD3DTexture *target);

protected:
  void PrepareParameters(CRenderBuffer* videoBuffer, CRect sourceRect, CPoint dest[],
                         float contrast, float brightness);
  void SetShaderParameters(CRenderBuffer* videoBuffer);

private:
  CYUV2RGBMatrix      m_matrix;
  unsigned int        m_sourceWidth, m_sourceHeight;
  CRect               m_sourceRect;
  CPoint              m_dest[4];
  EBufferFormat       m_format;
  float               m_texSteps[2];
  COutputShader *m_pOutShader;

  struct CUSTOMVERTEX {
      FLOAT x, y, z;
      FLOAT tu, tv;   // Y Texture coordinates
      FLOAT tu2, tv2; // U and V Textures coordinates
  };
};

class CConvolutionShader : public CWinShader
{
public:
  virtual bool Create(ESCALINGMETHOD method, COutputShader *pOutShader = nullptr) = 0;
  virtual void Render(CD3DTexture &sourceTexture,
                      unsigned int sourceWidth, unsigned int sourceHeight,
                      unsigned int destWidth, unsigned int destHeight,
                      CRect sourceRect, CRect destRect, bool useLimitRange, 
                      CD3DTexture *target) = 0;
  CConvolutionShader();
  virtual ~CConvolutionShader();
  
protected:
  virtual bool ChooseKernelD3DFormat();
  virtual bool CreateHQKernel(ESCALINGMETHOD method);
  virtual void SetShaderParameters(CD3DTexture &sourceTexture, float* texSteps, int texStepsCount, bool useLimitRange) = 0;

  CD3DTexture m_HQKernelTexture;
  DXGI_FORMAT m_KernelFormat;
  bool m_floattex;
  bool m_rgba;
  COutputShader *m_pOutShader;

  struct CUSTOMVERTEX {
      FLOAT x, y, z;
      FLOAT tu, tv;
  };
};

class CConvolutionShader1Pass : public CConvolutionShader
{
public:
  bool Create(ESCALINGMETHOD method, COutputShader *pCLUT = nullptr) override;
  void Render(CD3DTexture &sourceTexture,
              unsigned int sourceWidth, unsigned int sourceHeight,
              unsigned int destWidth, unsigned int destHeight,
              CRect sourceRect, CRect destRect, bool useLimitRange, 
              CD3DTexture *target) override;
  CConvolutionShader1Pass() : CConvolutionShader(), m_sourceWidth(0), m_sourceHeight(0) {}

protected:
  void PrepareParameters(unsigned int sourceWidth, unsigned int sourceHeight,
                         CRect sourceRect, CRect destRect);
  void SetShaderParameters(CD3DTexture &sourceTexture, float* texSteps, 
                           int texStepsCount, bool useLimitRange) override;


private:
  unsigned int  m_sourceWidth, m_sourceHeight;
  CRect         m_sourceRect, m_destRect;
};

class CConvolutionShaderSeparable : public CConvolutionShader
{
public:
  CConvolutionShaderSeparable();
  bool Create(ESCALINGMETHOD method, COutputShader *pOutShader = nullptr) override;
  void Render(CD3DTexture &sourceTexture,
              unsigned int sourceWidth, unsigned int sourceHeight,
              unsigned int destWidth, unsigned int destHeight,
              CRect sourceRect, CRect destRect, bool useLimitRange,
              CD3DTexture *target) override;
  virtual ~CConvolutionShaderSeparable();

protected:
  bool ChooseIntermediateD3DFormat();
  bool CreateIntermediateRenderTarget(unsigned int width, unsigned int height);
  bool ClearIntermediateRenderTarget();
  void PrepareParameters(unsigned int sourceWidth, unsigned int sourceHeight,
                         unsigned int destWidth, unsigned int destHeight,
                         CRect sourceRect, CRect destRect);
  void SetShaderParameters(CD3DTexture &sourceTexture, float* texSteps, 
                           int texStepsCount, bool useLimitRange) override;
  void SetStepParams(UINT stepIndex) override;

private:
  CD3DTexture m_IntermediateTarget;
  DXGI_FORMAT m_IntermediateFormat;
  unsigned int m_sourceWidth, m_sourceHeight;
  unsigned int m_destWidth, m_destHeight;
  CRect m_sourceRect, m_destRect;
};

class CTestShader : public CWinShader
{
public:
  virtual bool Create();
};
