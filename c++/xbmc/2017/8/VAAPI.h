/*
 *      Copyright (C) 2005-2014 Team XBMC
 *      http://xbmc.org
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */
#pragma once

#include "DVDVideoCodec.h"
#include "cores/VideoPlayer/Process/VideoBuffer.h"
#include "settings/VideoSettings.h"
#include "threads/CriticalSection.h"
#include "threads/SharedSection.h"
#include "threads/Event.h"
#include "threads/Thread.h"
#include "utils/ActorProtocol.h"
#include "guilib/Geometry.h"
#include <list>
#include <map>
#include <memory>
#include <vector>
#include <va/va.h>
#include "linux/sse4/DllLibSSE4.h"

extern "C" {
#include "libavutil/avutil.h"
#include "libavfilter/avfilter.h"
}

using namespace Actor;

class CProcessInfo;

#define FULLHD_WIDTH                       1920

namespace VAAPI
{

//-----------------------------------------------------------------------------
// VAAPI data structs
//-----------------------------------------------------------------------------

class CDecoder;

/**
 * Buffer statistics used to control number of frames in queue
 */

class CVaapiBufferStats
{
public:
  uint16_t decodedPics;
  uint16_t processedPics;
  uint16_t renderPics;
  uint64_t latency;         // time decoder has waited for a frame, ideally there is no latency
  int codecFlags;
  bool canSkipDeint;
  int processCmd;
  bool isVpp;

  void IncDecoded() { CSingleLock l(m_sec); decodedPics++;}
  void DecDecoded() { CSingleLock l(m_sec); decodedPics--;}
  void IncProcessed() { CSingleLock l(m_sec); processedPics++;}
  void DecProcessed() { CSingleLock l(m_sec); processedPics--;}
  void IncRender() { CSingleLock l(m_sec); renderPics++;}
  void DecRender() { CSingleLock l(m_sec); renderPics--;}
  void Reset() { CSingleLock l(m_sec); decodedPics=0; processedPics=0;renderPics=0;latency=0;isVpp=false;}
  void Get(uint16_t &decoded, uint16_t &processed, uint16_t &render, bool &vpp) {CSingleLock l(m_sec); decoded = decodedPics, processed=processedPics, render=renderPics; vpp=isVpp;}
  void SetParams(uint64_t time, int flags) { CSingleLock l(m_sec); latency = time; codecFlags = flags; }
  void GetParams(uint64_t &lat, int &flags) { CSingleLock l(m_sec); lat = latency; flags = codecFlags; }
  void SetCmd(int cmd) { CSingleLock l(m_sec); processCmd = cmd; }
  void GetCmd(int &cmd) { CSingleLock l(m_sec); cmd = processCmd; processCmd = 0; }
  void SetCanSkipDeint(bool canSkip) { CSingleLock l(m_sec); canSkipDeint = canSkip; }
  bool CanSkipDeint() { CSingleLock l(m_sec); if (canSkipDeint) return true; else return false;}
  void SetVpp(bool vpp) {CSingleLock l(m_sec); isVpp = vpp;}
private:
  CCriticalSection m_sec;
};

/**
 *  CVaapiConfig holds all configuration parameters needed by vaapi
 *  The structure is sent to the internal classes CMixer and COutput
 *  for init.
 */

class CVideoSurfaces;
class CVAAPIContext;

struct CVaapiConfig
{
  int surfaceWidth;
  int surfaceHeight;
  int vidWidth;
  int vidHeight;
  int outWidth;
  int outHeight;
  AVRational aspect;
  VAConfigID configId;
  CVaapiBufferStats *stats;
  int upscale;
  CVideoSurfaces *videoSurfaces;
  uint32_t maxReferences;
  CVAAPIContext *context;
  VADisplay dpy;
  VAProfile profile;
  VAConfigAttrib attrib;
  CProcessInfo *processInfo;
};

/**
 * Holds a decoded frame
 * Input to COutput for further processing
 */
struct CVaapiDecodedPicture
{
  CVaapiDecodedPicture() = default;
  CVaapiDecodedPicture(const CVaapiDecodedPicture &rhs)
  {
    *this = rhs;
  }
  CVaapiDecodedPicture& operator=(const CVaapiDecodedPicture& rhs)
  {
    DVDPic.SetParams(rhs.DVDPic);
    videoSurface = rhs.videoSurface;
    index = rhs.index;
    return *this;
  };
  VideoPicture DVDPic;
  VASurfaceID videoSurface;
  int index;
};

/**
 * Frame after having been processed by vpp
 */
struct CVaapiProcessedPicture
{
  CVaapiProcessedPicture() = default;
  CVaapiProcessedPicture(const CVaapiProcessedPicture &rhs)
  {
    *this = rhs;
  }
  CVaapiProcessedPicture& operator=(const CVaapiProcessedPicture& rhs)
  {
    DVDPic.SetParams(rhs.DVDPic);
    videoSurface = rhs.videoSurface;
    frame = rhs.frame;
    id = rhs.id;
    source = rhs.source;
    crop = rhs.crop;
    return *this;
  };

  VideoPicture DVDPic;
  VASurfaceID videoSurface;
  AVFrame *frame;
  int id;
  enum
  {
    VPP_SRC,
    FFMPEG_SRC,
    SKIP_SRC
  }source;
  bool crop;
};

class CVaapiRenderPicture : public CVideoBuffer
{
public:
  explicit CVaapiRenderPicture(int id) : CVideoBuffer(id) { }
  VideoPicture DVDPic;
  CVaapiProcessedPicture procPic;
  AVFrame *avFrame = nullptr;

  bool valid = false;
  VADisplay vadsp;
};

//-----------------------------------------------------------------------------
// Output
//-----------------------------------------------------------------------------

class COutputControlProtocol : public Protocol
{
public:
  COutputControlProtocol(std::string name, CEvent* inEvent, CEvent *outEvent) : Protocol(name, inEvent, outEvent) {};
  enum OutSignal
  {
    INIT,
    FLUSH,
    PRECLEANUP,
    TIMEOUT,
  };
  enum InSignal
  {
    ACC,
    ERROR,
    STATS,
  };
};

class COutputDataProtocol : public Protocol
{
public:
  COutputDataProtocol(std::string name, CEvent* inEvent, CEvent *outEvent) : Protocol(name, inEvent, outEvent) {};
  enum OutSignal
  {
    NEWFRAME = 0,
    RETURNPIC,
    RETURNPROCPIC,
  };
  enum InSignal
  {
    PICTURE,
  };
};

struct SDiMethods
{
  EINTERLACEMETHOD diMethods[8];
  int numDiMethods;
};

/**
 * COutput is embedded in CDecoder and embeds vpp
 * The class has its own OpenGl context which is shared with render thread
 * COutput generated ready to render textures and passes them back to
 * CDecoder
 */

class CDecoder;
class CPostproc;
class CVaapiBufferPool;

class COutput : private CThread
{
public:
  COutput(CDecoder &decoder, CEvent *inMsgEvent);
  ~COutput() override;
  void Start();
  void Dispose();
  COutputControlProtocol m_controlPort;
  COutputDataProtocol m_dataPort;
protected:
  void OnStartup() override;
  void OnExit() override;
  void Process() override;
  void StateMachine(int signal, Protocol *port, Message *msg);
  bool HasWork();
  bool PreferPP();
  void InitCycle();
  CVaapiRenderPicture* ProcessPicture(CVaapiProcessedPicture &pic);
  void QueueReturnPicture(CVaapiRenderPicture *pic);
  void ProcessReturnPicture(CVaapiRenderPicture *pic);
  void ProcessReturnProcPicture(int id);
  void ProcessSyncPicture();
  void ReleaseProcessedPicture(CVaapiProcessedPicture &pic);
  void DropVppProcessedPictures();
  bool Init();
  bool Uninit();
  void Flush();
  void EnsureBufferPool();
  void ReleaseBufferPool(bool precleanup = false);
  bool CheckSuccess(VAStatus status);
  CEvent m_outMsgEvent;
  CEvent *m_inMsgEvent;
  int m_state;
  bool m_bStateMachineSelfTrigger;
  CDecoder &m_vaapi;

  // extended state variables for state machine
  int m_extTimeout;
  bool m_vaError;
  CVaapiConfig m_config;
  std::shared_ptr<CVaapiBufferPool> m_bufferPool;
  CVaapiDecodedPicture m_currentPicture;
  CPostproc *m_pp;
  SDiMethods m_diMethods;
  EINTERLACEMETHOD m_currentDiMethod;
};

//-----------------------------------------------------------------------------
// VAAPI Video Surface states
//-----------------------------------------------------------------------------

class CVideoSurfaces
{
public:
  void AddSurface(VASurfaceID surf);
  void ClearReference(VASurfaceID surf);
  bool MarkRender(VASurfaceID surf);
  void ClearRender(VASurfaceID surf);
  bool IsValid(VASurfaceID surf);
  VASurfaceID GetFree(VASurfaceID surf);
  VASurfaceID GetAtIndex(int idx);
  VASurfaceID RemoveNext(bool skiprender = false);
  void Reset();
  int Size();
  bool HasFree();
  bool HasRefs();
  int NumFree();
protected:
  std::map<VASurfaceID, int> m_state;
  std::list<VASurfaceID> m_freeSurfaces;
  CCriticalSection m_section;
};

//-----------------------------------------------------------------------------
// VAAPI decoder
//-----------------------------------------------------------------------------

class CVAAPIContext
{
public:
  static bool EnsureContext(CVAAPIContext **ctx, CDecoder *decoder);
  void Release(CDecoder *decoder);
  VADisplay GetDisplay();
  bool SupportsProfile(VAProfile profile);
  VAConfigAttrib GetAttrib(VAProfile profile);
  VAConfigID CreateConfig(VAProfile profile, VAConfigAttrib attrib);
  static void FFReleaseBuffer(void *opaque, uint8_t *data);
private:
  CVAAPIContext();
  void Close();
  void SetVaDisplayForSystem();
  bool CreateContext();
  void DestroyContext();
  void QueryCaps();
  bool CheckSuccess(VAStatus status);
  bool IsValidDecoder(CDecoder *decoder);
  void SetValidDRMVaDisplayFromRenderNode();
  static CVAAPIContext *m_context;
  static CCriticalSection m_section;
  VADisplay m_display;
  int m_refCount;
  int m_attributeCount;
  VADisplayAttribute *m_attributes;
  int m_profileCount;
  VAProfile *m_profiles;
  std::vector<CDecoder*> m_decoders;
  int m_renderNodeFD{-1};
};

/**
 *  VAAPI main class
 */
class CDecoder
 : public IHardwareDecoder
{
   friend class CVaapiBufferPool;

public:

  explicit CDecoder(CProcessInfo& processInfo);
  ~CDecoder() override;

  bool Open (AVCodecContext* avctx, AVCodecContext* mainctx, const enum AVPixelFormat) override;
  CDVDVideoCodec::VCReturn Decode (AVCodecContext* avctx, AVFrame* frame) override;
  bool GetPicture(AVCodecContext* avctx, VideoPicture* picture) override;
  void Reset() override;
  virtual void Close();
  long Release() override;
  bool CanSkipDeint() override;
  unsigned GetAllowedReferences() override { return 4; }

  CDVDVideoCodec::VCReturn Check(AVCodecContext* avctx) override;
  const std::string Name() override { return "vaapi"; }
  void SetCodecControl(int flags) override;

  void FFReleaseBuffer(uint8_t *data);
  static int FFGetBuffer(AVCodecContext *avctx, AVFrame *pic, int flags);

  static IHardwareDecoder* Create(CDVDStreamInfo &hint, CProcessInfo &processInfo, AVPixelFormat fmt);
  static void Register(bool hevc);

protected:
  void SetWidthHeight(int width, int height);
  bool ConfigVAAPI();
  bool CheckStatus(VAStatus vdp_st, int line);
  void FiniVAAPIOutput();
  void ReturnRenderPicture(CVaapiRenderPicture *renderPic);
  long ReleasePicReference();
  bool CheckSuccess(VAStatus status);

  enum EDisplayState
  { VAAPI_OPEN
  , VAAPI_RESET
  , VAAPI_LOST
  , VAAPI_ERROR
  } m_DisplayState;
  CCriticalSection m_DecoderSection;
  CEvent m_DisplayEvent;
  int m_ErrorCount;

  ThreadIdentifier m_decoderThread;
  bool m_vaapiConfigured;
  CVaapiConfig  m_vaapiConfig;
  CVideoSurfaces m_videoSurfaces;
  AVCodecContext* m_avctx;
  int m_getBufferError;

  COutput m_vaapiOutput;
  CVaapiBufferStats m_bufferStats;
  CEvent m_inMsgEvent;
  CVaapiRenderPicture *m_presentPicture = nullptr;

  int m_codecControl;
  CProcessInfo& m_processInfo;

  static bool m_capGeneral;
  static bool m_capHevc;
};

//-----------------------------------------------------------------------------
// Postprocessing
//-----------------------------------------------------------------------------

/**
 *  Base class
 */
class CPostproc
{
public:
  virtual ~CPostproc() = default;
  virtual bool PreInit(CVaapiConfig &config, SDiMethods *methods = NULL) = 0;
  virtual bool Init(EINTERLACEMETHOD method) = 0;
  virtual bool AddPicture(CVaapiDecodedPicture &inPic) = 0;
  virtual bool Filter(CVaapiProcessedPicture &outPic) = 0;
  virtual void ClearRef(VASurfaceID surf) = 0;
  virtual void Flush() = 0;
  virtual bool Compatible(EINTERLACEMETHOD method) = 0;
  virtual bool DoesSync() = 0;
  virtual bool WantsPic() {return true;}
protected:
  CVaapiConfig m_config;
  int m_step;
};

/**
 *  skip post processing
 */
class CSkipPostproc : public CPostproc
{
public:
  bool PreInit(CVaapiConfig &config, SDiMethods *methods = NULL) override;
  bool Init(EINTERLACEMETHOD method) override;
  bool AddPicture(CVaapiDecodedPicture &inPic) override;
  bool Filter(CVaapiProcessedPicture &outPic) override;
  void ClearRef(VASurfaceID surf) override;
  void Flush() override;
  bool Compatible(EINTERLACEMETHOD method) override;
  bool DoesSync() override;
protected:
  CVaapiDecodedPicture m_pic;
};

/**
 *  VAAPI post processing
 */
class CVppPostproc : public CPostproc
{
public:
  CVppPostproc();
  ~CVppPostproc() override;
  bool PreInit(CVaapiConfig &config, SDiMethods *methods = NULL) override;
  bool Init(EINTERLACEMETHOD method) override;
  bool AddPicture(CVaapiDecodedPicture &inPic) override;
  bool Filter(CVaapiProcessedPicture &outPic) override;
  void ClearRef(VASurfaceID surf) override;
  void Flush() override;
  bool Compatible(EINTERLACEMETHOD method) override;
  bool DoesSync() override;
  bool WantsPic() override;
protected:
  bool CheckSuccess(VAStatus status);
  void Dispose();
  void Advance();
  VAConfigID m_configId;
  VAContextID m_contextId;
  CVideoSurfaces m_videoSurfaces;
  std::deque<CVaapiDecodedPicture> m_decodedPics;
  VABufferID m_filter;
  int m_forwardRefs, m_backwardRefs;
  int m_currentIdx;
  int m_frameCount;
  EINTERLACEMETHOD m_vppMethod;
};

/**
 *  ffmpeg filter
 */
class CFFmpegPostproc : public CPostproc
{
public:
  CFFmpegPostproc();
  ~CFFmpegPostproc() override;
  bool PreInit(CVaapiConfig &config, SDiMethods *methods = NULL) override;
  bool Init(EINTERLACEMETHOD method) override;
  bool AddPicture(CVaapiDecodedPicture &inPic) override;
  bool Filter(CVaapiProcessedPicture &outPic) override;
  void ClearRef(VASurfaceID surf) override;
  void Flush() override;
  bool Compatible(EINTERLACEMETHOD method) override;
  bool DoesSync() override;
protected:
  bool CheckSuccess(VAStatus status);
  void Close();
  DllLibSSE4 m_dllSSE4;
  uint8_t *m_cache;
  AVFilterGraph* m_pFilterGraph;
  AVFilterContext* m_pFilterIn;
  AVFilterContext* m_pFilterOut;
  AVFrame *m_pFilterFrameIn;
  AVFrame *m_pFilterFrameOut;
  EINTERLACEMETHOD m_diMethod;
  VideoPicture m_DVDPic;
  double m_frametime;
  double m_lastOutPts;
};

}
