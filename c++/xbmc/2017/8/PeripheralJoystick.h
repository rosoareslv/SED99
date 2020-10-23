/*
 *      Copyright (C) 2014-2017 Team Kodi
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

#include "Peripheral.h"
#include "input/joysticks/IDriverReceiver.h"
#include "input/joysticks/JoystickMonitor.h"
#include "input/joysticks/JoystickTypes.h"
#include "threads/CriticalSection.h"

#include <memory>
#include <string>
#include <vector>

#define JOYSTICK_PORT_UNKNOWN  (-1)

namespace KODI
{
namespace JOYSTICK
{
  class CDeadzoneFilter;
  class CKeymapHandling;
  class CRumbleGenerator;
  class IButtonMap;
  class IDriverHandler;
}
}

namespace PERIPHERALS
{
  class CPeripherals;

  class CPeripheralJoystick : public CPeripheral, //! @todo extend CPeripheralHID
                              public KODI::JOYSTICK::IDriverReceiver
  {
  public:
    CPeripheralJoystick(CPeripherals& manager, const PeripheralScanResult& scanResult, CPeripheralBus* bus);

    ~CPeripheralJoystick(void) override;

    // implementation of CPeripheral
    bool InitialiseFeature(const PeripheralFeature feature) override;
    void OnUserNotification() override;
    bool TestFeature(PeripheralFeature feature) override;
    void RegisterJoystickDriverHandler(KODI::JOYSTICK::IDriverHandler* handler, bool bPromiscuous) override;
    void UnregisterJoystickDriverHandler(KODI::JOYSTICK::IDriverHandler* handler) override;
    KODI::JOYSTICK::IDriverReceiver* GetDriverReceiver() override { return this; }
    IKeymap *GetKeymap(const std::string &controllerId) override;

    bool OnButtonMotion(unsigned int buttonIndex, bool bPressed);
    bool OnHatMotion(unsigned int hatIndex, KODI::JOYSTICK::HAT_STATE state);
    bool OnAxisMotion(unsigned int axisIndex, float position);
    void ProcessAxisMotions(void);

    // implementation of IDriverReceiver
    bool SetMotorState(unsigned int motorIndex, float magnitude) override;

    /*!
     * \brief Get the name of the driver or API providing this joystick
     */
    const std::string& Provider(void) const { return m_strProvider; }

    /*!
     * \brief Get the specific port number requested by this joystick
     *
     * This could indicate that the joystick is connected to a hardware port
     * with a number label; some controllers, such as the Xbox 360 controller,
     * also have LEDs that indicate the controller is on a specific port.
     *
     * \return The 0-indexed port number, or JOYSTICK_PORT_UNKNOWN if no port is requested
     */
    int RequestedPort(void) const { return m_requestedPort; }

    /*!
     * \brief Get the number of elements reported by the driver
     */
    unsigned int ButtonCount(void) const { return m_buttonCount; }
    unsigned int HatCount(void) const    { return m_hatCount; }
    unsigned int AxisCount(void) const   { return m_axisCount; }
    unsigned int MotorCount(void) const  { return m_motorCount; }
    bool SupportsPowerOff(void) const    { return m_supportsPowerOff; }

    /*!
     * \brief Set joystick properties
     */
    void SetProvider(const std::string& provider) { m_strProvider   = provider; }
    void SetRequestedPort(int port)               { m_requestedPort = port; }
    void SetButtonCount(unsigned int buttonCount) { m_buttonCount   = buttonCount; }
    void SetHatCount(unsigned int hatCount)       { m_hatCount      = hatCount; }
    void SetAxisCount(unsigned int axisCount)     { m_axisCount     = axisCount; }
    void SetMotorCount(unsigned int motorCount); // specialized to update m_features
    void SetSupportsPowerOff(bool bSupportsPowerOff); // specialized to update m_features

  protected:
    void InitializeDeadzoneFiltering();

    void PowerOff();

    struct DriverHandler
    {
      KODI::JOYSTICK::IDriverHandler* handler;
      bool bPromiscuous;
    };

    std::string                         m_strProvider;
    int                                 m_requestedPort;
    unsigned int                        m_buttonCount;
    unsigned int                        m_hatCount;
    unsigned int                        m_axisCount;
    unsigned int                        m_motorCount;
    bool                                m_supportsPowerOff;
    std::unique_ptr<KODI::JOYSTICK::CKeymapHandling> m_appInput;
    std::unique_ptr<KODI::JOYSTICK::CRumbleGenerator> m_rumbleGenerator;
    KODI::JOYSTICK::CJoystickMonitor          m_joystickMonitor;
    std::unique_ptr<KODI::JOYSTICK::IButtonMap>      m_buttonMap;
    std::unique_ptr<KODI::JOYSTICK::CDeadzoneFilter> m_deadzoneFilter;
    std::vector<DriverHandler>          m_driverHandlers;
    CCriticalSection                    m_handlerMutex;
  };
}
