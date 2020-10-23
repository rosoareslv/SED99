/*
 *      Copyright (C) 2012-2017 Team Kodi
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

#include "GameClientProperties.h"
#include "GameClientTiming.h"
#include "addons/binary-addons/AddonDll.h"
#include "addons/kodi-addon-dev-kit/include/kodi/kodi_game_types.h"
#include "games/controllers/ControllerTypes.h"
#include "games/GameTypes.h"
#include "threads/CriticalSection.h"

#include <atomic>
#include <set>
#include <stdint.h>
#include <string>
#include <vector>

class CFileItem;

namespace KODI
{
namespace GAME
{

class CGameClientHardware;
class CGameClientInGameSaves;
class CGameClientJoystick;
class CGameClientKeyboard;
class CGameClientMouse;
class IGameAudioCallback;
class IGameClientPlayback;
class IGameInputCallback;
class IGameVideoCallback;

// --- CGameClient -------------------------------------------------------------

/*!
 * \ingroup games
 * \brief Interface between Kodi and Game add-ons.
 */
class CGameClient : public ADDON::CAddonDll
{
public:
  static std::unique_ptr<CGameClient> FromExtension(ADDON::CAddonInfo addonInfo, const cp_extension_t* ext);

  explicit CGameClient(ADDON::CAddonInfo addonInfo);

  virtual ~CGameClient(void);

  // Implementation of IAddon via CAddonDll
  virtual std::string     LibPath() const override;
  virtual ADDON::AddonPtr GetRunningInstance() const override;

  // Query properties of the game client
  bool                         SupportsStandalone() const { return m_bSupportsStandalone; }
  bool                         SupportsPath() const;
  bool                         SupportsVFS() const { return m_bSupportsVFS; }
  const std::set<std::string>& GetExtensions() const { return m_extensions; }
  bool                         SupportsAllExtensions() const { return m_bSupportsAllExtensions; }
  bool                         IsExtensionValid(const std::string& strExtension) const;

  // Start/stop gameplay
  bool Initialize(void);
  void Unload();
  bool OpenFile(const CFileItem& file, IGameAudioCallback* audio, IGameVideoCallback* video, IGameInputCallback *input);
  bool OpenStandalone(IGameAudioCallback* audio, IGameVideoCallback* video, IGameInputCallback *input);
  void Reset(unsigned int port);
  void CloseFile();
  const std::string& GetGamePath() const { return m_gamePath; }

  // Playback control
  bool IsPlaying() const { return m_bIsPlaying; }
  IGameClientPlayback* GetPlayback() { return m_playback.get(); }
  const CGameClientTiming& Timing() const { return m_timing; }
  void RunFrame();

  // Audio/video callbacks
  bool OpenPixelStream(GAME_PIXEL_FORMAT format, unsigned int width, unsigned int height, GAME_VIDEO_ROTATION rotation);
  bool OpenVideoStream(GAME_VIDEO_CODEC codec);
  bool OpenPCMStream(GAME_PCM_FORMAT format, const GAME_AUDIO_CHANNEL* channelMap);
  bool OpenAudioStream(GAME_AUDIO_CODEC codec, const GAME_AUDIO_CHANNEL* channelMap);
  void AddStreamData(GAME_STREAM_TYPE stream, const uint8_t* data, unsigned int size);
  void CloseStream(GAME_STREAM_TYPE stream);

  // Access memory
  size_t SerializeSize() const { return m_serializeSize; }
  bool Serialize(uint8_t* data, size_t size);
  bool Deserialize(const uint8_t* data, size_t size);

  // Input callbacks
  bool OpenPort(unsigned int port);
  void ClosePort(unsigned int port);
  bool ReceiveInputEvent(const game_input_event& eventStruct);

  // Input functions
  bool AcceptsInput(void) const;

  /*!
    * @brief To get the interface table used between addon and kodi
    * @todo This function becomes removed after old callback library system
    * is removed.
    */
  AddonInstance_Game* GetInstanceInterface() { return &m_struct; }

private:
  // Private gameplay functions
  bool InitializeGameplay(const std::string& gamePath, IGameAudioCallback* audio, IGameVideoCallback* video, IGameInputCallback *input);
  bool LoadGameInfo();
  bool NormalizeAudio(IGameAudioCallback* audioCallback);
  void NotifyError(GAME_ERROR error);
  std::string GetMissingResource();
  void CreatePlayback();
  void ResetPlayback();

  // Private input functions
  void UpdatePort(unsigned int port, const ControllerPtr& controller);
  void ClearPorts(void);
  bool SetRumble(unsigned int port, const std::string& feature, float magnitude);
  void OpenKeyboard(void);
  void CloseKeyboard(void);
  void OpenMouse(void);
  void CloseMouse(void);
  ControllerVector GetControllers(void) const;

  // Private memory stream functions
  size_t GetSerializeSize();

  // Helper functions
  void LogAddonProperties(void) const;
  bool LogError(GAME_ERROR error, const char* strMethod) const;
  void LogException(const char* strFunctionName) const;

  /*!
   * @brief Callback functions from addon to kodi
   */
  //@{
  static void cb_close_game(void* kodiInstance);
  static int cb_open_pixel_stream(void* kodiInstance, GAME_PIXEL_FORMAT format, unsigned int width, unsigned int height, GAME_VIDEO_ROTATION rotation);
  static int cb_open_video_stream(void* kodiInstance, GAME_VIDEO_CODEC codec);
  static int cb_open_pcm_stream(void* kodiInstance, GAME_PCM_FORMAT format, const GAME_AUDIO_CHANNEL* channel_map);
  static int cb_open_audio_stream(void* kodiInstance, GAME_AUDIO_CODEC codec, const GAME_AUDIO_CHANNEL* channel_map);
  static void cb_add_stream_data(void* kodiInstance, GAME_STREAM_TYPE stream, const uint8_t* data, unsigned int size);
  static void cb_close_stream(void* kodiInstance, GAME_STREAM_TYPE stream);
  static void cb_enable_hardware_rendering(void* kodiInstance, const game_hw_info* hw_info);
  static uintptr_t cb_hw_get_current_framebuffer(void* kodiInstance);
  static game_proc_address_t cb_hw_get_proc_address(void* kodiInstance, const char* sym);
  static void cb_render_frame(void* kodiInstance);
  static bool cb_open_port(void* kodiInstance, unsigned int port);
  static void cb_close_port(void* kodiInstance, unsigned int port);
  static bool cb_input_event(void* kodiInstance, const game_input_event* event);
  //@}

  // Add-on properties
  CGameClientProperties m_libraryProps;        // Properties to pass to the DLL

  // Game API xml parameters
  bool                  m_bSupportsVFS;
  bool                  m_bSupportsStandalone;
  bool                  m_bSupportsKeyboard;
  bool                  m_bSupportsMouse;
  std::set<std::string> m_extensions;
  bool                  m_bSupportsAllExtensions;
  //GamePlatforms         m_platforms;

  // Properties of the current playing file
  std::atomic_bool      m_bIsPlaying;          // True between OpenFile() and CloseFile()
  std::string           m_gamePath;
  size_t                m_serializeSize;
  IGameAudioCallback*   m_audio;               // The audio callback passed to OpenFile()
  IGameVideoCallback*   m_video;               // The video callback passed to OpenFile()
  IGameInputCallback*   m_input = nullptr;     // The input callback passed to OpenFile()
  CGameClientTiming     m_timing;              // Class to scale playback to avoid resampling audio
  std::unique_ptr<IGameClientPlayback> m_playback; // Interface to control playback
  GAME_REGION           m_region;              // Region of the loaded game

  // In-game saves
  std::unique_ptr<CGameClientInGameSaves> m_inGameSaves;

  // Input
  std::map<int, std::unique_ptr<CGameClientJoystick>> m_ports;
  std::unique_ptr<CGameClientKeyboard> m_keyboard;
  std::unique_ptr<CGameClientMouse> m_mouse;
  std::unique_ptr<CGameClientHardware> m_hardware;

  CCriticalSection m_critSection;

  AddonInstance_Game m_struct;
};

} // namespace GAME
} // namespace KODI
