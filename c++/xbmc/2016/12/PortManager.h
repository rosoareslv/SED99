/*
 *      Copyright (C) 2015-2016 Team Kodi
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
#pragma once

#include "peripherals/PeripheralTypes.h"
#include "threads/CriticalSection.h"
#include "utils/Observer.h"

#include <map>
#include <vector>

namespace JOYSTICK { class IInputHandler; }
namespace PERIPHERALS { class CPeripheral; }

namespace GAME
{
  /*!
   * \brief Class to manage ports opened by game clients
   */
  class CPortManager : public Observable
  {
  private:
    CPortManager(void) = default;

  public:
    static CPortManager& GetInstance();

    /*!
     * \brief Request a new port be opened with input on that port sent to the
     *        specified handler.
     *
     * \param handler      The instance accepting all input delivered to the port
     * \param port         The port number belonging to the game client
     * \param requiredType Used to restrict port to devices of only a certain type
     */
    void OpenPort(JOYSTICK::IInputHandler* handler,
                  unsigned int port,
                  PERIPHERALS::PeripheralType requiredType = PERIPHERALS::PERIPHERAL_UNKNOWN);

    /*!
     * \brief Close an opened port
     *
     * \param handler  The handler used to open the port
     */
    void ClosePort(JOYSTICK::IInputHandler* handler);

    /*!
     * \brief Map a list of devices to the available ports
     *
     * \param devices  The devices capable of providing input to the ports
     * \param portMap  The resulting map of devices to ports
     *
     * If there are more devices than open ports, multiple devices may be assigned
     * to the same port. If a device requests a specific port, this function will
     * attempt to honor that request.
     */
    void MapDevices(const PERIPHERALS::PeripheralVector& devices,
                    std::map<PERIPHERALS::PeripheralPtr, JOYSTICK::IInputHandler*>& deviceToPortMap);

  private:
    JOYSTICK::IInputHandler* AssignToPort(const PERIPHERALS::PeripheralPtr& device, bool checkPortNumber = true);

    struct SPort
    {
      JOYSTICK::IInputHandler*    handler; // Input handler for this port
      unsigned int                port;    // Port number belonging to the game client
      PERIPHERALS::PeripheralType requiredType;
      void*                       device;
    };

    std::vector<SPort> m_ports;
    CCriticalSection   m_mutex;
  };
}
