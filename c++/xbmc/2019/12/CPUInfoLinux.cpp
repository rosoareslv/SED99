/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "CPUInfoLinux.h"

#include "utils/StringUtils.h"
#include "utils/SysfsUtils.h"
#include "utils/Temperature.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

#if (defined(__arm__) && defined(HAS_NEON)) || defined(__aarch64__)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#endif

#include <unistd.h>

namespace
{
enum CpuStates
{
  STATE_USER,
  STATE_NICE,
  STATE_SYSTEM,
  STATE_IDLE,
  STATE_IOWAIT,
  STATE_IRQ,
  STATE_SOFTIRQ,
  STATE_STEAL,
  STATE_GUEST,
  STATE_GUEST_NICE,
  STATE_MAX
};

struct CpuData
{
public:
  std::size_t GetActiveTime() const
  {
    return state[STATE_USER] + state[STATE_NICE] + state[STATE_SYSTEM] + state[STATE_IRQ] +
           state[STATE_SOFTIRQ] + state[STATE_STEAL] + state[STATE_GUEST] + state[STATE_GUEST_NICE];
  }

  std::size_t GetIdleTime() const { return state[STATE_IDLE] + state[STATE_IOWAIT]; }

  std::size_t GetTotalTime() const { return GetActiveTime() + GetIdleTime(); }

  std::string cpu;
  std::size_t state[STATE_MAX];
};
} // namespace

std::shared_ptr<CCPUInfo> CCPUInfo::GetCPUInfo()
{
  return std::make_shared<CCPUInfoLinux>();
}

CCPUInfoLinux::CCPUInfoLinux()
{
  // new socs use the sysfs soc interface to describe the hardware
  if (SysfsUtils::Has("/sys/bus/soc/devices/soc0"))
  {
    std::string machine;
    std::string family;
    std::string socId;
    if (SysfsUtils::Has("/sys/bus/soc/devices/soc0/machine"))
      SysfsUtils::GetString("/sys/bus/soc/devices/soc0/machine", machine);
    if (SysfsUtils::Has("/sys/bus/soc/devices/soc0/family"))
      SysfsUtils::GetString("/sys/bus/soc/devices/soc0/family", family);
    if (SysfsUtils::Has("/sys/bus/soc/devices/soc0/soc_id"))
      SysfsUtils::GetString("/sys/bus/soc/devices/soc0/soc_id", socId);
    if (m_cpuHardware.empty() && !machine.empty())
      m_cpuHardware = machine;
    if (!family.empty() && !socId.empty())
      m_cpuSoC = family + " " + socId;
  }

  m_cpuCount = sysconf(_SC_NPROCESSORS_ONLN);

  for (int core = 0; core < m_cpuCount; core++)
  {
    CoreInfo coreInfo;
    coreInfo.m_id = core;
    m_cores.emplace_back(coreInfo);
  }

#if defined(__i386__) || defined(__x86_64__)
  unsigned int eax;
  unsigned int ebx;
  unsigned int ecx;
  unsigned int edx;

  m_cpuVendor.clear();

  if (__get_cpuid(CPUID_INFOTYPE_MANUFACTURER, &eax, &ebx, &ecx, &edx))
  {
    m_cpuVendor.append(reinterpret_cast<const char*>(&ebx), 4);
    m_cpuVendor.append(reinterpret_cast<const char*>(&edx), 4);
    m_cpuVendor.append(reinterpret_cast<const char*>(&ecx), 4);
  }

  if (__get_cpuid(CPUID_INFOTYPE_EXTENDED_IMPLEMENTED, &eax, &ebx, &ecx, &edx))
  {
    if (eax >= CPUID_INFOTYPE_PROCESSOR_3)
    {
      m_cpuModel.clear();

      if (__get_cpuid(CPUID_INFOTYPE_PROCESSOR_1, &eax, &ebx, &ecx, &edx))
      {
        m_cpuModel.append(reinterpret_cast<const char*>(&eax), 4);
        m_cpuModel.append(reinterpret_cast<const char*>(&ebx), 4);
        m_cpuModel.append(reinterpret_cast<const char*>(&ecx), 4);
        m_cpuModel.append(reinterpret_cast<const char*>(&edx), 4);
      }

      if (__get_cpuid(CPUID_INFOTYPE_PROCESSOR_2, &eax, &ebx, &ecx, &edx))
      {
        m_cpuModel.append(reinterpret_cast<const char*>(&eax), 4);
        m_cpuModel.append(reinterpret_cast<const char*>(&ebx), 4);
        m_cpuModel.append(reinterpret_cast<const char*>(&ecx), 4);
        m_cpuModel.append(reinterpret_cast<const char*>(&edx), 4);
      }

      if (__get_cpuid(CPUID_INFOTYPE_PROCESSOR_3, &eax, &ebx, &ecx, &edx))
      {
        m_cpuModel.append(reinterpret_cast<const char*>(&eax), 4);
        m_cpuModel.append(reinterpret_cast<const char*>(&ebx), 4);
        m_cpuModel.append(reinterpret_cast<const char*>(&ecx), 4);
        m_cpuModel.append(reinterpret_cast<const char*>(&edx), 4);
      }
    }
  }

  if (__get_cpuid(CPUID_INFOTYPE_STANDARD, &eax, &eax, &ecx, &edx))
  {
    if (edx & CPUID_00000001_EDX_MMX)
      m_cpuFeatures |= CPU_FEATURE_MMX;

    if (edx & CPUID_00000001_EDX_SSE)
      m_cpuFeatures |= CPU_FEATURE_SSE;

    if (edx & CPUID_00000001_EDX_SSE2)
      m_cpuFeatures |= CPU_FEATURE_SSE2;

    if (ecx & CPUID_00000001_ECX_SSE3)
      m_cpuFeatures |= CPU_FEATURE_SSE3;

    if (ecx & CPUID_00000001_ECX_SSSE3)
      m_cpuFeatures |= CPU_FEATURE_SSSE3;

    if (ecx & CPUID_00000001_ECX_SSE4)
      m_cpuFeatures |= CPU_FEATURE_SSE4;

    if (ecx & CPUID_00000001_ECX_SSE42)
      m_cpuFeatures |= CPU_FEATURE_SSE42;
  }

  if (__get_cpuid(CPUID_INFOTYPE_EXTENDED_IMPLEMENTED, &eax, &eax, &ecx, &edx))
  {
    if (eax >= CPUID_INFOTYPE_EXTENDED)
    {
      if (edx & CPUID_80000001_EDX_MMX)
        m_cpuFeatures |= CPU_FEATURE_MMX;

      if (edx & CPUID_80000001_EDX_MMX2)
        m_cpuFeatures |= CPU_FEATURE_MMX2;

      if (edx & CPUID_80000001_EDX_3DNOW)
        m_cpuFeatures |= CPU_FEATURE_3DNOW;

      if (edx & CPUID_80000001_EDX_3DNOWEXT)
        m_cpuFeatures |= CPU_FEATURE_3DNOWEXT;
    }
  }
#else
  std::ifstream cpuinfo("/proc/cpuinfo");
  std::regex re(".*: (.*)$");

  for (std::string line; std::getline(cpuinfo, line);)
  {
    std::smatch match;

    if (std::regex_match(line, match, re))
    {
      if (match.size() == 2)
      {
        std::ssub_match value = match[1];

        if (line.find("model name") != std::string::npos)
          m_cpuModel = value.str();

        if (line.find("BogoMIPS") != std::string::npos)
          m_cpuBogoMips = value.str();

        if (line.find("Hardware") != std::string::npos)
          m_cpuHardware = value.str();

        if (line.find("Serial") != std::string::npos)
          m_cpuSerial = value.str();

        if (line.find("Revision") != std::string::npos)
          m_cpuRevision = value.str();
      }
    }
  }
#endif

#if defined(HAS_NEON) && defined(__arm__)
  if (getauxval(AT_HWCAP) & HWCAP_NEON)
    m_cpuFeatures |= CPU_FEATURE_NEON;
#endif

  // Set MMX2 when SSE is present as SSE is a superset of MMX2 and Intel doesn't set the MMX2 cap
  if (m_cpuFeatures & CPU_FEATURE_SSE)
    m_cpuFeatures |= CPU_FEATURE_MMX2;
}

int CCPUInfoLinux::GetUsedPercentage()
{
  if (!m_nextUsedReadTime.IsTimePast())
    return m_lastUsedPercentage;

  std::vector<CpuData> cpuData;

  std::ifstream infile("/proc/stat");
  std::string line;
  for (std::string line; std::getline(infile, line);)
  {
    if (line.find("cpu") != std::string::npos)
    {
      std::istringstream ss(line);
      CpuData info;

      ss >> info.cpu;

      for (int i = 0; i < STATE_MAX; i++)
      {
        ss >> info.state[i];
      }

      cpuData.emplace_back(info);
    }
  }

  auto activeTime = cpuData.front().GetActiveTime() - m_activeTime;
  auto idleTime = cpuData.front().GetIdleTime() - m_idleTime;
  auto totalTime = cpuData.front().GetTotalTime() - m_totalTime;

  m_activeTime += activeTime;
  m_idleTime += idleTime;
  m_totalTime += totalTime;

  m_lastUsedPercentage = activeTime * 100.0f / totalTime;
  m_nextUsedReadTime.Set(MINIMUM_TIME_BETWEEN_READS);

  cpuData.erase(cpuData.begin());

  for (std::size_t core = 0; core < cpuData.size(); core++)
  {
    auto activeTime = cpuData[core].GetActiveTime() - m_cores[core].m_activeTime;
    auto idleTime = cpuData[core].GetIdleTime() - m_cores[core].m_idleTime;
    auto totalTime = cpuData[core].GetTotalTime() - m_cores[core].m_totalTime;

    m_cores[core].m_usagePercent = activeTime * 100.0f / totalTime;

    m_cores[core].m_activeTime += activeTime;
    m_cores[core].m_idleTime += idleTime;
    m_cores[core].m_totalTime += totalTime;
  }

  return static_cast<int>(m_lastUsedPercentage);
}

float CCPUInfoLinux::GetCPUFrequency()
{
  int value{-1};
  if (SysfsUtils::Has("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq"))
    SysfsUtils::GetInt("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", value);

  value /= 1000.0;

  return value;
}

bool CCPUInfoLinux::GetTemperature(CTemperature& temperature)
{
  if (!SysfsUtils::Has("/sys/class/hwmon/hwmon0/temp1_input"))
    return CCPUInfoPosix::GetTemperature(temperature);

  int value{-1};
  char scale{'c'};

  SysfsUtils::GetInt("/sys/class/hwmon/hwmon0/temp1_input", value);
  value = value / 1000.0;
  scale = 'c';

  if (scale == 'C' || scale == 'c')
    temperature = CTemperature::CreateFromCelsius(value);
  else if (scale == 'F' || scale == 'f')
    temperature = CTemperature::CreateFromFahrenheit(value);
  else
    return false;

  temperature.SetValid(true);

  return true;
}
