/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "powermanagement/IPowerSyscall.h"

#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

class CCocoaPowerSyscall : public CPowerSyscallWithoutEvents
{
public:
  CCocoaPowerSyscall();
  ~CCocoaPowerSyscall();

  static IPowerSyscall* CreateInstance();
  static void Register();

  virtual bool Powerdown(void);
  virtual bool Suspend(void);
  virtual bool Hibernate(void);
  virtual bool Reboot(void);

  virtual bool CanPowerdown(void);
  virtual bool CanSuspend(void);
  virtual bool CanHibernate(void);
  virtual bool CanReboot(void);
          bool HasBattery(void);
  virtual int  BatteryLevel(void);

  virtual bool PumpPowerEvents(IPowerEventsCallback *callback);
private:
          void CreateOSPowerCallBacks(void);
          void DeleteOSPowerCallBacks(void);
  static  void OSPowerCallBack(void *refcon, io_service_t service, natural_t msg_type, void *msg_arg);
  static  void OSPowerSourceCallBack(void *refcon);

  // OS Power
  bool m_OnResume;
  bool m_OnSuspend;
  // OS Power Source
  bool m_OnBattery;
  int  m_HasBattery;
  int  m_BatteryPercent;
  bool m_SentBatteryMessage;

  io_connect_t m_root_port;             // a reference to the Root Power Domain IOService
  io_object_t  m_notifier_object;       // notifier object, used to deregister later
  IONotificationPortRef m_notify_port;  // notification port allocated by IORegisterForSystemPower
  CFRunLoopSourceRef m_power_source;
};
