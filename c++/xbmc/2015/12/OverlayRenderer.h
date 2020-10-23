/*
 *      Initial code sponsored by: Voddler Inc (voddler.com)
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

#pragma once

#include "threads/CriticalSection.h"
#include "BaseRenderer.h"

#include <vector>
#include <map>

class CDVDOverlay;
class CDVDOverlayImage;
class CDVDOverlaySpu;
class CDVDOverlaySSA;
class CRenderManager;

namespace OVERLAY {

  struct SRenderState
  {
    float x;
    float y;
    float width;
    float height;
  };

  class COverlay
  {
  public:
    COverlay();
    virtual ~COverlay();

    virtual void Render(SRenderState& state) = 0;
    virtual void PrepareRender() {};

    enum EType
    { TYPE_NONE
    , TYPE_TEXTURE
    , TYPE_GUITEXT
    } m_type;

    enum EAlign
    { ALIGN_SCREEN
    , ALIGN_VIDEO
    , ALIGN_SUBTITLE
    } m_align;

    enum EPosition
    { POSITION_ABSOLUTE
    , POSITION_ABSOLUTE_SCREEN
    , POSITION_RELATIVE
    } m_pos;

    float m_x;
    float m_y;
    float m_width;
    float m_height;
  };

  class CRenderer
  {
  public:
    CRenderer(CRenderManager *renderManager);
    virtual ~CRenderer();

    void AddOverlay(CDVDOverlay* o, double pts, int index);
    void Render(int idx);
    void Flush();
    void Release(int idx);
    bool HasOverlay(int idx);

  protected:

    struct SElement
    {
      SElement()
      {
        overlay_dvd = NULL;
        pts = 0.0;
      }
      double pts;
      CDVDOverlay* overlay_dvd;
    };

    void Render(COverlay* o, float adjust_height);
    COverlay* Convert(CDVDOverlay* o, double pts);
    COverlay* Convert(CDVDOverlaySSA* o, double pts);

    void Release(std::vector<SElement>& list);
    void ReleaseCache();
    void ReleaseUnused();

    CCriticalSection m_section;
    std::vector<SElement> m_buffers[NUM_BUFFERS];
    CRenderManager *m_pRenderManager;
    std::map<unsigned int, COverlay*> m_textureCache;
    static unsigned int m_textureid;
  };
}
