#pragma once
/*
 *      Copyright (C) 2012-2013 Team XBMC
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

#include <deque>
#include <vector>

#include "addons/PVRClient.h"
#include "threads/CriticalSection.h"
#include "threads/Thread.h"
#include "utils/Observer.h"

#include "pvr/channels/PVRChannel.h"
#include "pvr/recordings/PVRRecording.h"

namespace PVR
{
  class CPVREpg;

  typedef std::shared_ptr<CPVRClient> PVR_CLIENT;
  typedef std::map< int, PVR_CLIENT >                 PVR_CLIENTMAP;
  typedef std::map< int, PVR_CLIENT >::iterator       PVR_CLIENTMAP_ITR;
  typedef std::map< int, PVR_CLIENT >::const_iterator PVR_CLIENTMAP_CITR;
  typedef std::map< int, PVR_STREAM_PROPERTIES >      STREAMPROPS;

  /**
   * Holds generic data about a backend (number of channels etc.)
   */
  struct SBackend
  {
    std::string name;
    std::string version;
    std::string host;
    int         numTimers = 0;
    int         numRecordings = 0;
    int         numDeletedRecordings = 0;
    int         numChannels = 0;
    long long   diskUsed = 0;
    long long   diskTotal = 0;
  };

  class CPVRClients : public ADDON::IAddonMgrCallback
  {
  public:
    CPVRClients(void);
    ~CPVRClients(void) override;

    /*!
     * @brief Start the backend.
     */
    void Start(void);

    /*!
     * @brief Update add-ons from the AddonManager
     */
    void UpdateAddons(void);

    /*! @name Backend methods */
    //@{

    /*!
     * @brief Check whether a given client ID points to a created pvr client.
     * @param iClientId The client ID.
     * @return True if the the client ID represents a created client, false otherwise.
     */
    bool IsCreatedClient(int iClientId) const;

    /*!
     * @brief Check whether an given addon instance is a created pvr client.
     * @param addon The addon.
     * @return True if the the addon represents a created client, false otherwise.
     */
    bool IsCreatedClient(const ADDON::AddonPtr &addon);

    /*!
     * @brief Get the instance of the client, if it's created.
     * @param iClientId The id of the client to get.
     * @param addon The client.
     * @return True on success, false otherwise.
     */
    bool GetCreatedClient(int iClientId, PVR_CLIENT &addon) const;

    /*!
     * @brief Get all created clients.
     * @param clients Store the active clients in this map.
     * @return The amount of added clients.
     */
    int GetCreatedClients(PVR_CLIENTMAP &clients) const;

    /*!
     * @brief Restart a single client add-on.
     * @param addon The add-on to restart.
     * @param bDataChanged True if the client's data changed, false otherwise (unused).
     * @return True if the client was found and restarted, false otherwise.
     */
    bool RequestRestart(ADDON::AddonPtr addon, bool bDataChanged) override;

    /*!
     * @brief Remove a single client add-on.
     * @param addon The add-on to remove.
     * @return True if the client was found and removed, false otherwise.
     */
    bool RequestRemoval(ADDON::AddonPtr addon) override;

    /*!
     * @brief Unload all loaded add-ons and reset all class properties.
     */
    void Unload(void);

    /*!
     * @brief The ID of the first active client or -1 if no clients are active;
     */
    int GetFirstConnectedClientID(void);

    /*!
     * @return True when at least one client is known and enabled, false otherwise.
     */
    bool HasEnabledClients(void) const;

    /*!
     * @return The amount of enabled clients.
     */
    int EnabledClientAmount(void) const;

    /*!
     * @brief Stop a client.
     * @param addon The client to stop.
     * @param bRestart If true, restart the client.
     * @return True if the client was found, false otherwise.
     */
    bool StopClient(const ADDON::AddonPtr &client, bool bRestart);

    /*!
     * @return The amount of connected clients.
     */
    int CreatedClientAmount(void) const;

    /*!
     * @brief Check whether there are any connected clients.
     * @return True if at least one client is connected.
     */
    bool HasCreatedClients(void) const;

    /*!
     * @brief Get the friendly name for the client with the given id.
     * @param iClientId The id of the client.
     * @param strName The friendly name of the client or an empty string when it wasn't found.
     * @return True if the client was found, false otherwise.
     */
    bool GetClientFriendlyName(int iClientId, std::string &strName) const;

    /*!
     * @brief Get the addon name for the client with the given id.
     * @param iClientId The id of the client.
     * @param strName The addon name of the client or an empty string when it wasn't found.
     * @return True if the client was found, false otherwise.
     */
    bool GetClientAddonName(int iClientId, std::string &strName) const;

    /*!
     * @brief Get the addon icon for the client with the given id.
     * @param iClientId The id of the client.
     * @param strIcon The path to the addon icon of the client or an empty string when it wasn't found.
     * @return True if the client was found, false otherwise.
     */
    bool GetClientAddonIcon(int iClientId, std::string &strIcon) const;

    /*!
     * @brief Returns properties about all connected clients
     * @return the properties
     */
    std::vector<SBackend> GetBackendProperties() const;

    /*!
     * Get the add-on ID of the client
     * @param iClientId The db id of the client
     * @return The add-on id
     */
    std::string GetClientAddonId(int iClientId) const;

    /*!
     * @return The client ID of the client that is currently playing a stream or -1 if no client is playing.
     */
    int GetPlayingClientID(void) const;

    //@}

    /*! @name Stream methods */
    //@{

    /*!
     * @return True if a stream is playing, false otherwise.
     */
    bool IsPlaying(void) const;

    /*!
     * @return The friendly name of the client that is currently playing or an empty string if nothing is playing.
     */
    const std::string GetPlayingClientName(void) const;

    /*!
     * @brief Read from an open stream.
     * @param lpBuf Target buffer.
     * @param uiBufSize The size of the buffer.
     * @return The amount of bytes that was added.
     */
    int ReadStream(void* lpBuf, int64_t uiBufSize);

    /*!
     * @brief Return the filesize of the currently running stream.
     *        Limited to recordings playback at the moment.
     * @return The size of the stream.
     */
    int64_t GetStreamLength(void);

    /*!
     * @brief Seek to a position in a stream.
     *        Limited to recordings playback at the moment.
     * @param iFilePosition The position to seek to.
     * @param iWhence Specify how to seek ("new position=pos", "new position=pos+actual position" or "new position=filesize-pos")
     * @return The new stream position.
     */
    int64_t SeekStream(int64_t iFilePosition, int iWhence = SEEK_SET);

    /*!
     * @brief Close a PVR stream.
     */
    void CloseStream(void);

    /*!
     * @brief (Un)Pause a PVR stream (only called when timeshifting is supported)
     */
    void PauseStream(bool bPaused);

    /*!
     * @brief Check whether it is possible to pause the currently playing livetv or recording stream
     */
    bool CanPauseStream(void) const;

    /*!
     * @brief Check whether it is possible to seek the currently playing livetv or recording stream
     */
    bool CanSeekStream(void) const;

    /*!
     * @brief Get the input format name of the current playing stream content.
     * @return A pointer to the properties or NULL if no stream is playing.
     */
    std::string GetCurrentInputFormat(void) const;

    /*!
     * @return True if a TV channel is playing, false otherwise.
     */
    bool IsPlayingTV(void) const;

    /*!
     * @return True if a radio channel playing, false otherwise.
     */
    bool IsPlayingRadio(void) const;

    /*!
     * @return True if the currently playing channel is encrypted, false otherwise.
     */
    bool IsEncrypted(void) const;

    /*!
     * @brief Fill the file item for a channel with the properties required for playback. Values are obtained from the PVR backend.
     * @param fileItem The file item to be filled.
     * @return True if the stream properties have been set, false otherwiese.
     */
    bool FillChannelStreamFileItem(CFileItem &fileItem);

    /*!
     * @brief Fill the file item for a recording with the properties required for playback. Values are obtained from the PVR backend.
     * @param fileItem The file item to be filled.
     * @return True if the stream properties have been set, false otherwiese.
     */
    bool FillRecordingStreamFileItem(CFileItem &fileItem);

    /*!
     * @brief Open a stream on the given channel.
     * @param channel The channel to start playing.
     * @param bIsSwitchingChannel True when switching channels, false otherwise.
     * @return True if the stream was opened successfully, false otherwise.
     */
    bool OpenStream(const CPVRChannelPtr &channel, bool bIsSwitchingChannel);

    /*!
     * @brief Set the channel that is currently playing.
     * @param channel The channel that is currently playing.
     */
    void SetPlayingChannel(const CPVRChannelPtr channel);

    /*!
     * @brief Clear the channel that is currently playing, if any.
     */
    void ClearPlayingChannel();

    /*!
     * @brief Get the channel that is currently playing.
     * @return the channel that is currently playing, NULL otherwise.
     */
    CPVRChannelPtr GetPlayingChannel() const;

    /*!
     * @return True if a recording is playing, false otherwise.
     */
    bool IsPlayingRecording(void) const;

    /*!
     * @brief Open a stream from the given recording.
     * @param recording The recording to start playing.
     * @return True if the stream was opened successfully, false otherwise.
     */
    bool OpenStream(const CPVRRecordingPtr &recording);

    /*!
     * @brief Set the recording that is currently playing.
     * @param recording The recording that is currently playing.
     */
    void SetPlayingRecording(const CPVRRecordingPtr recording);

    /*!
     * @brief Clear the recording that is currently playing, if any.
     */
    void ClearPlayingRecording();

    /*!
     * @brief Get the recording that is currently playing.
     * @return The recording that is currently playing, NULL otherwise.
     */
    CPVRRecordingPtr GetPlayingRecording(void) const;

    //@}

    /*! @name Timer methods */
    //@{

    /*!
     * @brief Check whether there is at least one connected client supporting timers.
     * @return True if at least one connected client supports timers, false otherwise.
     */
    bool SupportsTimers() const;

    /*!
     * @brief Get all timers from clients
     * @param timers Store the timers in this container.
     * @param failedClients in case of errors will contain the ids of the clients for which the timers could not be obtained.
     * @return true on success for all clients, false in case of error for at least one client.
     */
    bool GetTimers(CPVRTimersContainer *timers, std::vector<int> &failedClients);

    /*!
     * @brief Add a new timer to a backend.
     * @param timer The timer to add.
     * @param error An error if it occured.
     * @return True if the timer was added successfully, false otherwise.
     */
    PVR_ERROR AddTimer(const CPVRTimerInfoTag &timer);

    /*!
     * @brief Update a timer on the backend.
     * @param timer The timer to update.
     * @param error An error if it occured.
     * @return True if the timer was updated successfully, false otherwise.
     */
    PVR_ERROR UpdateTimer(const CPVRTimerInfoTag &timer);

    /*!
     * @brief Delete a timer from the backend.
     * @param timer The timer to delete.
     * @param bForce Also delete when currently recording if true.
     * @param error An error if it occured.
     * @return True if the timer was deleted successfully, false otherwise.
     */
    PVR_ERROR DeleteTimer(const CPVRTimerInfoTag &timer, bool bForce);

    /*!
     * @brief Rename a timer on the backend.
     * @param timer The timer to rename.
     * @param strNewName The new name.
     * @param error An error if it occured.
     * @return True if the timer was renamed successfully, false otherwise.
     */
    PVR_ERROR RenameTimer(const CPVRTimerInfoTag &timer, const std::string &strNewName);

    /*!
     * @brief Get all supported timer types.
     * @param results The container to store the result in.
     * @return PVR_ERROR_NO_ERROR if the list has been fetched successfully.
     */
    PVR_ERROR GetTimerTypes(CPVRTimerTypes& results) const;

    /*!
     * @brief Get all timer types supported by a certain client.
     * @param iClientId The id of the client.
     * @param results The container to store the result in.
     * @return PVR_ERROR_NO_ERROR if the list has been fetched successfully.
     */
    PVR_ERROR GetTimerTypes(CPVRTimerTypes& results, int iClientId) const;

    //@}

    /*! @name Recording methods */
    //@{

    /*!
     * @brief Get all recordings from clients
     * @param recordings Store the recordings in this container.
     * @param deleted Return deleted recordings
     * @return The amount of recordings that were added.
     */
    PVR_ERROR GetRecordings(CPVRRecordings *recordings, bool deleted);

    /*!
     * @brief Rename a recordings on the backend.
     * @param recording The recordings to rename.
     * @param error An error if it occured.
     * @return True if the recording was renamed successfully, false otherwise.
     */
    PVR_ERROR RenameRecording(const CPVRRecording &recording);

    /*!
     * @brief Delete a recording from the backend.
     * @param recording The recording to delete.
     * @param error An error if it occured.
     * @return True if the recordings was deleted successfully, false otherwise.
     */
    PVR_ERROR DeleteRecording(const CPVRRecording &recording);

    /*!
     * @brief Undelete a recording from the backend.
     * @param recording The recording to undelete.
     * @param error An error if it occured.
     * @return True if the recording was undeleted successfully, false otherwise.
     */
    PVR_ERROR UndeleteRecording(const CPVRRecording &recording);

    /*!
     * @brief Delete all recordings permanent which in the deleted folder on the backend.
     * @return PVR_ERROR_NO_ERROR if the recordings has been deleted successfully.
     */
    PVR_ERROR DeleteAllRecordingsFromTrash();

    /*!
     * @brief Set the lifetime of a recording on the backend.
     * @param recording The recording to set the lifetime for. recording.m_iLifetime contains the new lifetime value.
     * @param error An error if it occured.
     * @return True if the recording's lifetime was set successfully, false otherwise.
     */
    bool SetRecordingLifetime(const CPVRRecording &recording, PVR_ERROR *error);

    /*!
     * @brief Set play count of a recording on the backend.
     * @param recording The recording to set the play count.
     * @param count Play count.
     * @param error An error if it occured.
     * @return True if the recording's play count was set successfully, false otherwise.
     */
    bool SetRecordingPlayCount(const CPVRRecording &recording, int count, PVR_ERROR *error);

    /*!
     * @brief Set the last watched position of a recording on the backend.
     * @param recording The recording.
     * @param position The last watched position in seconds
     * @param error An error if it occured.
     * @return True if the last played position was updated successfully, false otherwise
    */
    bool SetRecordingLastPlayedPosition(const CPVRRecording &recording, int lastplayedposition, PVR_ERROR *error);

    /*!
    * @brief Retrieve the last watched position of a recording on the backend.
    * @param recording The recording.
    * @return The last watched position in seconds
    */
    int GetRecordingLastPlayedPosition(const CPVRRecording &recording);

    /*!
    * @brief Retrieve the edit decision list (EDL) from the backend.
    * @param recording The recording.
    * @return The edit decision list (empty on error).
    */
    std::vector<PVR_EDL_ENTRY> GetRecordingEdl(const CPVRRecording &recording);

    /*!
     * @brief Check whether there is an active recording on the current channel.
     * @return True if there is, false otherwise.
     */
    bool IsRecordingOnPlayingChannel(void) const;

    /*!
     * @brief Check whether the current channel can be recorded instantly.
     * @return True if it can, false otherwise.
     */
    bool CanRecordInstantly(void);

    //@}

    /*! @name EPG methods */
    //@{

    /*!
     * @brief Get the EPG table for a channel.
     * @param channel The channel to get the EPG table for.
     * @param epg Store the EPG in this container.
     * @param start Get entries after this start time.
     * @param end Get entries before this end time.
     * @param error An error if it occured.
     * @return True if the EPG was transfered successfully, false otherwise.
     */
    PVR_ERROR GetEPGForChannel(const CPVRChannelPtr &channel, CPVREpg *epg, time_t start, time_t end);

    /*!
     * Tell the client the time frame to use when notifying epg events back to Kodi. The client might push epg events asynchronously
     * to Kodi using the callback function EpgEventStateChange. To be able to only push events that are actually of interest for Kodi,
     * client needs to know about the epg time frame Kodi uses.
     * @param iDays number of days from "now". EPG_TIMEFRAME_UNLIMITED means that Kodi is interested in all epg events, regardless of event times.
     * @return PVR_ERROR_NO_ERROR if new value was successfully set.
     */
    PVR_ERROR SetEPGTimeFrame(int iDays);

    /*
     * @brief Check if an epg tag can be recorded
     * @param tag The epg tag
     * @param bIsRecordable Set to true if the tag can be recorded
     * @return PVR_ERROR_NO_ERROR if bIsRecordable has been set successfully.
     */
    PVR_ERROR IsRecordable(const CConstPVREpgInfoTagPtr &tag, bool &bIsRecordable) const;

    /*
     * @brief Check if an epg tag can be played
     * @param tag The epg tag
     * @param bIsPlayable Set to true if the tag can be played
     * @return PVR_ERROR_NO_ERROR if bIsPlayable has been set successfully.
     */
    PVR_ERROR IsPlayable(const CConstPVREpgInfoTagPtr &tag, bool &bIsPlayable) const;

    /*!
     * @brief Fill the file item for an epg tag with the properties required for playback. Values are obtained from the PVR backend.
     * @param fileItem The file item to be filled.
     * @return True if the stream properties have been set, false otherwiese.
     */
    bool FillEpgTagStreamFileItem(CFileItem &fileItem);

    /*!
     * @brief Set the epg tag that is currently playing.
     * @param epgTag The tag that is currently playing.
     */
    void SetPlayingEpgTag(const CPVREpgInfoTagPtr epgTag);

    /*!
     * @brief Clear the epg tag that is currently playing, if any.
     */
    void ClearPlayingEpgTag();

    /*!
     * @brief Get the epg tag that is currently playing.
     * @return The tag that is currently playing, NULL otherwise.
     */
    CPVREpgInfoTagPtr GetPlayingEpgTag(void) const;

    /*!
     * @return True if an epg tag is playing, false otherwise.
     */
    bool IsPlayingEpgTag(void) const;

    //@}

    /*! @name Channel methods */
    //@{

    /*!
     * @brief Get all channels from backends.
     * @param group The container to store the channels in.
     * @param error An error if it occured.
     * @return The amount of channels that were added.
     */
    PVR_ERROR GetChannels(CPVRChannelGroupInternal *group);

    /*!
     * @brief Get all channel groups from backends.
     * @param groups Store the channel groups in this container.
     * @param error An error if it occured.
     * @return The amount of groups that were added.
     */
    PVR_ERROR GetChannelGroups(CPVRChannelGroups *groups);

    /*!
     * @brief Get all group members of a channel group.
     * @param group The group to get the member for.
     * @param error An error if it occured.
     * @return The amount of channels that were added.
     */
    PVR_ERROR GetChannelGroupMembers(CPVRChannelGroup *group);

    //@}

    /*! @name Menu hook methods */
    //@{

    /*!
     * @brief Check whether a client has any PVR specific menu entries.
     * @param iClientId The ID of the client to get the menu entries for. Get the menu for the active channel if iClientId < 0.
     * @return True if the client has any menu hooks, false otherwise.
     */
    bool HasMenuHooks(int iClientId, PVR_MENUHOOK_CAT cat);

    //@}

    /*! @name Channel scan methods */
    //@{

    /*!
     * @return All clients that support channel scanning.
     */
    std::vector<PVR_CLIENT> GetClientsSupportingChannelScan(void) const;

    //@}

    /*! @name Channel settings methods */
    //@{

    /*!
     * @return All clients that support channel settings inside addon.
     */
    std::vector<PVR_CLIENT> GetClientsSupportingChannelSettings(bool bRadio) const;

    /*!
     * @brief Open addon settings dialog to add a channel
     * @param channel The channel to edit.
     * @return PVR_ERROR_NO_ERROR if the dialog was opened successfully, the respective error code otherwise.
     */
    PVR_ERROR OpenDialogChannelAdd(const CPVRChannelPtr &channel);

    /*!
     * @brief Open addon settings dialog to related channel
     * @param channel The channel to edit.
     * @return PVR_ERROR_NO_ERROR if the dialog was opened successfully, the respective error code otherwise.
     */
    PVR_ERROR OpenDialogChannelSettings(const CPVRChannelPtr &channel);

    /*!
     * @brief Inform addon to delete channel
     * @param channel The channel to delete.
     * @return PVR_ERROR_NO_ERROR if the channel was deleted successfully, the respective error code otherwise.
     */
    PVR_ERROR DeleteChannel(const CPVRChannelPtr &channel);

    /*!
     * @brief Request the client to rename given channel
     * @param channel The channel to rename
     * @return True if the edit was successful, false otherwise.
     */
    bool RenameChannel(const CPVRChannelPtr &channel);

    //@}

    bool GetClient(const std::string &strId, ADDON::AddonPtr &addon) const;

    /*!
     * @brief Query the the given client's capabilities.
     * @param iClientId The client id
     * @return The capabilities.
     */
    CPVRClientCapabilities GetClientCapabilities(int iClientId) const;

    bool GetPlayingClient(PVR_CLIENT &client) const;

    std::string GetBackendHostnameByClientId(int iClientId) const;

    bool IsTimeshifting() const;
    time_t GetPlayingTime() const;
    time_t GetBufferTimeStart() const;
    time_t GetBufferTimeEnd() const;

    bool GetStreamTimes(PVR_STREAM_TIMES *times) const;

    int GetClientId(const std::string& strId) const;

    bool IsRealTimeStream() const;

    void ConnectionStateChange(CPVRClient *client, std::string &strConnectionString, PVR_CONNECTION_STATE newState,
                               std::string &strMessage);

    /*!
     * @brief Propagate event to clients
     */
    void OnSystemSleep();
    void OnSystemWake();
    void OnPowerSavingActivated();
    void OnPowerSavingDeactivated();

  private:
    /*!
     * @brief Get the instance of the client.
     * @param iClientId The id of the client to get.
     * @param addon The client.
     * @return True if the client was found, false otherwise.
     */
    bool GetClient(int iClientId, PVR_CLIENT &addon) const;

    /*!
     * @brief Check whether a client is registered.
     * @param client The client to check.
     * @return True if this client is registered, false otherwise.
     */
    bool IsKnownClient(const ADDON::AddonPtr &client) const;

    int GetClientId(const ADDON::AddonPtr &client) const;

    int                   m_playingClientId;          /*!< the ID of the client that is currently playing */
    bool                  m_bIsPlayingLiveTV;
    bool                  m_bIsPlayingRecording;
    bool                  m_bIsPlayingEpgTag;
    std::string           m_strPlayingClientName;     /*!< the name client that is currently playing a stream or an empty string if nothing is playing */
    PVR_CLIENTMAP         m_clientMap;                /*!< a map of all known clients */
    CCriticalSection      m_critSection;
    std::map<std::string, int> m_addonNameIds; /*!< map add-on names to IDs */
  };
}
