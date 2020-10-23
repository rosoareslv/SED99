/*
 *      Copyright (C) 2012-2017 Team Kodi
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
 *  along with this Program; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */
#pragma once

#include "games/addons/GameClientCallbacks.h"
//#include "threads/Thread.h"

#include <memory>

class CDVDClock;
class CDVDVideoCodec;
class CPixelConverter;
class CProcessInfo;
class CRenderManager;
struct VideoPicture;

namespace KODI
{
namespace RETRO
{
  class CRetroPlayerVideo : public GAME::IGameVideoCallback
                            //protected CThread
  {
  public:
    CRetroPlayerVideo(CRenderManager& m_renderManager, CProcessInfo& m_processInfo, CDVDClock &clock);

    ~CRetroPlayerVideo() override;

    // implementation of IGameVideoCallback
    bool OpenPixelStream(AVPixelFormat pixfmt, unsigned int width, unsigned int height, double framerate, unsigned int orientationDeg) override;
    bool OpenEncodedStream(AVCodecID codec) override;
    void AddData(const uint8_t* data, unsigned int size) override;
    void CloseStream() override;

    /*
  protected:
    // implementation of CThread
    virtual void Process(void);
    */

  private:
    bool Configure(VideoPicture& picture);
    bool GetPicture(const uint8_t* data, unsigned int size, VideoPicture& picture);
    void SendPicture(VideoPicture& picture);

    // Construction parameters
    CRenderManager& m_renderManager;
    CProcessInfo&   m_processInfo;
    CDVDClock       &m_clock;

    // Stream properties
    double       m_framerate;
    unsigned int m_orientation; // Degrees counter-clockwise
    bool         m_bConfigured; // Need first picture to configure the render manager
    unsigned int m_droppedFrames;
    std::unique_ptr<CPixelConverter> m_pixelConverter;
    std::unique_ptr<CDVDVideoCodec>  m_pVideoCodec;
  };
}
}
