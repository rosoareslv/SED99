#pragma once

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

#include <utility>
#include <vector>

#include "RenderInfo.h"
#include "guilib/Resolution.h"
#include "guilib/Geometry.h"
#include "VideoShaders/ShaderFormats.h"
#include "cores/IPlayer.h"
#include "cores/VideoPlayer/Process/VideoBuffer.h"

#define MAX_FIELDS 3
#define NUM_BUFFERS 6

class CSetting;

enum EFIELDSYNC
{
  FS_NONE,
  FS_TOP,
  FS_BOT
};

// Render Methods
enum RenderMethods
{
  RENDER_METHOD_AUTO     = 0,
  RENDER_METHOD_GLSL,
  RENDER_METHOD_SOFTWARE,
  RENDER_METHOD_D3D_PS,
  RENDER_METHOD_DXVA,
  RENDER_OVERLAYS        = 99   // to retain compatibility
};

struct VideoPicture;
class CRenderCapture;

class CBaseRenderer
{
public:
  CBaseRenderer();
  virtual ~CBaseRenderer();

  // Player functions
  virtual bool Configure(const VideoPicture &picture, float fps, unsigned flags, unsigned int orientation) = 0;
  virtual bool IsConfigured() = 0;
  virtual void AddVideoPicture(const VideoPicture &picture, int index, double currentClock) = 0;
  virtual bool IsPictureHW(const VideoPicture &picture) { return false; };
  virtual void UnInit() = 0;
  virtual void Flush() {};
  virtual void SetBufferSize(int numBuffers) { }
  virtual void ReleaseBuffer(int idx) { }
  virtual bool NeedBuffer(int idx) { return false; }
  virtual bool IsGuiLayer() { return true; }
  // Render info, can be called before configure
  virtual CRenderInfo GetRenderInfo() { return CRenderInfo(); }
  virtual void Update() = 0;
  virtual void RenderUpdate(int index, int index2, bool clear, unsigned int flags, unsigned int alpha) = 0;
  virtual bool RenderCapture(CRenderCapture* capture) = 0;
  virtual bool ConfigChanged(const VideoPicture &picture) = 0;

  // Feature support
  virtual bool SupportsMultiPassRendering() = 0;
  virtual bool Supports(ERENDERFEATURE feature) { return false; };
  virtual bool Supports(ESCALINGMETHOD method) = 0;

  virtual bool WantsDoublePass() { return false; };

  void SetViewMode(int viewMode);

  /*! \brief Get video rectangle and view window
  \param source is original size of the video
  \param dest is the target rendering area honoring aspect ratio of source
  \param view is the entire target rendering area for the video (including black bars)
  */
  void GetVideoRect(CRect &source, CRect &dest, CRect &view);
  float GetAspectRatio() const;

  static void SettingOptionsRenderMethodsFiller(std::shared_ptr<const CSetting> setting, std::vector< std::pair<std::string, int> > &list, int &current, void *data);

  void SetVideoSettings(const CVideoSettings &settings);

protected:
  void CalcNormalRenderRect(float offsetX, float offsetY, float width, float height,
                            float inputFrameRatio, float zoomAmount, float verticalShift);
  void CalculateFrameAspectRatio(unsigned int desired_width, unsigned int desired_height);
  virtual void ManageRenderArea();
  virtual void ReorderDrawPoints();//might be overwritten (by egl e.x.)
  virtual EShaderFormat GetShaderFormat();
  void saveRotatedCoords();//saves the current state of m_rotatedDestCoords
  void syncDestRectToRotatedPoints();//sync any changes of m_destRect to m_rotatedDestCoords
  void restoreRotatedCoords();//restore the current state of m_rotatedDestCoords from saveRotatedCoords
  void MarkDirty();

  unsigned int m_sourceWidth;
  unsigned int m_sourceHeight;
  float m_sourceFrameRatio;
  float m_fps;

  unsigned int m_renderOrientation; // orientation of the video in degrees counter clockwise
  unsigned int m_oldRenderOrientation; // orientation of the previous frame
  // for drawing the texture with glVertex4f (holds all 4 corner points of the destination rect
  // with correct orientation based on m_renderOrientation
  // 0 - top left, 1 - top right, 2 - bottom right, 3 - bottom left
  CPoint m_rotatedDestCoords[4];
  CPoint m_savedRotatedDestCoords[4];//saved points from saveRotatedCoords call

  CRect m_destRect;
  CRect m_oldDestRect; // destrect of the previous frame
  CRect m_sourceRect;
  CRect m_viewRect;

  // rendering flags
  unsigned m_iFlags;
  AVPixelFormat m_format = AV_PIX_FMT_NONE;

  CVideoSettings m_videoSettings;
};
