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

#include <memory>
#include <string>
#include <vector>

#include "XBDateTime.h"
#include "addons/kodi-addon-dev-kit/include/kodi/xbmc_pvr_types.h"
#include "utils/ISerializable.h"

#include "pvr/PVRTypes.h"
#include "pvr/channels/PVRChannel.h"
#include "pvr/recordings/PVRRecording.h"
#include "pvr/timers/PVRTimerInfoTag.h"

#define EPG_DEBUGGING 0

class CVariant;

namespace PVR
{
  class CPVREpg;

  class CPVREpgInfoTag : public ISerializable, public std::enable_shared_from_this<CPVREpgInfoTag>
  {
    friend class CPVREpg;
    friend class CPVREpgDatabase;

  public:
    /*!
     * @brief Create a new empty event .
     */
    static CPVREpgInfoTagPtr CreateDefaultTag();

    /*!
     * @brief Create a new EPG infotag with 'data' as content.
     * @param data The tag's content.
     */
    CPVREpgInfoTag(const EPG_TAG &data, int iClientId);

  private:
    /*!
     * @brief Create a new empty event.
     */
    CPVREpgInfoTag(void);

    /*!
     * @brief Create a new empty event without a unique ID.
     */
    CPVREpgInfoTag(CPVREpg *epg, const PVR::CPVRChannelPtr &channel, const std::string &strTableName = "", const std::string &strIconPath = "");

    CPVREpgInfoTag(const CPVREpgInfoTag &tag) = delete;
    CPVREpgInfoTag &operator =(const CPVREpgInfoTag &other) = delete;

  public:
    ~CPVREpgInfoTag() override = default;

    bool operator ==(const CPVREpgInfoTag& right) const;
    bool operator !=(const CPVREpgInfoTag& right) const;

    void Serialize(CVariant &value) const override;

    /*!
     * @brief Get the identifier of the client that serves this event.
     * @return The identifier.
     */
    int ClientID(void) const { return m_iClientId; }

    /*!
     * @brief Check if this event is currently active.
     * @return True if it's active, false otherwise.
     */
    bool IsActive(void) const;

    /*!
     * @return True when this event has already passed, false otherwise.
     */
    bool WasActive(void) const;

    /*!
     * @return True when this event is an upcoming event, false otherwise.
     */
    bool IsUpcoming(void) const;

    /*!
     * @return The current progress of this tag.
     */
    float ProgressPercentage(void) const;

    /*!
     * @return The current progress of this tag in seconds.
     */
    int Progress(void) const;

    /*!
     * @brief Get a pointer to the next event. Set by CPVREpg in a call to Sort()
     * @return A pointer to the next event or NULL if it's not set.
     */
    CPVREpgInfoTagPtr GetNextEvent(void) const;

    /*!
     * @brief The table this event belongs to
     * @return The table this event belongs to
     */
    const CPVREpg *GetTable() const;

    int EpgID(void) const;

    /*!
     * @brief Sets the epg reference of this event
     * @param epg The epg item
     */
    void SetEpg(CPVREpg *epg);

    /*!
     * @brief Change the unique broadcast ID of this event.
     * @param iUniqueBroadcastId The new unique broadcast ID.
     */
    void SetUniqueBroadcastID(unsigned int iUniqueBroadcastID);

    /*!
     * @brief Get the unique broadcast ID.
     * @return The unique broadcast ID.
     */
    unsigned int UniqueBroadcastID(void) const;

    /*!
     * @brief Get the event's database ID.
     * @return The database ID.
     */
    int BroadcastId(void) const;

    /*!
     * @brief Get the unique ID of the channel this event belongs to.
     * @return The unique channel ID.
     */
    unsigned int UniqueChannelID(void) const;

    /*!
     * @brief Get the event's start time.
     * @return The new start time.
     */
    CDateTime StartAsUTC(void) const;
    CDateTime StartAsLocalTime(void) const;

    /*!
     * @brief Get the event's end time.
     * @return The new start time.
     */
    CDateTime EndAsUTC(void) const;
    CDateTime EndAsLocalTime(void) const;

    /*!
     * @brief Change the event's end time.
     * @param end The new end time.
     */
    void SetEndFromUTC(const CDateTime &end);

    /*!
     * @brief Get the duration of this event in seconds.
     * @return The duration in seconds.
     */
    int GetDuration(void) const;

    /*!
     * @brief Check whether this event is parental locked.
     * @return True if whether this event is parental locked, false otherwise.
     */
    bool IsParentalLocked() const;

    /*!
     * @brief Get the title of this event.
     * @param bOverrideParental True to override parental control, false check it.
     * @return The title.
     */
    std::string Title(bool bOverrideParental = false) const;

    /*!
     * @brief Get the plot outline of this event.
     * @param bOverrideParental True to override parental control, false check it.
     * @return The plot outline.
     */
    std::string PlotOutline(bool bOverrideParental = false) const;

    /*!
     * @brief Get the plot of this event.
     * @param bOverrideParental True to override parental control, false check it.
     * @return The plot.
     */
    std::string Plot(bool bOverrideParental = false) const;

    /*!
     * @brief Get the originaltitle of this event.
     * @return The originaltitle.
     */
    std::string OriginalTitle(bool bOverrideParental = false) const;

    /*!
     * @brief Get the cast of this event.
     * @return The cast.
     */
    std::string Cast() const;

    /*!
     * @brief Get the director of this event.
     * @return The director.
     */
    std::string Director() const;

    /*!
     * @brief Get the writer of this event.
     * @return The writer.
     */
    std::string Writer() const;

    /*!
     * @brief Get the year of this event.
     * @return The year.
     */
    int Year() const;

    /*!
     * @brief Get the imdbnumber of this event.
     * @return The imdbnumber.
     */
    std::string IMDBNumber() const;

    /*!
     * @brief Get the genre type ID of this event.
     * @return The genre type ID.
     */
    int GenreType(void) const;

    /*!
     * @brief Get the genre subtype ID of this event.
     * @return The genre subtype ID.
     */
    int GenreSubType(void) const;

    /*!
     * @brief Get the genre as human readable string.
     * @return The genre.
     */
    const std::vector<std::string> Genre(void) const;

    /*!
     * @brief Get the first air date of this event.
     * @return The first air date.
     */
    CDateTime FirstAiredAsUTC(void) const;
    CDateTime FirstAiredAsLocalTime(void) const;

    /*!
     * @brief Get the parental rating of this event.
     * @return The parental rating.
     */
    int ParentalRating(void) const;

    /*!
     * @brief Get the star rating of this event.
     * @return The star rating.
     */
    int StarRating(void) const;

    /*!
     * @brief Notify on start if true.
     * @return Notify on start.
     */
    bool Notify(void) const;

    /*!
     * @brief The series number of this event.
     * @return The series number.
     */
    int SeriesNumber(void) const;

    /*!
     * @brief The series link for this event.
     * @return The series link or empty string, if not available.
     */
    std::string SeriesLink() const;

    /*!
     * @brief The episode number of this event.
     * @return The episode number.
     */
    int EpisodeNumber(void) const;

    /*!
     * @brief The episode part number of this event.
     * @return The episode part number.
     */
    int EpisodePart(void) const;

    /*!
     * @brief The episode name of this event.
     * @return The episode name.
     */
    std::string EpisodeName(void) const;

    /*!
     * @brief Get the path to the icon for this event.
     * @return The path to the icon
     */
    std::string Icon(void) const;

    /*!
     * @brief The path to this event.
     * @return The path.
     */
    std::string Path(void) const;

    /*!
     * @brief Set a timer for this event.
     * @param timer The timer.
     */
    void SetTimer(const PVR::CPVRTimerInfoTagPtr &timer);

    /*!
     * @brief Clear the timer for this event.
     */
    void ClearTimer(void);

    /*!
     * @brief Check whether this event has an active timer tag.
     * @return True if it has an active timer tag, false if not.
     */
    bool HasTimer(void) const;

    /*!
     * @brief Check whether this event has an active timer rule.
     * @return True if it has an active timer rule, false if not.
     */
    bool HasTimerRule(void) const;

    /*!
     * @brief Get a pointer to the timer for event or NULL if there is none.
     * @return A pointer to the timer for event or NULL if there is none.
     */
    PVR::CPVRTimerInfoTagPtr Timer(void) const;

    /*!
     * @brief Set a recording for this event or NULL to clear it.
     * @param recording The recording value.
     */
    void SetRecording(const PVR::CPVRRecordingPtr &recording);

    /*!
     * @brief Clear a recording for this event.
     */
    void ClearRecording(void);

    /*!
     * @brief Check whether this event has a recording tag.
     * @return True if it has a recording tag, false if not.
     */
    bool HasRecording(void) const;

    /*!
     * @brief Get a pointer to the recording for event or NULL if there is none.
     * @return A pointer to the recording for event or NULL if there is none.
     */
    PVR::CPVRRecordingPtr Recording(void) const;

    /*!
     * @brief Check if this event can be recorded.
     * @return True if it can be recorded, false otherwise.
     */
    bool IsRecordable(void) const;

    /*!
     * @brief Check if this event can be played.
     * @return True if it can be played, false otherwise.
     */
    bool IsPlayable(void) const;

    /*!
     * @brief Change the channel tag of this epg tag
     * @param channel The new channel
     */
    void SetChannel(const PVR::CPVRChannelPtr &channel);

    /*!
     * @return True if this tag has a PVR channel set.
     */
    bool HasChannel(void) const;

    int ChannelNumber(void) const;

    std::string ChannelName(void) const;

    /*!
     * @brief Get the channel that plays this event.
     * @return a pointer to the channel.
     */
    const PVR::CPVRChannelPtr Channel(void) const;

    /*!
     * @brief Persist this tag in the database.
     * @param bSingleUpdate True if this is a single update, false if more updates will follow.
     * @return True if the tag was persisted correctly, false otherwise.
     */
    bool Persist(bool bSingleUpdate = true);

    /*!
     * @brief Update the information in this tag with the info in the given tag.
     * @param tag The new info.
     * @param bUpdateBroadcastId If set to false, the tag BroadcastId (locally unique) will not be checked/updated
     * @return True if something changed, false otherwise.
     */
    bool Update(const CPVREpgInfoTag &tag, bool bUpdateBroadcastId = true);

    /*!
     * @return True if this tag has any series attributes, false otherwise
     */
    bool IsSeries() const;

    /*!
     * @brief Return the flags (EPG_TAG_FLAG_*) of this event as a bitfield.
     * @return the flags.
     */
    unsigned int Flags() const { return m_iFlags; }

  private:

    /*!
     * @brief Change the genre of this event.
     * @param iGenreType The genre type ID.
     * @param iGenreSubType The genre subtype ID.
     */
    void SetGenre(int iGenreType, int iGenreSubType, const char* strGenre);

    /*!
     * @brief Hook that is called when the start date changed.
     */
    void UpdatePath(void);

    /*!
     * @brief Get current time, taking timeshifting into account.
     */
    CDateTime GetCurrentPlayingTime(void) const;

    bool                     m_bNotify;            /*!< notify on start */
    int                      m_iClientId;          /*!< client id */
    int                      m_iBroadcastId;       /*!< database ID */
    int                      m_iGenreType;         /*!< genre type */
    int                      m_iGenreSubType;      /*!< genre subtype */
    int                      m_iParentalRating;    /*!< parental rating */
    int                      m_iStarRating;        /*!< star rating */
    int                      m_iSeriesNumber;      /*!< series number */
    int                      m_iEpisodeNumber;     /*!< episode number */
    int                      m_iEpisodePart;       /*!< episode part number */
    unsigned int             m_iUniqueBroadcastID; /*!< unique broadcast ID */
    unsigned int             m_iUniqueChannelID;   /*!< unique channel ID */
    std::string              m_strTitle;           /*!< title */
    std::string              m_strPlotOutline;     /*!< plot outline */
    std::string              m_strPlot;            /*!< plot */
    std::string              m_strOriginalTitle;   /*!< original title */
    std::string              m_strCast;            /*!< cast */
    std::string              m_strDirector;        /*!< director */
    std::string              m_strWriter;          /*!< writer */
    int                      m_iYear;              /*!< year */
    std::string              m_strIMDBNumber;      /*!< imdb number */
    std::vector<std::string> m_genre;              /*!< genre */
    std::string              m_strEpisodeName;     /*!< episode name */
    std::string              m_strIconPath;        /*!< the path to the icon */
    std::string              m_strFileNameAndPath; /*!< the filename and path */
    CDateTime                m_startTime;          /*!< event start time */
    CDateTime                m_endTime;            /*!< event end time */
    CDateTime                m_firstAired;         /*!< first airdate */

    PVR::CPVRTimerInfoTagPtr m_timer;

    CPVREpg *                m_epg;                /*!< the schedule that this event belongs to */

    unsigned int             m_iFlags;             /*!< the flags applicable to this EPG entry */
    std::string              m_strSeriesLink;      /*!< series link */

    CCriticalSection         m_critSection;
    PVR::CPVRChannelPtr      m_channel;
    PVR::CPVRRecordingPtr    m_recording;
  };
}
