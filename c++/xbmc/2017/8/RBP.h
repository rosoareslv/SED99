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

#ifndef USE_VCHIQ_ARM
#define USE_VCHIQ_ARM
#endif
#ifndef __VIDEOCORE4__
#define __VIDEOCORE4__
#endif
#ifndef HAVE_VMCS_CONFIG
#define HAVE_VMCS_CONFIG
#endif

#if !defined(TARGET_WINDOWS)
#define DECLARE_UNUSED(a,b) a __attribute__((unused)) b;
#endif

#include "DllBCM.h"
#include "OMXCore.h"
#include "xbmc/utils/CPUInfo.h"
#include "threads/CriticalSection.h"
#include "threads/Event.h"


typedef struct AVRpiZcFrameGeometry
{
  unsigned int stride_y;
  unsigned int height_y;
  unsigned int stride_c;
  unsigned int height_c;
  unsigned int planes_c;
  unsigned int stripes;
  unsigned int bytes_per_pixel;
} AVRpiZcFrameGeometry;

class CGPUMEM
{
public:
  CGPUMEM(unsigned int numbytes, bool cached = true);
  ~CGPUMEM();
  void Flush();
  void *m_arm = nullptr; // Pointer to memory mapped on ARM side
  int m_vc_handle = 0;   // Videocore handle of relocatable memory
  int m_vcsm_handle = 0; // Handle for use by VCSM
  unsigned int m_vc = 0;       // Address for use in GPU code
  unsigned int m_numbytes = 0; // Size of memory block
  void *m_opaque = nullptr;
};

class CRBP
{
public:
  CRBP();
  ~CRBP();

  bool Initialize();
  void LogFirmwareVersion();
  void Deinitialize();
  int GetArmMem() { return m_arm_mem; }
  int GetGpuMem() { return m_gpu_mem; }
  bool GetCodecMpg2() { return m_codec_mpg2_enabled; }
  int RaspberryPiVersion() { return g_cpuInfo.getCPUCount() == 1 ? 1 : 2; };
  bool GetCodecWvc1() { return m_codec_wvc1_enabled; }
  void GetDisplaySize(int &width, int &height);
  DISPMANX_DISPLAY_HANDLE_T OpenDisplay(uint32_t device);
  void CloseDisplay(DISPMANX_DISPLAY_HANDLE_T display);
  int GetGUIResolutionLimit() { return m_gui_resolution_limit; }
  // stride can be null for packed output
  unsigned char *CaptureDisplay(int width, int height, int *stride, bool swap_red_blue, bool video_only = true);
  DllOMX *GetDllOMX() { return m_OMX ? m_OMX->GetDll() : NULL; }
  uint32_t LastVsync(int64_t &time);
  uint32_t LastVsync();
  uint32_t WaitVsync(uint32_t target = ~0U);
  void VSyncCallback();
  int GetMBox() { return m_mb; }
  AVRpiZcFrameGeometry GetFrameGeometry(uint32_t encoding, unsigned short video_width, unsigned short video_height);

private:
  DllBcmHost *m_DllBcmHost;
  bool       m_initialized;
  bool       m_omx_initialized;
  bool       m_omx_image_init;
  int        m_arm_mem;
  int        m_gpu_mem;
  int        m_gui_resolution_limit;
  bool       m_codec_mpg2_enabled;
  bool       m_codec_wvc1_enabled;
  COMXCore   *m_OMX;
  DISPMANX_DISPLAY_HANDLE_T m_display;
  CCriticalSection m_vsync_lock;
  XbmcThreads::ConditionVariable m_vsync_cond;
  uint32_t m_vsync_count;
  int64_t m_vsync_time;
  class DllLibOMXCore;
  CCriticalSection m_critSection;

  int m_mb;
};

extern CRBP g_RBP;
