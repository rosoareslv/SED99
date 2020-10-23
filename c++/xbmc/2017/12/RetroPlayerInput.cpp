/*
 *      Copyright (C) 2017 Team Kodi
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
 *  along with this Program; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "RetroPlayerInput.h"
#include "peripherals/Peripherals.h"
#include "peripherals/EventPollHandle.h"

using namespace KODI;
using namespace RETRO;

CRetroPlayerInput::CRetroPlayerInput(PERIPHERALS::CPeripherals &peripheralManager) :
  m_peripheralManager(peripheralManager)
{
  m_inputPollHandle = m_peripheralManager.RegisterEventPoller();
}

CRetroPlayerInput::~CRetroPlayerInput()
{
  m_inputPollHandle.reset();
}

void CRetroPlayerInput::SetSpeed(double speed)
{
  if (speed != 0)
    m_inputPollHandle->Activate();
  else
    m_inputPollHandle->Deactivate();
}

void CRetroPlayerInput::PollInput()
{
  m_inputPollHandle->HandleEvents(true);
}
