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

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <drm_mode.h>
#include <EGL/egl.h>
#include <unistd.h>

#include "WinSystemGbmGLESContext.h"
#include "guilib/gui3d.h"
#include "utils/log.h"
#include "settings/Settings.h"

#include "DRMLegacy.h"

static int flip_happening = 0;

bool CDRMLegacy::SetVideoMode(RESOLUTION_INFO res, struct gbm_bo *bo)
{
  struct drm_fb *drm_fb = DrmFbGetFromBo(bo);

  auto ret = drmModeSetCrtc(m_fd,
                            m_crtc->crtc->crtc_id,
                            drm_fb->fb_id,
                            0,
                            0,
                            &m_connector->connector->connector_id,
                            1,
                            m_mode);

  if(ret < 0)
  {
    CLog::Log(LOGERROR,
              "CDRMLegacy::%s - failed to set crtc mode: %dx%d%s @ %d Hz",
              __FUNCTION__,
              m_mode->hdisplay,
              m_mode->vdisplay,
              m_mode->flags & DRM_MODE_FLAG_INTERLACE ? "i" : "",
              m_mode->vrefresh);

    return false;
  }

  CLog::Log(LOGDEBUG, "CDRMLegacy::%s - set crtc mode: %dx%d%s @ %d Hz",
            __FUNCTION__,
            m_mode->hdisplay,
            m_mode->vdisplay,
            m_mode->flags & DRM_MODE_FLAG_INTERLACE ? "i" : "",
            m_mode->vrefresh);

  return true;
}

void CDRMLegacy::PageFlipHandler(int fd, unsigned int frame, unsigned int sec,
                                unsigned int usec, void *data)
{
  (void) fd, (void) frame, (void) sec, (void) usec;

  int *flip_happening = static_cast<int *>(data);
  *flip_happening = 0;
}

bool CDRMLegacy::WaitingForFlip()
{
  if(!flip_happening)
  {
    return false;
  }

  struct pollfd drm_fds =
  {
    m_fd,
    POLLIN,
    0,
  };

  drmEventContext drm_evctx =
  {
    DRM_EVENT_CONTEXT_VERSION,
    nullptr,
    PageFlipHandler,
  #if DRM_EVENT_CONTEXT_VERSION > 2
    nullptr,
  #endif
  };

  while(flip_happening)
  {
    auto ret = poll(&drm_fds, 1, -1);

    if(ret < 0)
    {
      return true;
    }

    if(drm_fds.revents & (POLLHUP | POLLERR))
    {
      return true;
    }

    if(drm_fds.revents & POLLIN)
    {
      drmHandleEvent(m_fd, &drm_evctx);
    }
  }

  return false;
}

bool CDRMLegacy::QueueFlip(struct gbm_bo *bo)
{
  struct drm_fb *drm_fb = DrmFbGetFromBo(bo);

  auto ret = drmModePageFlip(m_fd,
                             m_crtc->crtc->crtc_id,
                             drm_fb->fb_id,
                             DRM_MODE_PAGE_FLIP_EVENT,
                             &flip_happening);

  if(ret)
  {
    CLog::Log(LOGDEBUG, "CDRMLegacy::%s - failed to queue DRM page flip", __FUNCTION__);
    return false;
  }

  return true;
}

void CDRMLegacy::FlipPage(struct gbm_bo *bo)
{
  flip_happening = QueueFlip(bo);
  WaitingForFlip();
}

bool CDRMLegacy::InitDrm()
{
  if (!CDRMUtils::OpenDrm())
  {
    return false;
  }

  if (!CDRMUtils::InitDrm())
  {
    return false;
  }

  CLog::Log(LOGDEBUG, "CDRMLegacy::%s - initialized legacy DRM", __FUNCTION__);
  return true;
}
