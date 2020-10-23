/*
 *      Copyright (C) 2005-2015 Team XBMC
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
 *  along with Kodi; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "network/EventServer.h"
#include "network/Network.h"
#include "threads/SystemClock.h"
#include "system.h"
#include "Application.h"
#include "events/EventLog.h"
#include "events/NotificationEvent.h"
#include "interfaces/builtins/Builtins.h"
#include "utils/JobManager.h"
#include "utils/Variant.h"
#include "LangInfo.h"
#include "utils/Screenshot.h"
#include "Util.h"
#include "URL.h"
#include "guilib/TextureManager.h"
#include "cores/IPlayer.h"
#include "cores/VideoPlayer/DVDFileInfo.h"
#include "cores/AudioEngine/Engines/ActiveAE/AudioDSPAddons/ActiveAEDSP.h"
#include "cores/AudioEngine/Interfaces/AE.h"
#include "cores/AudioEngine/Utils/AEUtil.h"
#include "cores/playercorefactory/PlayerCoreFactory.h"
#include "PlayListPlayer.h"
#include "Autorun.h"
#include "video/Bookmark.h"
#include "video/VideoLibraryQueue.h"
#include "music/MusicLibraryQueue.h"
#include "guilib/GUIControlProfiler.h"
#include "utils/LangCodeExpander.h"
#include "GUIInfoManager.h"
#include "playlists/PlayListFactory.h"
#include "guilib/GUIFontManager.h"
#include "guilib/GUIColorManager.h"
#include "guilib/StereoscopicsManager.h"
#include "addons/BinaryAddonCache.h"
#include "addons/LanguageResource.h"
#include "addons/Skin.h"
#include "addons/VFSEntry.h"
#include "interfaces/generic/ScriptInvocationManager.h"
#ifdef HAS_PYTHON
#include "interfaces/python/XBPython.h"
#endif
#include "input/ActionTranslator.h"
#include "input/ButtonTranslator.h"
#include "guilib/GUIAudioManager.h"
#include "GUIPassword.h"
#include "input/InertialScrollingHandler.h"
#include "messaging/ThreadMessage.h"
#include "messaging/ApplicationMessenger.h"
#include "messaging/helpers/DialogHelper.h"
#include "messaging/helpers/DialogOKHelper.h"
#include "SectionLoader.h"
#include "cores/DllLoader/DllLoaderContainer.h"
#include "GUIUserMessages.h"
#include "filesystem/Directory.h"
#include "filesystem/DirectoryCache.h"
#include "filesystem/StackDirectory.h"
#include "filesystem/SpecialProtocol.h"
#include "filesystem/DllLibCurl.h"
#include "filesystem/PluginDirectory.h"
#include "utils/SystemInfo.h"
#include "utils/TimeUtils.h"
#include "GUILargeTextureManager.h"
#include "TextureCache.h"
#include "playlists/SmartPlayList.h"
#include "playlists/PlayList.h"
#include "profiles/ProfilesManager.h"
#include "windowing/WinSystem.h"
#include "powermanagement/PowerManager.h"
#include "powermanagement/DPMSSupport.h"
#include "settings/Settings.h"
#include "settings/AdvancedSettings.h"
#include "settings/DisplaySettings.h"
#include "settings/MediaSettings.h"
#include "settings/SkinSettings.h"
#include "guilib/LocalizeStrings.h"
#include "utils/CPUInfo.h"
#include "utils/FileExtensionProvider.h"
#include "utils/log.h"
#include "SeekHandler.h"
#include "ServiceBroker.h"

#include "input/KeyboardLayoutManager.h"

#ifdef HAS_UPNP
#include "network/upnp/UPnP.h"
#include "filesystem/UPnPDirectory.h"
#endif
#if defined(TARGET_POSIX) && defined(HAS_FILESYSTEM_SMB)
#include "filesystem/SMBDirectory.h"
#endif
#ifdef HAS_FILESYSTEM_NFS
#include "filesystem/NFSFile.h"
#endif
#ifdef HAS_FILESYSTEM_SFTP
#include "filesystem/SFTPFile.h"
#endif
#include "PartyModeManager.h"
#include "network/ZeroconfBrowser.h"
#ifndef TARGET_POSIX
#include "threads/platform/win/Win32Exception.h"
#endif
#ifdef HAS_DBUS
#include <dbus/dbus.h>
#endif
#include "interfaces/json-rpc/JSONRPC.h"
#include "interfaces/AnnouncementManager.h"
#include "peripherals/Peripherals.h"
#include "peripherals/devices/PeripheralImon.h"
#include "music/infoscanner/MusicInfoScanner.h"

// Windows includes
#include "guilib/GUIWindowManager.h"
#include "video/dialogs/GUIDialogVideoInfo.h"
#include "windows/GUIWindowScreensaver.h"
#include "video/PlayerController.h"

// Dialog includes
#include "video/dialogs/GUIDialogVideoBookmarks.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "dialogs/GUIDialogSubMenu.h"
#include "dialogs/GUIDialogButtonMenu.h"
#include "dialogs/GUIDialogSimpleMenu.h"
#include "dialogs/GUIDialogVolumeBar.h"
#include "addons/settings/GUIDialogAddonSettings.h"

// PVR related include Files
#include "pvr/PVRGUIActions.h"
#include "pvr/PVRManager.h"

#include "video/dialogs/GUIDialogFullScreenInfo.h"
#include "guilib/GUIControlFactory.h"
#include "dialogs/GUIDialogCache.h"
#include "dialogs/GUIDialogPlayEject.h"
#include "utils/URIUtils.h"
#include "utils/XMLUtils.h"
#include "addons/AddonInstaller.h"
#include "addons/AddonManager.h"
#include "addons/RepositoryUpdater.h"
#include "music/tags/MusicInfoTag.h"
#include "music/tags/MusicInfoTagLoaderFactory.h"
#include "CompileInfo.h"

#ifdef TARGET_WINDOWS
#include "win32util.h"
#endif

#ifdef TARGET_DARWIN_OSX
#include "platform/darwin/osx/CocoaInterface.h"
#include "platform/darwin/osx/XBMCHelper.h"
#endif
#ifdef TARGET_DARWIN
#include "platform/darwin/DarwinUtils.h"
#endif

#ifdef HAS_DVD_DRIVE
#include <cdio/logging.h>
#endif

#include "storage/MediaManager.h"
#include "utils/SaveFileStateJob.h"
#include "utils/AlarmClock.h"
#include "utils/StringUtils.h"
#include "DatabaseManager.h"
#include "input/InputManager.h"

#ifdef TARGET_POSIX
#include "XHandle.h"
#include "XTimeUtils.h"
#include "filesystem/posix/PosixDirectory.h"
#endif

#if defined(TARGET_ANDROID)
#include <androidjni/Build.h>
#include "platform/android/activity/XBMCApp.h"
#include "platform/android/activity/AndroidFeatures.h"
#endif

#ifdef TARGET_WINDOWS
#include "utils/Environment.h"
#endif

#if defined(HAS_LIBAMCODEC)
#include "utils/AMLUtils.h"
#endif

//TODO: XInitThreads
#ifdef HAVE_X11
#include "X11/Xlib.h"
#endif

#include "cores/FFmpeg.h"
#include "utils/CharsetConverter.h"
#include "pictures/GUIWindowSlideShow.h"
#include "windows/GUIWindowLoginScreen.h"

using namespace ADDON;
using namespace XFILE;
#ifdef HAS_DVD_DRIVE
using namespace MEDIA_DETECT;
#endif
using namespace PLAYLIST;
using namespace VIDEO;
using namespace MUSIC_INFO;
using namespace EVENTSERVER;
using namespace JSONRPC;
using namespace ANNOUNCEMENT;
using namespace PVR;
using namespace PERIPHERALS;
using namespace KODI::MESSAGING;
using namespace ActiveAE;

using namespace XbmcThreads;

using KODI::MESSAGING::HELPERS::DialogResponse;

#define MAX_FFWD_SPEED 5

//extern IDirectSoundRenderer* m_pAudioDecoder;
CApplication::CApplication(void)
:
#ifdef HAS_DVD_DRIVE
  m_Autorun(new CAutorun()),
#endif
  m_iScreenSaveLock(0)
  , m_confirmSkinChange(true)
  , m_ignoreSkinSettingChanges(false)
  , m_saveSkinOnUnloading(true)
  , m_autoExecScriptExecuted(false)
  , m_screensaverActive(false)
  , m_bInhibitIdleShutdown(false)
  , m_dpmsIsActive(false)
  , m_dpmsIsManual(false)
  , m_itemCurrentFile(new CFileItem)
  , m_threadID(0)
  , m_bInitializing(true)
  , m_bPlatformDirectories(true)
  , m_nextPlaylistItem(-1)
  , m_lastRenderTime(0)
  , m_skipGuiRender(false)
  , m_bStandalone(false)
  , m_bEnableLegacyRes(false)
  , m_bTestMode(false)
  , m_bSystemScreenSaverEnable(false)
  , m_muted(false)
  , m_volumeLevel(VOLUME_MAXIMUM)
  , m_pInertialScrollingHandler(new CInertialScrollingHandler())
  , m_WaitingExternalCalls(0)
  , m_ProcessedExternalCalls(0)
{
  TiXmlBase::SetCondenseWhiteSpace(false);

#ifdef HAVE_X11
  XInitThreads();
#endif
}

CApplication::~CApplication(void)
{
  delete m_pInertialScrollingHandler;

  m_actionListeners.clear();
}

bool CApplication::OnEvent(XBMC_Event& newEvent)
{
  m_winEvents.push_back(newEvent);
  return true;
}

void CApplication::HandleWinEvents()
{
  while (!m_winEvents.empty())
  {
    auto newEvent = m_winEvents.front();
    m_winEvents.pop_front();
    switch(newEvent.type)
    {
      case XBMC_QUIT:
        if (!g_application.m_bStop)
          CApplicationMessenger::GetInstance().PostMsg(TMSG_QUIT);
        break;
      case XBMC_VIDEORESIZE:
        if (g_windowManager.Initialized())
        {
          if (!g_advancedSettings.m_fullScreen)
          {
            g_graphicsContext.ApplyWindowResize(newEvent.resize.w, newEvent.resize.h);
            CServiceBroker::GetSettings().SetInt(CSettings::SETTING_WINDOW_WIDTH, newEvent.resize.w);
            CServiceBroker::GetSettings().SetInt(CSettings::SETTING_WINDOW_HEIGHT, newEvent.resize.h);
            CServiceBroker::GetSettings().Save();
          }
        }
        break;
      case XBMC_VIDEOMOVE:
      {
        CServiceBroker::GetWinSystem().OnMove(newEvent.move.x, newEvent.move.y);
      }
        break;
      case XBMC_MODECHANGE:
        g_graphicsContext.ApplyModeChange(newEvent.mode.res);
        break;
      case XBMC_USEREVENT:
        CApplicationMessenger::GetInstance().PostMsg(static_cast<uint32_t>(newEvent.user.code));
        break;
      case XBMC_APPCOMMAND:
        g_application.OnAppCommand(newEvent.appcommand.action);
      case XBMC_SETFOCUS:
        // Reset the screensaver
        g_application.ResetScreenSaver();
        g_application.WakeUpScreenSaverAndDPMS();
        // Send a mouse motion event with no dx,dy for getting the current guiitem selected
        g_application.OnAction(CAction(ACTION_MOUSE_MOVE, 0, static_cast<float>(newEvent.focus.x), static_cast<float>(newEvent.focus.y), 0, 0));
        break;
      default:
        CServiceBroker::GetInputManager().OnEvent(newEvent);
    }
  }
}

extern "C" void __stdcall init_emu_environ();
extern "C" void __stdcall update_emu_environ();
extern "C" void __stdcall cleanup_emu_environ();

//
// Utility function used to copy files from the application bundle
// over to the user data directory in Application Support/Kodi.
//
static void CopyUserDataIfNeeded(const std::string &strPath, const std::string &file, const std::string &destname = "")
{
  std::string destPath;
  if (destname == "")
    destPath = URIUtils::AddFileToFolder(strPath, file);
  else
    destPath = URIUtils::AddFileToFolder(strPath, destname);

  if (!CFile::Exists(destPath))
  {
    // need to copy it across
    std::string srcPath = URIUtils::AddFileToFolder("special://xbmc/userdata/", file);
    CFile::Copy(srcPath, destPath);
  }
}

void CApplication::Preflight()
{
#ifdef HAS_DBUS
  // call 'dbus_threads_init_default' before any other dbus calls in order to
  // avoid race conditions with other threads using dbus connections
  dbus_threads_init_default();
#endif

  // run any platform preflight scripts.
#if defined(TARGET_DARWIN_OSX)
  std::string install_path;

  install_path = CUtil::GetHomePath();
  setenv("KODI_HOME", install_path.c_str(), 0);
  install_path += "/tools/darwin/runtime/preflight";
  system(install_path.c_str());
#endif
}

bool CApplication::Create(const CAppParamParser &params)
{
  // Grab a handle to our thread to be used later in identifying the render thread.
  m_threadID = CThread::GetCurrentThreadId();

  m_ServiceManager.reset(new CServiceManager());

  // TODO
  // some of the serives depend on the WinSystem :(
  std::unique_ptr<CWinSystemBase> winSystem = CWinSystemBase::CreateWinSystem();
  m_ServiceManager->SetWinSystem(std::move(winSystem));

  if (!m_ServiceManager->InitStageOne())
  {
    return false;
  }

  Preflight();

  // here we register all global classes for the CApplicationMessenger,
  // after that we can send messages to the corresponding modules
  CApplicationMessenger::GetInstance().RegisterReceiver(this);
  CApplicationMessenger::GetInstance().RegisterReceiver(&CServiceBroker::GetPlaylistPlayer());
  CApplicationMessenger::GetInstance().RegisterReceiver(&g_infoManager);
  CApplicationMessenger::GetInstance().SetGUIThread(m_threadID);

  for (int i = RES_HDTV_1080i; i <= RES_PAL60_16x9; i++)
  {
    g_graphicsContext.ResetScreenParameters((RESOLUTION)i);
    g_graphicsContext.ResetOverscan((RESOLUTION)i, CDisplaySettings::GetInstance().GetResolutionInfo(i).Overscan);
  }

  //! @todo - move to CPlatformXXX
#ifdef TARGET_POSIX
  tzset();   // Initialize timezone information variables
#endif


  //! @todo - move to CPlatformXXX
  #if defined(TARGET_POSIX)
    // set special://envhome
    if (getenv("HOME"))
    {
      CSpecialProtocol::SetEnvHomePath(getenv("HOME"));
    }
    else
    {
      fprintf(stderr, "The HOME environment variable is not set!\n");
      /* Cleanup. Leaving this out would lead to another crash */
      m_ServiceManager->DeinitStageOne();
      return false;
    }
  #endif

  // only the InitDirectories* for the current platform should return true
  bool inited = InitDirectoriesLinux();
  if (!inited)
    inited = InitDirectoriesOSX();
  if (!inited)
    inited = InitDirectoriesWin32();

  // copy required files
  CopyUserDataIfNeeded("special://masterprofile/", "RssFeeds.xml");
  CopyUserDataIfNeeded("special://masterprofile/", "favourites.xml");
  CopyUserDataIfNeeded("special://masterprofile/", "Lircmap.xml");

  //! @todo - move to CPlatformXXX
  #ifdef TARGET_DARWIN_IOS
    CopyUserDataIfNeeded("special://masterprofile/", "iOS/sources.xml", "sources.xml");
  #endif

  if (!CLog::Init(CSpecialProtocol::TranslatePath("special://logpath").c_str()))
  {
    fprintf(stderr,"Could not init logging classes. Log folder error (%s)\n", CSpecialProtocol::TranslatePath("special://logpath").c_str());
    return false;
  }

  // Init our DllLoaders emu env
  init_emu_environ();

  CProfilesManager::GetInstance().Load();

  CLog::Log(LOGNOTICE, "-----------------------------------------------------------------------");
  CLog::Log(LOGNOTICE, "Starting %s (%s). Platform: %s %s %d-bit", CSysInfo::GetAppName().c_str(), CSysInfo::GetVersion().c_str(),
            g_sysinfo.GetBuildTargetPlatformName().c_str(), g_sysinfo.GetBuildTargetCpuFamily().c_str(), g_sysinfo.GetXbmcBitness());

  std::string buildType;
#if defined(_DEBUG)
  buildType = "Debug";
#elif defined(NDEBUG)
  buildType = "Release";
#else
  buildType = "Unknown";
#endif
  std::string specialVersion;

  //! @todo - move to CPlatformXXX
#if defined(TARGET_RASPBERRY_PI)
  specialVersion = " (version for Raspberry Pi)";
//#elif defined(some_ID) // uncomment for special version/fork
//  specialVersion = " (version for XXXX)";
#endif
  CLog::Log(LOGNOTICE, "Using %s %s x%d build%s", buildType.c_str(), CSysInfo::GetAppName().c_str(), g_sysinfo.GetXbmcBitness(), specialVersion.c_str());
  CLog::Log(LOGNOTICE, "%s compiled " __DATE__ " by %s for %s %s %d-bit %s (%s)", CSysInfo::GetAppName().c_str(), g_sysinfo.GetUsedCompilerNameAndVer().c_str(), g_sysinfo.GetBuildTargetPlatformName().c_str(),
            g_sysinfo.GetBuildTargetCpuFamily().c_str(), g_sysinfo.GetXbmcBitness(), g_sysinfo.GetBuildTargetPlatformVersionDecoded().c_str(),
            g_sysinfo.GetBuildTargetPlatformVersion().c_str());

  std::string deviceModel(g_sysinfo.GetModelName());
  if (!g_sysinfo.GetManufacturerName().empty())
    deviceModel = g_sysinfo.GetManufacturerName() + " " + (deviceModel.empty() ? std::string("device") : deviceModel);
  if (!deviceModel.empty())
    CLog::Log(LOGNOTICE, "Running on %s with %s, kernel: %s %s %d-bit version %s", deviceModel.c_str(), g_sysinfo.GetOsPrettyNameWithVersion().c_str(),
              g_sysinfo.GetKernelName().c_str(), g_sysinfo.GetKernelCpuFamily().c_str(), g_sysinfo.GetKernelBitness(), g_sysinfo.GetKernelVersionFull().c_str());
  else
    CLog::Log(LOGNOTICE, "Running on %s, kernel: %s %s %d-bit version %s", g_sysinfo.GetOsPrettyNameWithVersion().c_str(),
              g_sysinfo.GetKernelName().c_str(), g_sysinfo.GetKernelCpuFamily().c_str(), g_sysinfo.GetKernelBitness(), g_sysinfo.GetKernelVersionFull().c_str());

  CLog::Log(LOGNOTICE, "FFmpeg version/source: %s", av_version_info());

  std::string cpuModel(g_cpuInfo.getCPUModel());
  if (!cpuModel.empty())
    CLog::Log(LOGNOTICE, "Host CPU: %s, %d core%s available", cpuModel.c_str(), g_cpuInfo.getCPUCount(), (g_cpuInfo.getCPUCount() == 1) ? "" : "s");
  else
    CLog::Log(LOGNOTICE, "%d CPU core%s available", g_cpuInfo.getCPUCount(), (g_cpuInfo.getCPUCount() == 1) ? "" : "s");

  //! @todo - move to CPlatformXXX ???
#if defined(TARGET_WINDOWS)
  CLog::Log(LOGNOTICE, "%s", CWIN32Util::GetResInfoString().c_str());
  CLog::Log(LOGNOTICE, "Running with %s rights", (CWIN32Util::IsCurrentUserLocalAdministrator() == TRUE) ? "administrator" : "restricted");
  CLog::Log(LOGNOTICE, "Aero is %s", (g_sysinfo.IsAeroDisabled() == true) ? "disabled" : "enabled");
#endif
#if defined(TARGET_ANDROID)
  CLog::Log(LOGNOTICE,
        "Product: %s, Device: %s, Board: %s - Manufacturer: %s, Brand: %s, Model: %s, Hardware: %s",
        CJNIBuild::PRODUCT.c_str(), CJNIBuild::DEVICE.c_str(), CJNIBuild::BOARD.c_str(),
        CJNIBuild::MANUFACTURER.c_str(), CJNIBuild::BRAND.c_str(), CJNIBuild::MODEL.c_str(), CJNIBuild::HARDWARE.c_str());
  std::string extstorage;
  bool extready = CXBMCApp::GetExternalStorage(extstorage);
  CLog::Log(LOGNOTICE, "External storage path = %s; status = %s", extstorage.c_str(), extready ? "ok" : "nok");
#endif

#if defined(__arm__) || defined(__aarch64__)
  if (g_cpuInfo.GetCPUFeatures() & CPU_FEATURE_NEON)
    CLog::Log(LOGNOTICE, "ARM Features: Neon enabled");
  else
    CLog::Log(LOGNOTICE, "ARM Features: Neon disabled");
#endif
  CSpecialProtocol::LogPaths();

  std::string executable = CUtil::ResolveExecutablePath();
  CLog::Log(LOGNOTICE, "The executable running is: %s", executable.c_str());
  std::string hostname("[unknown]");
  m_ServiceManager->GetNetwork().GetHostName(hostname);
  CLog::Log(LOGNOTICE, "Local hostname: %s", hostname.c_str());
  std::string lowerAppName = CCompileInfo::GetAppName();
  StringUtils::ToLower(lowerAppName);
  CLog::Log(LOGNOTICE, "Log File is located: %s/%s.log", CSpecialProtocol::TranslatePath("special://logpath").c_str(), lowerAppName.c_str());
  CRegExp::LogCheckUtf8Support();
  CLog::Log(LOGNOTICE, "-----------------------------------------------------------------------");

  std::string strExecutablePath = CUtil::GetHomePath();

  // for python scripts that check the OS
  //! @todo - move to CPlatformXXX
#if defined(TARGET_DARWIN)
  setenv("OS","OS X",true);
#elif defined(TARGET_POSIX)
  setenv("OS","Linux",true);
#elif defined(TARGET_WINDOWS)
  CEnvironment::setenv("OS", "win32");
#endif

  // register ffmpeg lockmanager callback
  av_lockmgr_register(&ffmpeg_lockmgr_cb);
  // register avcodec
  avcodec_register_all();
  // register avformat
  av_register_all();
  // register avfilter
  avfilter_register_all();
  // initialize network protocols
  avformat_network_init();
  // set avutil callback
  av_log_set_callback(ff_avutil_log);

  // Initialize default Settings - don't move
  CLog::Log(LOGNOTICE, "load settings...");
  if (!m_ServiceManager->GetSettings().Initialize())
    return false;

  // load the actual values
  if (!m_ServiceManager->GetSettings().Load())
  {
    CLog::Log(LOGFATAL, "unable to load settings");
    return false;
  }
  m_ServiceManager->GetSettings().SetLoaded();

  CLog::Log(LOGINFO, "creating subdirectories");
  CLog::Log(LOGINFO, "userdata folder: %s", CURL::GetRedacted(CProfilesManager::GetInstance().GetProfileUserDataFolder()).c_str());
  CLog::Log(LOGINFO, "recording folder: %s", CURL::GetRedacted(m_ServiceManager->GetSettings().GetString(CSettings::SETTING_AUDIOCDS_RECORDINGPATH)).c_str());
  CLog::Log(LOGINFO, "screenshots folder: %s", CURL::GetRedacted(m_ServiceManager->GetSettings().GetString(CSettings::SETTING_DEBUG_SCREENSHOTPATH)).c_str());
  CDirectory::Create(CProfilesManager::GetInstance().GetUserDataFolder());
  CDirectory::Create(CProfilesManager::GetInstance().GetProfileUserDataFolder());
  CProfilesManager::GetInstance().CreateProfileFolders();

  update_emu_environ();//apply the GUI settings

  //! @todo - move to CPlatformXXX
#ifdef TARGET_WINDOWS
  CWIN32Util::SetThreadLocalLocale(true); // enable independent locale for each thread, see https://connect.microsoft.com/VisualStudio/feedback/details/794122
#endif // TARGET_WINDOWS

  // initialize the addon database (must be before the addon manager is init'd)
  CDatabaseManager::GetInstance().Initialize(true);

  if (!m_ServiceManager->InitStageTwo(params))
  {
    return false;
  }

  if (!m_ServiceManager->CreateAudioEngine())
  {
    CLog::Log(LOGFATAL, "CApplication::Create: Failed to load an AudioEngine");
    return false;
  }
  // start the AudioEngine
  if(!m_ServiceManager->StartAudioEngine())
  {
    CLog::Log(LOGFATAL, "CApplication::Create: Failed to start the AudioEngine");
    return false;
  }

  // restore AE's previous volume state
  SetHardwareVolume(m_volumeLevel);
  m_ServiceManager->GetActiveAE().SetMute(m_muted);
  m_ServiceManager->GetActiveAE().SetSoundMode(m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_AUDIOOUTPUT_GUISOUNDMODE));

  // initialize m_replayGainSettings
  m_replayGainSettings.iType = m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_MUSICPLAYER_REPLAYGAINTYPE);
  m_replayGainSettings.iPreAmp = m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_MUSICPLAYER_REPLAYGAINPREAMP);
  m_replayGainSettings.iNoGainPreAmp = m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_MUSICPLAYER_REPLAYGAINNOGAINPREAMP);
  m_replayGainSettings.bAvoidClipping = m_ServiceManager->GetSettings().GetBool(CSettings::SETTING_MUSICPLAYER_REPLAYGAINAVOIDCLIPPING);

  // load the keyboard layouts
  if (!CKeyboardLayoutManager::GetInstance().Load())
  {
    CLog::Log(LOGFATAL, "CApplication::Create: Unable to load keyboard layouts");
    return false;
  }

  //! @todo - move to CPlatformXXX
#if defined(TARGET_DARWIN_OSX)
  // Configure and possible manually start the helper.
  XBMCHelper::GetInstance().Configure();
#endif

  CUtil::InitRandomSeed();

  g_mediaManager.Initialize();

  m_lastRenderTime = XbmcThreads::SystemClockMillis();
  return true;
}

bool CApplication::CreateGUI()
{
  m_frameMoveGuard.lock();

  m_renderGUI = true;

  if (!CServiceBroker::GetWinSystem().InitWindowSystem())
  {
    CLog::Log(LOGFATAL, "CApplication::Create: Unable to init windowing system");
    return false;
  }


  // Retrieve the matching resolution based on GUI settings
  bool sav_res = false;
  CDisplaySettings::GetInstance().SetCurrentResolution(CDisplaySettings::GetInstance().GetDisplayResolution());
  CLog::Log(LOGNOTICE, "Checking resolution %i", CDisplaySettings::GetInstance().GetCurrentResolution());
  if (!g_graphicsContext.IsValidResolution(CDisplaySettings::GetInstance().GetCurrentResolution()))
  {
    CLog::Log(LOGNOTICE, "Setting safe mode %i", RES_DESKTOP);
    // defer saving resolution after window was created
    CDisplaySettings::GetInstance().SetCurrentResolution(RES_DESKTOP);
    sav_res = true;
  }

  // update the window resolution
  CServiceBroker::GetWinSystem().SetWindowResolution(m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_WINDOW_WIDTH), m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_WINDOW_HEIGHT));

  if (g_advancedSettings.m_startFullScreen && CDisplaySettings::GetInstance().GetCurrentResolution() == RES_WINDOW)
  {
    // defer saving resolution after window was created
    CDisplaySettings::GetInstance().SetCurrentResolution(RES_DESKTOP);
    sav_res = true;
  }

  if (!g_graphicsContext.IsValidResolution(CDisplaySettings::GetInstance().GetCurrentResolution()))
  {
    // Oh uh - doesn't look good for starting in their wanted screenmode
    CLog::Log(LOGERROR, "The screen resolution requested is not valid, resetting to a valid mode");
    CDisplaySettings::GetInstance().SetCurrentResolution(RES_DESKTOP);
    sav_res = true;
  }
  if (!InitWindow())
  {
    return false;
  }

  // Set default screen saver mode
  auto screensaverModeSetting = std::static_pointer_cast<CSettingString>(m_ServiceManager->GetSettings().GetSetting(CSettings::SETTING_SCREENSAVER_MODE));
  // Can only set this after windowing has been initialized since it depends on it
  if (CServiceBroker::GetWinSystem().GetOSScreenSaver())
  {
    // If OS has a screen saver, use it by default
    screensaverModeSetting->SetDefault("");
  }
  else
  {
    // If OS has no screen saver, use Kodi one by default
    screensaverModeSetting->SetDefault("screensaver.xbmc.builtin.dim");
  }
  CheckOSScreenSaverInhibitionSetting();

  if (sav_res)
    CDisplaySettings::GetInstance().SetCurrentResolution(RES_DESKTOP, true);

  CServiceBroker::GetRenderSystem().ShowSplash("");

  // The key mappings may already have been loaded by a peripheral
  CLog::Log(LOGINFO, "load keymapping");
  if (!CServiceBroker::GetInputManager().LoadKeymaps())
    return false;

  RESOLUTION_INFO info = g_graphicsContext.GetResInfo();
  CLog::Log(LOGINFO, "GUI format %ix%i, Display %s",
            info.iWidth,
            info.iHeight,
            info.strMode.c_str());

  g_windowManager.Initialize();

  return true;
}

bool CApplication::InitWindow(RESOLUTION res)
{
  if (res == RES_INVALID)
    res = CDisplaySettings::GetInstance().GetCurrentResolution();

  bool bFullScreen = res != RES_WINDOW;
  if (!CServiceBroker::GetWinSystem().CreateNewWindow(CSysInfo::GetAppName(),
                                                      bFullScreen, CDisplaySettings::GetInstance().GetResolutionInfo(res)))
  {
    CLog::Log(LOGFATAL, "CApplication::Create: Unable to create window");
    return false;
  }

  if (!CServiceBroker::GetRenderSystem().InitRenderSystem())
  {
    CLog::Log(LOGFATAL, "CApplication::Create: Unable to init rendering system");
    return false;
  }
  // set GUI res and force the clear of the screen
  g_graphicsContext.SetVideoResolution(res, false);
  return true;
}

bool CApplication::DestroyWindow()
{
  bool ret = CServiceBroker::GetWinSystem().DestroyWindow();
  std::unique_ptr<CWinSystemBase> winSystem;
  m_ServiceManager->SetWinSystem(std::move(winSystem));
  return ret;
}

bool CApplication::InitDirectoriesLinux()
{
/*
   The following is the directory mapping for Platform Specific Mode:

   special://xbmc/          => [read-only] system directory (/usr/share/kodi)
   special://home/          => [read-write] user's directory that will override special://kodi/ system-wide
                               installations like skins, screensavers, etc.
                               ($HOME/.kodi)
                               NOTE: XBMC will look in both special://xbmc/addons and special://home/addons for addons.
   special://masterprofile/ => [read-write] userdata of master profile. It will by default be
                               mapped to special://home/userdata ($HOME/.kodi/userdata)
   special://profile/       => [read-write] current profile's userdata directory.
                               Generally special://masterprofile for the master profile or
                               special://masterprofile/profiles/<profile_name> for other profiles.

   NOTE: All these root directories are lowercase. Some of the sub-directories
         might be mixed case.
*/

#if defined(TARGET_POSIX) && !defined(TARGET_DARWIN)
  std::string userName;
  if (getenv("USER"))
    userName = getenv("USER");
  else
    userName = "root";

  std::string userHome;
  if (getenv("HOME"))
    userHome = getenv("HOME");
  else
    userHome = "/root";

  std::string binaddonAltDir;
  if (getenv("KODI_BINADDON_PATH"))
    binaddonAltDir = getenv("KODI_BINADDON_PATH");

  std::string appPath;
  std::string appName = CCompileInfo::GetAppName();
  std::string dotLowerAppName = "." + appName;
  StringUtils::ToLower(dotLowerAppName);
  const char* envAppHome = "KODI_HOME";
  const char* envAppBinHome = "KODI_BIN_HOME";
  const char* envAppTemp = "KODI_TEMP";

  auto appBinPath = CUtil::GetHomePath(envAppBinHome);
  // overridden by user
  if (getenv(envAppHome))
    appPath = getenv(envAppHome);
  else
  {
    // use build time default
    appPath = INSTALL_PATH;
    /* Check if binaries and arch independent data files are being kept in
     * separate locations. */
    if (!CDirectory::Exists(URIUtils::AddFileToFolder(appPath, "userdata")))
    {
      /* Attempt to locate arch independent data files. */
      appPath = CUtil::GetHomePath(appBinPath);
      if (!CDirectory::Exists(URIUtils::AddFileToFolder(appPath, "userdata")))
      {
        fprintf(stderr, "Unable to find path to %s data files!\n", appName.c_str());
        exit(1);
      }
    }
  }

  /* Set some environment variables */
  setenv(envAppBinHome, appBinPath.c_str(), 0);
  setenv(envAppHome, appPath.c_str(), 0);

  if (m_bPlatformDirectories)
  {
    // map our special drives
    CSpecialProtocol::SetXBMCBinPath(appBinPath);
    CSpecialProtocol::SetXBMCAltBinAddonPath(binaddonAltDir);
    CSpecialProtocol::SetXBMCPath(appPath);
    CSpecialProtocol::SetHomePath(userHome + "/" + dotLowerAppName);
    CSpecialProtocol::SetMasterProfilePath(userHome + "/" + dotLowerAppName + "/userdata");

    std::string strTempPath = userHome;
    strTempPath = URIUtils::AddFileToFolder(strTempPath, dotLowerAppName + "/temp");
    if (getenv(envAppTemp))
      strTempPath = getenv(envAppTemp);
    CSpecialProtocol::SetTempPath(strTempPath);
    CSpecialProtocol::SetLogPath(strTempPath);

    CreateUserDirs();

  }
  else
  {
    URIUtils::AddSlashAtEnd(appPath);

    CSpecialProtocol::SetXBMCBinPath(appBinPath);
    CSpecialProtocol::SetXBMCAltBinAddonPath(binaddonAltDir);
    CSpecialProtocol::SetXBMCPath(appPath);
    CSpecialProtocol::SetHomePath(URIUtils::AddFileToFolder(appPath, "portable_data"));
    CSpecialProtocol::SetMasterProfilePath(URIUtils::AddFileToFolder(appPath, "portable_data/userdata"));

    std::string strTempPath = appPath;
    strTempPath = URIUtils::AddFileToFolder(strTempPath, "portable_data/temp");
    if (getenv(envAppTemp))
      strTempPath = getenv(envAppTemp);
    CSpecialProtocol::SetTempPath(strTempPath);
    CSpecialProtocol::SetLogPath(strTempPath);
    CreateUserDirs();
  }
  CSpecialProtocol::SetXBMCBinAddonPath(appBinPath + "/addons");

#if defined(TARGET_ANDROID)
  CXBMCApp::InitDirectories();
#endif

  return true;
#else
  return false;
#endif
}

bool CApplication::InitDirectoriesOSX()
{
#if defined(TARGET_DARWIN)
  std::string userName;
  if (getenv("USER"))
    userName = getenv("USER");
  else
    userName = "root";

  std::string userHome;
  if (getenv("HOME"))
    userHome = getenv("HOME");
  else
    userHome = "/root";

  std::string binaddonAltDir;
  if (getenv("KODI_BINADDON_PATH"))
    binaddonAltDir = getenv("KODI_BINADDON_PATH");

  std::string appPath = CUtil::GetHomePath();
  setenv("KODI_HOME", appPath.c_str(), 0);

#if defined(TARGET_DARWIN_IOS)
  std::string fontconfigPath;
  fontconfigPath = appPath + "/system/players/VideoPlayer/etc/fonts/fonts.conf";
  setenv("FONTCONFIG_FILE", fontconfigPath.c_str(), 0);
#endif

  // setup path to our internal dylibs so loader can find them
  std::string frameworksPath = CUtil::GetFrameworksPath();
  CSpecialProtocol::SetXBMCFrameworksPath(frameworksPath);

  // OSX always runs with m_bPlatformDirectories == true
  if (m_bPlatformDirectories)
  {
    // map our special drives
    CSpecialProtocol::SetXBMCBinPath(appPath);
    CSpecialProtocol::SetXBMCAltBinAddonPath(binaddonAltDir);
    CSpecialProtocol::SetXBMCPath(appPath);
    #if defined(TARGET_DARWIN_IOS)
      std::string appName = CCompileInfo::GetAppName();
      CSpecialProtocol::SetHomePath(userHome + "/" + CDarwinUtils::GetAppRootFolder() + "/" + appName);
      CSpecialProtocol::SetMasterProfilePath(userHome + "/" + CDarwinUtils::GetAppRootFolder() + "/" + appName + "/userdata");
    #else
      std::string appName = CCompileInfo::GetAppName();
      CSpecialProtocol::SetHomePath(userHome + "/Library/Application Support/" + appName);
      CSpecialProtocol::SetMasterProfilePath(userHome + "/Library/Application Support/" + appName + "/userdata");
    #endif

    std::string dotLowerAppName = "." + appName;
    StringUtils::ToLower(dotLowerAppName);
    // location for temp files
    #if defined(TARGET_DARWIN_IOS)
      std::string strTempPath = URIUtils::AddFileToFolder(userHome,  std::string(CDarwinUtils::GetAppRootFolder()) + "/" + appName + "/temp");
    #else
      std::string strTempPath = URIUtils::AddFileToFolder(userHome, dotLowerAppName + "/");
      CDirectory::Create(strTempPath);
      strTempPath = URIUtils::AddFileToFolder(userHome, dotLowerAppName + "/temp");
    #endif
    CSpecialProtocol::SetTempPath(strTempPath);

    // xbmc.log file location
    #if defined(TARGET_DARWIN_IOS)
      strTempPath = userHome + "/" + std::string(CDarwinUtils::GetAppRootFolder());
    #else
      strTempPath = userHome + "/Library/Logs";
    #endif
    CSpecialProtocol::SetLogPath(strTempPath);
    CreateUserDirs();
  }
  else
  {
    URIUtils::AddSlashAtEnd(appPath);

    CSpecialProtocol::SetXBMCBinPath(appPath);
    CSpecialProtocol::SetXBMCAltBinAddonPath(binaddonAltDir);
    CSpecialProtocol::SetXBMCPath(appPath);
    CSpecialProtocol::SetHomePath(URIUtils::AddFileToFolder(appPath, "portable_data"));
    CSpecialProtocol::SetMasterProfilePath(URIUtils::AddFileToFolder(appPath, "portable_data/userdata"));

    std::string strTempPath = URIUtils::AddFileToFolder(appPath, "portable_data/temp");
    CSpecialProtocol::SetTempPath(strTempPath);
    CSpecialProtocol::SetLogPath(strTempPath);
  }
  CSpecialProtocol::SetXBMCBinAddonPath(appPath + "/addons");
  return true;
#else
  return false;
#endif
}

bool CApplication::InitDirectoriesWin32()
{
#ifdef TARGET_WINDOWS
  std::string xbmcPath = CUtil::GetHomePath();
  CEnvironment::setenv("KODI_HOME", xbmcPath);
  CSpecialProtocol::SetXBMCBinPath(xbmcPath);
  CSpecialProtocol::SetXBMCPath(xbmcPath);
  CSpecialProtocol::SetXBMCBinAddonPath(xbmcPath + "/addons");

  std::string strWin32UserFolder = CWIN32Util::GetProfilePath();
  CSpecialProtocol::SetLogPath(strWin32UserFolder);
  CSpecialProtocol::SetHomePath(strWin32UserFolder);
  CSpecialProtocol::SetMasterProfilePath(URIUtils::AddFileToFolder(strWin32UserFolder, "userdata"));
  CSpecialProtocol::SetTempPath(URIUtils::AddFileToFolder(strWin32UserFolder,"cache"));

  CEnvironment::setenv("KODI_PROFILE_USERDATA", CSpecialProtocol::TranslatePath("special://masterprofile/"));

  CreateUserDirs();

  return true;
#else
  return false;
#endif
}

void CApplication::CreateUserDirs() const
{
  CDirectory::Create("special://home/");
  CDirectory::Create("special://home/addons");
  CDirectory::Create("special://home/addons/packages");
  CDirectory::Create("special://home/addons/temp");
  CDirectory::Create("special://home/media");
  CDirectory::Create("special://home/system");
  CDirectory::Create("special://masterprofile/");
  CDirectory::Create("special://temp/");
  CDirectory::Create("special://logpath");
  CDirectory::Create("special://temp/temp"); // temp directory for python and dllGetTempPathA

  //Let's clear our archive cache before starting up anything more
  auto archiveCachePath = CSpecialProtocol::TranslatePath("special://temp/archive_cache/");
  if (CDirectory::Exists(archiveCachePath))
    if (!CDirectory::RemoveRecursive(archiveCachePath))
      CLog::Log(LOGWARNING, "Failed to remove the archive cache at %s", archiveCachePath.c_str());
  CDirectory::Create(archiveCachePath);

}

bool CApplication::Initialize()
{
#if defined(HAS_DVD_DRIVE) && !defined(TARGET_WINDOWS) // somehow this throws an "unresolved external symbol" on win32
  // turn off cdio logging
  cdio_loglevel_default = CDIO_LOG_ERROR;
#endif

#ifdef TARGET_POSIX //! @todo Win32 has no special://home/ mapping by default, so we
              //!       must create these here. Ideally this should be using special://home/ and
              //!      be platform agnostic (i.e. unify the InitDirectories*() functions)
  if (!m_bPlatformDirectories)
#endif
  {
    CDirectory::Create("special://xbmc/addons");
  }

  // load the language and its translated strings
  if (!LoadLanguage(false))
    return false;

  CEventLog::GetInstance().Add(EventPtr(new CNotificationEvent(
    StringUtils::Format(g_localizeStrings.Get(177).c_str(), g_sysinfo.GetAppName().c_str()),
    StringUtils::Format(g_localizeStrings.Get(178).c_str(), g_sysinfo.GetAppName().c_str()),
    "special://xbmc/media/icon256x256.png", EventLevel::Basic)));

  m_ServiceManager->GetNetwork().WaitForNet();

  // Load curl so curl_global_init gets called before any service threads
  // are started. Unloading will have no effect as curl is never fully unloaded.
  // To quote man curl_global_init:
  //  "This function is not thread safe. You must not call it when any other
  //  thread in the program (i.e. a thread sharing the same memory) is running.
  //  This doesn't just mean no other thread that is using libcurl. Because
  //  curl_global_init() calls functions of other libraries that are similarly
  //  thread unsafe, it could conflict with any other thread that
  //  uses these other libraries."
  g_curlInterface.Load();
  g_curlInterface.Unload();

  // initialize (and update as needed) our databases
  CEvent event(true);
  CJobManager::GetInstance().Submit([&event]() {
    CDatabaseManager::GetInstance().Initialize();
    event.Set();
  });
  std::string localizedStr = g_localizeStrings.Get(24150);
  int iDots = 1;
  while (!event.WaitMSec(1000))
  {
    if (CDatabaseManager::GetInstance().m_bIsUpgrading)
      CServiceBroker::GetRenderSystem().ShowSplash(std::string(iDots, ' ') + localizedStr + std::string(iDots, '.'));
    if (iDots == 3)
      iDots = 1;
    else
      ++iDots;
  }
  CServiceBroker::GetRenderSystem().ShowSplash("");

  StartServices();

  // Init DPMS, before creating the corresponding setting control.
  m_dpms.reset(new DPMSSupport());
  bool uiInitializationFinished = true;
  if (g_windowManager.Initialized())
  {
    m_ServiceManager->GetSettings().GetSetting(CSettings::SETTING_POWERMANAGEMENT_DISPLAYSOFF)->SetRequirementsMet(m_dpms->IsSupported());

    g_windowManager.CreateWindows();

    m_confirmSkinChange = false;

    std::vector<std::string> incompatibleAddons;
    event.Reset();
    std::atomic<bool> isMigratingAddons(false);
    CJobManager::GetInstance().Submit([&event, &incompatibleAddons, &isMigratingAddons]() {
        incompatibleAddons = CAddonSystemSettings::GetInstance().MigrateAddons([&isMigratingAddons]() {
          isMigratingAddons = true;
        });
        event.Set();
      }, CJob::PRIORITY_DEDICATED);
    localizedStr = g_localizeStrings.Get(24151);
    iDots = 1;
    while (!event.WaitMSec(1000))
    {
      if (isMigratingAddons)
        CServiceBroker::GetRenderSystem().ShowSplash(std::string(iDots, ' ') + localizedStr + std::string(iDots, '.'));
      if (iDots == 3)
        iDots = 1;
      else
        ++iDots;
    }
    CServiceBroker::GetRenderSystem().ShowSplash("");
    m_incompatibleAddons = incompatibleAddons;
    m_confirmSkinChange = true;

    std::string defaultSkin = std::static_pointer_cast<const CSettingString>(m_ServiceManager->GetSettings().GetSetting(CSettings::SETTING_LOOKANDFEEL_SKIN))->GetDefault();
    if (!LoadSkin(m_ServiceManager->GetSettings().GetString(CSettings::SETTING_LOOKANDFEEL_SKIN)))
    {
      CLog::Log(LOGERROR, "Failed to load skin '%s'", m_ServiceManager->GetSettings().GetString(CSettings::SETTING_LOOKANDFEEL_SKIN).c_str());
      if (!LoadSkin(defaultSkin))
      {
        CLog::Log(LOGFATAL, "Default skin '%s' could not be loaded! Terminating..", defaultSkin.c_str());
        return false;
      }
    }

    // initialize splash window after splash screen disappears
    // because we need a real window in the background which gets
    // rendered while we load the main window or enter the master lock key
    if (g_advancedSettings.m_splashImage)
      g_windowManager.ActivateWindow(WINDOW_SPLASH);

    if (m_ServiceManager->GetSettings().GetBool(CSettings::SETTING_MASTERLOCK_STARTUPLOCK) &&
        CProfilesManager::GetInstance().GetMasterProfile().getLockMode() != LOCK_MODE_EVERYONE &&
       !CProfilesManager::GetInstance().GetMasterProfile().getLockCode().empty())
    {
       g_passwordManager.CheckStartUpLock();
    }

    // check if we should use the login screen
    if (CProfilesManager::GetInstance().UsingLoginScreen())
    {
      // the login screen still needs to perform additional initialization
      uiInitializationFinished = false;

      g_windowManager.ActivateWindow(WINDOW_LOGIN_SCREEN);
    }
    else
    {
      CJSONRPC::Initialize();
      CServiceBroker::GetServiceAddons().StartBeforeLogin();

      // activate the configured start window
      int firstWindow = g_SkinInfo->GetFirstWindow();
      g_windowManager.ActivateWindow(firstWindow);

      if (g_windowManager.GetActiveWindowID() == WINDOW_STARTUP_ANIM)
      {
        CLog::Log(LOGWARNING, "CApplication::Initialize - startup.xml taints init process");
      }

      // the startup window is considered part of the initialization as it most likely switches to the final window
      uiInitializationFinished = firstWindow != WINDOW_STARTUP_ANIM;

      CStereoscopicsManager::GetInstance().Initialize();

      if (!m_ServiceManager->InitStageThree())
      {
        CLog::Log(LOGERROR, "Application - Init3 failed");
      }
    }

  }
  else //No GUI Created
  {
    CJSONRPC::Initialize();
    CServiceBroker::GetServiceAddons().StartBeforeLogin();
  }

  g_sysinfo.Refresh();

  CLog::Log(LOGINFO, "removing tempfiles");
  CUtil::RemoveTempFiles();

  if (!CProfilesManager::GetInstance().UsingLoginScreen())
  {
    UpdateLibraries();
    SetLoggingIn(false);
  }

  m_slowTimer.StartZero();

  // configure seek handler
  m_appPlayer.GetSeekHandler().Configure();

  // register action listeners
  RegisterActionListener(&m_appPlayer.GetSeekHandler());
  RegisterActionListener(&CPlayerController::GetInstance());

  CServiceBroker::GetRepositoryUpdater().Start();
  CServiceBroker::GetServiceAddons().Start();

  CLog::Log(LOGNOTICE, "initialize done");

  // reset our screensaver (starts timers etc.)
  ResetScreenSaver();

  // if the user interfaces has been fully initialized let everyone know
  if (uiInitializationFinished)
  {
    CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UI_READY);
    g_windowManager.SendThreadMessage(msg);
  }

  return true;
}

bool CApplication::StartServer(enum ESERVERS eServer, bool bStart, bool bWait/* = false*/)
{
  bool ret = false;
  switch(eServer)
  {
    case ES_WEBSERVER:
      // the callback will take care of starting/stopping webserver
      ret = m_ServiceManager->GetSettings().SetBool(CSettings::SETTING_SERVICES_WEBSERVER, bStart);
      break;

    case ES_AIRPLAYSERVER:
      // the callback will take care of starting/stopping airplay
      ret = m_ServiceManager->GetSettings().SetBool(CSettings::SETTING_SERVICES_AIRPLAY, bStart);
      break;

    case ES_JSONRPCSERVER:
      // the callback will take care of starting/stopping jsonrpc server
      ret = m_ServiceManager->GetSettings().SetBool(CSettings::SETTING_SERVICES_ESENABLED, bStart);
      break;

    case ES_UPNPSERVER:
      // the callback will take care of starting/stopping upnp server
      ret = m_ServiceManager->GetSettings().SetBool(CSettings::SETTING_SERVICES_UPNPSERVER, bStart);
      break;

    case ES_UPNPRENDERER:
      // the callback will take care of starting/stopping upnp renderer
      ret = m_ServiceManager->GetSettings().SetBool(CSettings::SETTING_SERVICES_UPNPRENDERER, bStart);
      break;

    case ES_EVENTSERVER:
      // the callback will take care of starting/stopping event server
      ret = m_ServiceManager->GetSettings().SetBool(CSettings::SETTING_SERVICES_ESENABLED, bStart);
      break;

    case ES_ZEROCONF:
      // the callback will take care of starting/stopping zeroconf
      ret = m_ServiceManager->GetSettings().SetBool(CSettings::SETTING_SERVICES_ZEROCONF, bStart);
      break;

    default:
      ret = false;
      break;
  }
  m_ServiceManager->GetSettings().Save();

  return ret;
}

void CApplication::StartServices()
{
#if !defined(TARGET_WINDOWS) && defined(HAS_DVD_DRIVE)
  // Start Thread for DVD Mediatype detection
  CLog::Log(LOGNOTICE, "start dvd mediatype detection");
  m_DetectDVDType.Create(false, THREAD_MINSTACKSIZE);
#endif
}

void CApplication::StopServices()
{
  m_ServiceManager->GetNetwork().NetworkMessage(CNetwork::SERVICES_DOWN, 0);

#if !defined(TARGET_WINDOWS) && defined(HAS_DVD_DRIVE)
  CLog::Log(LOGNOTICE, "stop dvd detect media");
  m_DetectDVDType.StopThread();
#endif
}

void CApplication::OnSettingChanged(std::shared_ptr<const CSetting> setting)
{
  if (setting == NULL)
    return;

  const std::string &settingId = setting->GetId();

  if (settingId == CSettings::SETTING_LOOKANDFEEL_SKIN ||
      settingId == CSettings::SETTING_LOOKANDFEEL_FONT ||
      settingId == CSettings::SETTING_LOOKANDFEEL_SKINTHEME ||
      settingId == CSettings::SETTING_LOOKANDFEEL_SKINCOLORS)
  {
    // check if we should ignore this change event due to changing skins in which case we have to
    // change several settings and each one of them could lead to a complete skin reload which would
    // result in multiple skin reloads. Therefore we manually specify to ignore specific settings
    // which are going to be changed.
    if (m_ignoreSkinSettingChanges)
      return;

    // if the skin changes and the current color/theme/font is not the default one, reset
    // the it to the default value
    if (settingId == CSettings::SETTING_LOOKANDFEEL_SKIN)
    {
      SettingPtr skinRelatedSetting = m_ServiceManager->GetSettings().GetSetting(CSettings::SETTING_LOOKANDFEEL_SKINCOLORS);
      if (!skinRelatedSetting->IsDefault())
      {
        m_ignoreSkinSettingChanges = true;
        skinRelatedSetting->Reset();
      }

      skinRelatedSetting = m_ServiceManager->GetSettings().GetSetting(CSettings::SETTING_LOOKANDFEEL_SKINTHEME);
      if (!skinRelatedSetting->IsDefault())
      {
        m_ignoreSkinSettingChanges = true;
        skinRelatedSetting->Reset();
      }

      skinRelatedSetting = m_ServiceManager->GetSettings().GetSetting(CSettings::SETTING_LOOKANDFEEL_FONT);
      if (!skinRelatedSetting->IsDefault())
      {
        m_ignoreSkinSettingChanges = true;
        skinRelatedSetting->Reset();
      }
    }
    else if (settingId == CSettings::SETTING_LOOKANDFEEL_SKINTHEME)
    {
      std::shared_ptr<CSettingString> skinColorsSetting = std::static_pointer_cast<CSettingString>(m_ServiceManager->GetSettings().GetSetting(CSettings::SETTING_LOOKANDFEEL_SKINCOLORS));
      m_ignoreSkinSettingChanges = true;

      // we also need to adjust the skin color setting
      std::string colorTheme = std::static_pointer_cast<const CSettingString>(setting)->GetValue();
      URIUtils::RemoveExtension(colorTheme);
      if (setting->IsDefault() || StringUtils::EqualsNoCase(colorTheme, "Textures"))
        skinColorsSetting->Reset();
      else
        skinColorsSetting->SetValue(colorTheme);
    }

    m_ignoreSkinSettingChanges = false;

    if (g_SkinInfo)
    {
      // now we can finally reload skins
      std::string builtin("ReloadSkin");
      if (settingId == CSettings::SETTING_LOOKANDFEEL_SKIN && m_confirmSkinChange)
        builtin += "(confirm)";
      CApplicationMessenger::GetInstance().PostMsg(TMSG_EXECUTE_BUILT_IN, -1, -1, nullptr, builtin);
    }
  }
  else if (settingId == CSettings::SETTING_LOOKANDFEEL_SKINZOOM)
  {
    CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_WINDOW_RESIZE);
    g_windowManager.SendThreadMessage(msg);
  }
  else if (settingId == CSettings::SETTING_SCREENSAVER_MODE)
  {
    CheckOSScreenSaverInhibitionSetting();
  }
  else if (StringUtils::StartsWithNoCase(settingId, "audiooutput."))
  { // AE is master of audio settings and needs to be informed first
    m_ServiceManager->GetActiveAE().OnSettingsChange(settingId);

    if (settingId == CSettings::SETTING_AUDIOOUTPUT_GUISOUNDMODE)
    {
      m_ServiceManager->GetActiveAE().SetSoundMode((std::static_pointer_cast<const CSettingInt>(setting))->GetValue());
    }
    // this tells player whether to open an audio stream passthrough or PCM
    // if this is changed, audio stream has to be reopened
    else if (settingId == CSettings::SETTING_AUDIOOUTPUT_PASSTHROUGH)
    {
      CApplicationMessenger::GetInstance().PostMsg(TMSG_MEDIA_RESTART);
    }
  }
  else if (settingId == CSettings::SETTING_VIDEOSCREEN_FAKEFULLSCREEN)
  {
    if (g_graphicsContext.IsFullScreenRoot())
      g_graphicsContext.SetVideoResolution(g_graphicsContext.GetVideoResolution(), true);
  }
  else if (StringUtils::EqualsNoCase(settingId, CSettings::SETTING_MUSICPLAYER_REPLAYGAINTYPE))
    m_replayGainSettings.iType = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  else if (StringUtils::EqualsNoCase(settingId, CSettings::SETTING_MUSICPLAYER_REPLAYGAINPREAMP))
    m_replayGainSettings.iPreAmp = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  else if (StringUtils::EqualsNoCase(settingId, CSettings::SETTING_MUSICPLAYER_REPLAYGAINNOGAINPREAMP))
    m_replayGainSettings.iNoGainPreAmp = std::static_pointer_cast<const CSettingInt>(setting)->GetValue();
  else if (StringUtils::EqualsNoCase(settingId, CSettings::SETTING_MUSICPLAYER_REPLAYGAINAVOIDCLIPPING))
    m_replayGainSettings.bAvoidClipping = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
}

void CApplication::OnSettingAction(std::shared_ptr<const CSetting> setting)
{
  if (setting == NULL)
    return;

  const std::string &settingId = setting->GetId();
  if (settingId == CSettings::SETTING_LOOKANDFEEL_SKINSETTINGS)
    g_windowManager.ActivateWindow(WINDOW_SKIN_SETTINGS);
  else if (settingId == CSettings::SETTING_SCREENSAVER_PREVIEW)
    ActivateScreenSaver(true);
  else if (settingId == CSettings::SETTING_SCREENSAVER_SETTINGS)
  {
    AddonPtr addon;
    if (CServiceBroker::GetAddonMgr().GetAddon(m_ServiceManager->GetSettings().GetString(CSettings::SETTING_SCREENSAVER_MODE), addon, ADDON_SCREENSAVER))
      CGUIDialogAddonSettings::ShowForAddon(addon);
  }
  else if (settingId == CSettings::SETTING_AUDIOCDS_SETTINGS)
  {
    AddonPtr addon;
    if (CServiceBroker::GetAddonMgr().GetAddon(m_ServiceManager->GetSettings().GetString(CSettings::SETTING_AUDIOCDS_ENCODER), addon, ADDON_AUDIOENCODER))
      CGUIDialogAddonSettings::ShowForAddon(addon);
  }
  else if (settingId == CSettings::SETTING_VIDEOSCREEN_GUICALIBRATION)
    g_windowManager.ActivateWindow(WINDOW_SCREEN_CALIBRATION);
  else if (settingId == CSettings::SETTING_VIDEOSCREEN_TESTPATTERN)
    g_windowManager.ActivateWindow(WINDOW_TEST_PATTERN);
  else if (settingId == CSettings::SETTING_SOURCE_VIDEOS)
  {
    std::vector<std::string> params{"library://video/files.xml", "return"};
    g_windowManager.ActivateWindow(WINDOW_VIDEO_NAV, params);
  }
  else if (settingId == CSettings::SETTING_SOURCE_MUSIC)
  {
    std::vector<std::string> params{"library://music/files.xml", "return"};
    g_windowManager.ActivateWindow(WINDOW_MUSIC_NAV, params);
  }
  else if (settingId == CSettings::SETTING_SOURCE_PICTURES)
    g_windowManager.ActivateWindow(WINDOW_PICTURES);
}

bool CApplication::OnSettingUpdate(std::shared_ptr<CSetting> setting, const char *oldSettingId, const TiXmlNode *oldSettingNode)
{
  if (setting == NULL)
    return false;

#if defined(HAS_LIBAMCODEC)
  if (setting->GetId() == CSettings::SETTING_VIDEOPLAYER_USEAMCODEC)
  {
    // Do not permit amcodec to be used on non-aml platforms.
    // The setting will be hidden but the default value is true,
    // so change it to false.
    if (!aml_present())
    {
      std::shared_ptr<CSettingBool> useamcodec = std::static_pointer_cast<CSettingBool>(setting);
      return useamcodec->SetValue(false);
    }
  }
#endif
#if defined(TARGET_DARWIN_OSX)
  if (setting->GetId() == CSettings::SETTING_AUDIOOUTPUT_AUDIODEVICE)
  {
    std::shared_ptr<CSettingString> audioDevice = std::static_pointer_cast<CSettingString>(setting);
    // Gotham and older didn't enumerate audio devices per stream on osx
    // add stream0 per default which should be ok for all old settings.
    if (!StringUtils::EqualsNoCase(audioDevice->GetValue(), "DARWINOSX:default") &&
        StringUtils::FindWords(audioDevice->GetValue().c_str(), ":stream") == std::string::npos)
    {
      std::string newSetting = audioDevice->GetValue();
      newSetting += ":stream0";
      return audioDevice->SetValue(newSetting);
    }
  }
#endif

  return false;
}

bool CApplication::OnSettingsSaving() const
{
  // don't save settings when we're busy stopping the application
  // a lot of screens try to save settings on deinit and deinit is
  // called for every screen when the application is stopping
  if (m_bStop)
    return false;

  return true;
}

void CApplication::ReloadSkin(bool confirm/*=false*/)
{
  if (!g_SkinInfo || m_bInitializing)
    return; // Don't allow reload before skin is loaded by system

  std::string oldSkin = g_SkinInfo->ID();

  CGUIMessage msg(GUI_MSG_LOAD_SKIN, -1, g_windowManager.GetActiveWindow());
  g_windowManager.SendMessage(msg);

  std::string newSkin = m_ServiceManager->GetSettings().GetString(CSettings::SETTING_LOOKANDFEEL_SKIN);
  if (LoadSkin(newSkin))
  {
    /* The Reset() or SetString() below will cause recursion, so the m_confirmSkinChange boolean is set so as to not prompt the
       user as to whether they want to keep the current skin. */
    if (confirm && m_confirmSkinChange)
    {
      if (HELPERS::ShowYesNoDialogText(CVariant{13123}, CVariant{13111}, CVariant{""}, CVariant{""}, 10000) !=
        DialogResponse::YES)
      {
        m_confirmSkinChange = false;
        m_ServiceManager->GetSettings().SetString(CSettings::SETTING_LOOKANDFEEL_SKIN, oldSkin);
      }
    }
  }
  else
  {
    // skin failed to load - we revert to the default only if we didn't fail loading the default
    std::string defaultSkin = std::static_pointer_cast<CSettingString>(m_ServiceManager->GetSettings().GetSetting(CSettings::SETTING_LOOKANDFEEL_SKIN))->GetDefault();
    if (newSkin != defaultSkin)
    {
      m_confirmSkinChange = false;
      m_ServiceManager->GetSettings().GetSetting(CSettings::SETTING_LOOKANDFEEL_SKIN)->Reset();
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, g_localizeStrings.Get(24102), g_localizeStrings.Get(24103));
    }
  }
  m_confirmSkinChange = true;
}

bool CApplication::Load(const TiXmlNode *settings)
{
  if (settings == NULL)
    return false;

  const TiXmlElement *audioElement = settings->FirstChildElement("audio");
  if (audioElement != NULL)
  {
    XMLUtils::GetBoolean(audioElement, "mute", m_muted);
    if (!XMLUtils::GetFloat(audioElement, "fvolumelevel", m_volumeLevel, VOLUME_MINIMUM, VOLUME_MAXIMUM))
      m_volumeLevel = VOLUME_MAXIMUM;
  }

  return true;
}

bool CApplication::Save(TiXmlNode *settings) const
{
  if (settings == NULL)
    return false;

  TiXmlElement volumeNode("audio");
  TiXmlNode *audioNode = settings->InsertEndChild(volumeNode);
  if (audioNode == NULL)
    return false;

  XMLUtils::SetBoolean(audioNode, "mute", m_muted);
  XMLUtils::SetFloat(audioNode, "fvolumelevel", m_volumeLevel);

  return true;
}

bool CApplication::LoadSkin(const std::string& skinID)
{
  SkinPtr skin;
  {
    AddonPtr addon;
    if (!CServiceBroker::GetAddonMgr().GetAddon(skinID, addon, ADDON_SKIN))
      return false;
    skin = std::static_pointer_cast<ADDON::CSkinInfo>(addon);
  }

  // store player and rendering state
  bool bPreviousPlayingState = false;

  enum class RENDERING_STATE
  {
    NONE,
    VIDEO,
    GAME,
  } previousRenderingState = RENDERING_STATE::NONE;

  if (m_appPlayer.IsPlayingVideo())
  {
    bPreviousPlayingState = !m_appPlayer.IsPausedPlayback();
    if (bPreviousPlayingState)
      m_appPlayer.Pause();
    m_appPlayer.FlushRenderer();
    if (g_windowManager.GetActiveWindow() == WINDOW_FULLSCREEN_VIDEO)
    {
      g_windowManager.ActivateWindow(WINDOW_HOME);
      previousRenderingState = RENDERING_STATE::VIDEO;
    }
    else if (g_windowManager.GetActiveWindow() == WINDOW_FULLSCREEN_GAME)
    {
      g_windowManager.ActivateWindow(WINDOW_HOME);
      previousRenderingState = RENDERING_STATE::GAME;
    }

  }

  CSingleLock lock(g_graphicsContext);

  // store current active window with its focused control
  int currentWindowID = g_windowManager.GetActiveWindow();
  int currentFocusedControlID = -1;
  if (currentWindowID != WINDOW_INVALID)
  {
    CGUIWindow* pWindow = g_windowManager.GetWindow(currentWindowID);
    if (pWindow)
      currentFocusedControlID = pWindow->GetFocusedControlID();
  }

  UnloadSkin();

  skin->Start();

  // migrate any skin-specific settings that are still stored in guisettings.xml
  CSkinSettings::GetInstance().MigrateSettings(skin);

  // check if the skin has been properly loaded and if it has a Home.xml
  if (!skin->HasSkinFile("Home.xml"))
  {
    CLog::Log(LOGERROR, "failed to load requested skin '%s'", skin->ID().c_str());
    return false;
  }

  CLog::Log(LOGNOTICE, "  load skin from: %s (version: %s)", skin->Path().c_str(), skin->Version().asString().c_str());
  g_SkinInfo = skin;

  CLog::Log(LOGINFO, "  load fonts for skin...");
  g_graphicsContext.SetMediaDir(skin->Path());
  g_directoryCache.ClearSubPaths(skin->Path());

  g_colorManager.Load(m_ServiceManager->GetSettings().GetString(CSettings::SETTING_LOOKANDFEEL_SKINCOLORS));

  g_SkinInfo->LoadIncludes();

  g_fontManager.LoadFonts(m_ServiceManager->GetSettings().GetString(CSettings::SETTING_LOOKANDFEEL_FONT));

  // load in the skin strings
  std::string langPath = URIUtils::AddFileToFolder(skin->Path(), "language");
  URIUtils::AddSlashAtEnd(langPath);

  g_localizeStrings.LoadSkinStrings(langPath, m_ServiceManager->GetSettings().GetString(CSettings::SETTING_LOCALE_LANGUAGE));


  int64_t start;
  start = CurrentHostCounter();

  CLog::Log(LOGINFO, "  load new skin...");

  // Load custom windows
  LoadCustomWindows();

  int64_t end, freq;
  end = CurrentHostCounter();
  freq = CurrentHostFrequency();
  CLog::Log(LOGDEBUG,"Load Skin XML: %.2fms", 1000.f * (end - start) / freq);

  CLog::Log(LOGINFO, "  initialize new skin...");
  g_windowManager.AddMsgTarget(this);
  g_windowManager.AddMsgTarget(&CServiceBroker::GetPlaylistPlayer());
  g_windowManager.AddMsgTarget(&g_infoManager);
  g_windowManager.AddMsgTarget(&g_fontManager);
  g_windowManager.AddMsgTarget(&CStereoscopicsManager::GetInstance());
  g_windowManager.SetCallback(*this);
  g_windowManager.Initialize();
  CTextureCache::GetInstance().Initialize();
  g_audioManager.Enable(true);
  g_audioManager.Load();

  if (g_SkinInfo->HasSkinFile("DialogFullScreenInfo.xml"))
    g_windowManager.Add(new CGUIDialogFullScreenInfo);

  CLog::Log(LOGINFO, "  skin loaded...");

  // leave the graphics lock
  lock.Leave();

  // restore active window
  if (currentWindowID != WINDOW_INVALID)
  {
    g_windowManager.ActivateWindow(currentWindowID);
    if (currentFocusedControlID != -1)
    {
      CGUIWindow *pWindow = g_windowManager.GetWindow(currentWindowID);
      if (pWindow && pWindow->HasSaveLastControl())
      {
        CGUIMessage msg(GUI_MSG_SETFOCUS, currentWindowID, currentFocusedControlID, 0);
        pWindow->OnMessage(msg);
      }
    }
  }

  // restore player and rendering state
  if (m_appPlayer.IsPlayingVideo())
  {
    if (bPreviousPlayingState)
      m_appPlayer.Pause();

    switch (previousRenderingState)
    {
    case RENDERING_STATE::VIDEO:
      g_windowManager.ActivateWindow(WINDOW_FULLSCREEN_VIDEO);
      break;
    case RENDERING_STATE::GAME:
      g_windowManager.ActivateWindow(WINDOW_FULLSCREEN_GAME);
      break;
    default:
      break;
    }
  }

  return true;
}

void CApplication::UnloadSkin(bool forReload /* = false */)
{
  CLog::Log(LOGINFO, "Unloading old skin %s...", forReload ? "for reload " : "");

  if (g_SkinInfo != nullptr && m_saveSkinOnUnloading)
    g_SkinInfo->SaveSettings();
  else if (!m_saveSkinOnUnloading)
    m_saveSkinOnUnloading = true;

  g_audioManager.Enable(false);

  g_windowManager.DeInitialize();
  CTextureCache::GetInstance().Deinitialize();

  // remove the skin-dependent window
  g_windowManager.Delete(WINDOW_DIALOG_FULLSCREEN_INFO);

  g_TextureManager.Cleanup();
  g_largeTextureManager.CleanupUnusedImages(true);

  g_fontManager.Clear();

  g_colorManager.Clear();

  g_infoManager.Clear();

//  The g_SkinInfo shared_ptr ought to be reset here
// but there are too many places it's used without checking for NULL
// and as a result a race condition on exit can cause a crash.
}

bool CApplication::LoadCustomWindows()
{
  // Start from wherever home.xml is
  std::vector<std::string> vecSkinPath;
  g_SkinInfo->GetSkinPaths(vecSkinPath);

  for (const auto &skinPath : vecSkinPath)
  {
    CLog::Log(LOGINFO, "Loading custom window XMLs from skin path %s", skinPath.c_str());

    CFileItemList items;
    if (CDirectory::GetDirectory(skinPath, items, ".xml", DIR_FLAG_NO_FILE_DIRS))
    {
      for (const auto &item : items.GetList())
      {
        if (item->m_bIsFolder)
          continue;

        std::string skinFile = URIUtils::GetFileName(item->GetPath());
        if (StringUtils::StartsWithNoCase(skinFile, "custom"))
        {
          CXBMCTinyXML xmlDoc;
          if (!xmlDoc.LoadFile(item->GetPath()))
          {
            CLog::Log(LOGERROR, "Unable to load custom window XML %s. Line %d\n%s", item->GetPath().c_str(), xmlDoc.ErrorRow(), xmlDoc.ErrorDesc());
            continue;
          }

          // Root element should be <window>
          TiXmlElement* pRootElement = xmlDoc.RootElement();
          std::string strValue = pRootElement->Value();
          if (!StringUtils::EqualsNoCase(strValue, "window"))
          {
            CLog::Log(LOGERROR, "No <window> root element found for custom window in %s", skinFile.c_str());
            continue;
          }

          int id = WINDOW_INVALID;

          // Read the type attribute or element to get the window type to create
          // If no type is specified, create a CGUIWindow as default
          std::string strType;
          if (pRootElement->Attribute("type"))
            strType = pRootElement->Attribute("type");
          else
          {
            const TiXmlNode *pType = pRootElement->FirstChild("type");
            if (pType && pType->FirstChild())
              strType = pType->FirstChild()->Value();
          }

          // Read the id attribute or element to get the window id
          if (!pRootElement->Attribute("id", &id))
          {
            const TiXmlNode *pType = pRootElement->FirstChild("id");
            if (pType && pType->FirstChild())
              id = atol(pType->FirstChild()->Value());
          }

          int windowId = id + WINDOW_HOME;
          if (id == WINDOW_INVALID || g_windowManager.GetWindow(windowId))
          {
            // No id specified or id already in use
            CLog::Log(LOGERROR, "No id specified or id already in use for custom window in %s", skinFile.c_str());
            continue;
          }

          CGUIWindow* pWindow = NULL;
          bool hasVisibleCondition = false;

          if (StringUtils::EqualsNoCase(strType, "dialog"))
          {
            hasVisibleCondition = pRootElement->FirstChildElement("visible") != nullptr;
            pWindow = new CGUIDialog(windowId, skinFile);
          }
          else if (StringUtils::EqualsNoCase(strType, "submenu"))
          {
            pWindow = new CGUIDialogSubMenu(windowId, skinFile);
          }
          else if (StringUtils::EqualsNoCase(strType, "buttonmenu"))
          {
            pWindow = new CGUIDialogButtonMenu(windowId, skinFile);
          }
          else
          {
            pWindow = new CGUIWindow(windowId, skinFile);
          }

          if (!pWindow)
          {
            CLog::Log(LOGERROR, "Failed to create custom window from %s", skinFile.c_str());
            continue;
          }

          pWindow->SetCustom(true);

          // Determining whether our custom dialog is modeless (visible condition is present)
          // will be done on load. Therefore we need to initialize the custom dialog on gui init.
          pWindow->SetLoadType(hasVisibleCondition ? CGUIWindow::LOAD_ON_GUI_INIT : CGUIWindow::KEEP_IN_MEMORY);

          g_windowManager.AddCustomWindow(pWindow);
        }
      }
    }
  }
  return true;
}

void CApplication::Render()
{
  // do not render if we are stopped or in background
  if (m_bStop)
    return;

  bool hasRendered = false;

  // Whether externalplayer is playing and we're unfocused
  bool extPlayerActive = m_appPlayer.IsExternalPlaying() && !m_AppFocused;

  if (!extPlayerActive && g_graphicsContext.IsFullScreenVideo() && !m_appPlayer.IsPausedPlayback())
  {
    ResetScreenSaver();
  }

  if(!CServiceBroker::GetRenderSystem().BeginRender())
    return;

  // render gui layer
  if (!m_skipGuiRender)
  {
    if (g_graphicsContext.GetStereoMode())
    {
      g_graphicsContext.SetStereoView(RENDER_STEREO_VIEW_LEFT);
      hasRendered |= g_windowManager.Render();

      if (g_graphicsContext.GetStereoMode() != RENDER_STEREO_MODE_MONO)
      {
        g_graphicsContext.SetStereoView(RENDER_STEREO_VIEW_RIGHT);
        hasRendered |= g_windowManager.Render();
      }
      g_graphicsContext.SetStereoView(RENDER_STEREO_VIEW_OFF);
    }
    else
    {
      hasRendered |= g_windowManager.Render();
    }
    // execute post rendering actions (finalize window closing)
    g_windowManager.AfterRender();

    m_lastRenderTime = XbmcThreads::SystemClockMillis();
  }

  // render video layer
  g_windowManager.RenderEx();

  CServiceBroker::GetRenderSystem().EndRender();

  // reset our info cache - we do this at the end of Render so that it is
  // fresh for the next process(), or after a windowclose animation (where process()
  // isn't called)
  g_infoManager.ResetCache();

  if (hasRendered)
  {
    g_infoManager.UpdateFPS();
  }

  g_graphicsContext.Flip(hasRendered, m_appPlayer.IsRenderingVideoLayer());

  CTimeUtils::UpdateFrameTime(hasRendered);
}

void CApplication::SetStandAlone(bool value)
{
  g_advancedSettings.m_handleMounting = m_bStandalone = value;
}


// OnAppCommand is called in response to a XBMC_APPCOMMAND event.
// This needs to return true if it processed the appcommand or false if it didn't
bool CApplication::OnAppCommand(const CAction &action)
{
  // Reset the screen saver
  ResetScreenSaver();

  // If we were currently in the screen saver wake up and don't process the appcommand
  if (WakeUpScreenSaverAndDPMS())
    return true;

  // The action ID is the APPCOMMAND code. We need to retrieve the action
  // associated with this appcommand from the mapping table.
  uint32_t appcmd = action.GetID();
  CKey key(appcmd | KEY_APPCOMMAND, (unsigned int) 0);
  int iWin = g_windowManager.GetActiveWindow() & WINDOW_ID_MASK;
  CAction appcmdaction = CServiceBroker::GetInputManager().GetAction(iWin, key);

  // If we couldn't find an action return false to indicate we have not
  // handled this appcommand
  if (!appcmdaction.GetID())
  {
    CLog::LogF(LOGDEBUG, "unknown appcommand %d", appcmd);
    return false;
  }

  // Process the appcommand
  CLog::LogF(LOGDEBUG, "appcommand %d, trying action %s", appcmd, appcmdaction.GetName().c_str());
  OnAction(appcmdaction);

  // Always return true regardless of whether the action succeeded or not.
  // This stops Windows handling the appcommand itself.
  return true;
}

bool CApplication::OnAction(const CAction &action)
{
  // special case for switching between GUI & fullscreen mode.
  if (action.GetID() == ACTION_SHOW_GUI)
  { // Switch to fullscreen mode if we can
    if (SwitchToFullScreen())
    {
      m_navigationTimer.StartZero();
      return true;
    }
  }

  if (action.GetID() == ACTION_TOGGLE_FULLSCREEN)
  {
    g_graphicsContext.ToggleFullScreen();
    m_appPlayer.TriggerUpdateResolution();
    return true;
  }

  if (action.IsMouse())
    CServiceBroker::GetInputManager().SetMouseActive(true);

  if (action.GetID() == ACTION_CREATE_EPISODE_BOOKMARK)
  {
    CGUIDialogVideoBookmarks::OnAddEpisodeBookmark();
  }
  if (action.GetID() == ACTION_CREATE_BOOKMARK)
  {
    CGUIDialogVideoBookmarks::OnAddBookmark();
  }

  // The action PLAYPAUSE behaves as ACTION_PAUSE if we are currently
  // playing or ACTION_PLAYER_PLAY if we are seeking (FF/RW) or not playing.
  if (action.GetID() == ACTION_PLAYER_PLAYPAUSE)
  {
    if (m_appPlayer.IsPlaying() && m_appPlayer.GetPlaySpeed() == 1)
      return OnAction(CAction(ACTION_PAUSE));
    else
      return OnAction(CAction(ACTION_PLAYER_PLAY));
  }

  //if the action would start or stop inertial scrolling
  //by gesture - bypass the normal OnAction handler of current window
  if( !m_pInertialScrollingHandler->CheckForInertialScrolling(&action) )
  {
    // in normal case
    // just pass the action to the current window and let it handle it
    if (g_windowManager.OnAction(action))
    {
      m_navigationTimer.StartZero();
      return true;
    }
  }

  // handle extra global presses

  // notify action listeners
  if (NotifyActionListeners(action))
    return true;

  // screenshot : take a screenshot :)
  if (action.GetID() == ACTION_TAKE_SCREENSHOT)
  {
    CScreenShot::TakeScreenshot();
    return true;
  }
  // built in functions : execute the built-in
  if (action.GetID() == ACTION_BUILT_IN_FUNCTION)
  {
    if (!CBuiltins::GetInstance().IsSystemPowerdownCommand(action.GetName()) ||
        CServiceBroker::GetPVRManager().GUIActions()->CanSystemPowerdown())
    {
      CBuiltins::GetInstance().Execute(action.GetName());
      m_navigationTimer.StartZero();
    }
    return true;
  }

  // reload keymaps
  if (action.GetID() == ACTION_RELOAD_KEYMAPS)
    CServiceBroker::GetInputManager().ReloadKeymaps();

  // show info : Shows the current video or song information
  if (action.GetID() == ACTION_SHOW_INFO)
  {
    g_infoManager.ToggleShowInfo();
    return true;
  }

  if ((action.GetID() == ACTION_INCREASE_RATING || action.GetID() == ACTION_DECREASE_RATING) && m_appPlayer.IsPlayingAudio())
  {
    const CMusicInfoTag *tag = g_infoManager.GetCurrentSongTag();
    if (tag)
    {
      *m_itemCurrentFile->GetMusicInfoTag() = *tag;
      int userrating = tag->GetUserrating();
      bool needsUpdate(false);
      if (userrating > 0 && action.GetID() == ACTION_DECREASE_RATING)
      {
        m_itemCurrentFile->GetMusicInfoTag()->SetUserrating(userrating - 1);
        needsUpdate = true;
      }
      else if (userrating < 10 && action.GetID() == ACTION_INCREASE_RATING)
      {
        m_itemCurrentFile->GetMusicInfoTag()->SetUserrating(userrating + 1);
        needsUpdate = true;
      }
      if (needsUpdate)
      {
        CMusicDatabase db;
        if (db.Open())      // OpenForWrite() ?
        {
          db.SetSongUserrating(m_itemCurrentFile->GetPath(), m_itemCurrentFile->GetMusicInfoTag()->GetUserrating());
          db.Close();
        }
        // send a message to all windows to tell them to update the fileitem (eg playlistplayer, media windows)
        CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE_ITEM, 0, m_itemCurrentFile);
        g_windowManager.SendMessage(msg);
      }
    }
    return true;
  }
  else if ((action.GetID() == ACTION_INCREASE_RATING || action.GetID() == ACTION_DECREASE_RATING) && m_appPlayer.IsPlayingVideo())
  {
    const CVideoInfoTag *tag = g_infoManager.GetCurrentMovieTag();
    if (tag)
    {
      *m_itemCurrentFile->GetVideoInfoTag() = *tag;
      int rating = tag->m_iUserRating;
      bool needsUpdate(false);
      if (rating > 1 && action.GetID() == ACTION_DECREASE_RATING)
      {
        m_itemCurrentFile->GetVideoInfoTag()->m_iUserRating = rating - 1;
        needsUpdate = true;
      }
      else if (rating < 10 && action.GetID() == ACTION_INCREASE_RATING)
      {
        m_itemCurrentFile->GetVideoInfoTag()->m_iUserRating = rating + 1;
        needsUpdate = true;
      }
      if (needsUpdate)
      {
        CVideoDatabase db;
        if (db.Open())
        {
          db.SetVideoUserRating(m_itemCurrentFile->GetVideoInfoTag()->m_iDbId, m_itemCurrentFile->GetVideoInfoTag()->m_iUserRating, m_itemCurrentFile->GetVideoInfoTag()->m_type);
          db.Close();
        }
        // send a message to all windows to tell them to update the fileitem (eg playlistplayer, media windows)
        CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE_ITEM, 0, m_itemCurrentFile);
        g_windowManager.SendMessage(msg);
      }
    }
    return true;
  }

  // Now check with the playlist player if action can be handled.
  // In case of the action PREV_ITEM, we only allow the playlist player to take it if we're less than 3 seconds into playback.
  if (!(action.GetID() == ACTION_PREV_ITEM && m_appPlayer.CanSeek() && GetTime() > 3) )
  {
    if (CServiceBroker::GetPlaylistPlayer().OnAction(action))
      return true;
  }

  // Now check with the player if action can be handled.
  bool bIsPlayingPVRChannel = (CServiceBroker::GetPVRManager().IsStarted() && g_application.CurrentFileItem().IsPVRChannel());
  if (g_windowManager.GetActiveWindow() == WINDOW_FULLSCREEN_VIDEO ||
      g_windowManager.GetActiveWindow() == WINDOW_FULLSCREEN_GAME ||
      (g_windowManager.GetActiveWindow() == WINDOW_VISUALISATION && bIsPlayingPVRChannel) ||
      ((g_windowManager.GetActiveWindow() == WINDOW_DIALOG_VIDEO_OSD || (g_windowManager.GetActiveWindow() == WINDOW_DIALOG_MUSIC_OSD && bIsPlayingPVRChannel)) &&
        (action.GetID() == ACTION_NEXT_ITEM || action.GetID() == ACTION_PREV_ITEM || action.GetID() == ACTION_CHANNEL_UP || action.GetID() == ACTION_CHANNEL_DOWN)) ||
      action.GetID() == ACTION_STOP)
  {
    if (m_appPlayer.OnAction(action))
      return true;
    // Player ignored action; popup the OSD
    if ((action.GetID() == ACTION_MOUSE_MOVE && (action.GetAmount(2) || action.GetAmount(3)))  // filter "false" mouse move from touch
        || action.GetID() == ACTION_MOUSE_LEFT_CLICK)
    {
      CApplicationMessenger::GetInstance().PostMsg(TMSG_GUI_ACTION, WINDOW_INVALID, -1, static_cast<void*>(new CAction(ACTION_TRIGGER_OSD)));
    }
  }

  // stop : stops playing current audio song
  if (action.GetID() == ACTION_STOP)
  {
    StopPlaying();
    return true;
  }

  // In case the playlist player nor the player didn't handle PREV_ITEM, because we are past the 3 secs limit.
  // If so, we just jump to the start of the track.
  if (action.GetID() == ACTION_PREV_ITEM && m_appPlayer.CanSeek())
  {
    SeekTime(0);
    m_appPlayer.SetPlaySpeed(1);
    return true;
  }

  // forward action to graphic context and see if it can handle it
  if (CStereoscopicsManager::GetInstance().OnAction(action))
    return true;

  if (m_appPlayer.IsPlaying())
  {
    // forward channel switches to the player - he knows what to do
    if (action.GetID() == ACTION_CHANNEL_UP || action.GetID() == ACTION_CHANNEL_DOWN)
    {
      m_appPlayer.OnAction(action);
      return true;
    }

    // pause : toggle pause action
    if (action.GetID() == ACTION_PAUSE)
    {
      m_appPlayer.Pause();
      // go back to normal play speed on unpause
      if (!m_appPlayer.IsPaused() && m_appPlayer.GetPlaySpeed() != 1)
        m_appPlayer.SetPlaySpeed(1);

      g_audioManager.Enable(m_appPlayer.IsPaused());
      return true;
    }
    // play: unpause or set playspeed back to normal
    if (action.GetID() == ACTION_PLAYER_PLAY)
    {
      // if currently paused - unpause
      if (m_appPlayer.IsPaused())
        return OnAction(CAction(ACTION_PAUSE));
      // if we do a FF/RW then go back to normal speed
      if (m_appPlayer.GetPlaySpeed() != 1)
        m_appPlayer.SetPlaySpeed(1);
      return true;
    }
    if (!m_appPlayer.IsPaused())
    {
      if (action.GetID() == ACTION_PLAYER_FORWARD || action.GetID() == ACTION_PLAYER_REWIND)
      {
        float playSpeed = m_appPlayer.GetPlaySpeed();

        if (action.GetID() == ACTION_PLAYER_REWIND && (playSpeed == 1)) // Enables Rewinding
          playSpeed *= -2;
        else if (action.GetID() == ACTION_PLAYER_REWIND && playSpeed > 1) //goes down a notch if you're FFing
          playSpeed /= 2;
        else if (action.GetID() == ACTION_PLAYER_FORWARD && playSpeed < 1) //goes up a notch if you're RWing
          playSpeed /= 2;
        else
          playSpeed *= 2;

        if (action.GetID() == ACTION_PLAYER_FORWARD && playSpeed == -1) //sets iSpeed back to 1 if -1 (didn't plan for a -1)
          playSpeed = 1;
        if (playSpeed > 32 || playSpeed < -32)
          playSpeed = 1;

        m_appPlayer.SetPlaySpeed(playSpeed);
        return true;
      }
      else if ((action.GetAmount() || m_appPlayer.GetPlaySpeed() != 1) && (action.GetID() == ACTION_ANALOG_REWIND || action.GetID() == ACTION_ANALOG_FORWARD))
      {
        // calculate the speed based on the amount the button is held down
        int iPower = (int)(action.GetAmount() * MAX_FFWD_SPEED + 0.5f);
        // amount can be negative, for example rewind and forward share the same axis
        iPower = std::abs(iPower);
        // returns 0 -> MAX_FFWD_SPEED
        int iSpeed = 1 << iPower;
        if (iSpeed != 1 && action.GetID() == ACTION_ANALOG_REWIND)
          iSpeed = -iSpeed;
        m_appPlayer.SetPlaySpeed(static_cast<float>(iSpeed));
        if (iSpeed == 1)
          CLog::Log(LOGDEBUG,"Resetting playspeed");
        return true;
      }
    }
    // allow play to unpause
    else
    {
      if (action.GetID() == ACTION_PLAYER_PLAY)
      {
        // unpause, and set the playspeed back to normal
        m_appPlayer.Pause();
        g_audioManager.Enable(m_appPlayer.IsPaused());

        m_appPlayer.SetPlaySpeed(1);
        return true;
      }
    }
  }


  if (action.GetID() == ACTION_SWITCH_PLAYER)
  {
    if(m_appPlayer.IsPlaying())
    {
      std::vector<std::string> players;
      CFileItem item(*m_itemCurrentFile.get());
      CPlayerCoreFactory::GetInstance().GetPlayers(item, players);
      std::string player = CPlayerCoreFactory::GetInstance().SelectPlayerDialog(players);
      if (!player.empty())
      {
        item.m_lStartOffset = (int)(GetTime() * 75);
        PlayFile(std::move(item), player, true);
      }
    }
    else
    {
      std::vector<std::string> players;
      CPlayerCoreFactory::GetInstance().GetRemotePlayers(players);
      std::string player = CPlayerCoreFactory::GetInstance().SelectPlayerDialog(players);
      if (!player.empty())
      {
        PlayFile(CFileItem(), player, false);
      }
    }
  }

  if (CServiceBroker::GetPeripherals().OnAction(action))
    return true;

  if (action.GetID() == ACTION_MUTE)
  {
    ToggleMute();
    ShowVolumeBar(&action);
    return true;
  }

  if (action.GetID() == ACTION_TOGGLE_DIGITAL_ANALOG)
  {
    bool passthrough = m_ServiceManager->GetSettings().GetBool(CSettings::SETTING_AUDIOOUTPUT_PASSTHROUGH);
    m_ServiceManager->GetSettings().SetBool(CSettings::SETTING_AUDIOOUTPUT_PASSTHROUGH, !passthrough);

    if (g_windowManager.GetActiveWindow() == WINDOW_SETTINGS_SYSTEM)
    {
      CGUIMessage msg(GUI_MSG_WINDOW_INIT, 0,0,WINDOW_INVALID,g_windowManager.GetActiveWindow());
      g_windowManager.SendMessage(msg);
    }
    return true;
  }

  // Check for global volume control
  if ((action.GetAmount() && (action.GetID() == ACTION_VOLUME_UP || action.GetID() == ACTION_VOLUME_DOWN)) || action.GetID() == ACTION_VOLUME_SET)
  {
    if (!m_appPlayer.IsPassthrough())
    {
      if (m_muted)
        UnMute();
      float volume = m_volumeLevel;
      int volumesteps = m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_AUDIOOUTPUT_VOLUMESTEPS);
      // sanity check
      if (volumesteps == 0)
        volumesteps = 90;

// Android has steps based on the max available volume level
#if defined(TARGET_ANDROID)
      float step = (VOLUME_MAXIMUM - VOLUME_MINIMUM) / CXBMCApp::GetMaxSystemVolume();
#else
      float step   = (VOLUME_MAXIMUM - VOLUME_MINIMUM) / volumesteps;

      if (action.GetRepeat())
        step *= action.GetRepeat() * 50; // 50 fps
#endif
      if (action.GetID() == ACTION_VOLUME_UP)
        volume += (float)(action.GetAmount() * action.GetAmount() * step);
      else if (action.GetID() == ACTION_VOLUME_DOWN)
        volume -= (float)(action.GetAmount() * action.GetAmount() * step);
      else
        volume = action.GetAmount() * step;
      if (volume != m_volumeLevel)
        SetVolume(volume, false);
    }
    // show visual feedback of volume or passthrough indicator
    ShowVolumeBar(&action);
    return true;
  }
  if (action.GetID() == ACTION_GUIPROFILE_BEGIN)
  {
    CGUIControlProfiler::Instance().SetOutputFile(CSpecialProtocol::TranslatePath("special://home/guiprofiler.xml"));
    CGUIControlProfiler::Instance().Start();
    return true;
  }
  if (action.GetID() == ACTION_SHOW_PLAYLIST)
  {
    int iPlaylist = CServiceBroker::GetPlaylistPlayer().GetCurrentPlaylist();
    if (iPlaylist == PLAYLIST_VIDEO && g_windowManager.GetActiveWindow() != WINDOW_VIDEO_PLAYLIST)
      g_windowManager.ActivateWindow(WINDOW_VIDEO_PLAYLIST);
    else if (iPlaylist == PLAYLIST_MUSIC && g_windowManager.GetActiveWindow() != WINDOW_MUSIC_PLAYLIST)
      g_windowManager.ActivateWindow(WINDOW_MUSIC_PLAYLIST);
    return true;
  }
  return false;
}

int CApplication::GetMessageMask()
{
  return TMSG_MASK_APPLICATION;
}

void CApplication::OnApplicationMessage(ThreadMessage* pMsg)
{
  uint32_t msg = pMsg->dwMessage;
  if (msg == TMSG_SYSTEM_POWERDOWN)
  {
    if (CServiceBroker::GetPVRManager().GUIActions()->CanSystemPowerdown())
      msg = pMsg->param1; // perform requested shutdown action
    else
      return; // no shutdown
  }

  switch (msg)
  {
  case TMSG_POWERDOWN:
    Stop(EXITCODE_POWERDOWN);
    CServiceBroker::GetPowerManager().Powerdown();
    break;

  case TMSG_QUIT:
    Stop(EXITCODE_QUIT);
    break;

  case TMSG_SHUTDOWN:
    HandleShutdownMessage();
    break;

  case TMSG_RENDERER_FLUSH:
    m_appPlayer.FlushRenderer();
    break;

  case TMSG_HIBERNATE:
    CServiceBroker::GetPowerManager().Hibernate();
    break;

  case TMSG_SUSPEND:
    CServiceBroker::GetPowerManager().Suspend();
    break;

  case TMSG_RESTART:
  case TMSG_RESET:
    Stop(EXITCODE_REBOOT);
    CServiceBroker::GetPowerManager().Reboot();
    break;

  case TMSG_RESTARTAPP:
#if defined(TARGET_WINDOWS) || defined(TARGET_LINUX)
    Stop(EXITCODE_RESTARTAPP);
#endif
    break;

  case TMSG_INHIBITIDLESHUTDOWN:
    InhibitIdleShutdown(pMsg->param1 != 0);
    break;

  case TMSG_ACTIVATESCREENSAVER:
    ActivateScreenSaver();
    break;

  case TMSG_VOLUME_SHOW:
  {
    CAction action(pMsg->param1);
    ShowVolumeBar(&action);
  }
  break;

#ifdef TARGET_ANDROID
  case TMSG_DISPLAY_SETUP:
    // We might come from a refresh rate switch destroying the native window; use the context resolution
    *static_cast<bool*>(pMsg->lpVoid) = InitWindow(g_graphicsContext.GetVideoResolution());
    SetRenderGUI(true);
    break;

  case TMSG_DISPLAY_DESTROY:
    *static_cast<bool*>(pMsg->lpVoid) = CServiceBroker::GetWinSystem().DestroyWindow();
    SetRenderGUI(false);
    break;
#endif

  case TMSG_START_ANDROID_ACTIVITY:
  {
#if defined(TARGET_ANDROID)
    if (pMsg->params.size())
    {
      CXBMCApp::StartActivity(pMsg->params[0],
        pMsg->params.size() > 1 ? pMsg->params[1] : "",
        pMsg->params.size() > 2 ? pMsg->params[2] : "",
        pMsg->params.size() > 3 ? pMsg->params[3] : "");
    }
#endif
  }
  break;

  case TMSG_NETWORKMESSAGE:
    m_ServiceManager->GetNetwork().NetworkMessage((CNetwork::EMESSAGE)pMsg->param1, pMsg->param2);
    break;

  case TMSG_SETLANGUAGE:
    SetLanguage(pMsg->strParam);
    break;


  case TMSG_SWITCHTOFULLSCREEN:
    if (g_windowManager.GetActiveWindow() != WINDOW_FULLSCREEN_VIDEO &&
        g_windowManager.GetActiveWindow() != WINDOW_FULLSCREEN_GAME)
      SwitchToFullScreen(true);
    break;

  case TMSG_VIDEORESIZE:
  {
    XBMC_Event newEvent;
    memset(&newEvent, 0, sizeof(newEvent));
    newEvent.type = XBMC_VIDEORESIZE;
    newEvent.resize.w = pMsg->param1;
    newEvent.resize.h = pMsg->param2;
    OnEvent(newEvent);
    g_windowManager.MarkDirty();
  }
    break;

  case TMSG_SETVIDEORESOLUTION:
    g_graphicsContext.SetVideoResolution(static_cast<RESOLUTION>(pMsg->param1), pMsg->param2 == 1);
    break;

  case TMSG_TOGGLEFULLSCREEN:
    g_graphicsContext.ToggleFullScreen();
    m_appPlayer.TriggerUpdateResolution();
    break;

  case TMSG_MINIMIZE:
    Minimize();
    break;

  case TMSG_EXECUTE_OS:
    /* Suspend AE temporarily so exclusive or hog-mode sinks */
    /* don't block external player's access to audio device  */
    if (!m_ServiceManager->GetActiveAE().Suspend())
    {
      CLog::Log(LOGNOTICE, "%s: Failed to suspend AudioEngine before launching external program", __FUNCTION__);
    }
#if defined( TARGET_POSIX) && !defined(TARGET_DARWIN)
    CUtil::RunCommandLine(pMsg->strParam.c_str(), (pMsg->param1 == 1));
#elif defined(TARGET_WINDOWS)
    CWIN32Util::XBMCShellExecute(pMsg->strParam.c_str(), (pMsg->param1 == 1));
#endif
    /* Resume AE processing of XBMC native audio */
    if (!m_ServiceManager->GetActiveAE().Resume())
    {
      CLog::Log(LOGFATAL, "%s: Failed to restart AudioEngine after return from external player", __FUNCTION__);
    }
    break;

  case TMSG_EXECUTE_SCRIPT:
    CScriptInvocationManager::GetInstance().ExecuteAsync(pMsg->strParam);
    break;

  case TMSG_EXECUTE_BUILT_IN:
    CBuiltins::GetInstance().Execute(pMsg->strParam.c_str());
    break;

  case TMSG_PICTURE_SHOW:
  {
    CGUIWindowSlideShow *pSlideShow = g_windowManager.GetWindow<CGUIWindowSlideShow>(WINDOW_SLIDESHOW);
    if (!pSlideShow) return;

    // stop playing file
    if (m_appPlayer.IsPlayingVideo()) g_application.StopPlaying();

    if (g_windowManager.GetActiveWindow() == WINDOW_FULLSCREEN_VIDEO)
      g_windowManager.PreviousWindow();

    g_application.ResetScreenSaver();
    g_application.WakeUpScreenSaverAndDPMS();

    if (g_windowManager.GetActiveWindow() != WINDOW_SLIDESHOW)
      g_windowManager.ActivateWindow(WINDOW_SLIDESHOW);
    if (URIUtils::IsZIP(pMsg->strParam) || URIUtils::IsRAR(pMsg->strParam)) // actually a cbz/cbr
    {
      CFileItemList items;
      CURL pathToUrl;
      if (URIUtils::IsZIP(pMsg->strParam))
        pathToUrl = URIUtils::CreateArchivePath("zip", CURL(pMsg->strParam), "");
      else
        pathToUrl = URIUtils::CreateArchivePath("rar", CURL(pMsg->strParam), "");

      CUtil::GetRecursiveListing(pathToUrl.Get(), items, CServiceBroker::GetFileExtensionProvider().GetPictureExtensions(), XFILE::DIR_FLAG_NO_FILE_DIRS);
      if (items.Size() > 0)
      {
        pSlideShow->Reset();
        for (int i = 0; i<items.Size(); ++i)
        {
          pSlideShow->Add(items[i].get());
        }
        pSlideShow->Select(items[0]->GetPath());
      }
    }
    else
    {
      CFileItem item(pMsg->strParam, false);
      pSlideShow->Reset();
      pSlideShow->Add(&item);
      pSlideShow->Select(pMsg->strParam);
    }
  }
  break;

  case TMSG_PICTURE_SLIDESHOW:
  {
    CGUIWindowSlideShow *pSlideShow = g_windowManager.GetWindow<CGUIWindowSlideShow>(WINDOW_SLIDESHOW);
    if (!pSlideShow) return;

    if (m_appPlayer.IsPlayingVideo())
      g_application.StopPlaying();

    pSlideShow->Reset();

    CFileItemList items;
    std::string strPath = pMsg->strParam;
    std::string extensions = CServiceBroker::GetFileExtensionProvider().GetPictureExtensions();
    if (pMsg->param1)
      extensions += "|.tbn";
    CUtil::GetRecursiveListing(strPath, items, extensions);

    if (items.Size() > 0)
    {
      for (int i = 0; i<items.Size(); ++i)
        pSlideShow->Add(items[i].get());
      pSlideShow->StartSlideShow(); //Start the slideshow!
    }

    if (g_windowManager.GetActiveWindow() != WINDOW_SLIDESHOW)
    {
      if (items.Size() == 0)
      {
        m_ServiceManager->GetSettings().SetString(CSettings::SETTING_SCREENSAVER_MODE, "screensaver.xbmc.builtin.dim");
        g_application.ActivateScreenSaver();
      }
      else
        g_windowManager.ActivateWindow(WINDOW_SLIDESHOW);
    }

  }
  break;

  case TMSG_LOADPROFILE:
    CGUIWindowLoginScreen::LoadProfile(pMsg->param1);
    break;

  default:
    CLog::Log(LOGERROR, "%s: Unhandled threadmessage sent, %u", __FUNCTION__, msg);
    break;
  }
}

void CApplication::HandleShutdownMessage()
{
  switch (m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_POWERMANAGEMENT_SHUTDOWNSTATE))
  {
  case POWERSTATE_SHUTDOWN:
    CApplicationMessenger::GetInstance().PostMsg(TMSG_POWERDOWN);
    break;

  case POWERSTATE_SUSPEND:
    CApplicationMessenger::GetInstance().PostMsg(TMSG_SUSPEND);
    break;

  case POWERSTATE_HIBERNATE:
    CApplicationMessenger::GetInstance().PostMsg(TMSG_HIBERNATE);
    break;

  case POWERSTATE_QUIT:
    CApplicationMessenger::GetInstance().PostMsg(TMSG_QUIT);
    break;

  case POWERSTATE_MINIMIZE:
    CApplicationMessenger::GetInstance().PostMsg(TMSG_MINIMIZE);
    break;

  default:
    CLog::Log(LOGERROR, "%s: No valid shutdownstate matched", __FUNCTION__);
    break;
  }
}

void CApplication::LockFrameMoveGuard()
{
  ++m_WaitingExternalCalls;
  m_frameMoveGuard.lock();
  ++m_ProcessedExternalCalls;
  g_graphicsContext.lock();
};

void CApplication::UnlockFrameMoveGuard()
{
  --m_WaitingExternalCalls;
  g_graphicsContext.unlock();
  m_frameMoveGuard.unlock();
};

void CApplication::FrameMove(bool processEvents, bool processGUI)
{
  if (processEvents)
  {
    // currently we calculate the repeat time (ie time from last similar keypress) just global as fps
    float frameTime = m_frameTime.GetElapsedSeconds();
    m_frameTime.StartZero();
    // never set a frametime less than 2 fps to avoid problems when debugging and on breaks
    if( frameTime > 0.5 )
      frameTime = 0.5;

    if (processGUI && m_renderGUI)
    {
      CSingleLock lock(g_graphicsContext);
      // check if there are notifications to display
      CGUIDialogKaiToast *toast = g_windowManager.GetWindow<CGUIDialogKaiToast>(WINDOW_DIALOG_KAI_TOAST);
      if (toast && toast->DoWork())
      {
        if (!toast->IsDialogRunning())
        {
          toast->Open();
        }
      }
    }

    HandleWinEvents();
    CServiceBroker::GetInputManager().Process(g_windowManager.GetActiveWindowID(), frameTime);

    if (processGUI && m_renderGUI)
    {
      m_pInertialScrollingHandler->ProcessInertialScroll(frameTime);
      m_appPlayer.GetSeekHandler().FrameMove();
    }

    // Open the door for external calls e.g python exactly here.
    // Window size can be between 2 and 10ms and depends on number of continuous requests
    if (m_WaitingExternalCalls)
    {
      CSingleExit ex(g_graphicsContext);
      m_frameMoveGuard.unlock();
      // Calculate a window size between 2 and 10ms, 4 continuous requests let the window grow by 1ms
      // WHen not playing video we allow it to increase to 80ms
      unsigned int max_sleep = m_appPlayer.IsPlayingVideo() && !m_appPlayer.IsPausedPlayback() ? 10 : 80;
      unsigned int sleepTime = std::max(static_cast<unsigned int>(2), std::min(m_ProcessedExternalCalls >> 2, max_sleep));
      Sleep(sleepTime);
      m_frameMoveGuard.lock();
      m_ProcessedExternalDecay = 5;
    }
    if (m_ProcessedExternalDecay && --m_ProcessedExternalDecay == 0)
      m_ProcessedExternalCalls = 0;
  }

  if (processGUI && m_renderGUI)
  {
    m_skipGuiRender = false;
#if defined(TARGET_RASPBERRY_PI)
    int fps = 0;

    // This code reduces rendering fps of the GUI layer when playing videos in fullscreen mode
    // it makes only sense on architectures with multiple layers
    if (g_graphicsContext.IsFullScreenVideo() && !m_appPlayer.IsPausedPlayback() && m_appPlayer.IsRenderingVideoLayer())
      fps = m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_VIDEOPLAYER_LIMITGUIUPDATE);

    unsigned int now = XbmcThreads::SystemClockMillis();
    unsigned int frameTime = now - m_lastRenderTime;
    if (fps > 0 && frameTime * fps < 1000)
      m_skipGuiRender = true;
#endif

    if (!m_bStop)
    {
      if (!m_skipGuiRender)
        g_windowManager.Process(CTimeUtils::GetFrameTime());
    }
    g_windowManager.FrameMove();
  }

  m_appPlayer.FrameMove();

  // this will go away when render systems gets its own thread
  CServiceBroker::GetWinSystem().DriveRenderLoop();
}



bool CApplication::Cleanup()
{
  try
  {
    StopPlaying();

    if (m_ServiceManager)
      m_ServiceManager->DeinitStageThree();

    CLog::Log(LOGNOTICE, "unload skin");
    UnloadSkin();

    // stop all remaining scripts; must be done after skin has been unloaded,
    // not before some windows still need it when deinitializing during skin
    // unloading
    CScriptInvocationManager::GetInstance().Uninitialize();

    m_globalScreensaverInhibitor.Release();
    m_screensaverInhibitor.Release();

    CServiceBroker::GetRenderSystem().DestroyRenderSystem();
    CServiceBroker::GetWinSystem().DestroyWindow();
    CServiceBroker::GetWinSystem().DestroyWindowSystem();
    g_windowManager.DestroyWindows();

    CLog::Log(LOGNOTICE, "unload sections");

    //  Shutdown as much as possible of the
    //  application, to reduce the leaks dumped
    //  to the vc output window before calling
    //  _CrtDumpMemoryLeaks(). Most of the leaks
    //  shown are no real leaks, as parts of the app
    //  are still allocated.

    g_localizeStrings.Clear();
    g_LangCodeExpander.Clear();
    g_charsetConverter.clear();
    g_directoryCache.Clear();
    //CServiceBroker::GetInputManager().ClearKeymaps(); //! @todo
    CEventServer::RemoveInstance();
    DllLoaderContainer::Clear();
    CServiceBroker::GetPlaylistPlayer().Clear();

    if (m_ServiceManager)
      m_ServiceManager->DeinitStageTwo();

    m_ServiceManager->GetSettings().Uninitialize();
    g_advancedSettings.Clear();

#ifdef TARGET_POSIX
    CXHandle::DumpObjectTracker();

#ifdef HAS_DVD_DRIVE
    CLibcdio::ReleaseInstance();
#endif
#endif
#ifdef _CRTDBG_MAP_ALLOC
    _CrtDumpMemoryLeaks();
    while(1); // execution ends
#endif

    // Cleanup was called more than once on exit during my tests
    if (m_ServiceManager)
    {
      m_ServiceManager->DeinitStageOne();
      m_ServiceManager.reset();
    }

    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "Exception in CApplication::Cleanup()");
    return false;
  }
}

void CApplication::Stop(int exitCode)
{
  try
  {
    m_frameMoveGuard.unlock();

    CVariant vExitCode(CVariant::VariantTypeObject);
    vExitCode["exitcode"] = exitCode;
    CAnnouncementManager::GetInstance().Announce(System, "xbmc", "OnQuit", vExitCode);

    // Abort any active screensaver
    WakeUpScreenSaverAndDPMS();

    g_alarmClock.StopThread();

    CLog::Log(LOGNOTICE, "Storing total System Uptime");
    g_sysinfo.SetTotalUptime(g_sysinfo.GetTotalUptime() + (int)(CTimeUtils::GetFrameTime() / 60000));

    // Update the settings information (volume, uptime etc. need saving)
    if (CFile::Exists(CProfilesManager::GetInstance().GetSettingsFile()))
    {
      CLog::Log(LOGNOTICE, "Saving settings");
      m_ServiceManager->GetSettings().Save();
    }
    else
      CLog::Log(LOGNOTICE, "Not saving settings (settings.xml is not present)");

    // kodi may crash or deadlock during exit (shutdown / reboot) due to
    // either a bug in core or misbehaving addons. so try saving
    // skin settings early
    CLog::Log(LOGNOTICE, "Saving skin settings");
    if (g_SkinInfo != nullptr)
      g_SkinInfo->SaveSettings();

    m_bStop = true;
    // Add this here to keep the same ordering behaviour for now
    // Needs cleaning up
    CApplicationMessenger::GetInstance().Stop();
    m_AppFocused = false;
    m_ExitCode = exitCode;
    CLog::Log(LOGNOTICE, "stop all");

    // cancel any jobs from the jobmanager
    CJobManager::GetInstance().CancelJobs();

    // stop scanning before we kill the network and so on
    if (CMusicLibraryQueue::GetInstance().IsRunning())
      CMusicLibraryQueue::GetInstance().CancelAllJobs();

    if (CVideoLibraryQueue::GetInstance().IsRunning())
      CVideoLibraryQueue::GetInstance().CancelAllJobs();

    CApplicationMessenger::GetInstance().Cleanup();

    CLog::Log(LOGNOTICE, "stop player");
    m_appPlayer.ClosePlayer();

    StopServices();

#ifdef HAS_ZEROCONF
    if(CZeroconfBrowser::IsInstantiated())
    {
      CLog::Log(LOGNOTICE, "stop zeroconf browser");
      CZeroconfBrowser::GetInstance()->Stop();
      CZeroconfBrowser::ReleaseInstance();
    }
#endif

#ifdef HAS_FILESYSTEM_SFTP
    CSFTPSessionManager::DisconnectAllSessions();
#endif

    for (const auto& vfsAddon : CServiceBroker::GetVFSAddonCache().GetAddonInstances())
      vfsAddon->DisconnectAll();

#if defined(TARGET_POSIX) && defined(HAS_FILESYSTEM_SMB)
    smb.Deinit();
#endif

#if defined(TARGET_DARWIN_OSX)
    if (XBMCHelper::GetInstance().IsAlwaysOn() == false)
      XBMCHelper::GetInstance().Stop();
#endif

    g_mediaManager.Stop();

    // Stop services before unloading Python
    CServiceBroker::GetServiceAddons().Stop();

    // unregister action listeners
    UnregisterActionListener(&m_appPlayer.GetSeekHandler());
    UnregisterActionListener(&CPlayerController::GetInstance());

    g_audioManager.DeInitialize();
    // shutdown the AudioEngine
    m_ServiceManager->DestroyAudioEngine();

    CLog::Log(LOGNOTICE, "closing down remote control service");
    CServiceBroker::GetInputManager().DisableRemoteControl();

    // unregister ffmpeg lock manager call back
    av_lockmgr_register(NULL);

    CLog::Log(LOGNOTICE, "stopped");
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "Exception in CApplication::Stop()");
  }

  cleanup_emu_environ();

  Sleep(200);
}

bool CApplication::PlayMedia(const CFileItem& item, const std::string &player, int iPlaylist)
{
  //If item is a plugin, expand out now and run ourselves again
  if (item.IsPlugin())
  {
    bool resume = item.m_lStartOffset == STARTOFFSET_RESUME;
    CFileItem item_new(item);
    if (XFILE::CPluginDirectory::GetPluginResult(item.GetPath(), item_new, resume))
      return PlayMedia(item_new, player, iPlaylist);
    return false;
  }
  if (item.IsSmartPlayList())
  {
    CFileItemList items;
    CUtil::GetRecursiveListing(item.GetPath(), items, "", DIR_FLAG_NO_FILE_DIRS);
    if (items.Size())
    {
      CSmartPlaylist smartpl;
      //get name and type of smartplaylist, this will always succeed as GetDirectory also did this.
      smartpl.OpenAndReadName(item.GetURL());
      CPlayList playlist;
      playlist.Add(items);
      return ProcessAndStartPlaylist(smartpl.GetName(), playlist, (smartpl.GetType() == "songs" || smartpl.GetType() == "albums") ? PLAYLIST_MUSIC:PLAYLIST_VIDEO);
    }
  }
  else if (item.IsPlayList() || item.IsInternetStream())
  {
    CGUIDialogCache* dlgCache = new CGUIDialogCache(5000, g_localizeStrings.Get(10214), item.GetLabel());

    //is or could be a playlist
    std::unique_ptr<CPlayList> pPlayList (CPlayListFactory::Create(item));
    bool gotPlayList = (pPlayList.get() && pPlayList->Load(item.GetPath()));

    if (dlgCache)
    {
       dlgCache->Close();
       if (dlgCache->IsCanceled())
          return true;
    }

    if (gotPlayList)
    {

      if (iPlaylist != PLAYLIST_NONE)
      {
        int track=0;
        if (item.HasProperty("playlist_starting_track"))
          track = (int)item.GetProperty("playlist_starting_track").asInteger();
        return ProcessAndStartPlaylist(item.GetPath(), *pPlayList, iPlaylist, track);
      }
      else
      {
        CLog::Log(LOGWARNING, "CApplication::PlayMedia called to play a playlist %s but no idea which playlist to use, playing first item", item.GetPath().c_str());
        if(pPlayList->size())
          return PlayFile(*(*pPlayList)[0], "", false) == PLAYBACK_OK;
      }
    }
  }
  else if (item.IsPVR())
  {
    return CServiceBroker::GetPVRManager().GUIActions()->PlayMedia(CFileItemPtr(new CFileItem(item)));
  }

  CURL path(item.GetPath());
  if (path.GetProtocol() == "game")
  {
    AddonPtr addon;
    if (CServiceBroker::GetAddonMgr().GetAddon(path.GetHostName(), addon, ADDON_GAMEDLL))
    {
      CFileItem addonItem(addon);
      return PlayFile(addonItem, player, false) == PLAYBACK_OK;
    }
  }

  //nothing special just play
  return PlayFile(item, player, false) == PLAYBACK_OK;
}

// PlayStack()
// For playing a multi-file video.  Particularly inefficient
// on startup, as we are required to calculate the length
// of each video, so we open + close each one in turn.
// A faster calculation of video time would improve this
// substantially.
// return value: same with PlayFile()
PlayBackRet CApplication::PlayStack(const CFileItem& item, bool bRestart)
{
  if (!m_stackHelper.InitializeStack(item))
    return PLAYBACK_FAIL;

  int startoffset = m_stackHelper.InitializeStackStartPartAndOffset(item);

  m_itemCurrentFile.reset(new CFileItem(item));
  CFileItem selectedStackPart = m_stackHelper.GetCurrentStackPartFileItem();
  selectedStackPart.m_lStartOffset = startoffset;

  return PlayFile(selectedStackPart, "", true);
}

PlayBackRet CApplication::PlayFile(CFileItem item, const std::string& player, bool bRestart)
{
  // Ensure the MIME type has been retrieved for http:// and shout:// streams
  if (item.GetMimeType().empty())
    item.FillInMimeType();

  if (!bRestart)
  {
    // bRestart will be true when called from PlayStack(), skipping this block
    m_appPlayer.SetPlaySpeed(1);

    m_itemCurrentFile.reset(new CFileItem(item));

    m_nextPlaylistItem = -1;
    m_stackHelper.Clear();

    if (item.IsVideo())
      CUtil::ClearSubtitles();
  }

  if (item.IsDiscStub())
  {
#ifdef HAS_DVD_DRIVE
    // Display the Play Eject dialog if there is any optical disc drive
    if (g_mediaManager.HasOpticalDrive())
    {
      if (CGUIDialogPlayEject::ShowAndGetInput(item))
        // PlayDiscAskResume takes path to disc. No parameter means default DVD drive.
        // Can't do better as CGUIDialogPlayEject calls CMediaManager::IsDiscInDrive, which assumes default DVD drive anyway
        return MEDIA_DETECT::CAutorun::PlayDiscAskResume() ? PLAYBACK_OK : PLAYBACK_FAIL;
    }
    else
#endif
      HELPERS::ShowOKDialogText(CVariant{435}, CVariant{436});

    return PLAYBACK_OK;
  }

  if (item.IsPlayList())
    return PLAYBACK_FAIL;

  if (item.IsPlugin())
  { // we modify the item so that it becomes a real URL
    bool resume = item.m_lStartOffset == STARTOFFSET_RESUME;
    CFileItem item_new(item);
    if (XFILE::CPluginDirectory::GetPluginResult(item.GetPath(), item_new, resume))
      return PlayFile(std::move(item_new), player, false);
    return PLAYBACK_FAIL;
  }

#ifdef HAS_UPNP
  if (URIUtils::IsUPnP(item.GetPath()))
  {
    CFileItem item_new(item);
    if (XFILE::CUPnPDirectory::GetResource(item.GetURL(), item_new))
      return PlayFile(std::move(item_new), player, false);
    return PLAYBACK_FAIL;
  }
#endif

  // if we have a stacked set of files, we need to setup our stack routines for
  // "seamless" seeking and total time of the movie etc.
  // will recall with restart set to true
  if (item.IsStack())
    return PlayStack(item, bRestart);

  CPlayerOptions options;

  if( item.HasProperty("StartPercent") )
  {
    double fallback = 0.0f;
    if(item.GetProperty("StartPercent").isString())
      fallback = (double)atof(item.GetProperty("StartPercent").asString().c_str());
    options.startpercent = item.GetProperty("StartPercent").asDouble(fallback);
  }

  options.starttime = item.m_lStartOffset / 75.0;

  if (bRestart)
  {
    // have to be set here due to playstack using this for starting the file
    if (item.HasVideoInfoTag())
      options.state = item.GetVideoInfoTag()->GetResumePoint().playerState;
    if (m_stackHelper.IsPlayingRegularStack() && m_itemCurrentFile->m_lStartOffset != 0)
      m_itemCurrentFile->m_lStartOffset = STARTOFFSET_RESUME; // to force fullscreen switching
  }
  if (!bRestart || m_stackHelper.IsPlayingISOStack())
  {
    // the following code block is only applicable when bRestart is false OR to ISO stacks

    if (item.IsVideo())
    {
      // open the d/b and retrieve the bookmarks for the current movie
      CVideoDatabase dbs;
      dbs.Open();

      if( item.m_lStartOffset == STARTOFFSET_RESUME )
      {
        options.starttime = 0.0f;
        if (item.IsResumePointSet())
        {
          options.starttime = item.GetCurrentResumeTime();
          if (item.HasVideoInfoTag())
            options.state = item.GetVideoInfoTag()->GetResumePoint().playerState;
        }
        else
        {
          CBookmark bookmark;
          std::string path = item.GetPath();
          if (item.HasVideoInfoTag() && StringUtils::StartsWith(item.GetVideoInfoTag()->m_strFileNameAndPath, "removable://"))
            path = item.GetVideoInfoTag()->m_strFileNameAndPath;
          else if (item.HasProperty("original_listitem_url") && URIUtils::IsPlugin(item.GetProperty("original_listitem_url").asString()))
            path = item.GetProperty("original_listitem_url").asString();
          if (dbs.GetResumeBookMark(path, bookmark))
          {
            options.starttime = bookmark.timeInSeconds;
            options.state = bookmark.playerState;
          }
        }

        if (options.starttime == 0.0f && item.HasVideoInfoTag())
        {
          // No resume point is set, but check if this item is part of a multi-episode file
          const CVideoInfoTag *tag = item.GetVideoInfoTag();

          if (tag->m_iBookmarkId > 0)
          {
            CBookmark bookmark;
            dbs.GetBookMarkForEpisode(*tag, bookmark);
            options.starttime = bookmark.timeInSeconds;
            options.state = bookmark.playerState;
          }
        }
      }
      else if (item.HasVideoInfoTag())
      {
        const CVideoInfoTag *tag = item.GetVideoInfoTag();

        if (tag->m_iBookmarkId > 0)
        {
          CBookmark bookmark;
          dbs.GetBookMarkForEpisode(*tag, bookmark);
          options.starttime = bookmark.timeInSeconds;
          options.state = bookmark.playerState;
        }
      }

      dbs.Close();
    }
  }

  // a disc image might be Blu-Ray disc
  if (!(options.startpercent > 0.0f || options.starttime > 0.0f) && (item.IsBDFile() || item.IsDiscImage()))
  {
    //check if we must show the simplified bd menu
    if (!CGUIDialogSimpleMenu::ShowPlaySelection(const_cast<CFileItem&>(item)))
      return PLAYBACK_CANCELED;
  }

  // this really aught to be inside !bRestart, but since PlayStack
  // uses that to init playback, we have to keep it outside
  int playlist = CServiceBroker::GetPlaylistPlayer().GetCurrentPlaylist();
  if (item.IsVideo() && playlist == PLAYLIST_VIDEO && CServiceBroker::GetPlaylistPlayer().GetPlaylist(playlist).size() > 1)
  { // playing from a playlist by the looks
    // don't switch to fullscreen if we are not playing the first item...
    options.fullscreen = !CServiceBroker::GetPlaylistPlayer().HasPlayedFirstFile() && g_advancedSettings.m_fullScreenOnMovieStart && !CMediaSettings::GetInstance().DoesVideoStartWindowed();
  }
  else if(m_stackHelper.IsPlayingRegularStack())
  {
    //! @todo - this will fail if user seeks back to first file in stack
    if(m_stackHelper.GetCurrentPartNumber() == 0 || m_itemCurrentFile->m_lStartOffset == STARTOFFSET_RESUME)
      options.fullscreen = g_advancedSettings.m_fullScreenOnMovieStart && !CMediaSettings::GetInstance().DoesVideoStartWindowed();
    else
      options.fullscreen = false;
    // reset this so we don't think we are resuming on seek
    m_itemCurrentFile->m_lStartOffset = 0;
  }
  else
    options.fullscreen = g_advancedSettings.m_fullScreenOnMovieStart && !CMediaSettings::GetInstance().DoesVideoStartWindowed();

  // reset VideoStartWindowed as it's a temp setting
  CMediaSettings::GetInstance().SetVideoStartWindowed(false);

  {
    // for playing a new item, previous playing item's callback may already
    // pushed some delay message into the threadmessage list, they are not
    // expected be processed after or during the new item playback starting.
    // so we clean up previous playing item's playback callback delay messages here.
    int previousMsgsIgnoredByNewPlaying[] = {
      GUI_MSG_PLAYBACK_STARTED,
      GUI_MSG_PLAYBACK_ENDED,
      GUI_MSG_PLAYBACK_STOPPED,
      GUI_MSG_PLAYLIST_CHANGED,
      GUI_MSG_PLAYLISTPLAYER_STOPPED,
      GUI_MSG_PLAYLISTPLAYER_STARTED,
      GUI_MSG_PLAYLISTPLAYER_CHANGED,
      GUI_MSG_QUEUE_NEXT_ITEM,
      0
    };
    int dMsgCount = g_windowManager.RemoveThreadMessageByMessageIds(&previousMsgsIgnoredByNewPlaying[0]);
    if (dMsgCount > 0)
      CLog::LogF(LOGDEBUG,"Ignored %d playback thread messages", dMsgCount);
  }

  std::string newPlayer;
  if (!player.empty())
    newPlayer = player;
  else if (bRestart && !m_appPlayer.GetCurrentPlayer().empty())
    newPlayer = m_appPlayer.GetCurrentPlayer();
  else
    newPlayer = CPlayerCoreFactory::GetInstance().GetDefaultPlayer(item);

  // We should restart the player, unless the previous and next tracks are using
  // one of the players that allows gapless playback (paplayer, VideoPlayer)
  // DVD playback does not support gapless
  if (item.IsDiscImage() || item.IsDVDFile())
    m_appPlayer.ClosePlayer();
  else
    m_appPlayer.ClosePlayerGapless(newPlayer);

  m_appPlayer.CreatePlayer(newPlayer, *this);

  PlayBackRet iResult;
  if (m_appPlayer.HasPlayer())
  {
    /* When playing video pause any low priority jobs, they will be unpaused  when playback stops.
     * This should speed up player startup for files on internet filesystems (eg. webdav) and
     * increase performance on low powered systems (Atom/ARM).
     */
    if (item.IsVideo() || item.IsGame())
    {
      CJobManager::GetInstance().PauseJobs();
    }

    // don't hold graphicscontext here since player
    // may wait on another thread, that requires gfx
    CSingleExit ex(g_graphicsContext);

    iResult = m_appPlayer.OpenFile(item, options);
  }
  else
  {
    CLog::Log(LOGERROR, "Error creating player for item %s (File doesn't exist?)", item.GetPath().c_str());
    iResult = PLAYBACK_FAIL;
  }

  if (iResult == PLAYBACK_OK)
  {
    m_appPlayer.SetVolume(m_volumeLevel);
    m_appPlayer.SetMute(m_muted);

    if(m_appPlayer.IsPlayingAudio())
    {
      if (g_windowManager.GetActiveWindow() == WINDOW_FULLSCREEN_VIDEO)
        g_windowManager.ActivateWindow(WINDOW_VISUALISATION);
    }
    else if(m_appPlayer.IsPlayingVideo())
    {
      // if player didn't manage to switch to fullscreen by itself do it here
      if (options.fullscreen && m_appPlayer.IsRenderingVideo() &&
          g_windowManager.GetActiveWindow() != WINDOW_FULLSCREEN_VIDEO &&
          g_windowManager.GetActiveWindow() != WINDOW_FULLSCREEN_GAME)
       SwitchToFullScreen(true);
    }
    else
    {
      if (g_windowManager.GetActiveWindow() == WINDOW_VISUALISATION ||
          g_windowManager.GetActiveWindow() == WINDOW_FULLSCREEN_VIDEO ||
          g_windowManager.GetActiveWindow() == WINDOW_FULLSCREEN_GAME)
        g_windowManager.PreviousWindow();
    }

#if !defined(TARGET_POSIX)
    g_audioManager.Enable(false);
#endif

    if (item.HasPVRChannelInfoTag())
      CServiceBroker::GetPlaylistPlayer().SetCurrentPlaylist(PLAYLIST_NONE);
  }

  return iResult;
}

void CApplication::OnPlayBackEnded()
{
  CLog::LogF(LOGDEBUG ,"CApplication::OnPlayBackEnded");

  // informs python script currently running playback has ended
  // (does nothing if python is not loaded)
#ifdef HAS_PYTHON
  g_pythonParser.OnPlayBackEnded();
#endif

  CServiceBroker::GetPVRManager().OnPlaybackEnded(m_itemCurrentFile);

  CVariant data(CVariant::VariantTypeObject);
  data["end"] = true;
  CAnnouncementManager::GetInstance().Announce(Player, "xbmc", "OnStop", m_itemCurrentFile, data);

  CGUIMessage msg(GUI_MSG_PLAYBACK_ENDED, 0, 0);
  g_windowManager.SendThreadMessage(msg);
}

void CApplication::OnPlayBackStarted(const CFileItem &file)
{
  CLog::LogF(LOGDEBUG,"CApplication::OnPlayBackStarted");

#ifdef HAS_PYTHON
  // informs python script currently running playback has started
  // (does nothing if python is not loaded)
  g_pythonParser.OnPlayBackStarted(file);
#endif

  CServiceBroker::GetPVRManager().OnPlaybackStarted(m_itemCurrentFile);
  m_stackHelper.OnPlayBackStarted(file);

  CGUIMessage msg(GUI_MSG_PLAYBACK_STARTED, 0, 0);
  g_windowManager.SendThreadMessage(msg);
}

void CApplication::OnPlayerCloseFile(const CFileItem &file, const CBookmark &bookmarkParam)
{
  CSingleLock lock(m_stackHelper.m_critSection);

  CFileItem fileItem(file);
  CBookmark bookmark = bookmarkParam;
  CBookmark resumeBookmark;
  bool playCountUpdate = false;
  float percent = 0.0f;

  if (m_stackHelper.GetRegisteredStack(fileItem) != nullptr && m_stackHelper.GetRegisteredStackTotalTimeMs(fileItem) > 0)
  {
    // regular stack case: we have to save the bookmark on the stack
    fileItem = *m_stackHelper.GetRegisteredStack(file);
    // the bookmark coming from the player is only relative to the current part, thus needs to be corrected with these attributes (start time will be 0 for non-stackparts)
    bookmark.timeInSeconds += m_stackHelper.GetRegisteredStackPartStartTimeMs(file) / 1000.0;
    if (m_stackHelper.GetRegisteredStackTotalTimeMs(file) > 0)
      bookmark.totalTimeInSeconds = m_stackHelper.GetRegisteredStackTotalTimeMs(file) / 1000.0;
    bookmark.partNumber = m_stackHelper.GetRegisteredStackPartNumber(file);
  }

  percent = bookmark.timeInSeconds / bookmark.totalTimeInSeconds * 100;

  if ((fileItem.IsAudio() && g_advancedSettings.m_audioPlayCountMinimumPercent > 0 &&
       percent >= g_advancedSettings.m_audioPlayCountMinimumPercent) ||
      (fileItem.IsVideo() && g_advancedSettings.m_videoPlayCountMinimumPercent > 0 &&
       percent >= g_advancedSettings.m_videoPlayCountMinimumPercent))
  {
    playCountUpdate = true;
  }

  if (g_advancedSettings.m_videoIgnorePercentAtEnd > 0 &&
      bookmark.totalTimeInSeconds - bookmark.timeInSeconds <
        0.01f * g_advancedSettings.m_videoIgnorePercentAtEnd * bookmark.totalTimeInSeconds)
  {
    resumeBookmark.timeInSeconds = -1.0f;
  }
  else if (bookmark.timeInSeconds > g_advancedSettings.m_videoIgnoreSecondsAtStart)
  {
    resumeBookmark = bookmark;
    if (m_stackHelper.GetRegisteredStack(file) != nullptr)
    {
      // also update video info tag with total time
      fileItem.GetVideoInfoTag()->m_streamDetails.SetVideoDuration(0, resumeBookmark.totalTimeInSeconds);
    }
  }
  else
  {
    resumeBookmark.timeInSeconds = 0.0f;
  }

  if (CProfilesManager::GetInstance().GetCurrentProfile().canWriteDatabases())
  {
    CSaveFileState::DoWork(fileItem, resumeBookmark, playCountUpdate);
  }
}

void CApplication::OnQueueNextItem()
{
  CLog::LogF(LOGDEBUG,"CApplication::OnQueueNextItem");

  // informs python script currently running that we are requesting the next track
  // (does nothing if python is not loaded)
#ifdef HAS_PYTHON
  g_pythonParser.OnQueueNextItem(); // currently unimplemented
#endif

  CGUIMessage msg(GUI_MSG_QUEUE_NEXT_ITEM, 0, 0);
  g_windowManager.SendThreadMessage(msg);
}

void CApplication::OnPlayBackStopped()
{
  CLog::LogF(LOGDEBUG, "CApplication::OnPlayBackStopped");

  // informs python script currently running playback has ended
  // (does nothing if python is not loaded)
#ifdef HAS_PYTHON
  g_pythonParser.OnPlayBackStopped();
#endif
#if defined(TARGET_DARWIN_IOS)
  CDarwinUtils::EnableOSScreenSaver(true);
#endif

  CServiceBroker::GetPVRManager().OnPlaybackStopped(m_itemCurrentFile);

  CVariant data(CVariant::VariantTypeObject);
  data["end"] = false;
  CAnnouncementManager::GetInstance().Announce(Player, "xbmc", "OnStop", m_itemCurrentFile, data);

  CGUIMessage msg( GUI_MSG_PLAYBACK_STOPPED, 0, 0 );
  g_windowManager.SendThreadMessage(msg);
}

void CApplication::OnPlayBackError()
{
  //@todo Playlists can be continued by calling OnPlaybackEnded instead
  // open error dialog
  HELPERS::ShowOKDialogText(CVariant{16026}, CVariant{16027});
  OnPlayBackStopped();
}

void CApplication::OnPlayBackPaused()
{
#ifdef HAS_PYTHON
  g_pythonParser.OnPlayBackPaused();
#endif
#if defined(TARGET_DARWIN_IOS)
  CDarwinUtils::EnableOSScreenSaver(true);
#endif

  CVariant param;
  param["player"]["speed"] = 0;
  param["player"]["playerid"] = CServiceBroker::GetPlaylistPlayer().GetCurrentPlaylist();
  CAnnouncementManager::GetInstance().Announce(Player, "xbmc", "OnPause", m_itemCurrentFile, param);
}

void CApplication::OnPlayBackResumed()
{
#ifdef HAS_PYTHON
  g_pythonParser.OnPlayBackResumed();
#endif
#if defined(TARGET_DARWIN_IOS)
  if (m_appPlayer.IsPlayingVideo())
    CDarwinUtils::EnableOSScreenSaver(false);
#endif

  CVariant param;
  param["player"]["speed"] = 1;
  param["player"]["playerid"] = CServiceBroker::GetPlaylistPlayer().GetCurrentPlaylist();
  CAnnouncementManager::GetInstance().Announce(Player, "xbmc", "OnPlay", m_itemCurrentFile, param);
}

void CApplication::OnPlayBackSpeedChanged(int iSpeed)
{
#ifdef HAS_PYTHON
  g_pythonParser.OnPlayBackSpeedChanged(iSpeed);
#endif

  CVariant param;
  param["player"]["speed"] = iSpeed;
  param["player"]["playerid"] = CServiceBroker::GetPlaylistPlayer().GetCurrentPlaylist();
  CAnnouncementManager::GetInstance().Announce(Player, "xbmc", "OnSpeedChanged", m_itemCurrentFile, param);
}

void CApplication::OnPlayBackSeek(int64_t iTime, int64_t seekOffset)
{
#ifdef HAS_PYTHON
  g_pythonParser.OnPlayBackSeek(static_cast<int>(iTime), static_cast<int>(seekOffset));
#endif

  CVariant param;
  CJSONUtils::MillisecondsToTimeObject(iTime, param["player"]["time"]);
  CJSONUtils::MillisecondsToTimeObject(seekOffset, param["player"]["seekoffset"]);
  param["player"]["playerid"] = CServiceBroker::GetPlaylistPlayer().GetCurrentPlaylist();
  param["player"]["speed"] = (int)m_appPlayer.GetPlaySpeed();
  CAnnouncementManager::GetInstance().Announce(Player, "xbmc", "OnSeek", m_itemCurrentFile, param);
  g_infoManager.SetDisplayAfterSeek(2500, static_cast<int>(seekOffset));
}

void CApplication::OnPlayBackSeekChapter(int iChapter)
{
#ifdef HAS_PYTHON
  g_pythonParser.OnPlayBackSeekChapter(iChapter);
#endif
}

void CApplication::OnAVChange()
{
}

void CApplication::RequestVideoSettings(const CFileItem &fileItem)
{
  CVideoDatabase dbs;
  if (dbs.Open())
  {
    CLog::Log(LOGDEBUG, "Loading settings for %s", CURL::GetRedacted(fileItem.GetPath()).c_str());

    // Load stored settings if they exist, otherwise use default
    CVideoSettings vs;
    if (!dbs.GetVideoSettings(fileItem, vs))
      vs = CMediaSettings::GetInstance().GetDefaultVideoSettings();

    m_appPlayer.SetVideoSettings(vs);

    dbs.Close();
  }
}

void CApplication::StoreVideoSettings(const CFileItem &fileItem, CVideoSettings vs)
{
  if (vs != CMediaSettings::GetInstance().GetDefaultVideoSettings())
  {
    CVideoDatabase dbs;
    if (dbs.Open())
    {
      dbs.SetVideoSettings(fileItem, vs);
      dbs.Close();
    }
  }
}

bool CApplication::IsPlayingFullScreenVideo() const
{
  return m_appPlayer.IsPlayingVideo() && g_graphicsContext.IsFullScreenVideo();
}

bool CApplication::IsFullScreen()
{
  return IsPlayingFullScreenVideo() ||
        (g_windowManager.GetActiveWindow() == WINDOW_VISUALISATION) ||
         g_windowManager.GetActiveWindow() == WINDOW_SLIDESHOW;
}

void CApplication::StopPlaying()
{
  int iWin = g_windowManager.GetActiveWindow();
  if (m_appPlayer.IsPlaying())
  {
    m_appPlayer.ClosePlayer();

    // turn off visualisation window when stopping
    if ((iWin == WINDOW_VISUALISATION ||
         iWin == WINDOW_FULLSCREEN_VIDEO ||
         iWin == WINDOW_FULLSCREEN_GAME) &&
         !m_bStop)
      g_windowManager.PreviousWindow();

    g_partyModeManager.Disable();
  }
}

void CApplication::ResetSystemIdleTimer()
{
  // reset system idle timer
  m_idleTimer.StartZero();
#if defined(TARGET_DARWIN_IOS)
  CDarwinUtils::ResetSystemIdleTimer();
#endif
}

void CApplication::ResetScreenSaver()
{
  // reset our timers
  m_shutdownTimer.StartZero();

  // screen saver timer is reset only if we're not already in screensaver or
  // DPMS mode
  if ((!m_screensaverActive && m_iScreenSaveLock == 0) && !m_dpmsIsActive)
    ResetScreenSaverTimer();
}

void CApplication::ResetScreenSaverTimer()
{
  m_screenSaverTimer.StartZero();
}

void CApplication::StopScreenSaverTimer()
{
  m_screenSaverTimer.Stop();
}

bool CApplication::ToggleDPMS(bool manual)
{
  if (manual || (m_dpmsIsManual == manual))
  {
    if (m_dpmsIsActive)
    {
      m_dpmsIsActive = false;
      m_dpmsIsManual = false;
      SetRenderGUI(true);
      CAnnouncementManager::GetInstance().Announce(GUI, "xbmc", "OnDPMSDeactivated");
      return m_dpms->DisablePowerSaving();
    }
    else
    {
      if (m_dpms->EnablePowerSaving(m_dpms->GetSupportedModes()[0]))
      {
        m_dpmsIsActive = true;
        m_dpmsIsManual = manual;
        SetRenderGUI(false);
        CAnnouncementManager::GetInstance().Announce(GUI, "xbmc", "OnDPMSActivated");
        return true;
      }
    }
  }
  return false;
}

bool CApplication::WakeUpScreenSaverAndDPMS(bool bPowerOffKeyPressed /* = false */)
{
  bool result = false;

  // First reset DPMS, if active
  if (m_dpmsIsActive)
  {
    if (m_dpmsIsManual)
      return false;
    //! @todo if screensaver lock is specified but screensaver is not active
    //! (DPMS came first), activate screensaver now.
    ToggleDPMS(false);
    ResetScreenSaverTimer();
    result = !m_screensaverActive || WakeUpScreenSaver(bPowerOffKeyPressed);
  }
  else if (m_screensaverActive)
    result = WakeUpScreenSaver(bPowerOffKeyPressed);

  if(result)
  {
    // allow listeners to ignore the deactivation if it precedes a powerdown/suspend etc
    CVariant data(CVariant::VariantTypeObject);
    data["shuttingdown"] = bPowerOffKeyPressed;
    CAnnouncementManager::GetInstance().Announce(GUI, "xbmc", "OnScreensaverDeactivated", data);
#ifdef TARGET_ANDROID
    // Screensaver deactivated -> acquire wake lock
    CXBMCApp::EnableWakeLock(true);
#endif
  }

  return result;
}

bool CApplication::WakeUpScreenSaver(bool bPowerOffKeyPressed /* = false */)
{
  if (m_iScreenSaveLock == 2)
    return false;

  // if Screen saver is active
  if (m_screensaverActive && !m_screensaverIdInUse.empty())
  {
    if (m_iScreenSaveLock == 0)
      if (CProfilesManager::GetInstance().GetMasterProfile().getLockMode() != LOCK_MODE_EVERYONE &&
          (CProfilesManager::GetInstance().UsingLoginScreen() || m_ServiceManager->GetSettings().GetBool(CSettings::SETTING_MASTERLOCK_STARTUPLOCK)) &&
          CProfilesManager::GetInstance().GetCurrentProfile().getLockMode() != LOCK_MODE_EVERYONE &&
          m_screensaverIdInUse != "screensaver.xbmc.builtin.dim" && m_screensaverIdInUse != "screensaver.xbmc.builtin.black" && m_screensaverIdInUse != "visualization")
      {
        m_iScreenSaveLock = 2;
        CGUIMessage msg(GUI_MSG_CHECK_LOCK,0,0);

        CGUIWindow* pWindow = g_windowManager.GetWindow(WINDOW_SCREENSAVER);
        if (pWindow)
          pWindow->OnMessage(msg);
      }
    if (m_iScreenSaveLock == -1)
    {
      m_iScreenSaveLock = 0;
      return true;
    }

    // disable screensaver
    m_screensaverActive = false;
    m_iScreenSaveLock = 0;
    ResetScreenSaverTimer();

    if (m_screensaverIdInUse == "visualization")
    {
      // we can just continue as usual from vis mode
      return false;
    }
    else if (m_screensaverIdInUse == "screensaver.xbmc.builtin.dim" ||
             m_screensaverIdInUse == "screensaver.xbmc.builtin.black" ||
             m_screensaverIdInUse.empty())
    {
      return true;
    }
    else if (!m_screensaverIdInUse.empty())
    { // we're in screensaver window
      if (m_pythonScreenSaver)
      {
        // What sound does a python screensaver make?
        #define SCRIPT_ALARM "sssssscreensaver"
        #define SCRIPT_TIMEOUT 15 // seconds

        /* FIXME: This is a hack but a proper fix is non-trivial. Basically this code
        * makes sure the addon gets terminated after we've moved out of the screensaver window.
        * If we don't do this, we may simply lockup.
        */
        g_alarmClock.Start(SCRIPT_ALARM, SCRIPT_TIMEOUT, "StopScript(" + m_pythonScreenSaver->LibPath() + ")", true, false);
        m_pythonScreenSaver.reset();
      }
      if (g_windowManager.GetActiveWindow() == WINDOW_SCREENSAVER)
        g_windowManager.PreviousWindow();  // show the previous window
      else if (g_windowManager.GetActiveWindow() == WINDOW_SLIDESHOW)
        CApplicationMessenger::GetInstance().SendMsg(TMSG_GUI_ACTION, WINDOW_SLIDESHOW, -1, static_cast<void*>(new CAction(ACTION_STOP)));
    }
    return true;
  }
  else
    return false;
}

void CApplication::CheckOSScreenSaverInhibitionSetting()
{
  // Kodi screen saver overrides OS one: always inhibit OS screen saver then
  if (!m_ServiceManager->GetSettings().GetString(CSettings::SETTING_SCREENSAVER_MODE).empty() &&
      CServiceBroker::GetWinSystem().GetOSScreenSaver())
  {
    if (!m_globalScreensaverInhibitor)
    {
      m_globalScreensaverInhibitor = CServiceBroker::GetWinSystem().GetOSScreenSaver()->CreateInhibitor();
    }
  }
  else if (m_globalScreensaverInhibitor)
  {
    m_globalScreensaverInhibitor.Release();
  }
}

void CApplication::CheckScreenSaverAndDPMS()
{
  bool maybeScreensaver =
      !m_dpmsIsActive && !m_screensaverActive
      && !m_ServiceManager->GetSettings().GetString(CSettings::SETTING_SCREENSAVER_MODE).empty();
  bool maybeDPMS =
      !m_dpmsIsActive && m_dpms->IsSupported()
      && m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_POWERMANAGEMENT_DISPLAYSOFF) > 0;
  // whether the current state of the application should be regarded as active even when there is no
  // explicit user activity such as input
  bool haveIdleActivity =
    // * Are we playing a video and it is not paused?
    (m_appPlayer.IsPlayingVideo() && !m_appPlayer.IsPaused())
    // * Are we playing some music in fullscreen vis?
    || (m_appPlayer.IsPlayingAudio() && g_windowManager.GetActiveWindow() == WINDOW_VISUALISATION
        && !m_ServiceManager->GetSettings().GetString(CSettings::SETTING_MUSICPLAYER_VISUALISATION).empty());

  // Handle OS screen saver state
  if (haveIdleActivity && CServiceBroker::GetWinSystem().GetOSScreenSaver())
  {
    // Always inhibit OS screen saver during these kinds of activities
    m_screensaverInhibitor = CServiceBroker::GetWinSystem().GetOSScreenSaver()->CreateInhibitor();
  }
  else if (m_screensaverInhibitor)
  {
    m_screensaverInhibitor.Release();
  }

  // Has the screen saver window become active?
  if (maybeScreensaver && g_windowManager.IsWindowActive(WINDOW_SCREENSAVER))
  {
    m_screensaverActive = true;
    maybeScreensaver = false;
  }

  if (m_screensaverActive && m_appPlayer.IsPlayingVideo() && !m_appPlayer.IsPaused())
  {
    WakeUpScreenSaverAndDPMS();
    return;
  }

  if (!maybeScreensaver && !maybeDPMS) return;  // Nothing to do.

  // See if we need to reset timer.
  if (haveIdleActivity)
  {
    ResetScreenSaverTimer();
    return;
  }

  float elapsed = m_screenSaverTimer.IsRunning() ? m_screenSaverTimer.GetElapsedSeconds() : 0.f;

  // DPMS has priority (it makes the screensaver not needed)
  if (maybeDPMS
      && elapsed > m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_POWERMANAGEMENT_DISPLAYSOFF) * 60)
  {
    ToggleDPMS(false);
    WakeUpScreenSaver();
  }
  else if (maybeScreensaver
           && elapsed > m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_SCREENSAVER_TIME) * 60)
  {
    ActivateScreenSaver();
  }
}

// activate the screensaver.
// if forceType is true, we ignore the various conditions that can alter
// the type of screensaver displayed
void CApplication::ActivateScreenSaver(bool forceType /*= false */)
{
  if (m_appPlayer.IsPlayingAudio() && m_ServiceManager->GetSettings().GetBool(CSettings::SETTING_SCREENSAVER_USEMUSICVISINSTEAD) &&
      !m_ServiceManager->GetSettings().GetString(CSettings::SETTING_MUSICPLAYER_VISUALISATION).empty())
  { // just activate the visualisation if user toggled the usemusicvisinstead option
    g_windowManager.ActivateWindow(WINDOW_VISUALISATION);
    return;
  }

  m_screensaverActive = true;
  CAnnouncementManager::GetInstance().Announce(GUI, "xbmc", "OnScreensaverActivated");

  // disable screensaver lock from the login screen
  m_iScreenSaveLock = g_windowManager.GetActiveWindow() == WINDOW_LOGIN_SCREEN ? 1 : 0;
  // set to Dim in the case of a dialog on screen or playing video
  if (!forceType && (g_windowManager.HasModalDialog() ||
                     (m_appPlayer.IsPlayingVideo() && m_ServiceManager->GetSettings().GetBool(CSettings::SETTING_SCREENSAVER_USEDIMONPAUSE)) ||
                     CServiceBroker::GetPVRManager().GUIActions()->IsRunningChannelScan()))
    m_screensaverIdInUse = "screensaver.xbmc.builtin.dim";
  else // Get Screensaver Mode
    m_screensaverIdInUse = m_ServiceManager->GetSettings().GetString(CSettings::SETTING_SCREENSAVER_MODE);

  if (m_screensaverIdInUse == "screensaver.xbmc.builtin.dim" ||
      m_screensaverIdInUse == "screensaver.xbmc.builtin.black")
  {
#ifdef TARGET_ANDROID
    // Default screensaver activated -> release wake lock
    CXBMCApp::EnableWakeLock(false);
#endif
    return;
  }
  else if (m_screensaverIdInUse.empty())
    return;
  else if (CServiceBroker::GetAddonMgr().GetAddon(m_screensaverIdInUse, m_pythonScreenSaver, ADDON_SCREENSAVER))
  {
    std::string libPath = m_pythonScreenSaver->LibPath();
    if (CScriptInvocationManager::GetInstance().HasLanguageInvoker(libPath))
    {
      CLog::Log(LOGDEBUG, "using python screensaver add-on %s", m_screensaverIdInUse.c_str());

      // Don't allow a previously-scheduled alarm to kill our new screensaver
      g_alarmClock.Stop(SCRIPT_ALARM, true);

      if (!CScriptInvocationManager::GetInstance().Stop(libPath))
        CScriptInvocationManager::GetInstance().ExecuteAsync(libPath, AddonPtr(new CAddon(dynamic_cast<ADDON::CAddon&>(*m_pythonScreenSaver))));
      return;
    }
    m_pythonScreenSaver.reset();
  }

  g_windowManager.ActivateWindow(WINDOW_SCREENSAVER);
}

void CApplication::CheckShutdown()
{
  // first check if we should reset the timer
  if (m_bInhibitIdleShutdown
      || m_appPlayer.IsPlaying() || m_appPlayer.IsPausedPlayback() // is something playing?
      || CMusicLibraryQueue::GetInstance().IsRunning()
      || CVideoLibraryQueue::GetInstance().IsRunning()
      || g_windowManager.IsWindowActive(WINDOW_DIALOG_PROGRESS) // progress dialog is onscreen
      || !CServiceBroker::GetPVRManager().GUIActions()->CanSystemPowerdown(false))
  {
    m_shutdownTimer.StartZero();
    return;
  }

  float elapsed = m_shutdownTimer.IsRunning() ? m_shutdownTimer.GetElapsedSeconds() : 0.f;
  if ( elapsed > m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_POWERMANAGEMENT_SHUTDOWNTIME) * 60 )
  {
    // Since it is a sleep instead of a shutdown, let's set everything to reset when we wake up.
    m_shutdownTimer.Stop();

    // Sleep the box
    CApplicationMessenger::GetInstance().PostMsg(TMSG_SHUTDOWN);
  }
}

void CApplication::InhibitIdleShutdown(bool inhibit)
{
  m_bInhibitIdleShutdown = inhibit;
}

bool CApplication::IsIdleShutdownInhibited() const
{
  return m_bInhibitIdleShutdown;
}

bool CApplication::OnMessage(CGUIMessage& message)
{
  switch ( message.GetMessage() )
  {
  case GUI_MSG_NOTIFY_ALL:
    {
      if (message.GetParam1()==GUI_MSG_REMOVED_MEDIA)
      {
        // Update general playlist: Remove DVD playlist items
        int nRemoved = CServiceBroker::GetPlaylistPlayer().RemoveDVDItems();
        if ( nRemoved > 0 )
        {
          CGUIMessage msg( GUI_MSG_PLAYLIST_CHANGED, 0, 0 );
          g_windowManager.SendMessage( msg );
        }
        // stop the file if it's on dvd (will set the resume point etc)
        if (m_itemCurrentFile->IsOnDVD())
          StopPlaying();
      }
      else if (message.GetParam1() == GUI_MSG_UI_READY)
      {
        // remove splash window
        g_windowManager.Delete(WINDOW_SPLASH);

        // show the volumebar if the volume is muted
        if (IsMuted() || GetVolume(false) <= VOLUME_MINIMUM)
          ShowVolumeBar();

        if (!m_incompatibleAddons.empty())
        {
          auto addonList = StringUtils::Join(m_incompatibleAddons, ", ");
          auto msg = StringUtils::Format(g_localizeStrings.Get(24149).c_str(), addonList.c_str());
          HELPERS::ShowOKDialogText(CVariant{24148}, CVariant{std::move(msg)});
          m_incompatibleAddons.clear();
        }

        // show info dialog about moved configuration files if needed
        ShowAppMigrationMessage();

        m_bInitializing = false;
      }
    }
    break;

  case GUI_MSG_PLAYBACK_STARTED:
    {
#ifdef TARGET_DARWIN_IOS
      CDarwinUtils::SetScheduling(message.GetMessage());
#endif
      CPlayList playList = CServiceBroker::GetPlaylistPlayer().GetPlaylist(CServiceBroker::GetPlaylistPlayer().GetCurrentPlaylist());

      // Update our infoManager with the new details etc.
      if (m_nextPlaylistItem >= 0)
      {
        // playing an item which is not in the list - player might be stopped already
        // so do nothing
        if (playList.size() <= m_nextPlaylistItem)
          return true;

        // we've started a previously queued item
        CFileItemPtr item = playList[m_nextPlaylistItem];
        // update the playlist manager
        int currentSong = CServiceBroker::GetPlaylistPlayer().GetCurrentSong();
        int param = ((currentSong & 0xffff) << 16) | (m_nextPlaylistItem & 0xffff);
        CGUIMessage msg(GUI_MSG_PLAYLISTPLAYER_CHANGED, 0, 0, CServiceBroker::GetPlaylistPlayer().GetCurrentPlaylist(), param, item);
        g_windowManager.SendThreadMessage(msg);
        CServiceBroker::GetPlaylistPlayer().SetCurrentSong(m_nextPlaylistItem);
        m_itemCurrentFile.reset(new CFileItem(*item));
      }
      g_infoManager.SetCurrentItem(*m_itemCurrentFile);
      g_partyModeManager.OnSongChange(true);

      CVariant param;
      param["player"]["speed"] = 1;
      param["player"]["playerid"] = CServiceBroker::GetPlaylistPlayer().GetCurrentPlaylist();
      CAnnouncementManager::GetInstance().Announce(Player, "xbmc", "OnPlay", m_itemCurrentFile, param);
      return true;
    }
    break;

  case GUI_MSG_QUEUE_NEXT_ITEM:
    {
      // Check to see if our playlist player has a new item for us,
      // and if so, we check whether our current player wants the file
      int iNext = CServiceBroker::GetPlaylistPlayer().GetNextSong();
      CPlayList& playlist = CServiceBroker::GetPlaylistPlayer().GetPlaylist(CServiceBroker::GetPlaylistPlayer().GetCurrentPlaylist());
      if (iNext < 0 || iNext >= playlist.size())
      {
        m_appPlayer.OnNothingToQueueNotify();
        return true; // nothing to do
      }

      // ok, grab the next song
      CFileItem file(*playlist[iNext]);
      // handle plugin://
      CURL url(file.GetPath());
      if (url.IsProtocol("plugin"))
        XFILE::CPluginDirectory::GetPluginResult(url.Get(), file, false);

      // Don't queue if next media type is different from current one
      if ((!file.IsVideo() && m_appPlayer.IsPlayingVideo())
          || ((!file.IsAudio() || file.IsVideo()) && m_appPlayer.IsPlayingAudio()))
      {
        m_appPlayer.OnNothingToQueueNotify();
        return true;
      }

#ifdef HAS_UPNP
      if (URIUtils::IsUPnP(file.GetPath()))
      {
        if (!XFILE::CUPnPDirectory::GetResource(file.GetURL(), file))
          return true;
      }
#endif

      // ok - send the file to the player, if it accepts it
      if (m_appPlayer.QueueNextFile(file))
      {
        // player accepted the next file
        m_nextPlaylistItem = iNext;
      }
      else
      {
        /* Player didn't accept next file: *ALWAYS* advance playlist in this case so the player can
            queue the next (if it wants to) and it doesn't keep looping on this song */
        CServiceBroker::GetPlaylistPlayer().SetCurrentSong(iNext);
      }

      return true;
    }
    break;

  case GUI_MSG_PLAYBACK_STOPPED:
  case GUI_MSG_PLAYBACK_ENDED:
  case GUI_MSG_PLAYLISTPLAYER_STOPPED:
    {
#ifdef TARGET_DARWIN_IOS
      CDarwinUtils::SetScheduling(message.GetMessage());
#endif
      // first check if we still have items in the stack to play
      if (message.GetMessage() == GUI_MSG_PLAYBACK_ENDED)
      {
        if (m_stackHelper.IsPlayingRegularStack() && m_stackHelper.HasNextStackPartFileItem())
        { // just play the next item in the stack
          PlayFile(m_stackHelper.SetNextStackPartCurrentFileItem(), "", true);
          return true;
        }
      }

      // reset the current playing file
      m_itemCurrentFile->Reset();
      g_infoManager.ResetCurrentItem();
      m_stackHelper.Clear();

      if (message.GetMessage() == GUI_MSG_PLAYBACK_ENDED)
      {
        if (!CServiceBroker::GetPlaylistPlayer().PlayNext(1, true))
          m_appPlayer.ClosePlayer();
      }

      if (!m_appPlayer.IsPlaying())
      {
        g_audioManager.Enable(true);
      }

      if (!m_appPlayer.IsPlayingVideo())
      {
        if(g_windowManager.GetActiveWindow() == WINDOW_FULLSCREEN_VIDEO ||
           g_windowManager.GetActiveWindow() == WINDOW_FULLSCREEN_GAME)
        {
          g_windowManager.PreviousWindow();
        }
        else
        {
          //  resets to res_desktop or look&feel resolution (including refreshrate)
          g_graphicsContext.SetFullScreenVideo(false);
        }
      }

      if (!m_appPlayer.IsPlayingAudio() && CServiceBroker::GetPlaylistPlayer().GetCurrentPlaylist() == PLAYLIST_NONE && g_windowManager.GetActiveWindow() == WINDOW_VISUALISATION)
      {
        m_ServiceManager->GetSettings().Save();  // save vis settings
        WakeUpScreenSaverAndDPMS();
        g_windowManager.PreviousWindow();
      }

      // DVD ejected while playing in vis ?
      if (!m_appPlayer.IsPlayingAudio() && (m_itemCurrentFile->IsCDDA() || m_itemCurrentFile->IsOnDVD()) && !g_mediaManager.IsDiscInDrive() && g_windowManager.GetActiveWindow() == WINDOW_VISUALISATION)
      {
        // yes, disable vis
        m_ServiceManager->GetSettings().Save();    // save vis settings
        WakeUpScreenSaverAndDPMS();
        g_windowManager.PreviousWindow();
      }

      if (IsEnableTestMode())
        CApplicationMessenger::GetInstance().PostMsg(TMSG_QUIT);
      return true;
    }
    break;

  case GUI_MSG_PLAYLISTPLAYER_STARTED:
  case GUI_MSG_PLAYLISTPLAYER_CHANGED:
    {
      return true;
    }
    break;
  case GUI_MSG_FULLSCREEN:
    { // Switch to fullscreen, if we can
      SwitchToFullScreen();
      return true;
    }
    break;
  case GUI_MSG_EXECUTE:
    if (message.GetNumStringParams())
      return ExecuteXBMCAction(message.GetStringParam(), message.GetItem());
    break;
  }
  return false;
}

bool CApplication::ExecuteXBMCAction(std::string actionStr, const CGUIListItemPtr &item /* = NULL */)
{
  // see if it is a user set string

  //We don't know if there is unsecure information in this yet, so we
  //postpone any logging
  const std::string in_actionStr(actionStr);
  if (item)
    actionStr = CGUIInfoLabel::GetItemLabel(actionStr, item.get());
  else
    actionStr = CGUIInfoLabel::GetLabel(actionStr);

  // user has asked for something to be executed
  if (CBuiltins::GetInstance().HasCommand(actionStr))
  {
    if (!CBuiltins::GetInstance().IsSystemPowerdownCommand(actionStr) ||
        CServiceBroker::GetPVRManager().GUIActions()->CanSystemPowerdown())
      CBuiltins::GetInstance().Execute(actionStr);
  }
  else
  {
    // try translating the action from our ButtonTranslator
    unsigned int actionID;
    if (CActionTranslator::TranslateString(actionStr, actionID))
    {
      OnAction(CAction(actionID));
      return true;
    }
    CFileItem item(actionStr, false);
#ifdef HAS_PYTHON
    if (item.IsPythonScript())
    { // a python script
      CScriptInvocationManager::GetInstance().ExecuteAsync(item.GetPath());
    }
    else
#endif
    if (item.IsAudio() || item.IsVideo() || item.IsGame())
    { // an audio or video file
      PlayFile(item, "");
    }
    else
    {
      //At this point we have given up to translate, so even though
      //there may be insecure information, we log it.
      CLog::LogF(LOGDEBUG,"Tried translating, but failed to understand %s", in_actionStr.c_str());
      return false;
    }
  }
  return true;
}

// inform the user that the configuration data has moved from old XBMC location
// to new Kodi location - if applicable
void CApplication::ShowAppMigrationMessage()
{
  // .kodi_migration_complete will be created from the installer/packaging
  // once an old XBMC configuration was moved to the new Kodi location
  // if this is the case show the migration info to the user once which
  // tells him to have a look into the wiki where the move of configuration
  // is further explained.
  if (CFile::Exists("special://home/.kodi_data_was_migrated") &&
      !CFile::Exists("special://home/.kodi_migration_info_shown"))
  {
    HELPERS::ShowOKDialogText(CVariant{24128}, CVariant{24129});
    CFile tmpFile;
    // create the file which will prevent this dialog from appearing in the future
    tmpFile.OpenForWrite("special://home/.kodi_migration_info_shown");
    tmpFile.Close();
  }
}

void CApplication::Process()
{
  // dispatch the messages generated by python or other threads to the current window
  g_windowManager.DispatchThreadMessages();

  // process messages which have to be send to the gui
  // (this can only be done after g_windowManager.Render())
  CApplicationMessenger::GetInstance().ProcessWindowMessages();

  if (m_autoExecScriptExecuted)
  {
    m_autoExecScriptExecuted = false;

    // autoexec.py - profile
    std::string strAutoExecPy = CSpecialProtocol::TranslatePath("special://profile/autoexec.py");

    if (XFILE::CFile::Exists(strAutoExecPy))
      CScriptInvocationManager::GetInstance().ExecuteAsync(strAutoExecPy);
    else
      CLog::Log(LOGDEBUG, "no profile autoexec.py (%s) found, skipping", strAutoExecPy.c_str());
  }

  // handle any active scripts

  {
    // Allow processing of script threads to let them shut down properly.
    CSingleExit ex(g_graphicsContext);
    m_frameMoveGuard.unlock();
    CScriptInvocationManager::GetInstance().Process();
    m_frameMoveGuard.lock();
  }

  // process messages, even if a movie is playing
  CApplicationMessenger::GetInstance().ProcessMessages();
  if (g_application.m_bStop) return; //we're done, everything has been unloaded

  // update sound
  m_appPlayer.DoAudioWork();

  // do any processing that isn't needed on each run
  if( m_slowTimer.GetElapsedMilliseconds() > 500 )
  {
    m_slowTimer.Reset();
    ProcessSlow();
  }
#if !defined(TARGET_DARWIN)
  g_cpuInfo.getUsedPercentage(); // must call it to recalculate pct values
#endif
}

// We get called every 500ms
void CApplication::ProcessSlow()
{
  CServiceBroker::GetPowerManager().ProcessEvents();

#if defined(TARGET_DARWIN_OSX)
  // There is an issue on OS X that several system services ask the cursor to become visible
  // during their startup routines.  Given that we can't control this, we hack it in by
  // forcing the
  if (CServiceBroker::GetWinSystem().IsFullScreen())
  { // SDL thinks it's hidden
    Cocoa_HideMouse();
  }
#endif

  // Temporarily pause pausable jobs when viewing video/picture
  int currentWindow = g_windowManager.GetActiveWindow();
  if (CurrentFileItem().IsVideo() ||
      CurrentFileItem().IsPicture() ||
      currentWindow == WINDOW_FULLSCREEN_VIDEO ||
      currentWindow == WINDOW_FULLSCREEN_GAME ||
      currentWindow == WINDOW_SLIDESHOW)
  {
    CJobManager::GetInstance().PauseJobs();
  }
  else
  {
    CJobManager::GetInstance().UnPauseJobs();
  }

  // Check if we need to activate the screensaver / DPMS.
  CheckScreenSaverAndDPMS();

  // Check if we need to shutdown (if enabled).
#if defined(TARGET_DARWIN)
  if (m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_POWERMANAGEMENT_SHUTDOWNTIME) && g_advancedSettings.m_fullScreen)
#else
  if (m_ServiceManager->GetSettings().GetInt(CSettings::SETTING_POWERMANAGEMENT_SHUTDOWNTIME))
#endif
  {
    CheckShutdown();
  }

  // check if we should restart the player
  CheckDelayedPlayerRestart();

  //  check if we can unload any unreferenced dlls or sections
  if (!m_appPlayer.IsPlayingVideo())
    CSectionLoader::UnloadDelayed();

#ifdef TARGET_ANDROID
  // Pass the slow loop to droid
  CXBMCApp::get()->ProcessSlow();
#endif

  // check for any idle curl connections
  g_curlInterface.CheckIdle();

  g_largeTextureManager.CleanupUnusedImages();

  g_TextureManager.FreeUnusedTextures(5000);

#ifdef HAS_DVD_DRIVE
  // checks whats in the DVD drive and tries to autostart the content (xbox games, dvd, cdda, avi files...)
  if (!m_appPlayer.IsPlayingVideo())
    m_Autorun->HandleAutorun();
#endif

  // update upnp server/renderer states
#ifdef HAS_UPNP
  if (CServiceBroker::GetSettings().GetBool(CSettings::SETTING_SERVICES_UPNP) && UPNP::CUPnP::IsInstantiated())
    UPNP::CUPnP::GetInstance()->UpdateState();
#endif

#if defined(TARGET_POSIX) && defined(HAS_FILESYSTEM_SMB)
  smb.CheckIfIdle();
#endif

#ifdef HAS_FILESYSTEM_NFS
  gNfsConnection.CheckIfIdle();
#endif

#ifdef HAS_FILESYSTEM_SFTP
  CSFTPSessionManager::ClearOutIdleSessions();
#endif

  for (const auto& vfsAddon : CServiceBroker::GetVFSAddonCache().GetAddonInstances())
    vfsAddon->ClearOutIdle();

  g_mediaManager.ProcessEvents();

  m_ServiceManager->GetActiveAE().GarbageCollect();

  g_windowManager.SendMessage(GUI_MSG_REFRESH_TIMER,0,0);

  // if we don't render the gui there's no reason to start the screensaver.
  // that way the screensaver won't kick in if we maximize the XBMC window
  // after the screensaver start time.
  if(!m_renderGUI)
    ResetScreenSaverTimer();
}

// Global Idle Time in Seconds
// idle time will be reset if on any OnKey()
// int return: system Idle time in seconds! 0 is no idle!
int CApplication::GlobalIdleTime()
{
  if(!m_idleTimer.IsRunning())
    m_idleTimer.StartZero();
  return (int)m_idleTimer.GetElapsedSeconds();
}

float CApplication::NavigationIdleTime()
{
  if (!m_navigationTimer.IsRunning())
    m_navigationTimer.StartZero();
  return m_navigationTimer.GetElapsedSeconds();
}

void CApplication::DelayedPlayerRestart()
{
  m_restartPlayerTimer.StartZero();
}

void CApplication::CheckDelayedPlayerRestart()
{
  if (m_restartPlayerTimer.GetElapsedSeconds() > 3)
  {
    m_restartPlayerTimer.Stop();
    m_restartPlayerTimer.Reset();
    Restart(true);
  }
}

void CApplication::Restart(bool bSamePosition)
{
  // this function gets called when the user changes a setting (like noninterleaved)
  // and which means we gotta close & reopen the current playing file

  // first check if we're playing a file
  if ( !m_appPlayer.IsPlayingVideo() && !m_appPlayer.IsPlayingAudio())
    return ;

  if( !m_appPlayer.HasPlayer() )
    return ;

  // do we want to return to the current position in the file
  if (false == bSamePosition)
  {
    // no, then just reopen the file and start at the beginning
    PlayFile(*m_itemCurrentFile, "", true);
    return ;
  }

  // else get current position
  double time = GetTime();

  // get player state, needed for dvd's
  std::string state = m_appPlayer.GetPlayerState();

  // set the requested starttime
  m_itemCurrentFile->m_lStartOffset = (long)(time * 75.0);

  // reopen the file
  if ( PlayFile(*m_itemCurrentFile, "", true) == PLAYBACK_OK )
    m_appPlayer.SetPlayerState(state);
}

const std::string& CApplication::CurrentFile()
{
  return m_itemCurrentFile->GetPath();
}

std::shared_ptr<CFileItem> CApplication::CurrentFileItemPtr()
{
  return m_itemCurrentFile;
}

CFileItem& CApplication::CurrentFileItem()
{
  return *m_itemCurrentFile;
}

CFileItem& CApplication::CurrentUnstackedItem()
{
  if (m_stackHelper.IsPlayingISOStack() || m_stackHelper.IsPlayingRegularStack())
    return m_stackHelper.GetCurrentStackPartFileItem();
  else
    return *m_itemCurrentFile;
}

void CApplication::ShowVolumeBar(const CAction *action)
{
  CGUIDialogVolumeBar *volumeBar = g_windowManager.GetWindow<CGUIDialogVolumeBar>(WINDOW_DIALOG_VOLUME_BAR);
  if (volumeBar != nullptr && volumeBar->IsVolumeBarEnabled())
  {
    volumeBar->Open();
    if (action)
      volumeBar->OnAction(*action);
  }
}

bool CApplication::IsMuted() const
{
  if (CServiceBroker::GetPeripherals().IsMuted())
    return true;
  return m_ServiceManager->GetActiveAE().IsMuted();
}

void CApplication::ToggleMute(void)
{
  if (m_muted)
    UnMute();
  else
    Mute();
}

void CApplication::SetMute(bool mute)
{
  if (m_muted != mute)
  {
    ToggleMute();
    m_muted = mute;
  }
}

void CApplication::Mute()
{
  if (CServiceBroker::GetPeripherals().Mute())
    return;

  m_ServiceManager->GetActiveAE().SetMute(true);
  m_muted = true;
  VolumeChanged();
}

void CApplication::UnMute()
{
  if (CServiceBroker::GetPeripherals().UnMute())
    return;

  m_ServiceManager->GetActiveAE().SetMute(false);
  m_muted = false;
  VolumeChanged();
}

void CApplication::SetVolume(float iValue, bool isPercentage/*=true*/)
{
  float hardwareVolume = iValue;

  if(isPercentage)
    hardwareVolume /= 100.0f;

  SetHardwareVolume(hardwareVolume);
  VolumeChanged();
}

void CApplication::SetHardwareVolume(float hardwareVolume)
{
  hardwareVolume = std::max(VOLUME_MINIMUM, std::min(VOLUME_MAXIMUM, hardwareVolume));
  m_volumeLevel = hardwareVolume;

  m_ServiceManager->GetActiveAE().SetVolume(hardwareVolume);
}

float CApplication::GetVolume(bool percentage /* = true */) const
{
  if (percentage)
  {
    // converts the hardware volume to a percentage
    return m_volumeLevel * 100.0f;
  }

  return m_volumeLevel;
}

void CApplication::VolumeChanged()
{
  CVariant data(CVariant::VariantTypeObject);
  data["volume"] = GetVolume();
  data["muted"] = m_muted;
  CAnnouncementManager::GetInstance().Announce(Application, "xbmc", "OnVolumeChanged", data);

  // if player has volume control, set it.
  m_appPlayer.SetVolume(m_volumeLevel);
  m_appPlayer.SetMute(m_muted);
}

int CApplication::GetSubtitleDelay()
{
  // converts subtitle delay to a percentage
  return int(((float)(m_appPlayer.GetVideoSettings().m_SubtitleDelay + g_advancedSettings.m_videoSubsDelayRange)) / (2 * g_advancedSettings.m_videoSubsDelayRange)*100.0f + 0.5f);
}

int CApplication::GetAudioDelay()
{
  // converts audio delay to a percentage
  return int(((float)(m_appPlayer.GetVideoSettings().m_AudioDelay + g_advancedSettings.m_videoAudioDelayRange)) / (2 * g_advancedSettings.m_videoAudioDelayRange)*100.0f + 0.5f);
}

// Returns the total time in seconds of the current media.  Fractional
// portions of a second are possible - but not necessarily supported by the
// player class.  This returns a double to be consistent with GetTime() and
// SeekTime().
double CApplication::GetTotalTime() const
{
  double rc = 0.0;

  if (m_appPlayer.IsPlaying())
  {
    if (m_stackHelper.IsPlayingRegularStack())
      rc = m_stackHelper.GetStackTotalTimeMs() * 0.001f;
    else
      rc = static_cast<double>(m_appPlayer.GetTotalTime() * 0.001f);
  }

  return rc;
}

void CApplication::StopShutdownTimer()
{
  m_shutdownTimer.Stop();
}

void CApplication::ResetShutdownTimers()
{
  // reset system shutdown timer
  m_shutdownTimer.StartZero();

  // delete custom shutdown timer
  if (g_alarmClock.HasAlarm("shutdowntimer"))
    g_alarmClock.Stop("shutdowntimer", true);
}

// Returns the current time in seconds of the currently playing media.
// Fractional portions of a second are possible.  This returns a double to
// be consistent with GetTotalTime() and SeekTime().
double CApplication::GetTime() const
{
  double rc = 0.0;

  if (m_appPlayer.IsPlaying())
  {
    if (m_stackHelper.IsPlayingRegularStack())
    {
      uint64_t startOfCurrentFile = m_stackHelper.GetCurrentStackPartStartTimeMs();
      rc = (startOfCurrentFile + m_appPlayer.GetTime()) * 0.001f;
    }
    else
      rc = static_cast<double>(m_appPlayer.GetTime() * 0.001f);
  }

  return rc;
}

// Sets the current position of the currently playing media to the specified
// time in seconds.  Fractional portions of a second are valid.  The passed
// time is the time offset from the beginning of the file as opposed to a
// delta from the current position.  This method accepts a double to be
// consistent with GetTime() and GetTotalTime().
void CApplication::SeekTime( double dTime )
{
  if (m_appPlayer.IsPlaying() && (dTime >= 0.0))
  {
    if (!m_appPlayer.CanSeek())
      return;
    if (m_stackHelper.IsPlayingRegularStack())
    {
      // find the item in the stack we are seeking to, and load the new
      // file if necessary, and calculate the correct seek within the new
      // file.  Otherwise, just fall through to the usual routine if the
      // time is higher than our total time.
      int partNumberToPlay = m_stackHelper.GetStackPartNumberAtTimeMs(static_cast<uint64_t>(dTime * 1000.0));
      uint64_t startOfNewFile = m_stackHelper.GetStackPartStartTimeMs(partNumberToPlay);
      if (partNumberToPlay == m_stackHelper.GetCurrentPartNumber())
        m_appPlayer.SeekTime(static_cast<uint64_t>(dTime * 1000.0) - startOfNewFile);
      else
      { // seeking to a new file
        m_stackHelper.SetStackPartCurrentFileItem(partNumberToPlay);
        CFileItem *item = new CFileItem(m_stackHelper.GetCurrentStackPartFileItem());
        item->m_lStartOffset = (static_cast<uint64_t>(dTime * 1000.0) - startOfNewFile) * 75 / 1000;
        // don't just call "PlayFile" here, as we are quite likely called from the
        // player thread, so we won't be able to delete ourselves.
        CApplicationMessenger::GetInstance().PostMsg(TMSG_MEDIA_PLAY, 1, 0, static_cast<void*>(item));
      }
      return;
    }
    // convert to milliseconds and perform seek
    m_appPlayer.SeekTime( static_cast<int64_t>( dTime * 1000.0 ) );
  }
}

float CApplication::GetPercentage() const
{
  if (m_appPlayer.IsPlaying())
  {
    if (m_appPlayer.GetTotalTime() == 0 && m_appPlayer.IsPlayingAudio() && m_itemCurrentFile->HasMusicInfoTag())
    {
      const CMusicInfoTag& tag = *m_itemCurrentFile->GetMusicInfoTag();
      if (tag.GetDuration() > 0)
        return (float)(GetTime() / tag.GetDuration() * 100);
    }

    if (m_stackHelper.IsPlayingRegularStack())
    {
      double totalTime = GetTotalTime();
      if (totalTime > 0.0f)
        return (float)(GetTime() / totalTime * 100);
    }
    else
      return m_appPlayer.GetPercentage();
  }
  return 0.0f;
}

float CApplication::GetCachePercentage() const
{
  if (m_appPlayer.IsPlaying())
  {
    // Note that the player returns a relative cache percentage and we want an absolute percentage
    if (m_stackHelper.IsPlayingRegularStack())
    {
      float stackedTotalTime = (float) GetTotalTime();
      // We need to take into account the stack's total time vs. currently playing file's total time
      if (stackedTotalTime > 0.0f)
        return std::min( 100.0f, GetPercentage() + (m_appPlayer.GetCachePercentage() * m_appPlayer.GetTotalTime() * 0.001f / stackedTotalTime ) );
    }
    else
      return std::min( 100.0f, m_appPlayer.GetPercentage() + m_appPlayer.GetCachePercentage() );
  }
  return 0.0f;
}

void CApplication::SeekPercentage(float percent)
{
  if (m_appPlayer.IsPlaying() && (percent >= 0.0))
  {
    if (!m_appPlayer.CanSeek())
      return;
    if (m_stackHelper.IsPlayingRegularStack())
      SeekTime(percent * 0.01 * GetTotalTime());
    else
      m_appPlayer.SeekPercentage(percent);
  }
}

// SwitchToFullScreen() returns true if a switch is made, else returns false
bool CApplication::SwitchToFullScreen(bool force /* = false */)
{
  // don't switch if the slideshow is active
  if (g_windowManager.GetFocusedWindow() == WINDOW_SLIDESHOW)
    return false;

  // if playing from the video info window, close it first!
  if (g_windowManager.HasModalDialog() && g_windowManager.GetTopMostModalDialogID() == WINDOW_DIALOG_VIDEO_INFO)
  {
    CGUIDialogVideoInfo* pDialog = g_windowManager.GetWindow<CGUIDialogVideoInfo>(WINDOW_DIALOG_VIDEO_INFO);
    if (pDialog) pDialog->Close(true);
  }

  int windowID = WINDOW_INVALID;

  // See if we're playing a game, and are in GUI mode
  if (m_appPlayer.IsPlayingGame() && g_windowManager.GetActiveWindow() != WINDOW_FULLSCREEN_GAME)
    windowID = WINDOW_FULLSCREEN_GAME;

  // See if we're playing a video, and are in GUI mode
  else if (m_appPlayer.IsPlayingVideo() && g_windowManager.GetActiveWindow() != WINDOW_FULLSCREEN_VIDEO)
    windowID = WINDOW_FULLSCREEN_VIDEO;

  // special case for switching between GUI & visualisation mode. (only if we're playing an audio song)
  if (m_appPlayer.IsPlayingAudio() && g_windowManager.GetActiveWindow() != WINDOW_VISUALISATION)
    windowID = WINDOW_VISUALISATION;


  if (windowID != WINDOW_INVALID)
  {
    if (force)
      g_windowManager.ForceActivateWindow(windowID);
    else
      g_windowManager.ActivateWindow(windowID);
    return true;
  }

  return false;
}

void CApplication::Minimize()
{
  CServiceBroker::GetWinSystem().Minimize();
}

std::string CApplication::GetCurrentPlayer()
{
  return m_appPlayer.GetCurrentPlayer();
}

CApplicationPlayer& CApplication::GetAppPlayer()
{
  return m_appPlayer;
}

CApplicationStackHelper& CApplication::GetAppStackHelper()
{
  return m_stackHelper;
}

void CApplication::UpdateLibraries()
{
  if (m_ServiceManager->GetSettings().GetBool(CSettings::SETTING_VIDEOLIBRARY_UPDATEONSTARTUP))
  {
    CLog::LogF(LOGNOTICE, "Starting video library startup scan");
    StartVideoScan("", !m_ServiceManager->GetSettings().GetBool(CSettings::SETTING_VIDEOLIBRARY_BACKGROUNDUPDATE));
  }

  if (m_ServiceManager->GetSettings().GetBool(CSettings::SETTING_MUSICLIBRARY_UPDATEONSTARTUP))
  {
    CLog::LogF(LOGNOTICE, "Starting music library startup scan");
    StartMusicScan("", !m_ServiceManager->GetSettings().GetBool(CSettings::SETTING_MUSICLIBRARY_BACKGROUNDUPDATE));
  }
}

bool CApplication::IsVideoScanning() const
{
  return CVideoLibraryQueue::GetInstance().IsScanningLibrary();
}

bool CApplication::IsMusicScanning() const
{
  return CMusicLibraryQueue::GetInstance().IsScanningLibrary();
}

void CApplication::StopVideoScan()
{
  CVideoLibraryQueue::GetInstance().StopLibraryScanning();
}

void CApplication::StopMusicScan()
{
  CMusicLibraryQueue::GetInstance().StopLibraryScanning();
}

void CApplication::StartVideoCleanup(bool userInitiated /* = true */,
                                     const std::string& content /* = "" */)
{
  if (userInitiated && CVideoLibraryQueue::GetInstance().IsRunning())
    return;

  std::set<int> paths;
  if (!content.empty())
  {
    CVideoDatabase db;
    std::set<std::string> contentPaths;
    if (db.Open() && db.GetPaths(contentPaths))
    {
      for (const std::string& path : contentPaths)
      {
        if (db.GetContentForPath(path) == content)
        {
          paths.insert(db.GetPathId(path));
          std::vector<std::pair<int, std::string>> sub;
          if (db.GetSubPaths(path, sub))
          {
            for (const auto& it : sub)
              paths.insert(it.first);
          }
        }
      }
    }
    if (paths.empty())
      return;
  }
  if (userInitiated)
    CVideoLibraryQueue::GetInstance().CleanLibraryModal(paths);
  else
    CVideoLibraryQueue::GetInstance().CleanLibrary(paths, true);
}

void CApplication::StartVideoScan(const std::string &strDirectory, bool userInitiated /* = true */, bool scanAll /* = false */)
{
  CVideoLibraryQueue::GetInstance().ScanLibrary(strDirectory, scanAll, userInitiated);
}

void CApplication::StartMusicCleanup(bool userInitiated /* = true */)
{
  if (userInitiated && CMusicLibraryQueue::GetInstance().IsRunning())
    return;

  if (userInitiated)
    /*
     CMusicLibraryQueue::GetInstance().CleanLibraryModal();
     As cleaning is non-granular and does not offer many opportunities to update progress 
     dialog rendering, do asynchronously with model dialog
    */
    CMusicLibraryQueue::GetInstance().CleanLibrary(true);
  else
    CMusicLibraryQueue::GetInstance().CleanLibrary(false);
}

void CApplication::StartMusicScan(const std::string &strDirectory, bool userInitiated /* = true */, int flags /* = 0 */)
{
  if (IsMusicScanning())
    return;

  // Setup default flags
  if (!flags)
  { // Online scraping of additional info during scanning
    if (m_ServiceManager->GetSettings().GetBool(CSettings::SETTING_MUSICLIBRARY_DOWNLOADINFO))
      flags |= CMusicInfoScanner::SCAN_ONLINE;
  }
  if (!userInitiated || m_ServiceManager->GetSettings().GetBool(CSettings::SETTING_MUSICLIBRARY_BACKGROUNDUPDATE))
    flags |= CMusicInfoScanner::SCAN_BACKGROUND;

  CMusicLibraryQueue::GetInstance().ScanLibrary(strDirectory, flags, !(flags & CMusicInfoScanner::SCAN_BACKGROUND));
}

void CApplication::StartMusicAlbumScan(const std::string& strDirectory, bool refresh)
{
  if (IsMusicScanning())
    return;

  CMusicLibraryQueue::GetInstance().StartAlbumScan(strDirectory, refresh);
}

void CApplication::StartMusicArtistScan(const std::string& strDirectory,
                                        bool refresh)
{
  if (IsMusicScanning())
    return;

  CMusicLibraryQueue::GetInstance().StartArtistScan(strDirectory, refresh);
}

bool CApplication::ProcessAndStartPlaylist(const std::string& strPlayList, CPlayList& playlist, int iPlaylist, int track)
{
  CLog::Log(LOGDEBUG,"CApplication::ProcessAndStartPlaylist(%s, %i)",strPlayList.c_str(), iPlaylist);

  // initial exit conditions
  // no songs in playlist just return
  if (playlist.size() == 0)
    return false;

  // illegal playlist
  if (iPlaylist < PLAYLIST_MUSIC || iPlaylist > PLAYLIST_VIDEO)
    return false;

  // setup correct playlist
  CServiceBroker::GetPlaylistPlayer().ClearPlaylist(iPlaylist);

  // if the playlist contains an internet stream, this file will be used
  // to generate a thumbnail for musicplayer.cover
  g_application.m_strPlayListFile = strPlayList;

  // add the items to the playlist player
  CServiceBroker::GetPlaylistPlayer().Add(iPlaylist, playlist);

  // if we have a playlist
  if (CServiceBroker::GetPlaylistPlayer().GetPlaylist(iPlaylist).size())
  {
    // start playing it
    CServiceBroker::GetPlaylistPlayer().SetCurrentPlaylist(iPlaylist);
    CServiceBroker::GetPlaylistPlayer().Reset();
    CServiceBroker::GetPlaylistPlayer().Play(track, "");
    return true;
  }
  return false;
}

bool CApplication::IsCurrentThread() const
{
  return CThread::IsCurrentThread(m_threadID);
}

void CApplication::SetRenderGUI(bool renderGUI)
{
  if (renderGUI && ! m_renderGUI)
    g_windowManager.MarkDirty();
  m_renderGUI = renderGUI;
}

bool CApplication::SetLanguage(const std::string &strLanguage)
{
  // nothing to be done if the language hasn't changed
  if (strLanguage == m_ServiceManager->GetSettings().GetString(CSettings::SETTING_LOCALE_LANGUAGE))
    return true;

  return m_ServiceManager->GetSettings().SetString(CSettings::SETTING_LOCALE_LANGUAGE, strLanguage);
}

bool CApplication::LoadLanguage(bool reload)
{
  // load the configured langauge
  if (!g_langInfo.SetLanguage("", reload))
    return false;

  // set the proper audio and subtitle languages
  g_langInfo.SetAudioLanguage(m_ServiceManager->GetSettings().GetString(CSettings::SETTING_LOCALE_AUDIOLANGUAGE));
  g_langInfo.SetSubtitleLanguage(m_ServiceManager->GetSettings().GetString(CSettings::SETTING_LOCALE_SUBTITLELANGUAGE));

  return true;
}

void CApplication::SetLoggingIn(bool switchingProfiles)
{
  // don't save skin settings on unloading when logging into another profile
  // because in that case we have already loaded the new profile and
  // would therefore write the previous skin's settings into the new profile
  // instead of into the previous one
  m_saveSkinOnUnloading = !switchingProfiles;

  // make sure that the autoexec.py script is executed after logging in
  m_autoExecScriptExecuted = true;
}

void CApplication::CloseNetworkShares()
{
  CLog::Log(LOGDEBUG,"CApplication::CloseNetworkShares: Closing all network shares");

#if defined(HAS_FILESYSTEM_SMB) && !defined(TARGET_WINDOWS)
  smb.Deinit();
#endif

#ifdef HAS_FILESYSTEM_NFS
  gNfsConnection.Deinit();
#endif

#ifdef HAS_FILESYSTEM_SFTP
  CSFTPSessionManager::DisconnectAllSessions();
#endif

  for (const auto& vfsAddon : CServiceBroker::GetVFSAddonCache().GetAddonInstances())
    vfsAddon->DisconnectAll();
}

void CApplication::RegisterActionListener(IActionListener *listener)
{
  CSingleLock lock(m_critSection);
  std::vector<IActionListener *>::iterator it = std::find(m_actionListeners.begin(), m_actionListeners.end(), listener);
  if (it == m_actionListeners.end())
    m_actionListeners.push_back(listener);
}

void CApplication::UnregisterActionListener(IActionListener *listener)
{
  CSingleLock lock(m_critSection);
  std::vector<IActionListener *>::iterator it = std::find(m_actionListeners.begin(), m_actionListeners.end(), listener);
  if (it != m_actionListeners.end())
    m_actionListeners.erase(it);
}

bool CApplication::NotifyActionListeners(const CAction &action) const
{
  CSingleLock lock(m_critSection);
  for (std::vector<IActionListener *>::const_iterator it = m_actionListeners.begin(); it != m_actionListeners.end(); ++it)
  {
    if ((*it)->OnAction(action))
      return true;
  }

  return false;
}
