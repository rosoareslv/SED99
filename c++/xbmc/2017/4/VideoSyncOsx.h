#pragma once
/*
 *      Copyright (C) 2005-2014 Team Kodi
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "windowing/VideoSync.h"
#include "guilib/DispResource.h"
#include "threads/Event.h"

class CVideoSyncOsx : public CVideoSync, IDispResource
{
public:

  CVideoSyncOsx(void *clock) :
    CVideoSync(clock),
    m_LastVBlankTime(0),
    m_displayLost(false),
    m_displayReset(false){};
  
  // CVideoSync interface
  virtual bool Setup(PUPDATECLOCK func) override;
  virtual void Run(std::atomic<bool>& stop) override;
  virtual void Cleanup() override;
  virtual float GetFps() override;
  virtual void RefreshChanged() override;
  
  // IDispResource interface
  virtual void OnLostDisplay() override;
  virtual void OnResetDisplay() override;

  // used in the displaylink callback
  void VblankHandler(int64_t nowtime, uint32_t timebase);
  
private:
  virtual bool InitDisplayLink();
  virtual void DeinitDisplayLink();

  int64_t m_LastVBlankTime;  //timestamp of the last vblank, used for calculating how many vblanks happened
  volatile bool m_displayLost;
  volatile bool m_displayReset;
  CEvent m_lostEvent;
};

