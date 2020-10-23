/*
 *      Copyright (C) 2015-2017 Team Kodi
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

#include "PortMapper.h"
#include "Port.h"
#include "PortManager.h"
#include "peripherals/devices/Peripheral.h"
#include "peripherals/Peripherals.h"

using namespace KODI;
using namespace GAME;
using namespace JOYSTICK;
using namespace PERIPHERALS;

CPortMapper::CPortMapper(PERIPHERALS::CPeripherals& peripheralManager, CPortManager& portManager) :
  m_peripheralManager(peripheralManager),
  m_portManager(portManager)
{
  m_peripheralManager.RegisterObserver(this);
  m_portManager.RegisterObserver(this);
}

CPortMapper::~CPortMapper()
{
  m_portManager.UnregisterObserver(this);
  m_peripheralManager.UnregisterObserver(this);
}

void CPortMapper::Notify(const Observable &obs, const ObservableMessage msg)
{
  switch (msg)
  {
    case ObservableMessagePeripheralsChanged:
    case ObservableMessagePortsChanged:
      ProcessPeripherals();
      break;
    default:
      break;
  }
}

void CPortMapper::ProcessPeripherals()
{
  PeripheralVector joysticks;
  m_peripheralManager.GetPeripheralsWithFeature(joysticks, FEATURE_JOYSTICK);

  // Perform the port mapping
  std::map<CPeripheral*, IInputHandler*> newPortMap;
  m_portManager.MapDevices(joysticks, newPortMap);

  // Update each joystick
  for (auto& joystick : joysticks)
  {
    auto itConnectedPort = newPortMap.find(joystick.get());
    auto itDisconnectedPort = m_portMap.find(joystick);

    IInputHandler* newHandler = itConnectedPort != newPortMap.end() ? itConnectedPort->second : nullptr;
    IInputHandler* oldHandler = itDisconnectedPort != m_portMap.end() ? itDisconnectedPort->second->InputHandler() : nullptr;

    if (oldHandler != newHandler)
    {
      // Unregister old handler
      if (oldHandler != nullptr)
      {
        PortPtr& oldPort = itDisconnectedPort->second;

        oldPort->UnregisterInput(joystick.get());

        m_portMap.erase(itDisconnectedPort);
      }

      // Register new handler
      if (newHandler != nullptr)
      {
        CGameClient *gameClient = m_portManager.GameClient(newHandler);
        if (gameClient)
        {
          PortPtr newPort(new CPort(newHandler, *gameClient));

          newPort->RegisterInput(joystick.get());

          m_portMap.insert(std::make_pair(std::move(joystick), std::move(newPort)));
        }
      }
    }
  }
}
