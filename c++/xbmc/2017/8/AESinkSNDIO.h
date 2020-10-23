/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
#pragma once
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

#include "cores/AudioEngine/Interfaces/AESink.h"
#include "cores/AudioEngine/Utils/AEDeviceInfo.h"
#include <stdint.h>
#include <sndio.h>

#include "threads/CriticalSection.h"

class CAESinkSNDIO : public IAESink
{
public:
  const char *GetName() override { return "sndio"; }

  CAESinkSNDIO();
  ~CAESinkSNDIO() override;

  bool Initialize(AEAudioFormat &format, std::string &device) override;
  void Deinitialize() override;

  virtual void Stop();
  void GetDelay(AEDelayStatus& status) override;
  double GetCacheTotal() override { return 0.0; }
  unsigned int AddPackets(uint8_t **data, unsigned int frames, unsigned int offset) override;
  void Drain() override;
  static void EnumerateDevicesEx(AEDeviceInfoList &list, bool force = false);
private:
  void AudioFormatToPar(AEAudioFormat& format);
  bool ParToAudioFormat(AEAudioFormat& format);
  static void OnmoveCb(void *arg, int delta);

  struct sio_hdl *m_hdl;
  struct sio_par m_par;
  ssize_t m_played;
  ssize_t m_written;
};

