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

#include "Util.h"
#include "threads/Atomics.h"
#include "MMALRenderer.h"
#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodec.h"
#include "filesystem/File.h"
#include "settings/AdvancedSettings.h"
#include "settings/DisplaySettings.h"
#include "settings/MediaSettings.h"
#include "settings/Settings.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "utils/MathUtils.h"
#include "windowing/WindowingFactory.h"
#include "cores/VideoPlayer/DVDCodecs/Video/MMALCodec.h"
#include "xbmc/Application.h"
#include "linux/RBP.h"

#define CLASSNAME "CMMALRenderer"


MMAL_POOL_T *CMMALRenderer::GetPool(ERenderFormat format, bool opaque)
{
  CSingleLock lock(m_sharedSection);
  if (!m_bMMALConfigured)
    m_bMMALConfigured = init_vout(format, opaque);

  return m_vout_input_pool;
}

CRenderInfo CMMALRenderer::GetRenderInfo()
{
  CSingleLock lock(m_sharedSection);
  CRenderInfo info;

  if (g_advancedSettings.CanLogComponent(LOGVIDEO))
    CLog::Log(LOGDEBUG, "%s::%s cookie:%p", CLASSNAME, __func__, (void *)m_vout_input_pool);

  info.max_buffer_size = NUM_BUFFERS;
  info.optimal_buffer_size = NUM_BUFFERS;
  info.opaque_pointer = (void *)this;
  info.formats = m_formats;
  return info;
}

void CMMALRenderer::vout_input_port_cb(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
  assert(!(buffer->flags & MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED));
  buffer->flags &= ~MMAL_BUFFER_HEADER_FLAG_USER2;
  CMMALBuffer *omvb = (CMMALBuffer *)buffer->user_data;
  if (g_advancedSettings.CanLogComponent(LOGVIDEO))
    CLog::Log(LOGDEBUG, "%s::%s YUV port:%p omvb:%p mmal:%p:%p len:%d cmd:%x flags:%x flight:%d", CLASSNAME, __func__, port, omvb, buffer, omvb->mmal_buffer, buffer->length, buffer->cmd, buffer->flags, m_inflight);
  assert(buffer == omvb->mmal_buffer);
  m_inflight--;
  omvb->Release();
}

static void vout_input_port_cb_static(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
  CMMALRenderer *mmal = reinterpret_cast<CMMALRenderer*>(port->userdata);
  mmal->vout_input_port_cb(port, buffer);
}

bool CMMALRenderer::init_vout(ERenderFormat format, bool opaque)
{
  CSingleLock lock(m_sharedSection);
  bool formatChanged = m_format != format || m_opaque != opaque;
  MMAL_STATUS_T status;

  CLog::Log(LOGDEBUG, "%s::%s configured:%d format %d->%d opaque %d->%d", CLASSNAME, __func__, m_bConfigured, m_format, format, m_opaque, opaque);

  if (m_bMMALConfigured && formatChanged)
    UnInitMMAL();

  if (m_bMMALConfigured)
    return true;

  m_format = format;
  m_opaque = opaque;

  /* Create video renderer */
  status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER, &m_vout);
  if(status != MMAL_SUCCESS)
  {
    CLog::Log(LOGERROR, "%s::%s Failed to create vout component (status=%x %s)", CLASSNAME, __func__, status, mmal_status_to_string(status));
    return false;
  }

  m_vout_input = m_vout->input[0];
  m_vout_input->userdata = (struct MMAL_PORT_USERDATA_T *)this;
  MMAL_ES_FORMAT_T *es_format = m_vout_input->format;

  es_format->type = MMAL_ES_TYPE_VIDEO;
  if (CONF_FLAGS_YUVCOEF_MASK(m_iFlags) == CONF_FLAGS_YUVCOEF_BT709)
    es_format->es->video.color_space = MMAL_COLOR_SPACE_ITUR_BT709;
  else if (CONF_FLAGS_YUVCOEF_MASK(m_iFlags) == CONF_FLAGS_YUVCOEF_BT601)
    es_format->es->video.color_space = MMAL_COLOR_SPACE_ITUR_BT601;
  else if (CONF_FLAGS_YUVCOEF_MASK(m_iFlags) == CONF_FLAGS_YUVCOEF_240M)
    es_format->es->video.color_space = MMAL_COLOR_SPACE_SMPTE240M;

  es_format->es->video.crop.width = m_sourceWidth;
  es_format->es->video.crop.height = m_sourceHeight;
  es_format->es->video.width = m_sourceWidth;
  es_format->es->video.height = m_sourceHeight;

  es_format->encoding = m_opaque ? MMAL_ENCODING_OPAQUE : MMAL_ENCODING_I420;

  status = mmal_port_parameter_set_boolean(m_vout_input, MMAL_PARAMETER_ZERO_COPY,  MMAL_TRUE);
  if (status != MMAL_SUCCESS)
     CLog::Log(LOGERROR, "%s::%s Failed to enable zero copy mode on %s (status=%x %s)", CLASSNAME, __func__, m_vout_input->name, status, mmal_status_to_string(status));

  status = mmal_port_format_commit(m_vout_input);
  if (status != MMAL_SUCCESS)
  {
    CLog::Log(LOGERROR, "%s::%s Failed to commit vout input format (status=%x %s)", CLASSNAME, __func__, status, mmal_status_to_string(status));
    return false;
  }

  m_vout_input->buffer_num = std::max(m_vout_input->buffer_num_recommended, (uint32_t)m_NumYV12Buffers+(m_opaque?0:32));
  m_vout_input->buffer_size = m_vout_input->buffer_size_recommended;

  status = mmal_port_enable(m_vout_input, vout_input_port_cb_static);
  if(status != MMAL_SUCCESS)
  {
    CLog::Log(LOGERROR, "%s::%s Failed to vout enable input port (status=%x %s)", CLASSNAME, __func__, status, mmal_status_to_string(status));
    return false;
  }

  status = mmal_component_enable(m_vout);
  if(status != MMAL_SUCCESS)
  {
    CLog::Log(LOGERROR, "%s::%s Failed to enable vout component (status=%x %s)", CLASSNAME, __func__, status, mmal_status_to_string(status));
    return false;
  }

  CLog::Log(LOGDEBUG, "%s::%s Created pool of size %d x %d", CLASSNAME, __func__, m_vout_input->buffer_num, m_vout_input->buffer_size);
  m_vout_input_pool = mmal_port_pool_create(m_vout_input , m_vout_input->buffer_num, m_opaque ? m_vout_input->buffer_size:0);
  if (!m_vout_input_pool)
  {
    CLog::Log(LOGERROR, "%s::%s Failed to create pool for decoder input port (status=%x %s)", CLASSNAME, __func__, status, mmal_status_to_string(status));
    return false;
  }
  return true;
}

CMMALRenderer::CMMALRenderer() : CThread("MMALRenderer")
{
  CLog::Log(LOGDEBUG, "%s::%s", CLASSNAME, __func__);
  m_vout = NULL;
  m_vout_input = NULL;
  m_vout_input_pool = NULL;
  memset(m_buffers, 0, sizeof m_buffers);
  m_iFlags = 0;
  m_format = RENDER_FMT_NONE;
  m_opaque = true;
  m_bConfigured = false;
  m_bMMALConfigured = false;
  m_iYV12RenderBuffer = 0;
  m_inflight = 0;
  m_queue = mmal_queue_create();
  m_error = 0.0;
  Create();
}

CMMALRenderer::~CMMALRenderer()
{
  CSingleLock lock(m_sharedSection);
  CLog::Log(LOGDEBUG, "%s::%s", CLASSNAME, __func__);
  StopThread(true);
  mmal_queue_destroy(m_queue);
  UnInit();
}


void CMMALRenderer::Process()
{
  SetPriority(THREAD_PRIORITY_ABOVE_NORMAL);
  while (!m_bStop)
  {
    g_RBP.WaitVsync();
    double dfps = g_graphicsContext.GetFPS();
    if (dfps <= 0.0)
      dfps = m_fps;
    // This algorithm is basically making the decision according to Bresenham's line algorithm.  Imagine drawing a line where x-axis is display frames, and y-axis is video frames
    m_error += m_fps / dfps;
    // we may need to discard frames if queue length gets too high or video frame rate is above display frame rate
    while (mmal_queue_length(m_queue) > 2 || m_error > 1.0)
    {
      if (m_error > 1.0)
        m_error -= 1.0;
      MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(m_queue);
      if (buffer)
      {
        CMMALBuffer *omvb = (CMMALBuffer *)buffer->user_data;
        assert(buffer == omvb->mmal_buffer);
        m_inflight--;
        omvb->Release();
        if (g_advancedSettings.CanLogComponent(LOGVIDEO))
          CLog::Log(LOGDEBUG, "%s::%s - discard buffer:%p vsync:%d queue:%d diff:%f", CLASSNAME, __func__, buffer, g_RBP.LastVsync(), mmal_queue_length(m_queue), m_error);
      }
    }
    // this is case where we would like to display a new frame
    if (m_error > 0.0)
    {
      m_error -= 1.0;
      MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(m_queue);
      if (buffer)
        mmal_port_send_buffer(m_vout_input, buffer);
      if (g_advancedSettings.CanLogComponent(LOGVIDEO))
        CLog::Log(LOGDEBUG, "%s::%s - buffer:%p vsync:%d queue:%d diff:%f", CLASSNAME, __func__, buffer, g_RBP.LastVsync(), mmal_queue_length(m_queue), m_error);
    }
  }
}

void CMMALRenderer::AddVideoPictureHW(DVDVideoPicture& pic, int index)
{
  if (m_format != RENDER_FMT_MMAL)
  {
    assert(0);
    return;
  }

  CMMALBuffer *buffer = pic.MMALBuffer;
  assert(buffer);
  if (g_advancedSettings.CanLogComponent(LOGVIDEO))
    CLog::Log(LOGDEBUG, "%s::%s MMAL - %p (%p) %i", CLASSNAME, __func__, buffer, buffer->mmal_buffer, index);

  m_buffers[index] = buffer->Acquire();
}

bool CMMALRenderer::Configure(unsigned int width, unsigned int height, unsigned int d_width, unsigned int d_height, float fps, unsigned flags, ERenderFormat format, unsigned extended_format, unsigned int orientation)
{
  CSingleLock lock(m_sharedSection);
  ReleaseBuffers();

  m_sourceWidth  = width;
  m_sourceHeight = height;
  m_renderOrientation = orientation;

  m_fps = fps;
  m_iFlags = flags;

  // cause SetVideoRect to trigger - needed after a hdmi mode change
  m_src_rect.SetRect(0, 0, 0, 0);
  m_dst_rect.SetRect(0, 0, 0, 0);

  CLog::Log(LOGDEBUG, "%s::%s - %dx%d->%dx%d@%.2f flags:%x format:%d ext:%x orient:%d", CLASSNAME, __func__, width, height, d_width, d_height, fps, flags, format, extended_format, orientation);
  if (format != RENDER_FMT_BYPASS && format != RENDER_FMT_MMAL)
  {
    CLog::Log(LOGERROR, "%s::%s - format:%d not supported", CLASSNAME, __func__, format);
    return false;
  }

  // calculate the input frame aspect ratio
  CalculateFrameAspectRatio(d_width, d_height);
  SetViewMode(CMediaSettings::GetInstance().GetCurrentVideoSettings().m_ViewMode);
  ManageRenderArea();

  m_bMMALConfigured = init_vout(format, m_opaque);
  m_bConfigured = m_bMMALConfigured;
  assert(m_bConfigured);
  return m_bConfigured;
}

int CMMALRenderer::GetImage(YV12Image *image, int source, bool readonly)
{
  if (!image || source < 0 || m_format != RENDER_FMT_MMAL)
  {
    if (g_advancedSettings.CanLogComponent(LOGVIDEO))
      CLog::Log(LOGDEBUG, "%s::%s - invalid: format:%d image:%p source:%d ro:%d flight:%d", CLASSNAME, __func__, m_format, image, source, readonly, m_inflight);
    return -1;
  }
  if (g_advancedSettings.CanLogComponent(LOGVIDEO))
    CLog::Log(LOGDEBUG, "%s::%s - MMAL: image:%p source:%d ro:%d flight:%d", CLASSNAME, __func__, image, source, readonly, m_inflight);
  return source;
}

void CMMALRenderer::ReleaseBuffer(int idx)
{
  CSingleLock lock(m_sharedSection);
  if (!m_bMMALConfigured || m_format != RENDER_FMT_MMAL)
  {
    if (g_advancedSettings.CanLogComponent(LOGVIDEO))
      CLog::Log(LOGDEBUG, "%s::%s - not configured: source:%d", CLASSNAME, __func__, idx);
    return;
  }

  CMMALBuffer *omvb = m_buffers[idx];
  if (g_advancedSettings.CanLogComponent(LOGVIDEO))
    CLog::Log(LOGDEBUG, "%s::%s - MMAL: source:%d omvb:%p mmal:%p flight:%d", CLASSNAME, __func__, idx, omvb, omvb ? omvb->mmal_buffer:NULL, m_inflight);
  if (omvb)
    SAFE_RELEASE(m_buffers[idx]);
}

void CMMALRenderer::ReleaseImage(int source, bool preserve)
{
}

void CMMALRenderer::Reset()
{
  CLog::Log(LOGDEBUG, "%s::%s", CLASSNAME, __func__);
}

void CMMALRenderer::Flush()
{
  m_iYV12RenderBuffer = 0;
  CLog::Log(LOGDEBUG, "%s::%s", CLASSNAME, __func__);
}

void CMMALRenderer::Update()
{
  if (g_advancedSettings.CanLogComponent(LOGVIDEO))
    CLog::Log(LOGDEBUG, "%s::%s", CLASSNAME, __func__);
  if (!m_bConfigured) return;
  ManageRenderArea();
}

void CMMALRenderer::RenderUpdate(bool clear, DWORD flags, DWORD alpha)
{
  CSingleLock lock(m_sharedSection);
  int source = m_iYV12RenderBuffer;
  CMMALBuffer *omvb = nullptr;

  if (!m_bConfigured)
  {
    if (g_advancedSettings.CanLogComponent(LOGVIDEO))
      CLog::Log(LOGDEBUG, "%s::%s - not configured: clear:%d flags:%x alpha:%d source:%d", CLASSNAME, __func__, clear, flags, alpha, source);
    goto exit;
  }

  if (m_format == RENDER_FMT_MMAL)
    omvb = m_buffers[source];

  // we only want to upload frames once
  if (omvb && omvb->mmal_buffer && omvb->mmal_buffer->flags & MMAL_BUFFER_HEADER_FLAG_USER1)
  {
    if (g_advancedSettings.CanLogComponent(LOGVIDEO))
      CLog::Log(LOGDEBUG, "%s::%s - MMAL: clear:%d flags:%x alpha:%d source:%d omvb:%p mmal:%p mflags:%x skipping", CLASSNAME, __func__, clear, flags, alpha, source, omvb, omvb->mmal_buffer, omvb->mmal_buffer->flags);
    goto exit;
  }

  ManageRenderArea();

  if (m_format != RENDER_FMT_MMAL)
  {
    if (g_advancedSettings.CanLogComponent(LOGVIDEO))
      CLog::Log(LOGDEBUG, "%s::%s - bypass: clear:%d flags:%x alpha:%d source:%d format:%d", CLASSNAME, __func__, clear, flags, alpha, source, m_format);
    goto exit;
  }
  SetVideoRect(m_sourceRect, m_destRect);

  if (omvb && omvb->mmal_buffer)
  {
    if (g_advancedSettings.CanLogComponent(LOGVIDEO))
      CLog::Log(LOGDEBUG, "%s::%s - MMAL: clear:%d flags:%x alpha:%d source:%d omvb:%p mmal:%p mflags:%x", CLASSNAME, __func__, clear, flags, alpha, source, omvb, omvb->mmal_buffer, omvb->mmal_buffer->flags);
    // check for changes in aligned sizes
    if (omvb->m_width != (uint32_t)m_vout_input->format->es->video.crop.width || omvb->m_height != (uint32_t)m_vout_input->format->es->video.crop.height ||
        omvb->m_aligned_width != m_vout_input->format->es->video.width || omvb->m_aligned_height != m_vout_input->format->es->video.height)
    {
      CLog::Log(LOGDEBUG, "%s::%s Changing dimensions from %dx%d (%dx%d) to %dx%d (%dx%d)", CLASSNAME, __func__,
          m_vout_input->format->es->video.crop.width, m_vout_input->format->es->video.crop.height, omvb->m_width, omvb->m_height,
          m_vout_input->format->es->video.width, m_vout_input->format->es->video.height, omvb->m_aligned_width, omvb->m_aligned_height);
      m_vout_input->format->es->video.width = omvb->m_aligned_width;
      m_vout_input->format->es->video.height = omvb->m_aligned_height;
      m_vout_input->format->es->video.crop.width = omvb->m_width;
      m_vout_input->format->es->video.crop.height = omvb->m_height;
      MMAL_STATUS_T status = mmal_port_format_commit(m_vout_input);
      if (status != MMAL_SUCCESS)
      {
        CLog::Log(LOGERROR, "%s::%s Failed to commit vout input format (status=%x %s)", CLASSNAME, __func__, status, mmal_status_to_string(status));
        goto exit;
      }
    }
    m_inflight++;
    assert(omvb->mmal_buffer && omvb->mmal_buffer->data && omvb->mmal_buffer->length);
    omvb->Acquire();
    omvb->mmal_buffer->flags |= MMAL_BUFFER_HEADER_FLAG_USER1 | MMAL_BUFFER_HEADER_FLAG_USER2;
    omvb->mmal_buffer->user_data = omvb;
    if (!CSettings::GetInstance().GetBool("videoplayer.usedisplayasclock") && m_fps > 0.0f)
      mmal_queue_put(m_queue, omvb->mmal_buffer);
    else
      mmal_port_send_buffer(m_vout_input, omvb->mmal_buffer);
  }
  else
    CLog::Log(LOGDEBUG, "%s::%s - MMAL: No buffer to update clear:%d flags:%x alpha:%d source:%d omvb:%p mmal:%p", CLASSNAME, __func__, clear, flags, alpha, source, omvb, omvb ? omvb->mmal_buffer : nullptr);

exit:
   lock.Leave();
   g_RBP.WaitVsync();
}

void CMMALRenderer::FlipPage(int source)
{
  CSingleLock lock(m_sharedSection);
  if (!m_bConfigured || m_format != RENDER_FMT_MMAL)
  {
    if (g_advancedSettings.CanLogComponent(LOGVIDEO))
      CLog::Log(LOGDEBUG, "%s::%s - not configured: source:%d format:%d", CLASSNAME, __func__, source, m_format);
    return;
  }

  if (g_advancedSettings.CanLogComponent(LOGVIDEO))
    CLog::Log(LOGDEBUG, "%s::%s - source:%d", CLASSNAME, __func__, source);

  m_iYV12RenderBuffer = source;
}

void CMMALRenderer::PreInit()
{
  CSingleLock lock(m_sharedSection);
  m_bConfigured = false;
  UnInit();

  m_iFlags = 0;

  CLog::Log(LOGDEBUG, "%s::%s", CLASSNAME, __func__);

  m_formats.clear();
  m_formats.push_back(RENDER_FMT_MMAL);
  m_formats.push_back(RENDER_FMT_BYPASS);

  memset(m_buffers, 0, sizeof m_buffers);
  m_iYV12RenderBuffer = 0;
  m_NumYV12Buffers = NUM_BUFFERS;
}

void CMMALRenderer::ReleaseBuffers()
{
  if (g_advancedSettings.CanLogComponent(LOGVIDEO))
    CLog::Log(LOGDEBUG, "%s::%s", CLASSNAME, __func__);
  for (int i=0; i<NUM_BUFFERS; i++)
    ReleaseBuffer(i);
}

void CMMALRenderer::UnInitMMAL()
{
  CSingleLock lock(m_sharedSection);
  CLog::Log(LOGDEBUG, "%s::%s pool(%p)", CLASSNAME, __func__, m_vout_input_pool);
  if (m_vout)
  {
    mmal_component_disable(m_vout);
  }

  if (m_vout_input)
  {
    mmal_port_flush(m_vout_input);
    mmal_port_disable(m_vout_input);
  }

  ReleaseBuffers();

  if (m_vout_input_pool)
  {
    mmal_port_pool_destroy(m_vout_input, m_vout_input_pool);
    m_vout_input_pool = NULL;
  }
  m_vout_input = NULL;

  if (m_vout)
  {
    mmal_component_release(m_vout);
    m_vout = NULL;
  }

  m_src_rect.SetRect(0, 0, 0, 0);
  m_dst_rect.SetRect(0, 0, 0, 0);
  m_video_stereo_mode = RENDER_STEREO_MODE_OFF;
  m_display_stereo_mode = RENDER_STEREO_MODE_OFF;
  m_StereoInvert = false;
  m_format = RENDER_FMT_NONE;

  m_bConfigured = false;
  m_bMMALConfigured = false;
}

void CMMALRenderer::UnInit()
{
  UnInitMMAL();
}

bool CMMALRenderer::RenderCapture(CRenderCapture* capture)
{
  if (!m_bConfigured)
    return false;

  CLog::Log(LOGDEBUG, "%s::%s - %p", CLASSNAME, __func__, capture);

  capture->BeginRender();
  capture->EndRender();

  return true;
}

//********************************************************************************************************
// YV12 Texture creation, deletion, copying + clearing
//********************************************************************************************************

bool CMMALRenderer::Supports(EDEINTERLACEMODE mode)
{
  if(mode == VS_DEINTERLACEMODE_OFF
  || mode == VS_DEINTERLACEMODE_AUTO
  || mode == VS_DEINTERLACEMODE_FORCE)
    return true;

  return false;
}

bool CMMALRenderer::Supports(EINTERLACEMETHOD method)
{
  if (method == VS_INTERLACEMETHOD_AUTO)
    return true;
  if (method == VS_INTERLACEMETHOD_MMAL_ADVANCED)
    return true;
  if (method == VS_INTERLACEMETHOD_MMAL_ADVANCED_HALF)
    return true;
  if (method == VS_INTERLACEMETHOD_MMAL_BOB)
    return true;
  if (method == VS_INTERLACEMETHOD_MMAL_BOB_HALF)
    return true;

  return false;
}

bool CMMALRenderer::Supports(ERENDERFEATURE feature)
{
  if (feature == RENDERFEATURE_STRETCH         ||
      feature == RENDERFEATURE_ZOOM            ||
      feature == RENDERFEATURE_ROTATION        ||
      feature == RENDERFEATURE_VERTICAL_SHIFT  ||
      feature == RENDERFEATURE_PIXEL_RATIO)
    return true;

  return false;
}

bool CMMALRenderer::Supports(ESCALINGMETHOD method)
{
  return false;
}

EINTERLACEMETHOD CMMALRenderer::AutoInterlaceMethod()
{
  return m_sourceWidth * m_sourceHeight <= 576 * 720 ? VS_INTERLACEMETHOD_MMAL_ADVANCED : VS_INTERLACEMETHOD_MMAL_BOB;
}

void CMMALRenderer::SetVideoRect(const CRect& InSrcRect, const CRect& InDestRect)
{
  CSingleLock lock(m_sharedSection);
  assert(g_graphicsContext.GetStereoView() != RENDER_STEREO_VIEW_RIGHT);

  if (!m_vout_input)
    return;

  CRect SrcRect = InSrcRect, DestRect = InDestRect;
  RENDER_STEREO_MODE video_stereo_mode = (m_iFlags & CONF_FLAGS_STEREO_MODE_SBS) ? RENDER_STEREO_MODE_SPLIT_VERTICAL :
                                         (m_iFlags & CONF_FLAGS_STEREO_MODE_TAB) ? RENDER_STEREO_MODE_SPLIT_HORIZONTAL : RENDER_STEREO_MODE_OFF;
  bool stereo_invert                   = (m_iFlags & CONF_FLAGS_STEREO_CADANCE_RIGHT_LEFT) ? true : false;
  RENDER_STEREO_MODE display_stereo_mode = g_graphicsContext.GetStereoMode();

  // ignore video stereo mode when 3D display mode is disabled
  if (display_stereo_mode == RENDER_STEREO_MODE_OFF)
    video_stereo_mode = RENDER_STEREO_MODE_OFF;

  // fix up transposed video
  if (m_renderOrientation == 90 || m_renderOrientation == 270)
  {
    float diff = (DestRect.Height() - DestRect.Width()) * 0.5f;
    DestRect.x1 -= diff;
    DestRect.x2 += diff;
    DestRect.y1 += diff;
    DestRect.y2 -= diff;
  }

  // check if destination rect or video view mode has changed
  if (!(m_dst_rect != DestRect) && !(m_src_rect != SrcRect) && m_video_stereo_mode == video_stereo_mode && m_display_stereo_mode == display_stereo_mode && m_StereoInvert == stereo_invert)
    return;

  CLog::Log(LOGDEBUG, "%s::%s %d,%d,%d,%d -> %d,%d,%d,%d (o:%d v:%d d:%d i:%d)", CLASSNAME, __func__,
      (int)SrcRect.x1, (int)SrcRect.y1, (int)SrcRect.x2, (int)SrcRect.y2,
      (int)DestRect.x1, (int)DestRect.y1, (int)DestRect.x2, (int)DestRect.y2,
      m_renderOrientation, video_stereo_mode, display_stereo_mode, stereo_invert);

  m_src_rect = SrcRect;
  m_dst_rect = DestRect;
  m_video_stereo_mode = video_stereo_mode;
  m_display_stereo_mode = display_stereo_mode;
  m_StereoInvert = stereo_invert;

  // might need to scale up m_dst_rect to display size as video decodes
  // to separate video plane that is at display size.
  RESOLUTION res = g_graphicsContext.GetVideoResolution();
  CRect gui(0, 0, CDisplaySettings::GetInstance().GetResolutionInfo(res).iWidth, CDisplaySettings::GetInstance().GetResolutionInfo(res).iHeight);
  CRect display(0, 0, CDisplaySettings::GetInstance().GetResolutionInfo(res).iScreenWidth, CDisplaySettings::GetInstance().GetResolutionInfo(res).iScreenHeight);

  if (display_stereo_mode == RENDER_STEREO_MODE_SPLIT_VERTICAL)
  {
    float width = DestRect.x2 - DestRect.x1;
    DestRect.x1 *= 2.0f;
    DestRect.x2 = DestRect.x1 + 2.0f * width;
  }
  else if (display_stereo_mode == RENDER_STEREO_MODE_SPLIT_HORIZONTAL)
  {
    float height = DestRect.y2 - DestRect.y1;
    DestRect.y1 *= 2.0f;
    DestRect.y2 = DestRect.y1 + 2.0f * height;
  }

  if (gui != display)
  {
    float xscale = display.Width()  / gui.Width();
    float yscale = display.Height() / gui.Height();
    DestRect.x1 *= xscale;
    DestRect.x2 *= xscale;
    DestRect.y1 *= yscale;
    DestRect.y2 *= yscale;
  }

  MMAL_DISPLAYREGION_T region;
  memset(&region, 0, sizeof region);

  region.set                 = MMAL_DISPLAY_SET_DEST_RECT|MMAL_DISPLAY_SET_SRC_RECT|MMAL_DISPLAY_SET_FULLSCREEN|MMAL_DISPLAY_SET_NOASPECT|MMAL_DISPLAY_SET_MODE|MMAL_DISPLAY_SET_TRANSFORM;
  region.dest_rect.x         = lrintf(DestRect.x1);
  region.dest_rect.y         = lrintf(DestRect.y1);
  region.dest_rect.width     = lrintf(DestRect.Width());
  region.dest_rect.height    = lrintf(DestRect.Height());

  region.src_rect.x          = lrintf(SrcRect.x1);
  region.src_rect.y          = lrintf(SrcRect.y1);
  region.src_rect.width      = lrintf(SrcRect.Width());
  region.src_rect.height     = lrintf(SrcRect.Height());

  region.fullscreen = MMAL_FALSE;
  region.noaspect = MMAL_TRUE;
  region.mode = MMAL_DISPLAY_MODE_LETTERBOX;

  if (m_renderOrientation == 90)
    region.transform = MMAL_DISPLAY_ROT90;
  else if (m_renderOrientation == 180)
    region.transform = MMAL_DISPLAY_ROT180;
  else if (m_renderOrientation == 270)
    region.transform = MMAL_DISPLAY_ROT270;
  else
    region.transform = MMAL_DISPLAY_ROT0;

  if (m_video_stereo_mode == RENDER_STEREO_MODE_SPLIT_HORIZONTAL)
    region.transform = (MMAL_DISPLAYTRANSFORM_T)(region.transform | DISPMANX_STEREOSCOPIC_TB);
  else if (m_video_stereo_mode == RENDER_STEREO_MODE_SPLIT_VERTICAL)
    region.transform = (MMAL_DISPLAYTRANSFORM_T)(region.transform | DISPMANX_STEREOSCOPIC_SBS);
  else
    region.transform = (MMAL_DISPLAYTRANSFORM_T)(region.transform | DISPMANX_STEREOSCOPIC_MONO);

  if (m_StereoInvert)
    region.transform = (MMAL_DISPLAYTRANSFORM_T)(region.transform | DISPMANX_STEREOSCOPIC_INVERT);

  MMAL_STATUS_T status = mmal_util_set_display_region(m_vout_input, &region);
  if (status != MMAL_SUCCESS)
    CLog::Log(LOGERROR, "%s::%s Failed to set display region (status=%x %s)", CLASSNAME, __func__, status, mmal_status_to_string(status));

  CLog::Log(LOGDEBUG, "%s::%s %d,%d,%d,%d -> %d,%d,%d,%d t:%x", CLASSNAME, __func__,
      region.src_rect.x, region.src_rect.y, region.src_rect.width, region.src_rect.height,
      region.dest_rect.x, region.dest_rect.y, region.dest_rect.width, region.dest_rect.height, region.transform);
}
