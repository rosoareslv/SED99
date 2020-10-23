/*
 *      Copyright (C) 2012-2013 Team XBMC
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

#include "DVDFactoryInputStream.h"
#include "DVDInputStreamPVRManager.h"
#include "DVDDemuxers/DVDDemuxPacket.h"
#include "ServiceBroker.h"
#include "URL.h"
#include "pvr/PVRManager.h"
#include "pvr/channels/PVRChannel.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "pvr/addons/PVRClients.h"
#include "pvr/channels/PVRChannelGroupsContainer.h"
#include "pvr/recordings/PVRRecordingsPath.h"
#include "pvr/recordings/PVRRecordings.h"
#include "settings/Settings.h"
#include "cores/VideoPlayer/DVDDemuxers/DVDDemux.h"

#include <assert.h>

using namespace XFILE;
using namespace PVR;

/************************************************************************
 * Description: Class constructor, initialize member variables
 *              public class is CDVDInputStream
 */
CDVDInputStreamPVRManager::CDVDInputStreamPVRManager(IVideoPlayer* pPlayer, const CFileItem& fileitem)
  : CDVDInputStream(DVDSTREAM_TYPE_PVRMANAGER, fileitem)
{
  m_pPlayer = pPlayer;
  m_eof = true;
  m_ScanTimeout.Set(0);
  m_demuxActive = false;

  m_StreamProps = new PVR_STREAM_PROPERTIES;
}

/************************************************************************
 * Description: Class destructor
 */
CDVDInputStreamPVRManager::~CDVDInputStreamPVRManager()
{
  Close();

  m_streamMap.clear();
  delete m_StreamProps;
}

void CDVDInputStreamPVRManager::ResetScanTimeout(unsigned int iTimeoutMs)
{
  m_ScanTimeout.Set(iTimeoutMs);
}

bool CDVDInputStreamPVRManager::IsEOF()
{
  // don't mark as eof while within the scan timeout
  if (!m_ScanTimeout.IsTimePast())
    return false;

  return m_eof;
}

bool CDVDInputStreamPVRManager::Open()
{
  if (!CDVDInputStream::Open())
    return false;

  CURL url(m_item.GetDynPath());

  std::string strURL = url.Get();

  if (StringUtils::StartsWith(strURL, "pvr://channels/tv/") ||
      StringUtils::StartsWith(strURL, "pvr://channels/radio/"))
  {
    CFileItemPtr tag = CServiceBroker::GetPVRManager().ChannelGroups()->GetByPath(strURL);
    if (tag && tag->HasPVRChannelInfoTag())
    {
      if (!CServiceBroker::GetPVRManager().OpenLiveStream(*tag))
        return false;

      m_isRecording = false;
      CLog::Log(LOGDEBUG, "CDVDInputStreamPVRManager - %s - playback has started on filename %s", __FUNCTION__, strURL.c_str());
    }
    else
    {
      CLog::Log(LOGERROR, "CDVDInputStreamPVRManager - %s - channel not found with filename %s", __FUNCTION__, strURL.c_str());
      return false;
    }
  }
  else if (CPVRRecordingsPath(strURL).IsActive())
  {
    CFileItemPtr tag = CServiceBroker::GetPVRManager().Recordings()->GetByPath(strURL);
    if (tag && tag->HasPVRRecordingInfoTag())
    {
      if (!CServiceBroker::GetPVRManager().OpenRecordedStream(tag->GetPVRRecordingInfoTag()))
        return false;

      m_isRecording = true;
      CLog::Log(LOGDEBUG, "%s - playback has started on recording %s (%s)", __FUNCTION__, strURL.c_str(), tag->GetPVRRecordingInfoTag()->m_strIconPath.c_str());
    }
    else
    {
      CLog::Log(LOGERROR, "CDVDInputStreamPVRManager - Recording not found with filename %s", strURL.c_str());
      return false;
    }
  }
  else if (CPVRRecordingsPath(strURL).IsDeleted())
  {
    CLog::Log(LOGNOTICE, "CDVDInputStreamPVRManager - Playback of deleted recordings is not possible (%s)", strURL.c_str());
    return false;
  }
  else
  {
    CLog::Log(LOGERROR, "%s - invalid path specified %s", __FUNCTION__, strURL.c_str());
    return false;
  }

  m_eof = false;

  if (URIUtils::IsPVRChannel(url.Get()))
  {
    std::shared_ptr<CPVRClient> client;
    if (CServiceBroker::GetPVRManager().Clients()->GetPlayingClient(client) &&
        client->GetClientCapabilities().HandlesDemuxing())
      m_demuxActive = true;
  }

  ResetScanTimeout((unsigned int) CServiceBroker::GetSettings().GetInt(CSettings::SETTING_PVRPLAYBACK_SCANTIME) * 1000);
  CLog::Log(LOGDEBUG, "CDVDInputStreamPVRManager::Open - stream opened: %s", CURL::GetRedacted(m_item.GetDynPath()).c_str());

  m_StreamProps->iStreamCount = 0;
  return true;
}

// close file and reset everything
void CDVDInputStreamPVRManager::Close()
{
  CServiceBroker::GetPVRManager().CloseStream();

  CDVDInputStream::Close();

  m_eof = true;

  CLog::Log(LOGDEBUG, "CDVDInputStreamPVRManager::Close - stream closed");
}

int CDVDInputStreamPVRManager::Read(uint8_t* buf, int buf_size)
{
  int ret = CServiceBroker::GetPVRManager().Clients()->ReadStream((BYTE*)buf, buf_size);
  if (ret < 0)
    ret = -1;

  /* we currently don't support non completing reads */
  if( ret == 0 )
    m_eof = true;

  return ret;
}

int64_t CDVDInputStreamPVRManager::Seek(int64_t offset, int whence)
{
  if (whence == SEEK_POSSIBLE)
  {
    if (CServiceBroker::GetPVRManager().Clients()->CanSeekStream())
      return 1;
    else
      return 0;
  }

  int64_t ret = CServiceBroker::GetPVRManager().Clients()->SeekStream(offset, whence);

  // if we succeed, we are not eof anymore
  if( ret >= 0 )
    m_eof = false;

  return ret;
}

int64_t CDVDInputStreamPVRManager::GetLength()
{
  return CServiceBroker::GetPVRManager().Clients()->GetStreamLength();
}

int CDVDInputStreamPVRManager::GetTotalTime()
{
  if (!m_isRecording)
    return CServiceBroker::GetPVRManager().GetTotalTime();
  return 0;
}

int CDVDInputStreamPVRManager::GetTime()
{
  if (!m_isRecording)
    return CServiceBroker::GetPVRManager().GetStartTime();
  return 0;
}

bool CDVDInputStreamPVRManager::GetTimes(Times &times)
{
  PVR_STREAM_TIMES streamTimes;
  bool ret = CServiceBroker::GetPVRManager().Clients()->GetStreamTimes(&streamTimes);
  if (ret)
  {
    times.startTime = streamTimes.startTime;
    times.ptsStart = streamTimes.ptsStart;
    times.ptsBegin = streamTimes.ptsBegin;
    times.ptsEnd = streamTimes.ptsEnd;
  }

  return ret;
}

CPVRChannelPtr CDVDInputStreamPVRManager::GetSelectedChannel()
{
  return CServiceBroker::GetPVRManager().GetCurrentChannel();
}

CDVDInputStream::ENextStream CDVDInputStreamPVRManager::NextStream()
{
  m_eof = IsEOF();

  if(!m_isRecording)
  {
    if (m_eof)
      return NEXTSTREAM_OPEN;
    else
      return NEXTSTREAM_RETRY;
  }
  return NEXTSTREAM_NONE;
}

bool CDVDInputStreamPVRManager::CanRecord()
{
  if (!m_isRecording)
    return CServiceBroker::GetPVRManager().Clients()->CanRecordInstantly();
  return false;
}

bool CDVDInputStreamPVRManager::IsRecording()
{
  return CServiceBroker::GetPVRManager().Clients()->IsRecordingOnPlayingChannel();
}

void CDVDInputStreamPVRManager::Record(bool bOnOff)
{
  CServiceBroker::GetPVRManager().StartRecordingOnPlayingChannel(bOnOff);
}

bool CDVDInputStreamPVRManager::CanPause()
{
  return CServiceBroker::GetPVRManager().Clients()->CanPauseStream();
}

bool CDVDInputStreamPVRManager::CanSeek()
{
  return CServiceBroker::GetPVRManager().Clients()->CanSeekStream();
}

void CDVDInputStreamPVRManager::Pause(bool bPaused)
{
  CServiceBroker::GetPVRManager().Clients()->PauseStream(bPaused);
}

std::string CDVDInputStreamPVRManager::GetInputFormat()
{
  return CServiceBroker::GetPVRManager().Clients()->GetCurrentInputFormat();
}

bool CDVDInputStreamPVRManager::IsRealtime()
{
  return CServiceBroker::GetPVRManager().Clients()->IsRealTimeStream();
}

inline CDVDInputStream::IDemux* CDVDInputStreamPVRManager::GetIDemux()
{
  if (m_demuxActive)
    return this;
  else
    return nullptr;
}

bool CDVDInputStreamPVRManager::OpenDemux()
{
  PVR_CLIENT client;
  if (!CServiceBroker::GetPVRManager().Clients()->GetPlayingClient(client))
  {
    return false;
  }

  client->GetStreamProperties(m_StreamProps);
  UpdateStreamMap();
  return true;
}

DemuxPacket* CDVDInputStreamPVRManager::ReadDemux()
{
  PVR_CLIENT client;
  if (!CServiceBroker::GetPVRManager().Clients()->GetPlayingClient(client))
  {
    return nullptr;
  }

  DemuxPacket* pPacket = client->DemuxRead();
  if (!pPacket)
  {
    return nullptr;
  }
  else if (pPacket->iStreamId == DMX_SPECIALID_STREAMINFO)
  {
    client->GetStreamProperties(m_StreamProps);
    return pPacket;
  }
  else if (pPacket->iStreamId == DMX_SPECIALID_STREAMCHANGE)
  {
    client->GetStreamProperties(m_StreamProps);
    UpdateStreamMap();
  }

  return pPacket;
}

CDemuxStream* CDVDInputStreamPVRManager::GetStream(int iStreamId) const
{
  auto stream = m_streamMap.find(iStreamId);
  if (stream != m_streamMap.end())
  {
    return stream->second.get();
  }
  else
    return nullptr;
}

std::vector<CDemuxStream*> CDVDInputStreamPVRManager::GetStreams() const
{
  std::vector<CDemuxStream*> streams;

  for (auto& st : m_streamMap)
  {
    streams.push_back(st.second.get());
  }

  return streams;
}

int CDVDInputStreamPVRManager::GetNrOfStreams() const
{
  return m_StreamProps->iStreamCount;
}

void CDVDInputStreamPVRManager::SetSpeed(int Speed)
{
  PVR_CLIENT client;
  if (CServiceBroker::GetPVRManager().Clients()->GetPlayingClient(client))
  {
    client->SetSpeed(Speed);
  }
}

bool CDVDInputStreamPVRManager::SeekTime(double timems, bool backwards, double *startpts)
{
  PVR_CLIENT client;
  if (CServiceBroker::GetPVRManager().Clients()->GetPlayingClient(client))
  {
    return client->SeekTime(timems, backwards, startpts);
  }
  return false;
}

void CDVDInputStreamPVRManager::AbortDemux()
{
  PVR_CLIENT client;
  if (CServiceBroker::GetPVRManager().Clients()->GetPlayingClient(client))
  {
    client->DemuxAbort();
  }
}

void CDVDInputStreamPVRManager::FlushDemux()
{
  PVR_CLIENT client;
  if (CServiceBroker::GetPVRManager().Clients()->GetPlayingClient(client))
  {
    client->DemuxFlush();
  }
}

std::shared_ptr<CDemuxStream> CDVDInputStreamPVRManager::GetStreamInternal(int iStreamId)
{
  auto stream = m_streamMap.find(iStreamId);
  if (stream != m_streamMap.end())
  {
    return stream->second;
  }
  else
    return nullptr;
}

void CDVDInputStreamPVRManager::UpdateStreamMap()
{
  std::map<int, std::shared_ptr<CDemuxStream>> m_newStreamMap;

  int num = GetNrOfStreams();
  for (int i = 0; i < num; ++i)
  {
    PVR_STREAM_PROPERTIES::PVR_STREAM stream = m_StreamProps->stream[i];

    std::shared_ptr<CDemuxStream> dStream = GetStreamInternal(stream.iPID);

    if (stream.iCodecType == XBMC_CODEC_TYPE_AUDIO)
    {
      std::shared_ptr<CDemuxStreamAudio> streamAudio;

      if (dStream)
        streamAudio = std::dynamic_pointer_cast<CDemuxStreamAudio>(dStream);
      if (!streamAudio)
        streamAudio = std::make_shared<CDemuxStreamAudio>();

      streamAudio->iChannels = stream.iChannels;
      streamAudio->iSampleRate = stream.iSampleRate;
      streamAudio->iBlockAlign = stream.iBlockAlign;
      streamAudio->iBitRate = stream.iBitRate;
      streamAudio->iBitsPerSample = stream.iBitsPerSample;

      dStream = streamAudio;
    }
    else if (stream.iCodecType == XBMC_CODEC_TYPE_VIDEO)
    {
      std::shared_ptr<CDemuxStreamVideo> streamVideo;

      if (dStream)
        streamVideo = std::dynamic_pointer_cast<CDemuxStreamVideo>(dStream);
      if (!streamVideo)
        streamVideo = std::make_shared<CDemuxStreamVideo>();

      streamVideo->iFpsScale = stream.iFPSScale;
      streamVideo->iFpsRate = stream.iFPSRate;
      streamVideo->iHeight = stream.iHeight;
      streamVideo->iWidth = stream.iWidth;
      streamVideo->fAspect = stream.fAspect;
      streamVideo->stereo_mode = "mono";

      dStream = streamVideo;
    }
    else if (stream.iCodecId == AV_CODEC_ID_DVB_TELETEXT)
    {
      std::shared_ptr<CDemuxStreamTeletext> streamTeletext;

      if (dStream)
        streamTeletext = std::dynamic_pointer_cast<CDemuxStreamTeletext>(dStream);
      if (!streamTeletext)
        streamTeletext = std::make_shared<CDemuxStreamTeletext>();

      dStream = streamTeletext;
    }
    else if (stream.iCodecType == XBMC_CODEC_TYPE_SUBTITLE)
    {
      std::shared_ptr<CDemuxStreamSubtitle> streamSubtitle;

      if (dStream)
        streamSubtitle = std::dynamic_pointer_cast<CDemuxStreamSubtitle>(dStream);
      if (!streamSubtitle)
        streamSubtitle = std::make_shared<CDemuxStreamSubtitle>();

      if (stream.iSubtitleInfo)
      {
        streamSubtitle->ExtraData = new uint8_t[4];
        streamSubtitle->ExtraSize = 4;
        streamSubtitle->ExtraData[0] = (stream.iSubtitleInfo >> 8) & 0xff;
        streamSubtitle->ExtraData[1] = (stream.iSubtitleInfo >> 0) & 0xff;
        streamSubtitle->ExtraData[2] = (stream.iSubtitleInfo >> 24) & 0xff;
        streamSubtitle->ExtraData[3] = (stream.iSubtitleInfo >> 16) & 0xff;
      }
      dStream = streamSubtitle;
    }
    else if (stream.iCodecType == XBMC_CODEC_TYPE_RDS &&
      CServiceBroker::GetSettings().GetBool("pvrplayback.enableradiords"))
    {
      std::shared_ptr<CDemuxStreamRadioRDS> streamRadioRDS;

      if (dStream)
        streamRadioRDS = std::dynamic_pointer_cast<CDemuxStreamRadioRDS>(dStream);
      if (!streamRadioRDS)
        streamRadioRDS = std::make_shared<CDemuxStreamRadioRDS>();

      dStream = streamRadioRDS;
    }
    else
      dStream = std::make_shared<CDemuxStream>();

    dStream->codec = (AVCodecID)stream.iCodecId;
    dStream->uniqueId = stream.iPID;
    dStream->language[0] = stream.strLanguage[0];
    dStream->language[1] = stream.strLanguage[1];
    dStream->language[2] = stream.strLanguage[2];
    dStream->language[3] = stream.strLanguage[3];
    dStream->realtime = true;

    m_newStreamMap[stream.iPID] = dStream;
  }

  m_streamMap = m_newStreamMap;
}
