#pragma once
/*
 *      Copyright (C) 2016 Team Kodi
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "pvr/PVRTypes.h"

#include <memory>
#include <string>

class CFileItem;
typedef std::shared_ptr<CFileItem> CFileItemPtr;

class CGUIWindow;

namespace PVR
{
  class CPVRGUIActions
  {
  public:
    /*!
     * @brief Request an instance of class CPVRGUIActions.
     * @return the instance.
     */
    static CPVRGUIActions& GetInstance();

    /*!
     * @brief Open a dialog with epg information for a given item.
     * @param item containing epg data to show. item must be an epg tag, a channel or a timer.
     * @return true on success, false otherwise.
     */
    bool ShowEPGInfo(const CFileItemPtr &item) const;

    /*!
     * @brief Open a window containing a list of epg tags 'similar' to a given item.
     * @param item containing epg data for matching. item must be an epg tag, a channel or a recording.
     * @param windowToClose is the window to close before opening the window with the search reasults.
     * @return true on success, false otherwise.
     */
    bool FindSimilar(const CFileItemPtr &item, CGUIWindow *windowToClose = nullptr) const;

    /*!
     * @brief Open the timer settings dialog to create a new tv or radio timer.
     * @param bRadio indicates whether a radio or tv timer shall be created.
     * @return true on success, false otherwise.
     */
    bool AddTimer(bool bRadio) const;

    /*!
     * @brief Create a new timer, either interactive or non-interactive.
     * @param item containing epg data to create a timer for. item must be an epg tag or a channel.
     * @param bShowTimerSettings is used to control whether a settings dialog will be opened prior creating the timer.
     * @return true, if the timer was created successfully, false otherwise.
     */
    bool AddTimer(const CFileItemPtr &item, bool bShowTimerSettings) const;

    /*!
     * @brief Create a new timer rule, either interactive or non-interactive.
     * @param item containing epg data to create a timer rule for. item must be an epg tag or a channel.
     * @param bShowTimerSettings is used to control whether a settings dialog will be opened prior creating the timer rule.
     * @return true, if the timer rule was created successfully, false otherwise.
     */
    bool AddTimerRule(const CFileItemPtr &item, bool bShowTimerSettings) const;

    /*!
     * @brief Creates or deletes a timer for the given epg tag.
     * @param item containing an epg tag.
     * @return true on success, false otherwise.
     */
    bool ToggleTimer(const CFileItemPtr &item) const;

    /*!
     * @brief Toggles a given timer's enabled/disabled state.
     * @param item containing a timer.
     * @return true on success, false otherwise.
     */
    bool ToggleTimerState(const CFileItemPtr &item) const;

    /*!
     * @brief Open the timer settings dialog to edit an existing timer.
     * @param item containing an epg tag or a timer.
     * @return true on success, false otherwise.
     */
    bool EditTimer(const CFileItemPtr &item) const;

    /*!
     * @brief Open the timer settings dialog to edit an existing timer rule.
     * @param item containing an epg tag or a timer.
     * @return true on success, false otherwise.
     */
    bool EditTimerRule(const CFileItemPtr &item) const;

    /*!
     * @brief Rename a timer, showing a text input dialog.
     * @param item containing a timer to rename.
     * @return true, if the timer was renamed successfully, false otherwise.
     */
    bool RenameTimer(const CFileItemPtr &item) const;

    /*!
     * @brief Delete a timer, always showing a confirmation dialog.
     * @param item containing a timer to delete. item must be a timer, an epg tag or a channel.
     * @return true, if the timer was deleted successfully, false otherwise.
     */
    bool DeleteTimer(const CFileItemPtr &item) const;

    /*!
     * @brief Delete a timer rule, always showing a confirmation dialog.
     * @param item containing a timer rule to delete. item must be a timer, an epg tag or a channel.
     * @return true, if the timer rule was deleted successfully, false otherwise.
     */
    bool DeleteTimerRule(const CFileItemPtr &item) const;

    /*!
     * @brief Open a dialog with information for a given recording.
     * @param item containing a recording.
     * @return true on success, false otherwise.
     */
    bool ShowRecordingInfo(const CFileItemPtr &item) const;

    /*!
     * @brief Stop a currently active recording, always showing a confirmation dialog.
     * @param item containing a recording to stop. item must be a timer, an epg tag or a channel.
     * @return true, if the recording was stopped successfully, false otherwise.
     */
    bool StopRecording(const CFileItemPtr &item) const;

    /*!
     * @brief Rename a recording, showing a text input dialog.
     * @param item containing a recording to rename.
     * @return true, if the recording was renamed successfully, false otherwise.
     */
    bool RenameRecording(const CFileItemPtr &item) const;

    /*!
     * @brief Delete a recording, always showing a confirmation dialog.
     * @param item containing a recording to delete.
     * @return true, if the recording was deleted successfully, false otherwise.
     */
    bool DeleteRecording(const CFileItemPtr &item) const;

    /*!
     * @brief Delete all recordings from trash, always showing a confirmation dialog.
     * @return true, if the recordings were permanently deleted successfully, false otherwise.
     */
    bool DeleteAllRecordingsFromTrash() const;

    /*!
     * @brief Undelete a recording.
     * @param item containing a recording to undelete.
     * @return true, if the recording was undeleted successfully, false otherwise.
     */
    bool UndeleteRecording(const CFileItemPtr &item) const;

    /*!
     * @brief Get a localized resume play label, if the given item can be resumed.
     * @param item containing a recording or an epg tag.
     * @return the localized resume play label that can be used for instance as context menu item label or an empty string if resume is not possible.
     */
    std::string GetResumeLabel(const CFileItem &item) const;

    /*!
     * @brief Resume a previously not completely played recording.
     * @param item containing a recording or an epg tag.
     * @param bFallbackToPlay controls whether playback of the recording should be started at the beginning ig no resume data are available.
     * @return true on success, false otherwise.
     */
    bool ResumePlayRecording(const CFileItemPtr &item, bool bFallbackToPlay) const;

    /*!
     * @brief Play recording.
     * @param item containing a recording or an epg tag.
     * @param bCheckResume controls resume check.
     * @return true on success, false otherwise.
     */
    bool PlayRecording(const CFileItemPtr &item, bool bCheckResume) const;

    /*!
     * @brief Switch channel.
     * @param item containing a channel or an epg tag.
     * @param bCheckResume controls resume check in case a recording for the current epg event is present.
     * @return true on success, false otherwise.
     */
    bool SwitchToChannel(const CFileItemPtr &item, bool bCheckResume) const;

    /*!
     * @brief Playback the given file item.
     * @param item containing a channel or a recording.
     * @return True if the item could be played, false otherwise.
     */
    bool PlayMedia(const CFileItemPtr &item) const;

    /*!
     * @brief Hide a channel, always showing a confirmation dialog.
     * @param item containing a channel or an epg tag.
     * @return true on success, false otherwise.
     */
    bool HideChannel(const CFileItemPtr &item) const;

  private:
    CPVRGUIActions() = default;
    CPVRGUIActions(const CPVRGUIActions&) = delete;
    CPVRGUIActions const& operator=(CPVRGUIActions const&) = delete;
    virtual ~CPVRGUIActions() {}

    /*!
     * @brief Open the timer settings dialog.
     * @param timer containing the timer the settings shall be displayed for.
     * @return true, if the dialog was ended successfully, false otherwise.
     */
    bool ShowTimerSettings(const CPVRTimerInfoTagPtr &timer) const;

    /*!
     * @brief Add a timer or timer rule, either interactive or non-interactive.
     * @param item containing epg data to create a timer or timer rule for. item must be an epg tag or a channel.
     * @param bCreateteRule denotes whether to create a one-shot timer or a timer rule.
     * @param bShowTimerSettings is used to control whether a settings dialog will be opened prior creating the timer or timer rule.
     * @return true, if the timer or timer rule was created successfully, false otherwise.
     */
    bool AddTimer(const CFileItemPtr &item, bool bCreateRule, bool bShowTimerSettings) const;

    /*!
     * @brief Delete a timer or timer rule, always showing a confirmation dialog.
     * @param item containing a timer or timer rule to delete. item must be a timer, an epg tag or a channel.
     * @param bIsRecording denotes whether the timer is currently recording (controls correct confirmation dialog).
     * @param bDeleteRule denotes to delete a timer rule. For convenience, one can pass a timer creted by a rule.
     * @return true, if the timer or timer rule was deleted successfully, false otherwise.
    */
    bool DeleteTimer(const CFileItemPtr &item, bool bIsRecording, bool bDeleteRule) const;

    /*!
     * @brief Open a dialog to confirm timer delete.
     * @param timer the timer to delete.
     * @param bDeleteRule in: ignored
     *                    out, for one shot timer scheduled by a timer rule: true to also delete the timer
     *                         rule that has scheduled this timer, false to only delete the one shot timer.
     *                    out, for one shot timer not scheduled by a timer rule: ignored
     * @return true, to proceed with delete, false otherwise.
     */
    bool ConfirmDeleteTimer(const CPVRTimerInfoTagPtr &timer, bool &bDeleteRule) const;

    /*!
     * @brief Open a dialog to confirm stop recording.
     * @param timer the recording to stop (actually the timer to delete).
     * @return true, to proceed with delete, false otherwise.
     */
    bool ConfirmStopRecording(const CPVRTimerInfoTagPtr &timer) const;

    /*!
     * @brief Open a dialog to confirm to delete a recording.
     * @param item the recording to delete.
     * @return true, to proceed with delete, false otherwise.
     */
    bool ConfirmDeleteRecording(const CFileItemPtr &item) const;

    /*!
     * @brief Open a dialog to confirm to permaantly remove all deleted recordings.
     * @return true, to proceed with delete, false otherwise.
     */
    bool ConfirmDeleteAllRecordingsFromTrash() const;

    /*!
     * @brief Check whether resume play is possible for a given item, display "resume from ..."/"play from start" context menu in case.
     * @param item containing a recording or an epg tag.
     * @return true, to play/resume the item, false otherwise.
     */
    bool CheckResumeRecording(const CFileItemPtr &item) const;

    /*!
     * @brief Check "play minimized" settings value and switch to fullscreen if not set.
     */
    void CheckAndSwitchToFullscreen() const;

    /*!
     * @brief Try a fast Live TV/Radio channel switch. Calls directly into active player instead of using messaging
     * @param channel the channel to switch to.
     * @return true if the switch was succesful, false otherwise.
     */
    bool TryFastChannelSwitch(const CPVRChannelPtr &channel) const;

    /*!
     * @brief Start playback of the given item.
     * @param item containing a channel or a recording.
     */
    void StartPlayback(CFileItem *item) const;
  };

} // namespace PVR
