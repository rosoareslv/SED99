/*
 *  Copyright (C) 2012-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <map>
#include <memory>
#include <queue>
#include <vector>

#include "XBDateTime.h"
#include "addons/kodi-addon-dev-kit/include/kodi/xbmc_pvr_types.h"
#include "threads/Thread.h"
#include "utils/Observer.h"

#include "pvr/PVRSettings.h"
#include "pvr/PVRTypes.h"
#include "pvr/timers/PVRTimerInfoTag.h"

class CFileItem;
class CFileItemList;
typedef std::shared_ptr<CFileItem> CFileItemPtr;

namespace PVR
{
  class CPVRTimersPath;

  class CPVRTimersContainer
  {
  public:
    /*!
     * @brief Add a timer tag to this container or update the tag if already present in this container.
     * @param The timer tag
     * @return True, if the update was successful. False, otherwise.
     */
    bool UpdateFromClient(const CPVRTimerInfoTagPtr &timer);

    /*!
     * @brief Get the timer tag denoted by given client id and timer id.
     * @param iClientId The client id.
     * @param iClientIndex The timer id.
     * @return the timer tag if found, null otherwise.
     */
    CPVRTimerInfoTagPtr GetByClient(int iClientId, int iClientIndex) const;

    typedef std::vector<CPVRTimerInfoTagPtr> VecTimerInfoTag;
    typedef std::map<CDateTime, VecTimerInfoTag> MapTags;

    /*!
     * @brief Get the timertags map.
     * @return The map.
     */
    const MapTags& GetTags() const { return m_tags; }

  protected:
    void InsertEntry(const std::shared_ptr<CPVRTimerInfoTag>& newTimer);

    mutable CCriticalSection m_critSection;
    unsigned int m_iLastId = 0;
    MapTags m_tags;
  };

  class CPVRTimers : public CPVRTimersContainer, public Observer, private CThread
  {
  public:
    CPVRTimers(void);
    ~CPVRTimers(void) override;

    /**
     * @brief (re)load the timers from the clients.
     * @return True if loaded successfully, false otherwise.
     */
    bool Load(void);

    /**
     * @brief unload all timers.
     */
    void Unload();

    /**
     * @brief refresh the timer list from the clients.
     */
    bool Update(void);

    /**
     * @brief load the local timers from database.
     * @return True if loaded successfully, false otherwise.
     */
    bool LoadFromDatabase(void);

    /*!
     * @param bIgnoreReminders include or ignore reminders
     * @return The tv or radio timer that will be active next (state scheduled), or an empty fileitemptr if none.
     */
    std::shared_ptr<CPVRTimerInfoTag> GetNextActiveTimer(bool bIgnoreReminders = true) const;

    /*!
     * @return The tv timer that will be active next (state scheduled), or nullptr if none.
     */
    std::shared_ptr<CPVRTimerInfoTag> GetNextActiveTVTimer(void) const;

    /*!
     * @return The radio timer that will be active next (state scheduled), or nullptr if none.
     */
    std::shared_ptr<CPVRTimerInfoTag> GetNextActiveRadioTimer(void) const;

    /*!
     * @return All timers that are active (states scheduled or recording)
     */
    std::vector<std::shared_ptr<CPVRTimerInfoTag>> GetActiveTimers(void) const;

    /*!
     * @return Next due reminder, if any. Removes it from the queue of due reminders.
     */
    std::shared_ptr<CPVRTimerInfoTag> GetNextReminderToAnnnounce();

    /*!
     * Get all timers
     * @param items The list to add the timers to
     */
    void GetAll(CFileItemList& items) const;

    /*!
     * @return The amount of tv and radio timers that are active (states scheduled or recording)
     */
    int AmountActiveTimers(void) const;

    /*!
     * @return The amount of tv timers that are active (states scheduled or recording)
     */
    int AmountActiveTVTimers(void) const;

    /*!
     * @return The amount of radio timers that are active (states scheduled or recording)
     */
    int AmountActiveRadioTimers(void) const;

    /*!
     * @return All tv and radio timers that are recording
     */
    std::vector<std::shared_ptr<CPVRTimerInfoTag>> GetActiveRecordings() const;

    /*!
     * @return All tv timers that are recording
     */
    std::vector<std::shared_ptr<CPVRTimerInfoTag>> GetActiveTVRecordings() const;

    /*!
     * @return All radio timers that are recording
     */
    std::vector<std::shared_ptr<CPVRTimerInfoTag>> GetActiveRadioRecordings() const;

    /*!
     * @return True when recording, false otherwise.
     */
    bool IsRecording(void) const;

    /*!
     * @brief Check if a recording is running on the given channel.
     * @param channel The channel to check.
     * @return True when recording, false otherwise.
     */
    bool IsRecordingOnChannel(const CPVRChannel &channel) const;

    /*!
     * @brief Obtain the active timer for a given channel.
     * @param channel The channel to check.
     * @return the timer, null otherwise.
     */
    CPVRTimerInfoTagPtr GetActiveTimerForChannel(const CPVRChannelPtr &channel) const;

    /*!
     * @return The amount of tv and radio timers that are currently recording
     */
    int AmountActiveRecordings(void) const;

    /*!
     * @return The amount of tv timers that are currently recording
     */
    int AmountActiveTVRecordings(void) const;

    /*!
     * @return The amount of radio timers that are currently recording
     */
    int AmountActiveRadioRecordings(void) const;

    /*!
     * @brief Get all timers for the given path.
     * @param strPath The vfs path to get the timers for.
     * @param items The results.
     * @return True when the path was valid, false otherwise.
     */
    bool GetDirectory(const std::string& strPath, CFileItemList &items) const;

    /*!
     * @brief Delete all timers on a channel.
     * @param channel The channel to delete the timers for.
     * @param bDeleteTimerRules True to delete timer rules too, false otherwise.
     * @param bCurrentlyActiveOnly True to delete timers that are currently running only.
     * @return True if timers any were deleted, false otherwise.
     */
    bool DeleteTimersOnChannel(const CPVRChannelPtr &channel, bool bDeleteTimerRules = true, bool bCurrentlyActiveOnly = false);

    /*!
     * @return Next event time (timer or daily wake up)
     */
    CDateTime GetNextEventTime(void) const;

    /*!
     * @brief Add a timer to the client. Doesn't add the timer to the container. The backend will do this.
     * @param tag The timer to add.
     * @return True if timer add request was sent correctly, false if not.
     */
    bool AddTimer(const std::shared_ptr<CPVRTimerInfoTag>& tag);

    /*!
     * @brief Delete a timer on the client. Doesn't delete the timer from the container. The backend will do this.
     * @param tag The timer to delete.
     * @param bForce Control what to do in case the timer is currently recording.
     *        True to force to delete the timer, false to return TimerDeleteResult::RECORDING.
     * @param bDeleteRule Also delete the timer rule that scheduled the timer instead of single timer only.
     * @return The result.
     */
    TimerOperationResult DeleteTimer(const std::shared_ptr<CPVRTimerInfoTag>& tag, bool bForce = false, bool bDeleteRule = false);

    /*!
     * @brief Rename a timer on the client. Doesn't update the timer in the container. The backend will do this.
     * @param tag The timer to rename.
     * @return True if timer rename request was sent correctly, false if not.
     */
    bool RenameTimer(const std::shared_ptr<CPVRTimerInfoTag>& tag, const std::string &strNewName);

    /*!
     * @brief Update the timer on the client. Doesn't update the timer in the container. The backend will do this.
     * @param tag The timer to update.
     * @return True if timer update request was sent correctly, false if not.
     */
    bool UpdateTimer(const std::shared_ptr<CPVRTimerInfoTag>& tag);

    /*!
     * @brief Get the timer tag that matches the given epg tag.
     * @param epgTag The epg tag.
     * @return The requested timer tag, or nullptr if none was found.
     */
    CPVRTimerInfoTagPtr GetTimerForEpgTag(const CPVREpgInfoTagPtr &epgTag) const;

    /*!
     * Get the timer rule for a given timer tag
     * @param timer The timer to query the timer rule for
     * @return The timer rule, or nullptr if none was found.
     */
    CPVRTimerInfoTagPtr GetTimerRule(const CPVRTimerInfoTagPtr &timer) const;

    /*!
     * Get the timer rule for a given timer tag
     * @param item The timer to query the timer rule for
     * @return The timer rule, or an empty fileitemptr if none was found.
     */
    CFileItemPtr GetTimerRule(const CFileItemPtr &item) const;

    /*!
     * @brief Update the channel pointers.
     */
    void UpdateChannels(void);

    void Notify(const Observable &obs, const ObservableMessage msg) override;

    /*!
     * Get a timer tag given it's unique ID
     * @param iTimerId The ID to find
     * @return The tag, or an empty one when not found
     */
    CPVRTimerInfoTagPtr GetById(unsigned int iTimerId) const;

  private:
    void Process() override;

    void RemoveEntry(const std::shared_ptr<CPVRTimerInfoTag>& tag);
    bool UpdateEntries(const CPVRTimersContainer &timers, const std::vector<int> &failedClients);
    bool UpdateEntries(int iMaxNotificationDelay);
    std::shared_ptr<CPVRTimerInfoTag> UpdateEntry(const std::shared_ptr<CPVRTimerInfoTag>& timer);
    bool GetRootDirectory(const CPVRTimersPath &path, CFileItemList &items) const;
    bool GetSubDirectory(const CPVRTimersPath &path, CFileItemList &items) const;

    bool AddLocalTimer(const std::shared_ptr<CPVRTimerInfoTag>& tag, bool bNotify);
    bool DeleteLocalTimer(const std::shared_ptr<CPVRTimerInfoTag>& tag, bool bNotify);
    bool RenameLocalTimer(const std::shared_ptr<CPVRTimerInfoTag>& tag, const std::string& strNewName);
    bool UpdateLocalTimer(const std::shared_ptr<CPVRTimerInfoTag>& tag);
    std::shared_ptr<CPVRTimerInfoTag> PersistAndUpdateLocalTimer(const std::shared_ptr<CPVRTimerInfoTag>& timer,
                                                                 const std::shared_ptr<CPVRTimerInfoTag>& parentTimer);
    void NotifyTimersEvent(bool bAddedOrDeleted = true);

    enum TimerKind
    {
      TimerKindAny = 0,
      TimerKindTV,
      TimerKindRadio
    };

    bool KindMatchesTag(const TimerKind &eKind, const CPVRTimerInfoTagPtr &tag) const;

    std::shared_ptr<CPVRTimerInfoTag> GetNextActiveTimer(const TimerKind& eKind, bool bIgnoreReminders) const;
    int AmountActiveTimers(const TimerKind &eKind) const;
    std::vector<std::shared_ptr<CPVRTimerInfoTag>> GetActiveRecordings(const TimerKind& eKind) const;
    int AmountActiveRecordings(const TimerKind &eKind) const;

    bool m_bIsUpdating = false;
    CPVRSettings m_settings;
    std::queue<std::shared_ptr<CPVRTimerInfoTag>> m_remindersToAnnounce;
    bool m_bReminderRulesUpdatePending = false;
  };
}
