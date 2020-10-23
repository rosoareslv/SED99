/*
 *  Copyright (C) 2010-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <vector>

#include "system_gl.h"

#include "FrameBufferObject.h"
#include "xbmc/guilib/Shader.h"
#include "cores/VideoSettings.h"
#include "RenderFlags.h"
#include "RenderInfo.h"
#include "windowing/GraphicContext.h"
#include "BaseRenderer.h"
#include "xbmc/cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodec.h"

class CRenderCapture;
class CRenderSystemGLES;

class CBaseTexture;
namespace Shaders { class BaseYUV2RGBShader; }
namespace Shaders { class BaseVideoFilterShader; }

struct DRAWRECT
{
  float left;
  float top;
  float right;
  float bottom;
};

struct YUVRANGE
{
  int y_min, y_max;
  int u_min, u_max;
  int v_min, v_max;
};

struct YUVCOEF
{
  float r_up, r_vp;
  float g_up, g_vp;
  float b_up, b_vp;
};

enum RenderMethod
{
  RENDER_GLSL = 0x01,
  RENDER_CUSTOM = 0x02,
};

enum RenderQuality
{
  RQ_LOW=1,
  RQ_SINGLEPASS,
  RQ_MULTIPASS,
  RQ_SOFTWARE
};

#define PLANE_Y 0
#define PLANE_U 1
#define PLANE_V 2

#define FIELD_FULL 0
#define FIELD_TOP 1
#define FIELD_BOT 2

extern YUVRANGE yuv_range_lim;
extern YUVRANGE yuv_range_full;
extern YUVCOEF yuv_coef_bt601;
extern YUVCOEF yuv_coef_bt709;
extern YUVCOEF yuv_coef_ebu;
extern YUVCOEF yuv_coef_smtp240m;

class CEvent;

class CLinuxRendererGLES : public CBaseRenderer
{
public:
  CLinuxRendererGLES();
  virtual ~CLinuxRendererGLES();

  // Registration
  static CBaseRenderer* Create(CVideoBuffer *buffer);
  static bool Register();

  // Player functions
  virtual bool Configure(const VideoPicture &picture, float fps, unsigned int orientation) override;
  virtual bool IsConfigured() override { return m_bConfigured; }
  virtual void AddVideoPicture(const VideoPicture &picture, int index, double currentClock) override;
  virtual void UnInit() override;
  virtual bool Flush(bool saveBuffers) override;
  virtual void ReorderDrawPoints() override;
  virtual void SetBufferSize(int numBuffers) override { m_NumYV12Buffers = numBuffers; }
  virtual bool IsGuiLayer() override;
  virtual void ReleaseBuffer(int idx) override;
  virtual void RenderUpdate(int index, int index2, bool clear, unsigned int flags, unsigned int alpha) override;
  virtual void Update() override;
  virtual bool RenderCapture(CRenderCapture* capture) override;
  virtual CRenderInfo GetRenderInfo() override;
  virtual bool ConfigChanged(const VideoPicture &picture) override;

  // Feature support
  virtual bool SupportsMultiPassRendering() override;
  virtual bool Supports(ERENDERFEATURE feature) override;
  virtual bool Supports(ESCALINGMETHOD method) override;

protected:
  virtual void Render(unsigned int flags, int index);
  virtual void RenderUpdateVideo(bool clear, unsigned int flags = 0, unsigned int alpha = 255);

  int  NextYV12Texture();
  virtual bool ValidateRenderTarget();
  virtual void LoadShaders(int field=FIELD_FULL);
  virtual void ReleaseShaders();
  void SetTextureFilter(GLenum method);
  void UpdateVideoFilter();
  AVColorPrimaries GetSrcPrimaries(AVColorPrimaries srcPrimaries, unsigned int width, unsigned int height);

  // textures
  virtual bool UploadTexture(int index);
  virtual void DeleteTexture(int index);
  virtual bool CreateTexture(int index);

  bool UploadYV12Texture(int index);
  void DeleteYV12Texture(int index);
  bool CreateYV12Texture(int index);
  virtual bool SkipUploadYV12(int index) { return false; }

  bool UploadNV12Texture(int index);
  void DeleteNV12Texture(int index);
  bool CreateNV12Texture(int index);

  void CalculateTextureSourceRects(int source, int num_planes);

  // renderers
  void RenderToFBO(int index, int field, bool weave = false);
  void RenderFromFBO();
  void RenderSinglePass(int index, int field);    // single pass glsl renderer

  // hooks for HwDec Renderered
  virtual bool LoadShadersHook() { return false; };
  virtual bool RenderHook(int idx) { return false; };
  virtual void AfterRenderHook(int idx) {};

  struct
  {
    CFrameBufferObject fbo;
    float width, height;
  } m_fbo;

  int m_iYV12RenderBuffer;
  int m_NumYV12Buffers;

  bool m_bConfigured;
  bool m_bValidated;
  GLenum m_textureTarget;
  int m_renderMethod;
  int m_oldRenderMethod;
  RenderQuality m_renderQuality;
  bool m_StrictBinding;

  // Raw data used by renderer
  int m_currentField;
  int m_reloadShaders;
  CRenderSystemGLES *m_renderSystem;

  struct YUVPLANE
  {
    GLuint id;
    CRect  rect;

    float  width;
    float  height;

    unsigned texwidth;
    unsigned texheight;

    //pixels per texel
    unsigned pixpertex_x;
    unsigned pixpertex_y;
  };

  struct CPictureBuffer
  {
    CPictureBuffer();
   ~CPictureBuffer();

    YUVPLANE fields[MAX_FIELDS][YuvImage::MAX_PLANES];
    YuvImage image;

    CVideoBuffer *videoBuffer;
    bool loaded;

    AVColorPrimaries m_srcPrimaries;
    AVColorSpace m_srcColSpace;
    int m_srcBits = 8;
    int m_srcTextureBits = 8;
    bool m_srcFullRange;

    bool hasDisplayMetadata = false;
    AVMasteringDisplayMetadata displayMetadata;
    bool hasLightMetadata = false;
    AVContentLightMetadata lightMetadata;
  };

  // YV12 decoder textures
  // field index 0 is full image, 1 is odd scanlines, 2 is even scanlines
  CPictureBuffer m_buffers[NUM_BUFFERS];

  void LoadPlane(YUVPLANE& plane, int type,
                 unsigned width,  unsigned height,
                 int stride, int bpp, void* data);

  Shaders::BaseYUV2RGBShader     *m_pYUVProgShader;
  Shaders::BaseYUV2RGBShader     *m_pYUVBobShader;
  Shaders::BaseVideoFilterShader *m_pVideoFilterShader;
  ESCALINGMETHOD m_scalingMethod;
  ESCALINGMETHOD m_scalingMethodGui;
  bool m_fullRange;
  AVColorPrimaries m_srcPrimaries;

  // clear colour for "black" bars
  float m_clearColour;
};
