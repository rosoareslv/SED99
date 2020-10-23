/*
 *      Copyright (C) 2005-2017 Team KODI
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
 *  along with KODI; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "AudioEngine.h"

#include "ServiceBroker.h"
#include "addons/kodi-addon-dev-kit/include/kodi/AddonBase.h"
#include "cores/AudioEngine/Interfaces/AE.h"
#include "cores/AudioEngine/Interfaces/AEStream.h"
#include "cores/AudioEngine/Utils/AEChannelData.h"
#include "utils/log.h"

using namespace kodi; // addon-dev-kit namespace
using namespace kodi::audioengine; // addon-dev-kit namespace

namespace ADDON
{

void Interface_AudioEngine::Init(AddonGlobalInterface* addonInterface)
{
  addonInterface->toKodi->kodi_audioengine = (AddonToKodiFuncTable_kodi_audioengine*)malloc(sizeof(AddonToKodiFuncTable_kodi_audioengine));

  // write KODI audio DSP specific add-on function addresses to callback table
  addonInterface->toKodi->kodi_audioengine->make_stream = audioengine_make_stream;
  addonInterface->toKodi->kodi_audioengine->free_stream = audioengine_free_stream;
  addonInterface->toKodi->kodi_audioengine->get_current_sink_format = audioengine_get_current_sink_format;

  // AEStream add-on function callback table
  addonInterface->toKodi->kodi_audioengine->aestream_get_space = aestream_get_space;
  addonInterface->toKodi->kodi_audioengine->aestream_add_data = aestream_add_data;
  addonInterface->toKodi->kodi_audioengine->aestream_get_delay = aestream_get_delay;
  addonInterface->toKodi->kodi_audioengine->aestream_is_buffering = aestream_is_buffering;
  addonInterface->toKodi->kodi_audioengine->aestream_get_cache_time = aestream_get_cache_time;
  addonInterface->toKodi->kodi_audioengine->aestream_get_cache_total = aestream_get_cache_total;
  addonInterface->toKodi->kodi_audioengine->aestream_pause = aestream_pause;
  addonInterface->toKodi->kodi_audioengine->aestream_resume = aestream_resume;
  addonInterface->toKodi->kodi_audioengine->aestream_drain = aestream_drain;
  addonInterface->toKodi->kodi_audioengine->aestream_is_draining = aestream_is_draining;
  addonInterface->toKodi->kodi_audioengine->aestream_is_drained = aestream_is_drained;
  addonInterface->toKodi->kodi_audioengine->aestream_flush = aestream_flush;
  addonInterface->toKodi->kodi_audioengine->aestream_get_volume = aestream_get_volume;
  addonInterface->toKodi->kodi_audioengine->aestream_set_volume = aestream_set_volume;
  addonInterface->toKodi->kodi_audioengine->aestream_get_amplification = aestream_get_amplification;
  addonInterface->toKodi->kodi_audioengine->aestream_set_amplification = aestream_set_amplification;
  addonInterface->toKodi->kodi_audioengine->aestream_get_frame_size = aestream_get_frame_size;
  addonInterface->toKodi->kodi_audioengine->aestream_get_channel_count = aestream_get_channel_count;
  addonInterface->toKodi->kodi_audioengine->aestream_get_sample_rate = aestream_get_sample_rate;
  addonInterface->toKodi->kodi_audioengine->aestream_get_data_format = aestream_get_data_format;
  addonInterface->toKodi->kodi_audioengine->aestream_get_resample_ratio = aestream_get_resample_ratio;
  addonInterface->toKodi->kodi_audioengine->aestream_set_resample_ratio = aestream_set_resample_ratio;
}

void Interface_AudioEngine::DeInit(AddonGlobalInterface* addonInterface)
{
  if (addonInterface->toKodi && /* <-- Safe check, needed so long old addon way is present */
      addonInterface->toKodi->kodi_audioengine)
  {
    free(addonInterface->toKodi->kodi_audioengine);
    addonInterface->toKodi->kodi_audioengine = nullptr;
  }
}

AEStreamHandle* Interface_AudioEngine::audioengine_make_stream(void* kodiBase, AudioEngineFormat* streamFormat, unsigned int options)
{
  if (!kodiBase || !streamFormat)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamFormat='%p')", __FUNCTION__, kodiBase, streamFormat);
    return nullptr;
  }

  AEAudioFormat format;
  format.m_dataFormat = streamFormat->m_dataFormat;
  format.m_sampleRate = streamFormat->m_sampleRate;
  format.m_channelLayout = streamFormat->m_channels;

  /* Translate addon options to kodi's options */
  int kodiOption = 0;
  if (options & AUDIO_STREAM_FORCE_RESAMPLE)
    kodiOption |= AESTREAM_FORCE_RESAMPLE;
  if (options & AUDIO_STREAM_PAUSED)
    kodiOption |= AESTREAM_PAUSED;
  if (options & AUDIO_STREAM_AUTOSTART)
    kodiOption |= AESTREAM_AUTOSTART;
  if (options & AUDIO_STREAM_BYPASS_ADSP)
    kodiOption |= AESTREAM_BYPASS_ADSP;

  return CServiceBroker::GetActiveAE().MakeStream(format, kodiOption);
}

void Interface_AudioEngine::audioengine_free_stream(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return;
  }

  CServiceBroker::GetActiveAE().FreeStream(static_cast<IAEStream*>(streamHandle));
}

bool Interface_AudioEngine::audioengine_get_current_sink_format(void* kodiBase, AudioEngineFormat *format)
{
  if (!kodiBase || !format)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', format='%p')", __FUNCTION__, kodiBase, format);
    return false;
  }

  AEAudioFormat sinkFormat;
  if (!CServiceBroker::GetActiveAE().GetCurrentSinkFormat(sinkFormat))
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - failed to get current sink format from AE!", __FUNCTION__);
    return false;
  }

  format->m_channelCount = sinkFormat.m_channelLayout.Count();
  for (unsigned int ch = 0; ch < format->m_channelCount; ++ch)
  {
    format->m_channels[ch] = sinkFormat.m_channelLayout[ch];
  }

  format->m_dataFormat = sinkFormat.m_dataFormat;
  format->m_sampleRate = sinkFormat.m_sampleRate;
  format->m_frames = sinkFormat.m_frames;
  format->m_frameSize = sinkFormat.m_frameSize;

  return true;
}

unsigned int Interface_AudioEngine::aestream_get_space(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return 0;
  }

  return static_cast<IAEStream*>(streamHandle)->GetSpace();
}

unsigned int Interface_AudioEngine::aestream_add_data(void* kodiBase, AEStreamHandle* streamHandle, uint8_t* const *data,
                                                     unsigned int offset, unsigned int frames, double pts)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return 0;
  }

  return static_cast<IAEStream*>(streamHandle)->AddData(data, offset, frames, pts);
}

double Interface_AudioEngine::aestream_get_delay(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return -1.0;
  }

  return static_cast<IAEStream*>(streamHandle)->GetDelay();
}

bool Interface_AudioEngine::aestream_is_buffering(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return false;
  }

  return static_cast<IAEStream*>(streamHandle)->IsBuffering();
}

double Interface_AudioEngine::aestream_get_cache_time(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return -1.0;
  }

  return static_cast<IAEStream*>(streamHandle)->GetCacheTime();
}

double Interface_AudioEngine::aestream_get_cache_total(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return -1.0;
  }

  return static_cast<IAEStream*>(streamHandle)->GetCacheTotal();
}

void Interface_AudioEngine::aestream_pause(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return;
  }

  static_cast<IAEStream*>(streamHandle)->Pause();
}

void Interface_AudioEngine::aestream_resume(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return;
  }

  static_cast<IAEStream*>(streamHandle)->Resume();
}

void Interface_AudioEngine::aestream_drain(void* kodiBase, AEStreamHandle* streamHandle, bool wait)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return;
  }

  static_cast<IAEStream*>(streamHandle)->Drain(wait);
}

bool Interface_AudioEngine::aestream_is_draining(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return false;
  }

  return static_cast<IAEStream*>(streamHandle)->IsDraining();
}

bool Interface_AudioEngine::aestream_is_drained(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return false;
  }

  return static_cast<IAEStream*>(streamHandle)->IsDrained();
}

void Interface_AudioEngine::aestream_flush(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return;
  }

  static_cast<IAEStream*>(streamHandle)->Flush();
}

float Interface_AudioEngine::aestream_get_volume(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return -1.0f;
  }

  return static_cast<IAEStream*>(streamHandle)->GetVolume();
}

void  Interface_AudioEngine::aestream_set_volume(void* kodiBase, AEStreamHandle* streamHandle, float volume)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return;
  }

  static_cast<IAEStream*>(streamHandle)->SetVolume(volume);
}

float Interface_AudioEngine::aestream_get_amplification(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return -1.0f;
  }

  return static_cast<IAEStream*>(streamHandle)->GetAmplification();
}

void Interface_AudioEngine::aestream_set_amplification(void* kodiBase, AEStreamHandle* streamHandle, float amplify)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return;
  }

  static_cast<IAEStream*>(streamHandle)->SetAmplification(amplify);
}

unsigned int Interface_AudioEngine::aestream_get_frame_size(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return 0;
  }

  return static_cast<IAEStream*>(streamHandle)->GetFrameSize();
}

unsigned int Interface_AudioEngine::aestream_get_channel_count(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return 0;
  }

  return static_cast<IAEStream*>(streamHandle)->GetChannelCount();
}

unsigned int Interface_AudioEngine::aestream_get_sample_rate(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return 0;
  }

  return static_cast<IAEStream*>(streamHandle)->GetSampleRate();
}

AEDataFormat Interface_AudioEngine::aestream_get_data_format(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return AE_FMT_INVALID;
  }

  return static_cast<IAEStream*>(streamHandle)->GetDataFormat();
}

double Interface_AudioEngine::aestream_get_resample_ratio(void* kodiBase, AEStreamHandle* streamHandle)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return -1.0f;
  }

  return static_cast<IAEStream*>(streamHandle)->GetResampleRatio();
}

void Interface_AudioEngine::aestream_set_resample_ratio(void* kodiBase, AEStreamHandle* streamHandle, double ratio)
{
  if (!kodiBase || !streamHandle)
  {
    CLog::Log(LOGERROR, "Interface_AudioEngine::%s - invalid stream data (kodiBase='%p', streamHandle='%p')", __FUNCTION__, kodiBase, streamHandle);
    return;
  }

  static_cast<IAEStream*>(streamHandle)->SetResampleRatio(ratio);
}

} /* namespace ADDON */
