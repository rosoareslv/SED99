/*
 *      Copyright (C) 2010-2013 Team XBMC
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

#include "AESinkXAudio.h"
#include "cores/AudioEngine/AESinkFactory.h"
#include "cores/AudioEngine/Sinks/windows/AESinkFactoryWin.h"
#include "cores/AudioEngine/Utils/AEDeviceInfo.h"
#include "cores/AudioEngine/Utils/AEUtil.h"
#include "platform/win32/CharsetConverter.h"
#include "platform/win10/AsyncHelpers.h"
#include "settings/AdvancedSettings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/TimeUtils.h"

#include <algorithm>
#include <collection.h>
#include <ksmedia.h>
#include <mfapi.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <stdint.h>
#include <wrl/implements.h>

using namespace Microsoft::WRL;

extern const char *WASAPIErrToStr(HRESULT err);

#define EXIT_ON_FAILURE(hr, reason, ...) if(FAILED(hr)) {CLog::Log(LOGERROR, reason " - %s", __VA_ARGS__, WASAPIErrToStr(hr)); goto failed;}
#define SAFE_DESTROY_VOICE(x) do { if(x) { x->DestroyVoice(); x = nullptr; } }while(0);
#define XAUDIO_BUFFERS_IN_QUEUE 2

///  ----------------- CAESinkXAudio ------------------------

CAESinkXAudio::CAESinkXAudio() :
  m_masterVoice(nullptr),
  m_sourceVoice(nullptr),
  m_encodedChannels(0),
  m_encodedSampleRate(0),
  sinkReqFormat(AE_FMT_INVALID),
  sinkRetFormat(AE_FMT_INVALID),
  m_AvgBytesPerSec(0),
  m_dwChunkSize(0),
  m_dwFrameSize(0),
  m_dwBufferLen(0),
  m_running(false),
  m_initialized(false),
  m_isSuspended(false),
  m_isDirty(false),
  m_uiBufferLen(0),
  m_avgTimeWaiting(50)
{
  m_channelLayout.Reset();

  HRESULT hr = XAudio2Create(m_xAudio2.ReleaseAndGetAddressOf(), 0);
  if (FAILED(hr))
  {
    CLog::LogFunction(LOGERROR, __FUNCTION__, "XAudio initialization failed.");
  }
#ifdef  _DEBUG
  else
  {
    XAUDIO2_DEBUG_CONFIGURATION config = { 0 };
    config.BreakMask = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS | XAUDIO2_LOG_API_CALLS | XAUDIO2_LOG_STREAMING;
    config.TraceMask = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS | XAUDIO2_LOG_API_CALLS | XAUDIO2_LOG_STREAMING;
    config.LogTiming = true;
    config.LogFunctionName = true;
    m_xAudio2->SetDebugConfiguration(&config, 0);
  }
#endif //  _DEBUG
}

CAESinkXAudio::~CAESinkXAudio()
{
  if (m_xAudio2)
    m_xAudio2.Reset();
}

void CAESinkXAudio::Register()
{
  AE::AESinkRegEntry reg;
  reg.sinkName = "XAUDIO";
  reg.createFunc = CAESinkXAudio::Create;
  reg.enumerateFunc = CAESinkXAudio::EnumerateDevicesEx;
  AE::CAESinkFactory::RegisterSink(reg);
}

IAESink* CAESinkXAudio::Create(std::string &device, AEAudioFormat &desiredFormat)
{
  IAESink *sink = new CAESinkXAudio();
  if (sink->Initialize(desiredFormat, device))
    return sink;

  delete sink;
  return nullptr;
}

bool CAESinkXAudio::Initialize(AEAudioFormat &format, std::string &device)
{
  if (m_initialized)
    return false;

  m_device = device;
  bool bdefault = false;
  HRESULT hr = S_OK;

  /* Save requested format */
  /* Clear returned format */
  sinkReqFormat = format.m_dataFormat;
  sinkRetFormat = AE_FMT_INVALID;

  if (!InitializeInternal(device, format))
  {
    CLog::Log(LOGINFO, __FUNCTION__": Could not Initialize voices with that format");
    goto failed;
  }

  format.m_frames       = m_uiBufferLen;
  m_format              = format;
  sinkRetFormat         = format.m_dataFormat;

  m_initialized = true;
  m_isDirty     = false;

  return true;

failed:
  CLog::Log(LOGERROR, __FUNCTION__": XAudio initialization failed.");
  return true;
}

void CAESinkXAudio::Deinitialize()
{
  if (!m_initialized && !m_isDirty)
    return;

  if (m_running)
  {
    try
    {
      m_sourceVoice->Stop();
      m_sourceVoice->FlushSourceBuffers();
    }
    catch (...)
    {
      CLog::Log(LOGDEBUG, "%s: Invalidated source voice - Releasing", __FUNCTION__);
    }
  }
  m_running = false;

  SAFE_DESTROY_VOICE(m_sourceVoice);
  SAFE_DESTROY_VOICE(m_masterVoice);

  m_initialized = false;
}

/**
 * @brief rescale uint64_t without overflowing on large values
 */
static uint64_t rescale_u64(uint64_t val, uint64_t num, uint64_t den)
{
  return ((val / den) * num) + (((val % den) * num) / den);
}

void CAESinkXAudio::GetDelay(AEDelayStatus& status)
{
  HRESULT hr = S_OK;
  uint64_t pos = 0, tick = 0;
  int retries = 0;

  if (!m_initialized)
    goto failed;

  XAUDIO2_VOICE_STATE state;
  m_sourceVoice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
  
  uint64_t framesInQueue = state.BuffersQueued * m_format.m_frames;
  status.SetDelay(framesInQueue / (double)m_format.m_sampleRate);
  return;

failed:
  status.SetDelay(0);
}

double CAESinkXAudio::GetCacheTotal()
{
  if (!m_initialized)
    return 0.0;

  return XAUDIO_BUFFERS_IN_QUEUE * m_format.m_frames / (double)m_format.m_sampleRate;
}

double CAESinkXAudio::GetLatency()
{
  if (!m_initialized || !m_xAudio2)
    return 0.0;

  XAUDIO2_PERFORMANCE_DATA perfData;
  m_xAudio2->GetPerformanceData(&perfData);

  return perfData.CurrentLatencyInSamples / (double) m_format.m_sampleRate;
}

unsigned int CAESinkXAudio::AddPackets(uint8_t **data, unsigned int frames, unsigned int offset)
{
  if (!m_initialized)
    return 0;

  HRESULT hr = S_OK;
  DWORD flags = 0;

#ifndef _DEBUG
  LARGE_INTEGER timerStart;
  LARGE_INTEGER timerStop;
  LARGE_INTEGER timerFreq;
#endif
  size_t dataLenght = frames * m_format.m_frameSize;
  uint8_t* buff = new uint8_t[dataLenght];
  memcpy(buff, data[0] + offset * m_format.m_frameSize, dataLenght);

  XAUDIO2_BUFFER xbuffer = { 0 };
  xbuffer.AudioBytes = dataLenght;
  xbuffer.pAudioData = buff;
  xbuffer.pContext = buff;

  if (!m_running) //first time called, pre-fill buffer then start voice
  {
    hr = m_sourceVoice->SubmitSourceBuffer(&xbuffer);
    if (FAILED(hr))
    {
      CLog::Log(LOGERROR, __FUNCTION__ " SourceVoice submit buffer failed due to %s", WASAPIErrToStr(hr));
      delete[] buff;
      return 0;
    }
    hr = m_sourceVoice->Start(0, XAUDIO2_COMMIT_NOW);
    if (FAILED(hr))
    {
      CLog::Log(LOGERROR, __FUNCTION__ " SourceVoice start failed due to %s", WASAPIErrToStr(hr));
      m_isDirty = true; //flag new device or re-init needed
      delete[] buff;
      return INT_MAX;
    }
    m_running = true; //signal that we're processing frames
    return frames;
  }

#ifndef _DEBUG
  /* Get clock time for latency checks */
  QueryPerformanceFrequency(&timerFreq);
  QueryPerformanceCounter(&timerStart);
#endif

  /* Wait for Audio Driver to tell us it's got a buffer available */
  XAUDIO2_VOICE_STATE state;
  while (m_sourceVoice->GetState(&state), state.BuffersQueued >= XAUDIO_BUFFERS_IN_QUEUE)
  {
    DWORD eventAudioCallback;
    eventAudioCallback = WaitForSingleObjectEx(m_voiceCallback.mBufferEnd.get(), 1100, TRUE);
    if (eventAudioCallback != WAIT_OBJECT_0)
    {
      CLog::Log(LOGERROR, __FUNCTION__": Endpoint Buffer timed out");
      delete[] buff;
      return INT_MAX;
    }
  }

  if (!m_running)
    return 0;

#ifndef _DEBUG
  QueryPerformanceCounter(&timerStop);
  LONGLONG timerDiff = timerStop.QuadPart - timerStart.QuadPart;
  double timerElapsed = (double) timerDiff * 1000.0 / (double) timerFreq.QuadPart;
  m_avgTimeWaiting += (timerElapsed - m_avgTimeWaiting) * 0.5;

  if (m_avgTimeWaiting < 3.0)
  {
    CLog::Log(LOGDEBUG, __FUNCTION__": Possible AQ Loss: Avg. Time Waiting for Audio Driver callback : %dmsec", (int)m_avgTimeWaiting);
  }
#endif

  hr = m_sourceVoice->SubmitSourceBuffer(&xbuffer);
  if (FAILED(hr))
  {
    #ifdef _DEBUG
      CLog::Log(LOGERROR, __FUNCTION__": SubmitSourceBuffer failed due to %s", WASAPIErrToStr(hr));
    #endif
    return INT_MAX;
  }

  return frames;
}

void CAESinkXAudio::EnumerateDevicesEx(AEDeviceInfoList &deviceInfoList, bool force)
{
  HRESULT hr = S_OK, hr2 = S_OK;
  CAEDeviceInfo deviceInfo;
  CAEChannelInfo deviceChannels;
  WAVEFORMATEXTENSIBLE wfxex = { 0 };
  bool add192 = false;

  UINT32 eflags = 0;// XAUDIO2_DEBUG_ENGINE;
  Microsoft::WRL::ComPtr<IXAudio2> xaudio2;
  hr = XAudio2Create(xaudio2.ReleaseAndGetAddressOf(), eflags);
  if (FAILED(hr))
  {
    CLog::Log(LOGDEBUG, __FUNCTION__": Failed to activate XAudio for capability testing.");
    goto failed;
  }

  IXAudio2MasteringVoice* mMasterVoice = nullptr;
  IXAudio2SourceVoice* mSourceVoice = nullptr;

  for(RendererDetail& details : CAESinkFactoryWin::GetRendererDetails())
  {
    deviceInfo.m_channels.Reset();
    deviceInfo.m_dataFormats.clear();
    deviceInfo.m_sampleRates.clear();

    std::wstring deviceId = KODI::PLATFORM::WINDOWS::ToW(details.strDeviceId);

    /* Test format DTS-HD */
    wfxex.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfxex.Format.nSamplesPerSec = 192000;
    wfxex.dwChannelMask = KSAUDIO_SPEAKER_7POINT1_SURROUND;
    wfxex.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfxex.SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DTS_HD;
    wfxex.Format.wBitsPerSample = 16;
    wfxex.Samples.wValidBitsPerSample = 16;
    wfxex.Format.nChannels = 8;
    wfxex.Format.nBlockAlign = wfxex.Format.nChannels * (wfxex.Format.wBitsPerSample >> 3);
    wfxex.Format.nAvgBytesPerSec = wfxex.Format.nSamplesPerSec * wfxex.Format.nBlockAlign;

    hr2 = xaudio2->CreateMasteringVoice(&mMasterVoice, wfxex.Format.nChannels, wfxex.Format.nSamplesPerSec,
                                        0, deviceId.c_str(), nullptr, AudioCategory_Media);
    hr = xaudio2->CreateSourceVoice(&mSourceVoice, &wfxex.Format);

    if (SUCCEEDED(hr) || details.eDeviceType == AE_DEVTYPE_HDMI)
    {
      if (FAILED(hr))
        CLog::Log(LOGNOTICE, __FUNCTION__": stream type \"%s\" on device \"%s\" seems to be not supported.", CAEUtil::StreamTypeToStr(CAEStreamInfo::STREAM_TYPE_DTSHD), details.strDescription.c_str());

      deviceInfo.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_DTSHD);
      add192 = true;
    }
    SAFE_DESTROY_VOICE(mSourceVoice);

    /* Test format Dolby TrueHD */
    wfxex.SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_MLP;

    hr = xaudio2->CreateSourceVoice(&mSourceVoice, &wfxex.Format);
    if (SUCCEEDED(hr) || details.eDeviceType == AE_DEVTYPE_HDMI)
    {
      if (FAILED(hr))
        CLog::Log(LOGNOTICE, __FUNCTION__": stream type \"%s\" on device \"%s\" seems to be not supported.", CAEUtil::StreamTypeToStr(CAEStreamInfo::STREAM_TYPE_TRUEHD), details.strDescription.c_str());

      deviceInfo.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_TRUEHD);
      add192 = true;
    }

    /* Test format Dolby EAC3 */
    wfxex.SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL_PLUS;
    wfxex.Format.nChannels = 2;
    wfxex.Format.nBlockAlign = wfxex.Format.nChannels * (wfxex.Format.wBitsPerSample >> 3);
    wfxex.Format.nAvgBytesPerSec = wfxex.Format.nSamplesPerSec * wfxex.Format.nBlockAlign;

    SAFE_DESTROY_VOICE(mSourceVoice);
    SAFE_DESTROY_VOICE(mMasterVoice);
    hr2 = xaudio2->CreateMasteringVoice(&mMasterVoice, wfxex.Format.nChannels, wfxex.Format.nSamplesPerSec,
                                        0, deviceId.c_str(), nullptr, AudioCategory_Media);
    hr = xaudio2->CreateSourceVoice(&mSourceVoice, &wfxex.Format);

    if (SUCCEEDED(hr) || details.eDeviceType == AE_DEVTYPE_HDMI)
    {
      if (FAILED(hr))
        CLog::Log(LOGNOTICE, __FUNCTION__": stream type \"%s\" on device \"%s\" seems to be not supported.", CAEUtil::StreamTypeToStr(CAEStreamInfo::STREAM_TYPE_EAC3), details.strDescription.c_str());

      deviceInfo.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_EAC3);
      add192 = true;
    }

    /* Test format DTS */
    wfxex.Format.nSamplesPerSec = 48000;
    wfxex.dwChannelMask = KSAUDIO_SPEAKER_5POINT1;
    wfxex.SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DTS;
    wfxex.Format.nBlockAlign = wfxex.Format.nChannels * (wfxex.Format.wBitsPerSample >> 3);
    wfxex.Format.nAvgBytesPerSec = wfxex.Format.nSamplesPerSec * wfxex.Format.nBlockAlign;

    SAFE_DESTROY_VOICE(mSourceVoice);
    SAFE_DESTROY_VOICE(mMasterVoice);
    hr2 = xaudio2->CreateMasteringVoice(&mMasterVoice, wfxex.Format.nChannels, wfxex.Format.nSamplesPerSec,
                                        0, deviceId.c_str(), nullptr, AudioCategory_Media);
    hr = xaudio2->CreateSourceVoice(&mSourceVoice, &wfxex.Format);
    if (SUCCEEDED(hr) || details.eDeviceType == AE_DEVTYPE_HDMI)
    {
      if (FAILED(hr))
        CLog::Log(LOGNOTICE, __FUNCTION__": stream type \"%s\" on device \"%s\" seems to be not supported.", "STREAM_TYPE_DTS", details.strDescription.c_str());

      deviceInfo.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_DTSHD_CORE);
      deviceInfo.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_DTS_2048);
      deviceInfo.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_DTS_1024);
      deviceInfo.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_DTS_512);
    }
    SAFE_DESTROY_VOICE(mSourceVoice);

    /* Test format Dolby AC3 */
    wfxex.SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL;

    hr = xaudio2->CreateSourceVoice(&mSourceVoice, &wfxex.Format);
    if (SUCCEEDED(hr) || details.eDeviceType == AE_DEVTYPE_HDMI)
    {
      if (FAILED(hr))
        CLog::Log(LOGNOTICE, __FUNCTION__": stream type \"%s\" on device \"%s\" seems to be not supported.", CAEUtil::StreamTypeToStr(CAEStreamInfo::STREAM_TYPE_AC3), details.strDescription.c_str());

      deviceInfo.m_streamTypes.push_back(CAEStreamInfo::STREAM_TYPE_AC3);
    }

    /* Test format for PCM format iteration */
    wfxex.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfxex.dwChannelMask = KSAUDIO_SPEAKER_STEREO;
    wfxex.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfxex.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    for (int p = AE_FMT_FLOAT; p > AE_FMT_INVALID; p--)
    {
      if (p < AE_FMT_FLOAT)
        wfxex.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
      wfxex.Format.wBitsPerSample = CAEUtil::DataFormatToBits((AEDataFormat)p);
      wfxex.Format.nBlockAlign = wfxex.Format.nChannels * (wfxex.Format.wBitsPerSample >> 3);
      wfxex.Format.nAvgBytesPerSec = wfxex.Format.nSamplesPerSec * wfxex.Format.nBlockAlign;
      if (p == AE_FMT_S24NE4MSB)
      {
        wfxex.Samples.wValidBitsPerSample = 24;
      }
      else if (p <= AE_FMT_S24NE4 && p >= AE_FMT_S24BE4)
      {
        // not supported
        continue;
      }
      else
      {
        wfxex.Samples.wValidBitsPerSample = wfxex.Format.wBitsPerSample;
      }

      SAFE_DESTROY_VOICE(mSourceVoice);
      hr = xaudio2->CreateSourceVoice(&mSourceVoice, &wfxex.Format);

      if (SUCCEEDED(hr))
        deviceInfo.m_dataFormats.push_back((AEDataFormat)p);
    }

    /* Test format for sample rate iteration */
    wfxex.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfxex.dwChannelMask = KSAUDIO_SPEAKER_STEREO;
    wfxex.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfxex.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    wfxex.Format.wBitsPerSample = 16;
    wfxex.Samples.wValidBitsPerSample = 16;
    wfxex.Format.nChannels = 2;
    wfxex.Format.nBlockAlign = wfxex.Format.nChannels * (wfxex.Format.wBitsPerSample >> 3);
    wfxex.Format.nAvgBytesPerSec = wfxex.Format.nSamplesPerSec * wfxex.Format.nBlockAlign;

    for (int j = 0; j < WASAPISampleRateCount; j++)
    {
      SAFE_DESTROY_VOICE(mSourceVoice);
      SAFE_DESTROY_VOICE(mMasterVoice);

      wfxex.Format.nSamplesPerSec = WASAPISampleRates[j];
      wfxex.Format.nAvgBytesPerSec = wfxex.Format.nSamplesPerSec * wfxex.Format.nBlockAlign;

      hr2 = xaudio2->CreateMasteringVoice(&mMasterVoice, wfxex.Format.nChannels, wfxex.Format.nSamplesPerSec,
                                          0, deviceId.c_str(), nullptr, AudioCategory_Media);
      hr = xaudio2->CreateSourceVoice(&mSourceVoice, &wfxex.Format);
      if (SUCCEEDED(hr))
        deviceInfo.m_sampleRates.push_back(WASAPISampleRates[j]);
      else if (wfxex.Format.nSamplesPerSec == 192000 && add192)
      {
        deviceInfo.m_sampleRates.push_back(WASAPISampleRates[j]);
        CLog::Log(LOGNOTICE, __FUNCTION__": sample rate 192khz on device \"%s\" seems to be not supported.", details.strDescription.c_str());
      }
    }
    SAFE_DESTROY_VOICE(mSourceVoice);
    SAFE_DESTROY_VOICE(mMasterVoice);

    deviceInfo.m_deviceName = details.strDeviceId;
    deviceInfo.m_displayName = details.strWinDevType.append(details.strDescription);
    deviceInfo.m_displayNameExtra = std::string("XAudio: ").append(details.strDescription);
    deviceInfo.m_deviceType = details.eDeviceType;
    deviceInfo.m_channels = layoutsByChCount[details.nChannels];

    /* Store the device info */
    deviceInfo.m_wantsIECPassthrough = true;

    if (!deviceInfo.m_streamTypes.empty())
      deviceInfo.m_dataFormats.push_back(AE_FMT_RAW);

    deviceInfoList.push_back(deviceInfo);

    if (details.bDefault)
    {
      deviceInfo.m_deviceName = std::string("default");
      deviceInfo.m_displayName = std::string("default");
      deviceInfo.m_displayNameExtra = std::string("");
      deviceInfo.m_wantsIECPassthrough = true;
      deviceInfoList.push_back(deviceInfo);
    }
  }

failed:

  if (FAILED(hr))
    CLog::Log(LOGERROR, __FUNCTION__": Failed to enumerate XAudio endpoint devices (%s).", WASAPIErrToStr(hr));
}

/// ------------------- Private utility functions -----------------------------------

bool CAESinkXAudio::InitializeInternal(std::string deviceId, AEAudioFormat &format)
{
  std::wstring device = KODI::PLATFORM::WINDOWS::ToW(deviceId);
  WAVEFORMATEXTENSIBLE wfxex = { 0 };

  if ( format.m_dataFormat <= AE_FMT_FLOAT
    || format.m_dataFormat == AE_FMT_RAW)
    CAESinkFactoryWin::BuildWaveFormatExtensible(format, wfxex);
  else
  {
    // planar formats are currently not supported by this sink
    format.m_dataFormat = AE_FMT_FLOAT;
    CAESinkFactoryWin::BuildWaveFormatExtensible(format, wfxex);
  }

  /* Test for incomplete format and provide defaults */
  if (format.m_sampleRate == 0 ||
      format.m_channelLayout == CAEChannelInfo(nullptr) ||
      format.m_dataFormat <= AE_FMT_INVALID ||
      format.m_dataFormat >= AE_FMT_MAX ||
      format.m_channelLayout.Count() == 0)
  {
    wfxex.Format.wFormatTag           = WAVE_FORMAT_EXTENSIBLE;
    wfxex.Format.nChannels            = 2;
    wfxex.Format.nSamplesPerSec       = 44100L;
    wfxex.Format.wBitsPerSample       = 16;
    wfxex.Format.nBlockAlign          = 4;
    wfxex.Samples.wValidBitsPerSample = 16;
    wfxex.Format.cbSize               = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfxex.Format.nAvgBytesPerSec      = wfxex.Format.nBlockAlign * wfxex.Format.nSamplesPerSec;
    wfxex.dwChannelMask               = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    wfxex.SubFormat                   = KSDATAFORMAT_SUBTYPE_PCM;
  }

  bool bdefault = StringUtils::EndsWithNoCase(deviceId, std::string("default"));

  HRESULT hr;
  IXAudio2MasteringVoice* pMasterVoice = nullptr;

  if (!bdefault)
  {
    hr = m_xAudio2->CreateMasteringVoice(&pMasterVoice, wfxex.Format.nChannels, wfxex.Format.nSamplesPerSec,
                                          0, device.c_str(), nullptr, AudioCategory_Media);
  }

  if (!pMasterVoice)
  {
    if (!bdefault)
      CLog::Log(LOGINFO, __FUNCTION__": Could not locate the device named \"%s\" in the list of Xaudio endpoint devices. Trying the default device...", device.c_str());

    // smartphone issue: providing device ID (even default ID) causes E_NOINTERFACE result
    // workaround: device = nullptr will initialize default audio endpoint
    hr = m_xAudio2->CreateMasteringVoice(&pMasterVoice, wfxex.Format.nChannels, wfxex.Format.nSamplesPerSec,
                                          0, 0, nullptr, AudioCategory_Media);
    if (FAILED(hr) || !pMasterVoice)
    {
      CLog::Log(LOGINFO, __FUNCTION__": Could not retrieve the default XAudio audio endpoint (%s).", WASAPIErrToStr(hr));
      return false;
    }
  }

  m_masterVoice = pMasterVoice;

  hr = m_xAudio2->CreateSourceVoice(&m_sourceVoice, &wfxex.Format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &m_voiceCallback);
  if (SUCCEEDED(hr))
  {
    CLog::Log(LOGINFO, __FUNCTION__": Format is Supported - will attempt to Initialize");
    goto initialize;
  }

  if (format.m_dataFormat == AE_FMT_RAW) //No sense in trying other formats for passthrough.
    return false;

  if (g_advancedSettings.CanLogComponent(LOGAUDIO))
    CLog::Log(LOGDEBUG, __FUNCTION__": CreateSourceVoice failed (%s) - trying to find a compatible format", WASAPIErrToStr(hr));

  int closestMatch;
  unsigned int requestedChannels = wfxex.Format.nChannels;
  unsigned int noOfCh;

  /* The requested format is not supported by the device.  Find something that works */
  for (int layout = -1; layout <= (int)ARRAYSIZE(layoutsList); layout++)
  {
    // if requested layout is not supported, try standard layouts with at least
    // the number of channels as requested
    // as the last resort try stereo
    if (layout == ARRAYSIZE(layoutsList))
    {
      wfxex.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
      wfxex.Format.nChannels = 2;
    }
    else if (layout >= 0)
    {
      wfxex.dwChannelMask = CAESinkFactoryWin::ChLayoutToChMask(layoutsList[layout], &noOfCh);
      wfxex.Format.nChannels = noOfCh;
      if (noOfCh < requestedChannels)
        continue;
    }

    for (int j = 0; j < sizeof(testFormats)/sizeof(sampleFormat); j++)
    {
      closestMatch = -1;

      wfxex.Format.wFormatTag           = WAVE_FORMAT_EXTENSIBLE;
      wfxex.SubFormat                   = testFormats[j].subFormat;
      wfxex.Format.wBitsPerSample       = testFormats[j].bitsPerSample;
      wfxex.Samples.wValidBitsPerSample = testFormats[j].validBitsPerSample;
      wfxex.Format.nBlockAlign          = wfxex.Format.nChannels * (wfxex.Format.wBitsPerSample >> 3);

      for (int i = 0 ; i < WASAPISampleRateCount; i++)
      {
        wfxex.Format.nSamplesPerSec    = WASAPISampleRates[i];
        wfxex.Format.nAvgBytesPerSec   = wfxex.Format.nSamplesPerSec * wfxex.Format.nBlockAlign;

        hr = m_xAudio2->CreateMasteringVoice(&m_masterVoice, wfxex.Format.nChannels, wfxex.Format.nSamplesPerSec,
                                             0, device.c_str(), nullptr, AudioCategory_Media);
        if (SUCCEEDED(hr))
        {
          hr = m_xAudio2->CreateSourceVoice(&m_sourceVoice, &wfxex.Format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &m_voiceCallback);
          if (SUCCEEDED(hr))
          {
            /* If the current sample rate matches the source then stop looking and use it */
            if ((WASAPISampleRates[i] == format.m_sampleRate) && (testFormats[j].subFormatType <= format.m_dataFormat))
              goto initialize;
            /* If this rate is closer to the source then the previous one, save it */
            else if (closestMatch < 0 || abs((int)WASAPISampleRates[i] - (int)format.m_sampleRate) < abs((int)WASAPISampleRates[closestMatch] - (int)format.m_sampleRate))
              closestMatch = i;
          }
        }

        if (FAILED(hr))
          CLog::Log(LOGERROR, __FUNCTION__": creating voices failed (%s)", WASAPIErrToStr(hr));
      }

      if (closestMatch >= 0)
      {
        wfxex.Format.nSamplesPerSec    = WASAPISampleRates[closestMatch];
        wfxex.Format.nAvgBytesPerSec   = wfxex.Format.nSamplesPerSec * wfxex.Format.nBlockAlign;
        goto initialize;
      }
    }
  }

  CLog::Log(LOGERROR, __FUNCTION__": Unable to locate a supported output format for the device.  Check the speaker settings in the control panel.");

  /* We couldn't find anything supported. This should never happen      */
  /* unless the user set the wrong speaker setting in the control panel */
  return false;

initialize:

  CAESinkFactoryWin::AEChannelsFromSpeakerMask(m_channelLayout, wfxex.dwChannelMask);
  format.m_channelLayout = m_channelLayout;

  /* When the stream is raw, the values in the format structure are set to the link    */
  /* parameters, so store the encoded stream values here for the IsCompatible function */
  m_encodedChannels   = wfxex.Format.nChannels;
  m_encodedSampleRate = (format.m_dataFormat == AE_FMT_RAW) ? format.m_streamInfo.m_sampleRate : format.m_sampleRate;

  /* Set up returned sink format for engine */
  if (format.m_dataFormat != AE_FMT_RAW)
  {
    if (wfxex.Format.wBitsPerSample == 32)
    {
      if (wfxex.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
        format.m_dataFormat = AE_FMT_FLOAT;
      else if (wfxex.Samples.wValidBitsPerSample == 32)
        format.m_dataFormat = AE_FMT_S32NE;
      else
        format.m_dataFormat = AE_FMT_S24NE4MSB;
    }
    else if (wfxex.Format.wBitsPerSample == 24)
      format.m_dataFormat = AE_FMT_S24NE3;
    else
      format.m_dataFormat = AE_FMT_S16NE;
  }

  format.m_sampleRate    = wfxex.Format.nSamplesPerSec; //PCM: Sample rate.  RAW: Link speed
  format.m_frameSize     = (wfxex.Format.wBitsPerSample >> 3) * wfxex.Format.nChannels;

  if (format.m_dataFormat == AE_FMT_RAW)
    format.m_dataFormat = AE_FMT_S16NE;

  hr = m_sourceVoice->Start(0, XAUDIO2_COMMIT_NOW);
  if (FAILED(hr))
  {
    CLog::Log(LOGERROR, __FUNCTION__": Voice start failed : %s", WASAPIErrToStr(hr));
    CLog::Log(LOGDEBUG, "  Sample Rate     : %d", wfxex.Format.nSamplesPerSec);
    CLog::Log(LOGDEBUG, "  Sample Format   : %s", CAEUtil::DataFormatToStr(format.m_dataFormat));
    CLog::Log(LOGDEBUG, "  Bits Per Sample : %d", wfxex.Format.wBitsPerSample);
    CLog::Log(LOGDEBUG, "  Valid Bits/Samp : %d", wfxex.Samples.wValidBitsPerSample);
    CLog::Log(LOGDEBUG, "  Channel Count   : %d", wfxex.Format.nChannels);
    CLog::Log(LOGDEBUG, "  Block Align     : %d", wfxex.Format.nBlockAlign);
    CLog::Log(LOGDEBUG, "  Avg. Bytes Sec  : %d", wfxex.Format.nAvgBytesPerSec);
    CLog::Log(LOGDEBUG, "  Samples/Block   : %d", wfxex.Samples.wSamplesPerBlock);
    CLog::Log(LOGDEBUG, "  Format cBSize   : %d", wfxex.Format.cbSize);
    CLog::Log(LOGDEBUG, "  Channel Layout  : %s", ((std::string)format.m_channelLayout).c_str());
    CLog::Log(LOGDEBUG, "  Channel Mask    : %d", wfxex.dwChannelMask);
    return false;
  }

  XAUDIO2_PERFORMANCE_DATA perfData;
  m_xAudio2->GetPerformanceData(&perfData);
  if (!perfData.TotalSourceVoiceCount)
  {
    CLog::Log(LOGERROR, __FUNCTION__": GetPerformanceData Failed : %s", WASAPIErrToStr(hr));
    return false;
  }

  m_uiBufferLen = (int)(format.m_sampleRate * 0.015);
  m_dwFrameSize = wfxex.Format.nBlockAlign;
  m_dwChunkSize = m_dwFrameSize * m_uiBufferLen;
  m_dwBufferLen = m_dwChunkSize * 4; 
  m_AvgBytesPerSec = wfxex.Format.nAvgBytesPerSec;

  CLog::Log(LOGINFO, __FUNCTION__": XAudio Sink Initialized using: %s, %d, %d",
                                     CAEUtil::DataFormatToStr(format.m_dataFormat),
                                     wfxex.Format.nSamplesPerSec,
                                     wfxex.Format.nChannels);

  m_sourceVoice->Stop();

  return true;
}

void CAESinkXAudio::Drain()
{
  if(!m_sourceVoice)
    return;

  AEDelayStatus status;
  GetDelay(status);

  Sleep((DWORD)(status.GetDelay() * 500));

  if (m_running)
  {
    try
    {
      m_sourceVoice->Stop();
    }
    catch (...)
    {
      CLog::Log(LOGDEBUG, "%s: Invalidated source voice - Releasing", __FUNCTION__);
    }
  }
  m_running = false;
}

bool CAESinkXAudio::IsUSBDevice()
{
#if 0 // TODO 
  IPropertyStore *pProperty = nullptr;
  PROPVARIANT varName;
  PropVariantInit(&varName);
  bool ret = false;

  HRESULT hr = m_pDevice->OpenPropertyStore(STGM_READ, &pProperty);
  if (!SUCCEEDED(hr))
    return ret;
  hr = pProperty->GetValue(PKEY_Device_EnumeratorName, &varName);

  std::string str = localWideToUtf(varName.pwszVal);
  StringUtils::ToUpper(str);
  ret = (str == "USB");
  PropVariantClear(&varName);
  SAFE_RELEASE(pProperty);
#endif
  return false;
}
