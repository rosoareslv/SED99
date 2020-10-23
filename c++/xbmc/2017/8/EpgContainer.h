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

#include <map>

#include "XBDateTime.h"
#include "threads/CriticalSection.h"
#include "threads/Thread.h"
#include "utils/Observer.h"

#include "pvr/PVRSettings.h"
#include "pvr/epg/Epg.h"
#include "pvr/epg/EpgDatabase.h"

class CFileItemList;
class CGUIDialogProgressBarHandle;

namespace PVR
{
  class CEpgUpdateRequest;
  class CEpgTagStateChange;

  class CPVREpgContainer : public Observer, public Observable, private CThread
  {
    friend class CPVREpgDatabase;

  public:
    /*!
     * @brief Create a new EPG table container.
     */
    CPVREpgContainer(void);

    /*!
     * @brief Destroy this instance.
     */
    ~CPVREpgContainer(void) override;

    /*!
     * @brief Get a pointer to the database instance.
     * @return A pointer to the database instance.
     */
    CPVREpgDatabase *GetDatabase(void) { return &m_database; }

    /*!
     * @brief Start the EPG update thread.
     * @param bAsync Should the EPG container starts asynchronously
     */
    void Start(bool bAsync);

    /*!
     * @brief Stop the EPG update thread.
     * @return
     */
    bool Stop(void);

    /*!
     * @brief Clear all EPG entries.
     * @param bClearDb Clear the database too if true.
     */
    void Clear(bool bClearDb = false);

    /*!
     * @brief Stop the update thread and unload all data.
     */
    void Unload(void);

    /*!
     * @brief Clear the EPG and all it's database entries.
     */
    void Reset(void) { Clear(true); }

    /*!
     * @brief Check whether the EpgContainer has fully started.
     * @return True if started, false otherwise.
     */
    bool IsStarted(void) const;

    /*!
     * @brief Delete an EPG table from this container.
     * @param epg The table to delete.
     * @param bDeleteFromDatabase Delete this table from the database too if true.
     * @return
     */
    bool DeleteEpg(const CPVREpg &epg, bool bDeleteFromDatabase = false);

    /*!
     * @brief Process a notification from an observable.
     * @param obs The observable that sent the update.
     * @param msg The update message.
     */
    void Notify(const Observable &obs, const ObservableMessage msg) override;

    CPVREpgPtr CreateChannelEpg(const PVR::CPVRChannelPtr &channel);

    /*!
     * @brief Get all EPG tables and apply a filter.
     * @param results The fileitem list to store the results in.
     * @param filter The filter to apply.
     * @return The amount of entries that were added.
     */
    int GetEPGSearch(CFileItemList &results, const CPVREpgSearchFilter &filter);

    /*!
     * @brief Get the start time of the first entry.
     * @return The start time.
     */
    const CDateTime GetFirstEPGDate(void);

    /*!
      * @brief Get the end time of the last entry.
      * @return The end time.
      */
    const CDateTime GetLastEPGDate(void);

    /*!
     * @brief Get an EPG table given it's ID.
     * @param iEpgId The database ID of the table.
     * @return The table or NULL if it wasn't found.
     */
    CPVREpgPtr GetById(int iEpgId) const;

    /*!
     * @brief Get the EPG event with the given event id
     * @param channel The channel to get the event for.
     * @param iBroadcastId The event id to get
     * @return The requested event, or an empty tag when not found
     */
    CPVREpgInfoTagPtr GetTagById(const PVR::CPVRChannelPtr &channel, unsigned int iBroadcastId) const;

    /*!
     * @brief Get the EPG events matching the given timer
     * @param timer The timer to get the matching events for.
     * @return The matching events, or an empty vector when no matching tag was found
     */
    std::vector<CPVREpgInfoTagPtr> GetEpgTagsForTimer(const PVR::CPVRTimerInfoTagPtr &timer) const;

    /*!
     * @brief Notify EPG table observers when the currently active tag changed.
     * @return True if the check was done, false if it was not the right time to check
     */
    bool CheckPlayingEvents(void);

    /*!
     * @brief The next EPG ID to be given to a table when the db isn't being used.
     * @return The next ID.
     */
    unsigned int NextEpgId(void);

    /*!
     * @brief Close the progress bar if it's visible.
     */
    void CloseProgressDialog(void);

    /*!
     * @brief Show the progress bar
     * @param bUpdating True if updating epg entries, false if just loading them from db
     */
    void ShowProgressDialog(bool bUpdating = true);

    /*!
     * @brief Update the progress bar.
     * @param iCurrent The current position.
     * @param iMax The maximum position.
     * @param strText The text to display.
     */
    void UpdateProgressDialog(int iCurrent, int iMax, const std::string &strText);

    /*!
     * @return True to not to store EPG entries in the database.
     */
    bool IgnoreDB() const;

    /*!
     * @brief Wait for an EPG update to finish.
     * @param bInterrupt True to interrupt a running update.
     */
    void WaitForUpdateFinish(bool bInterrupt = true);

    /*!
     * @brief Set to true to prevent updates.
     * @param bSetTo The new value.
     */
    void PreventUpdates(bool bSetTo = true) { m_bPreventUpdates = bSetTo;  }

    /*!
     * @brief Notify EPG container that there are pending manual EPG updates
     * @param bHasPendingUpdates The new value
     */
    void SetHasPendingUpdates(bool bHasPendingUpdates = true);

    /*!
     * @brief Call Persist() on each table
     * @return True when they all were persisted, false otherwise.
     */
    bool PersistAll(void);

    /*!
     * @brief A client triggered an epg update request for a channel
     * @param iClientID The id of the client which triggered the update request
     * @param iUniqueChannelID The uid of the channel for which the epg shall be updated
     */
    void UpdateRequest(int iClientID, unsigned int iUniqueChannelID);

    /*!
     * @brief A client announced an updated epg tag for a channel
     * @param tag The epg tag containing the updated data
     * @param eNewState The kind of change (CREATED, UPDATED, DELETED)
     */
    void UpdateFromClient(const CPVREpgInfoTagPtr tag, EPG_EVENT_STATE eNewState);

    /*!
     * @brief Get the number of past days to show in the guide and to import from backends.
     * @return the number of past epg days.
     */
    int GetPastDaysToDisplay() const;

    /*!
     * @brief Get the number of future days to show in the guide and to import from backends.
     * @return the number of future epg days.
     */
    int GetFutureDaysToDisplay() const;

  private:
    /*!
     * @brief Load the EPG settings.
     * @return True if the settings were loaded successfully, false otherwise.
     */
    bool LoadSettings(void);

    /*!
     * @brief Remove old EPG entries.
     * @return True if the old entries were removed successfully, false otherwise.
     */
    bool RemoveOldEntries(void);

    /*!
     * @brief Load and update the EPG data.
     * @param bOnlyPending Only check and update EPG tables with pending manual updates
     * @return True if the update has not been interrupted, false otherwise.
     */
    bool UpdateEPG(bool bOnlyPending = false);

    /*!
     * @return True if a running update should be interrupted, false otherwise.
     */
    bool InterruptUpdate(void) const;

    /*!
     * @brief EPG update thread
     */
    void Process(void) override;

    /*!
     * @brief Load all tables from the database
     */
    void LoadFromDB(void);

    void InsertFromDatabase(int iEpgID, const std::string &strName, const std::string &strScraperName);

    CPVREpgDatabase m_database; /*!< the EPG database */

    /** @name Class state properties */
    //@{
    bool         m_bIsUpdating;            /*!< true while an update is running */
    bool         m_bIsInitialising;        /*!< true while the epg manager hasn't loaded all tables */
    bool         m_bStarted;               /*!< true if EpgContainer has fully started */
    bool         m_bLoaded;                /*!< true after epg data is initially loaded from the database */
    bool         m_bPreventUpdates;        /*!< true to prevent EPG updates */
    int          m_pendingUpdates;         /*!< count of pending manual updates */
    time_t       m_iLastEpgCleanup;        /*!< the time the EPG was cleaned up */
    time_t       m_iNextEpgUpdate;         /*!< the time the EPG will be updated */
    time_t       m_iNextEpgActiveTagCheck; /*!< the time the EPG will be checked for active tag updates */
    unsigned int m_iNextEpgId;             /*!< the next epg ID that will be given to a new table when the db isn't being used */
    EPGMAP       m_epgs;                   /*!< the EPGs in this container */
    //@}

    CGUIDialogProgressBarHandle *  m_progressHandle; /*!< the progress dialog that is visible when updating the first time */
    CCriticalSection               m_critSection;    /*!< a critical section for changes to this container */
    CEvent                         m_updateEvent;    /*!< trigger when an update finishes */

    std::list<CEpgUpdateRequest> m_updateRequests; /*!< list of update requests triggered by addon */
    CCriticalSection m_updateRequestsLock;         /*!< protect update requests */

    std::list<CEpgTagStateChange> m_epgTagChanges; /*!< list of updated epg tags announced by addon */
    CCriticalSection m_epgTagChangesLock;          /*!< protect changed epg tags list */

    bool m_bUpdateNotificationPending; /*!< true while an epg updated notification to observers is pending. */
    CPVRSettings m_settings;
  };
}
