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

#include "Epg.h"

#include <utility>

#include "addons/kodi-addon-dev-kit/include/kodi/xbmc_epg_types.h"
#include "EpgContainer.h"
#include "EpgDatabase.h"
#include "ServiceBroker.h"
#include "guilib/LocalizeStrings.h"
#include "settings/AdvancedSettings.h"
#include "settings/Settings.h"
#include "threads/SingleLock.h"
#include "utils/log.h"

#include "pvr/PVRManager.h"
#include "pvr/addons/PVRClients.h"
#include "pvr/recordings/PVRRecordings.h"
#include "pvr/timers/PVRTimers.h"

using namespace PVR;

CPVREpg::CPVREpg(int iEpgID, const std::string &strName /* = "" */, const std::string &strScraperName /* = "" */, bool bLoadedFromDb /* = false */) :
    m_bChanged(!bLoadedFromDb),
    m_bTagsChanged(false),
    m_bLoaded(false),
    m_bUpdatePending(false),
    m_iEpgID(iEpgID),
    m_strName(strName),
    m_strScraperName(strScraperName),
    m_bUpdateLastScanTime(false)
{
}

CPVREpg::CPVREpg(const CPVRChannelPtr &channel, bool bLoadedFromDb /* = false */) :
    m_bChanged(!bLoadedFromDb),
    m_bTagsChanged(false),
    m_bLoaded(false),
    m_bUpdatePending(false),
    m_iEpgID(channel->EpgID()),
    m_strName(channel->ChannelName()),
    m_strScraperName(channel->EPGScraper()),
    m_pvrChannel(channel),
    m_bUpdateLastScanTime(false)
{
}

CPVREpg::CPVREpg(void) :
    m_bChanged(false),
    m_bTagsChanged(false),
    m_bLoaded(false),
    m_bUpdatePending(false),
    m_iEpgID(0),
    m_bUpdateLastScanTime(false)
{
}

CPVREpg::~CPVREpg(void)
{
  Clear();
}

CPVREpg &CPVREpg::operator =(const CPVREpg &right)
{
  m_bChanged          = right.m_bChanged;
  m_bTagsChanged      = right.m_bTagsChanged;
  m_bLoaded           = right.m_bLoaded;
  m_bUpdatePending    = right.m_bUpdatePending;
  m_iEpgID            = right.m_iEpgID;
  m_strName           = right.m_strName;
  m_strScraperName    = right.m_strScraperName;
  m_nowActiveStart    = right.m_nowActiveStart;
  m_lastScanTime      = right.m_lastScanTime;
  m_pvrChannel        = right.m_pvrChannel;

  for (std::map<CDateTime, CPVREpgInfoTagPtr>::const_iterator it = right.m_tags.begin(); it != right.m_tags.end(); ++it)
    m_tags.insert(make_pair(it->first, it->second));

  return *this;
}

/** @name Public methods */
//@{

void CPVREpg::SetName(const std::string &strName)
{
  CSingleLock lock(m_critSection);

  if (m_strName != strName)
  {
    m_bChanged = true;
    m_strName = strName;
  }
}

void CPVREpg::SetUpdatePending(bool bUpdatePending /* = true */)
{
  {
    CSingleLock lock(m_critSection);
    m_bUpdatePending = bUpdatePending;
  }

  if (bUpdatePending)
    CServiceBroker::GetPVRManager().EpgContainer().SetHasPendingUpdates(true);
}

void CPVREpg::ForceUpdate(void)
{
  SetUpdatePending();
}

bool CPVREpg::HasValidEntries(void) const
{
  CSingleLock lock(m_critSection);

  return (m_iEpgID > 0 && /* valid EPG ID */
      !m_tags.empty()  && /* contains at least 1 tag */
      m_tags.rbegin()->second->EndAsUTC() >= CDateTime::GetCurrentDateTime().GetAsUTCDateTime()); /* the last end time hasn't passed yet */
}

void CPVREpg::Clear(void)
{
  CSingleLock lock(m_critSection);
  m_tags.clear();
}

void CPVREpg::Cleanup(void)
{
  int iPastDays = CServiceBroker::GetPVRManager().EpgContainer().GetPastDaysToDisplay();
  const CDateTime cleanupTime = CDateTime::GetUTCDateTime() - CDateTimeSpan(iPastDays, 0, 0, 0);
  Cleanup(cleanupTime);
}

void CPVREpg::Cleanup(const CDateTime &Time)
{
  CSingleLock lock(m_critSection);
  for (std::map<CDateTime, CPVREpgInfoTagPtr>::iterator it = m_tags.begin(); it != m_tags.end();)
  {
    if (it->second->EndAsUTC() < Time)
    {
      if (m_nowActiveStart == it->first)
        m_nowActiveStart.SetValid(false);

      it->second->ClearTimer();
      it->second->ClearRecording();
      it = m_tags.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

CPVREpgInfoTagPtr CPVREpg::GetTagNow(bool bUpdateIfNeeded /* = true */) const
{
  CSingleLock lock(m_critSection);
  if (m_nowActiveStart.IsValid())
  {
    std::map<CDateTime, CPVREpgInfoTagPtr>::const_iterator it = m_tags.find(m_nowActiveStart);
    if (it != m_tags.end() && it->second->IsActive())
      return it->second;
  }

  if (bUpdateIfNeeded)
  {
    CPVREpgInfoTagPtr lastActiveTag;

    /* one of the first items will always match if the list is sorted */
    for (std::map<CDateTime, CPVREpgInfoTagPtr>::const_iterator it = m_tags.begin(); it != m_tags.end(); ++it)
    {
      if (it->second->IsActive())
      {
        m_nowActiveStart = it->first;
        return it->second;
      }
      else if (it->second->WasActive())
        lastActiveTag = it->second;
    }

    /* there might be a gap between the last and next event. return the last if found and it ended not more than 5 minutes ago */
    if (lastActiveTag &&
        lastActiveTag->EndAsUTC() + CDateTimeSpan(0, 0, 5, 0) >= CDateTime::GetUTCDateTime())
      return lastActiveTag;
  }

  return CPVREpgInfoTagPtr();
}

CPVREpgInfoTagPtr CPVREpg::GetTagNext() const
{
  CPVREpgInfoTagPtr nowTag(GetTagNow());
  if (nowTag)
  {
    CSingleLock lock(m_critSection);
    std::map<CDateTime, CPVREpgInfoTagPtr>::const_iterator it = m_tags.find(nowTag->StartAsUTC());
    if (it != m_tags.end() && ++it != m_tags.end())
      return it->second;
  }
  else if (Size() > 0)
  {
    /* return the first event that is in the future */
    for (std::map<CDateTime, CPVREpgInfoTagPtr>::const_iterator it = m_tags.begin(); it != m_tags.end(); ++it)
    {
      if (it->second->IsUpcoming())
        return it->second;
    }
  }

  return CPVREpgInfoTagPtr();
}

bool CPVREpg::CheckPlayingEvent(void)
{
  CPVREpgInfoTagPtr previousTag(GetTagNow(false));
  CPVREpgInfoTagPtr newTag(GetTagNow(true));

  bool bTagChanged = newTag && (!previousTag || *previousTag != *newTag);
  bool bTagRemoved = !newTag && previousTag;
  if (bTagChanged || bTagRemoved)
  {
    NotifyObservers(ObservableMessageEpgActiveItem);
    return true;
  }
  return false;
}

CPVREpgInfoTagPtr CPVREpg::GetTagByBroadcastId(unsigned int iUniqueBroadcastId) const
{
  if (iUniqueBroadcastId != EPG_TAG_INVALID_UID)
  {
    CSingleLock lock(m_critSection);
    for (const auto &infoTag : m_tags)
    {
      if (infoTag.second->UniqueBroadcastID() == iUniqueBroadcastId)
        return infoTag.second;
    }
  }
  return CPVREpgInfoTagPtr();
}

CPVREpgInfoTagPtr CPVREpg::GetTagBetween(const CDateTime &beginTime, const CDateTime &endTime) const
{
  CSingleLock lock(m_critSection);
  for (std::map<CDateTime, CPVREpgInfoTagPtr>::const_iterator it = m_tags.begin(); it != m_tags.end(); ++it)
  {
    if (it->second->StartAsUTC() >= beginTime && it->second->EndAsUTC() <= endTime)
      return it->second;
  }

  return CPVREpgInfoTagPtr();
}

std::vector<CPVREpgInfoTagPtr> CPVREpg::GetTagsBetween(const CDateTime &beginTime, const CDateTime &endTime) const
{
  std::vector<CPVREpgInfoTagPtr> epgTags;

  CSingleLock lock(m_critSection);
  for (const auto &infoTag : m_tags)
  {
    if (infoTag.second->StartAsUTC() >= beginTime)
    {
      if (infoTag.second->EndAsUTC() <= endTime)
        epgTags.emplace_back(infoTag.second);
      else
        break; // done.
    }
  }

  return epgTags;
}

void CPVREpg::AddEntry(const CPVREpgInfoTag &tag)
{
  CPVREpgInfoTagPtr newTag;
  CPVRChannelPtr channel;
  {
    CSingleLock lock(m_critSection);
    std::map<CDateTime, CPVREpgInfoTagPtr>::iterator itr = m_tags.find(tag.StartAsUTC());
    if (itr != m_tags.end())
      newTag = itr->second;
    else
    {
      newTag.reset(new CPVREpgInfoTag(this, m_pvrChannel, m_strName, m_pvrChannel ? m_pvrChannel->IconPath() : ""));
      m_tags.insert(make_pair(tag.StartAsUTC(), newTag));
    }

    channel = m_pvrChannel;
  }

  if (newTag)
  {
    newTag->Update(tag);
    newTag->SetChannel(channel);
    newTag->SetEpg(this);
    newTag->SetTimer(CServiceBroker::GetPVRManager().Timers()->GetTimerForEpgTag(newTag));
    newTag->SetRecording(CServiceBroker::GetPVRManager().Recordings()->GetRecordingForEpgTag(newTag));
  }
}

bool CPVREpg::Load(void)
{
  bool bReturn(false);
  CPVREpgDatabase *database = CServiceBroker::GetPVRManager().EpgContainer().GetDatabase();

  if (!database || !database->IsOpen())
  {
    CLog::Log(LOGERROR, "EPG - %s - could not open the database", __FUNCTION__);
    return bReturn;
  }

  CSingleLock lock(m_critSection);
  int iEntriesLoaded = database->Get(*this);
  if (iEntriesLoaded <= 0)
  {
    CLog::Log(LOGDEBUG, "EPG - %s - no database entries found for table '%s'.", __FUNCTION__, m_strName.c_str());
  }
  else
  {
    m_lastScanTime = GetLastScanTime();
#if EPG_DEBUGGING
    CLog::Log(LOGDEBUG, "EPG - %s - %d entries loaded for table '%s'.", __FUNCTION__, (int) m_tags.size(), m_strName.c_str());
#endif
    bReturn = true;
  }

  m_bLoaded = true;

  return bReturn;
}

bool CPVREpg::UpdateEntries(const CPVREpg &epg, bool bStoreInDb /* = true */)
{
  CSingleLock lock(m_critSection);
#if EPG_DEBUGGING
  CLog::Log(LOGDEBUG, "EPG - {0} - {1} entries in memory before merging", __FUNCTION__, m_tags.size());
#endif
  /* copy over tags */
  for (std::map<CDateTime, CPVREpgInfoTagPtr>::const_iterator it = epg.m_tags.begin(); it != epg.m_tags.end(); ++it)
    UpdateEntry(it->second, bStoreInDb);

#if EPG_DEBUGGING
  CLog::Log(LOGDEBUG, "EPG - {0} - {1} entries in memory after merging and before fixing", __FUNCTION__, m_tags.size());
#endif
  FixOverlappingEvents(bStoreInDb);

#if EPG_DEBUGGING
  CLog::Log(LOGDEBUG, "EPG - {0} - {1} entries in memory after fixing", __FUNCTION__, m_tags.size());
#endif
  /* update the last scan time of this table */
  m_lastScanTime = CDateTime::GetCurrentDateTime().GetAsUTCDateTime();
  m_bUpdateLastScanTime = true;

  SetChanged(true);
  lock.Leave();

  NotifyObservers(ObservableMessageEpg);

  return true;
}

CDateTime CPVREpg::GetLastScanTime(void)
{
  CDateTime lastScanTime;
  {
    CSingleLock lock(m_critSection);

    if (!m_lastScanTime.IsValid())
    {
      if (!CServiceBroker::GetSettings().GetBool(CSettings::SETTING_EPG_IGNOREDBFORCLIENT))
      {
        CPVREpgDatabase *database = CServiceBroker::GetPVRManager().EpgContainer().GetDatabase();
        CDateTime dtReturn; dtReturn.SetValid(false);

        if (database && database->IsOpen())
          database->GetLastEpgScanTime(m_iEpgID, &m_lastScanTime);
      }

      if (!m_lastScanTime.IsValid())
      {
        m_lastScanTime.SetDateTime(1970, 1, 1, 0, 0, 0);
        assert(m_lastScanTime.IsValid());
      }
    }
    lastScanTime = m_lastScanTime;
  }

  return m_lastScanTime;
}

bool CPVREpg::UpdateEntry(const EPG_TAG *data, int iClientId, bool bUpdateDatabase)
{
  if (!data)
    return false;

  CPVREpgInfoTagPtr tag(new CPVREpgInfoTag(*data, iClientId));
  return UpdateEntry(tag, bUpdateDatabase);
}

bool CPVREpg::UpdateEntry(const CPVREpgInfoTagPtr &tag, bool bUpdateDatabase)
{
  CPVREpgInfoTagPtr infoTag;

  {
    CSingleLock lock(m_critSection);
    std::map<CDateTime, CPVREpgInfoTagPtr>::iterator it = m_tags.find(tag->StartAsUTC());
    bool bNewTag(false);
    if (it != m_tags.end())
    {
      infoTag = it->second;
    }
    else
    {
      infoTag.reset(new CPVREpgInfoTag(this, m_pvrChannel, m_strName, m_pvrChannel ? m_pvrChannel->IconPath() : ""));
      infoTag->SetUniqueBroadcastID(tag->UniqueBroadcastID());
      m_tags.insert(std::make_pair(tag->StartAsUTC(), infoTag));
      bNewTag = true;
    }

    infoTag->Update(*tag, bNewTag);
    infoTag->SetEpg(this);
    infoTag->SetChannel(m_pvrChannel);

    if (bUpdateDatabase)
      m_changedTags.insert(std::make_pair(infoTag->UniqueBroadcastID(), infoTag));
  }

  infoTag->SetTimer(CServiceBroker::GetPVRManager().Timers()->GetTimerForEpgTag(infoTag));
  infoTag->SetRecording(CServiceBroker::GetPVRManager().Recordings()->GetRecordingForEpgTag(infoTag));

  return true;
}

bool CPVREpg::UpdateEntry(const CPVREpgInfoTagPtr &tag, EPG_EVENT_STATE newState, bool bUpdateDatabase)
{
  bool bRet(true);
  bool bNotify(true);

  if (newState == EPG_EVENT_CREATED || newState == EPG_EVENT_UPDATED)
  {
    bRet = UpdateEntry(tag, bUpdateDatabase);
  }
  else if (newState == EPG_EVENT_DELETED)
  {
    CSingleLock lock(m_critSection);

    auto it = m_tags.begin();
    for (; it != m_tags.end(); ++it)
    {
      if (it->second->UniqueBroadcastID() == tag->UniqueBroadcastID())
        break;
    }

    if (it == m_tags.end())
    {
      bRet = false;
    }
    else
    {
      // Respect epg linger time.
      int iPastDays = CServiceBroker::GetPVRManager().EpgContainer().GetPastDaysToDisplay();
      const CDateTime cleanupTime(CDateTime::GetUTCDateTime() - CDateTimeSpan(iPastDays, 0, 0, 0));
      if (it->second->EndAsUTC() < cleanupTime)
      {
        if (bUpdateDatabase)
          m_deletedTags.insert(std::make_pair(it->second->UniqueBroadcastID(), it->second));

        it->second->ClearTimer();
        it->second->ClearRecording();
        m_tags.erase(it);
      }
      else
      {
        bNotify = false;
      }
    }
  }
  else
  {
    CLog::Log(LOGERROR, "EPG - %s - unknown epg event state value: %d", __FUNCTION__, newState);
    bRet = false;
  }

  if (bRet && bNotify)
  {
    SetChanged();
    NotifyObservers(ObservableMessageEpgItemUpdate);
  }

  return bRet;
}

bool CPVREpg::Update(const time_t start, const time_t end, int iUpdateTime, bool bForceUpdate /* = false */)
{
  bool bGrabSuccess(true);
  bool bUpdate(false);

  /* load the entries from the db first */
  if (!m_bLoaded && !CServiceBroker::GetPVRManager().EpgContainer().IgnoreDB())
    Load();

  /* clean up if needed */
  if (m_bLoaded)
    Cleanup();

  /* get the last update time from the database */
  CDateTime lastScanTime = GetLastScanTime();

  /* enforce advanced settings update interval override for TV Channels with no EPG data */
  if (m_tags.empty() && !bUpdate && ChannelID() > 0 && !Channel()->IsRadio())
    iUpdateTime = g_advancedSettings.m_iEpgUpdateEmptyTagsInterval;

  if (!bForceUpdate)
  {
    /* check if we have to update */
    time_t iNow = 0;
    time_t iLastUpdate = 0;
    CDateTime::GetCurrentDateTime().GetAsUTCDateTime().GetAsTime(iNow);
    lastScanTime.GetAsTime(iLastUpdate);
    bUpdate = (iNow > iLastUpdate + iUpdateTime);
  }
  else
    bUpdate = true;

  if (bUpdate)
    bGrabSuccess = LoadFromClients(start, end);

  if (bGrabSuccess)
  {
    CPVRChannelPtr channel(CServiceBroker::GetPVRManager().GetCurrentChannel());
    if (channel &&
        channel->EpgID() == m_iEpgID)
      CServiceBroker::GetPVRManager().ResetPlayingTag();
    m_bLoaded = true;
  }
  else
    CLog::Log(LOGERROR, "EPG - %s - failed to update table '%s'", __FUNCTION__, Name().c_str());

  CSingleLock lock(m_critSection);
  m_bUpdatePending = false;

  return bGrabSuccess;
}

int CPVREpg::Get(CFileItemList &results) const
{
  int iInitialSize = results.Size();

  CSingleLock lock(m_critSection);

  for (std::map<CDateTime, CPVREpgInfoTagPtr>::const_iterator it = m_tags.begin(); it != m_tags.end(); ++it)
    results.Add(CFileItemPtr(new CFileItem(it->second)));

  return results.Size() - iInitialSize;
}

int CPVREpg::Get(CFileItemList &results, const CPVREpgSearchFilter &filter) const
{
  int iInitialSize = results.Size();

  if (!HasValidEntries())
    return -1;

  CSingleLock lock(m_critSection);

  for (std::map<CDateTime, CPVREpgInfoTagPtr>::const_iterator it = m_tags.begin(); it != m_tags.end(); ++it)
  {
    if (filter.FilterEntry(it->second))
      results.Add(CFileItemPtr(new CFileItem(it->second)));
  }

  return results.Size() - iInitialSize;
}

bool CPVREpg::Persist(void)
{
  if (CServiceBroker::GetSettings().GetBool(CSettings::SETTING_EPG_IGNOREDBFORCLIENT) || !NeedsSave())
    return true;

#if EPG_DEBUGGING
  CLog::Log(LOGDEBUG, "persist table '%s' (#%d) changed=%d deleted=%d", Name().c_str(), m_iEpgID, m_changedTags.size(), m_deletedTags.size());
#endif

  CPVREpgDatabase *database = CServiceBroker::GetPVRManager().EpgContainer().GetDatabase();
  if (!database || !database->IsOpen())
  {
    CLog::Log(LOGERROR, "EPG - %s - could not open the database", __FUNCTION__);
    return false;
  }

  {
    CSingleLock lock(m_critSection);
    if (m_iEpgID <= 0 || m_bChanged)
    {
      int iId = database->Persist(*this, m_iEpgID > 0);
      if (iId > 0)
        m_iEpgID = iId;
    }

    for (std::map<int, CPVREpgInfoTagPtr>::iterator it = m_deletedTags.begin(); it != m_deletedTags.end(); ++it)
      database->Delete(*it->second);

    for (std::map<int, CPVREpgInfoTagPtr>::iterator it = m_changedTags.begin(); it != m_changedTags.end(); ++it)
      it->second->Persist(false);

    if (m_bUpdateLastScanTime)
      database->PersistLastEpgScanTime(m_iEpgID, true);

    m_deletedTags.clear();
    m_changedTags.clear();
    m_bChanged            = false;
    m_bTagsChanged        = false;
    m_bUpdateLastScanTime = false;
  }

  return database->CommitInsertQueries();
}

CDateTime CPVREpg::GetFirstDate(void) const
{
  CDateTime first;

  CSingleLock lock(m_critSection);
  if (!m_tags.empty())
    first = m_tags.begin()->second->StartAsUTC();

  return first;
}

CDateTime CPVREpg::GetLastDate(void) const
{
  CDateTime last;

  CSingleLock lock(m_critSection);
  if (!m_tags.empty())
    last = m_tags.rbegin()->second->StartAsUTC();

  return last;
}

//@}

/** @name Private methods */
//@{

bool CPVREpg::FixOverlappingEvents(bool bUpdateDb /* = false */)
{
  bool bReturn(true);
  CPVREpgInfoTagPtr previousTag, currentTag;

  for (std::map<CDateTime, CPVREpgInfoTagPtr>::iterator it = m_tags.begin(); it != m_tags.end(); it != m_tags.end() ? it++ : it)
  {
    if (!previousTag)
    {
      previousTag = it->second;
      continue;
    }
    currentTag = it->second;

    if (previousTag->EndAsUTC() >= currentTag->EndAsUTC())
    {
      // delete the current tag. it's completely overlapped
      if (bUpdateDb)
        m_deletedTags.insert(make_pair(currentTag->UniqueBroadcastID(), currentTag));

      if (m_nowActiveStart == it->first)
        m_nowActiveStart.SetValid(false);

      it->second->ClearTimer();
      it->second->ClearRecording();
      m_tags.erase(it++);
    }
    else if (previousTag->EndAsUTC() > currentTag->StartAsUTC())
    {
      previousTag->SetEndFromUTC(currentTag->StartAsUTC());
      if (bUpdateDb)
        m_changedTags.insert(make_pair(previousTag->UniqueBroadcastID(), previousTag));

      previousTag = it->second;
    }
    else
    {
      previousTag = it->second;
    }
  }

  return bReturn;
}

bool CPVREpg::UpdateFromScraper(time_t start, time_t end)
{
  bool bGrabSuccess = false;
  if (ScraperName() == "client")
  {
    CPVRChannelPtr channel = Channel();
    if (!channel)
    {
      CLog::Log(LOGWARNING, "EPG - %s - channel not found, can't update", __FUNCTION__);
    }
    else if (!channel->EPGEnabled())
    {
#if EPG_DEBUGGING
      CLog::Log(LOGDEBUG, "EPG - %s - EPG updating disabled in the channel configuration", __FUNCTION__);
#endif
      bGrabSuccess = true;
    }
    else if (channel->IsHidden())
    {
#if EPG_DEBUGGING
      CLog::Log(LOGDEBUG, "EPG - %s - channel '%s' on client '%i' is hidden", __FUNCTION__, channel->ChannelName().c_str(), channel->ClientID());
#endif
      bGrabSuccess = true;
    }
    else if (!CServiceBroker::GetPVRManager().Clients()->GetClientCapabilities(channel->ClientID()).SupportsEPG())
    {
      CLog::Log(LOGDEBUG, "EPG - %s - the backend for channel '%s' on client '%i' does not support EPGs", __FUNCTION__, channel->ChannelName().c_str(), channel->ClientID());
    }
    else
    {
      CLog::Log(LOGDEBUG, "EPG - %s - updating EPG for channel '%s' from client '%i'", __FUNCTION__, channel->ChannelName().c_str(), channel->ClientID());
      bGrabSuccess = (CServiceBroker::GetPVRManager().Clients()->GetEPGForChannel(channel, this, start, end) == PVR_ERROR_NO_ERROR);
    }
  }
  else if (m_strScraperName.empty()) /* no grabber defined */
    CLog::Log(LOGWARNING, "EPG - %s - no EPG scraper defined for table '%s'", __FUNCTION__, m_strName.c_str());
  else
  {
    CLog::Log(LOGINFO, "EPG - %s - updating EPG table '%s' with scraper '%s'", __FUNCTION__, m_strName.c_str(), m_strScraperName.c_str());
    CLog::Log(LOGWARNING, "loading the EPG via scraper has not been implemented yet");
    //! @todo Add Support for Web EPG Scrapers here
  }

  return bGrabSuccess;
}

//@}

const std::string &CPVREpg::ConvertGenreIdToString(int iID, int iSubID)
{
  unsigned int iLabelId = 19499;
  switch (iID)
  {
    case EPG_EVENT_CONTENTMASK_MOVIEDRAMA:
      iLabelId = (iSubID <= 8) ? 19500 + iSubID : 19500;
      break;
    case EPG_EVENT_CONTENTMASK_NEWSCURRENTAFFAIRS:
      iLabelId = (iSubID <= 4) ? 19516 + iSubID : 19516;
      break;
    case EPG_EVENT_CONTENTMASK_SHOW:
      iLabelId = (iSubID <= 3) ? 19532 + iSubID : 19532;
      break;
    case EPG_EVENT_CONTENTMASK_SPORTS:
      iLabelId = (iSubID <= 11) ? 19548 + iSubID : 19548;
      break;
    case EPG_EVENT_CONTENTMASK_CHILDRENYOUTH:
      iLabelId = (iSubID <= 5) ? 19564 + iSubID : 19564;
      break;
    case EPG_EVENT_CONTENTMASK_MUSICBALLETDANCE:
      iLabelId = (iSubID <= 6) ? 19580 + iSubID : 19580;
      break;
    case EPG_EVENT_CONTENTMASK_ARTSCULTURE:
      iLabelId = (iSubID <= 11) ? 19596 + iSubID : 19596;
      break;
    case EPG_EVENT_CONTENTMASK_SOCIALPOLITICALECONOMICS:
      iLabelId = (iSubID <= 3) ? 19612 + iSubID : 19612;
      break;
    case EPG_EVENT_CONTENTMASK_EDUCATIONALSCIENCE:
      iLabelId = (iSubID <= 7) ? 19628 + iSubID : 19628;
      break;
    case EPG_EVENT_CONTENTMASK_LEISUREHOBBIES:
      iLabelId = (iSubID <= 7) ? 19644 + iSubID : 19644;
      break;
    case EPG_EVENT_CONTENTMASK_SPECIAL:
      iLabelId = (iSubID <= 3) ? 19660 + iSubID : 19660;
      break;
    case EPG_EVENT_CONTENTMASK_USERDEFINED:
      iLabelId = (iSubID <= 8) ? 19676 + iSubID : 19676;
      break;
    default:
      break;
  }

  return g_localizeStrings.Get(iLabelId);
}

bool CPVREpg::LoadFromClients(time_t start, time_t end)
{
  bool bReturn(false);
  CPVRChannelPtr channel = Channel();
  if (channel)
  {
    CPVREpg tmpEpg(channel);
    if (tmpEpg.UpdateFromScraper(start, end))
      bReturn = UpdateEntries(tmpEpg, !CServiceBroker::GetSettings().GetBool(CSettings::SETTING_EPG_IGNOREDBFORCLIENT));
  }
  else
  {
    CPVREpg tmpEpg(m_iEpgID, m_strName, m_strScraperName);
    if (tmpEpg.UpdateFromScraper(start, end))
      bReturn = UpdateEntries(tmpEpg, !CServiceBroker::GetSettings().GetBool(CSettings::SETTING_EPG_IGNOREDBFORCLIENT));
  }

  return bReturn;
}

CPVREpgInfoTagPtr CPVREpg::GetNextEvent(const CPVREpgInfoTag& tag) const
{
  CSingleLock lock(m_critSection);
  std::map<CDateTime, CPVREpgInfoTagPtr>::const_iterator it = m_tags.find(tag.StartAsUTC());
  if (it != m_tags.end() && ++it != m_tags.end())
    return it->second;

  CPVREpgInfoTagPtr retVal;
  return retVal;
}

CPVRChannelPtr CPVREpg::Channel(void) const
{
  CSingleLock lock(m_critSection);
  return m_pvrChannel;
}

int CPVREpg::ChannelID(void) const
{
  CSingleLock lock(m_critSection);
  return m_pvrChannel ? m_pvrChannel->ChannelID() : -1;
}

void CPVREpg::SetChannel(const PVR::CPVRChannelPtr &channel)
{
  CSingleLock lock(m_critSection);
  if (m_pvrChannel != channel)
  {
    if (channel)
    {
      SetName(channel->ChannelName());
      channel->SetEpgID(m_iEpgID);
    }
    m_pvrChannel = channel;
    for (std::map<CDateTime, CPVREpgInfoTagPtr>::iterator it = m_tags.begin(); it != m_tags.end(); ++it)
      it->second->SetChannel(m_pvrChannel);
  }
}

bool CPVREpg::UpdatePending(void) const
{
  CSingleLock lock(m_critSection);
  return m_bUpdatePending;
}

size_t CPVREpg::Size(void) const
{
  CSingleLock lock(m_critSection);
  return m_tags.size();
}

bool CPVREpg::NeedsSave(void) const
{
  CSingleLock lock(m_critSection);
  return !m_changedTags.empty() || !m_deletedTags.empty() || m_bChanged;
}

bool CPVREpg::IsValid(void) const
{
  CSingleLock lock(m_critSection);
  if (ScraperName() == "client")
    return m_pvrChannel != NULL;
  return true;
}

