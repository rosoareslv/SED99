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

#include "Application.h"
#include "dialogs/GUIDialogOK.h"
#include "dialogs/GUIDialogYesNo.h"
#include "epg/EpgInfoTag.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "messaging/ApplicationMessenger.h"
#include "utils/StringUtils.h"
#include "utils/Variant.h"

#include "pvr/PVRGUIActions.h"
#include "pvr/PVRManager.h"
#include "pvr/addons/PVRClients.h"
#include "pvr/channels/PVRChannelGroupsContainer.h"
#include "pvr/timers/PVRTimerInfoTag.h"
#include "pvr/windows/GUIWindowPVRSearch.h"

#include "GUIDialogPVRGuideInfo.h"

#include <utility>

using namespace PVR;
using namespace EPG;
using namespace KODI::MESSAGING;

#define CONTROL_BTN_FIND                4
#define CONTROL_BTN_SWITCH              5
#define CONTROL_BTN_RECORD              6
#define CONTROL_BTN_OK                  7
#define CONTROL_BTN_PLAY_RECORDING      8
#define CONTROL_BTN_ADD_TIMER           9

CGUIDialogPVRGuideInfo::CGUIDialogPVRGuideInfo(void)
    : CGUIDialog(WINDOW_DIALOG_PVR_GUIDE_INFO, "DialogPVRInfo.xml")
{
}

CGUIDialogPVRGuideInfo::~CGUIDialogPVRGuideInfo(void)
{
}

bool CGUIDialogPVRGuideInfo::OnClickButtonOK(CGUIMessage &message)
{
  bool bReturn = false;

  if (message.GetSenderId() == CONTROL_BTN_OK)
  {
    Close();
    bReturn = true;
  }

  return bReturn;
}

bool CGUIDialogPVRGuideInfo::OnClickButtonRecord(CGUIMessage &message)
{
  bool bReturn = false;

  if (message.GetSenderId() == CONTROL_BTN_RECORD)
  {
    bReturn = true;

    if (!m_progItem || !m_progItem->HasPVRChannel())
    {
      /* invalid channel */
      CGUIDialogOK::ShowAndGetInput(CVariant{19033}, CVariant{19067});
      Close();
      return bReturn;
    }

    const CPVRTimerInfoTagPtr timerTag(m_progItem->Timer());
    if (timerTag)
    {
      const CFileItemPtr item(new CFileItem(timerTag));
      if (timerTag->IsRecording())
        bReturn = CPVRGUIActions::GetInstance().StopRecording(item);
      else
        bReturn = CPVRGUIActions::GetInstance().DeleteTimer(item);
    }
    else
    {
      const CFileItemPtr item(new CFileItem(m_progItem));
      bReturn = CPVRGUIActions::GetInstance().AddTimer(item, false);
    }
  }

  if (bReturn)
    Close();

  return bReturn;
}

bool CGUIDialogPVRGuideInfo::OnClickButtonAddTimer(CGUIMessage &message)
{
  bool bReturn = false;

  if (message.GetSenderId() == CONTROL_BTN_ADD_TIMER)
  {
    if (m_progItem && !m_progItem->Timer())
    {
      const CFileItemPtr item(new CFileItem(m_progItem));
      bReturn = CPVRGUIActions::GetInstance().AddTimerRule(item, true);
    }
  }

  if (bReturn)
    Close();

  return bReturn;
}

bool CGUIDialogPVRGuideInfo::OnClickButtonPlay(CGUIMessage &message)
{
  bool bReturn = false;

  if (message.GetSenderId() == CONTROL_BTN_SWITCH || message.GetSenderId() == CONTROL_BTN_PLAY_RECORDING)
  {
    Close();

    const CFileItemPtr item(new CFileItem(m_progItem));
    if (message.GetSenderId() == CONTROL_BTN_PLAY_RECORDING)
      CPVRGUIActions::GetInstance().PlayRecording(item, true /* bCheckResume */);
    else
      CPVRGUIActions::GetInstance().SwitchToChannel(item, true /* bCheckResume */);

    bReturn = true;
  }

  return bReturn;
}

bool CGUIDialogPVRGuideInfo::OnClickButtonFind(CGUIMessage &message)
{
  bool bReturn = false;

  if (message.GetSenderId() == CONTROL_BTN_FIND)
    return CPVRGUIActions::GetInstance().FindSimilar(CFileItemPtr(new CFileItem(m_progItem)), this);

  return bReturn;
}

bool CGUIDialogPVRGuideInfo::OnMessage(CGUIMessage& message)
{
  switch (message.GetMessage())
  {
  case GUI_MSG_CLICKED:
    return OnClickButtonOK(message) ||
           OnClickButtonRecord(message) ||
           OnClickButtonPlay(message) ||
           OnClickButtonFind(message) ||
           OnClickButtonAddTimer(message);
  }

  return CGUIDialog::OnMessage(message);
}

bool CGUIDialogPVRGuideInfo::OnInfo(int actionID)
{
  Close();
  return true;
}

void CGUIDialogPVRGuideInfo::SetProgInfo(const EPG::CEpgInfoTagPtr &tag)
{
  m_progItem = tag;
}

CFileItemPtr CGUIDialogPVRGuideInfo::GetCurrentListItem(int offset)
{
  return CFileItemPtr(new CFileItem(m_progItem));
}

void CGUIDialogPVRGuideInfo::OnInitWindow()
{
  CGUIDialog::OnInitWindow();

  if (!m_progItem)
  {
    /* no epg event selected */
    return;
  }

  if (!m_progItem->HasRecording())
  {
    /* not recording. hide the play recording button */
    SET_CONTROL_HIDDEN(CONTROL_BTN_PLAY_RECORDING);
  }

  bool bHideRecord(true);
  bool bHideAddTimer(true);

  if (m_progItem->HasTimer())
  {
    if (m_progItem->Timer()->IsRecording())
    {
      SET_CONTROL_LABEL(CONTROL_BTN_RECORD, 19059); /* Stop recording */
      bHideRecord = false;
    }
    else if (m_progItem->Timer()->HasTimerType() && !m_progItem->Timer()->GetTimerType()->IsReadOnly())
    {
      SET_CONTROL_LABEL(CONTROL_BTN_RECORD, 19060); /* Delete timer */
      bHideRecord = false;
    }
  }
  else if (g_PVRClients->SupportsTimers() && m_progItem->EndAsLocalTime() > CDateTime::GetCurrentDateTime())
  {
    SET_CONTROL_LABEL(CONTROL_BTN_RECORD, 264);     /* Record */
    bHideRecord = false;
    bHideAddTimer = false;
  }

  if (bHideRecord)
    SET_CONTROL_HIDDEN(CONTROL_BTN_RECORD);

  if (bHideAddTimer)
    SET_CONTROL_HIDDEN(CONTROL_BTN_ADD_TIMER);
}
