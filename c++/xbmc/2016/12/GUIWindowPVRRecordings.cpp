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

#include "GUIInfoManager.h"
#include "ServiceBroker.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/GUIRadioButtonControl.h"
#include "guilib/GUIWindowManager.h"
#include "input/Key.h"
#include "settings/Settings.h"
#include "threads/SingleLock.h"
#include "utils/URIUtils.h"
#include "video/windows/GUIWindowVideoNav.h"

#include "pvr/PVRGUIActions.h"
#include "pvr/PVRManager.h"
#include "pvr/recordings/PVRRecordings.h"
#include "pvr/recordings/PVRRecordingsPath.h"

#include "GUIWindowPVRRecordings.h"

using namespace PVR;

CGUIWindowPVRRecordings::CGUIWindowPVRRecordings(bool bRadio) :
  CGUIWindowPVRBase(bRadio, bRadio ? WINDOW_RADIO_RECORDINGS : WINDOW_TV_RECORDINGS, "MyPVRRecordings.xml") ,
  m_bShowDeletedRecordings(false)
{
  g_infoManager.RegisterObserver(this);
}

CGUIWindowPVRRecordings::~CGUIWindowPVRRecordings()
{
  g_infoManager.UnregisterObserver(this);
}

void CGUIWindowPVRRecordings::OnWindowLoaded()
{
  CONTROL_SELECT(CONTROL_BTNGROUPITEMS);
}

std::string CGUIWindowPVRRecordings::GetDirectoryPath(void)
{
  const std::string basePath = CPVRRecordingsPath(m_bShowDeletedRecordings, m_bRadio);
  return URIUtils::PathHasParent(m_vecItems->GetPath(), basePath) ? m_vecItems->GetPath() : basePath;
}

void CGUIWindowPVRRecordings::GetContextButtons(int itemNumber, CContextButtons &buttons)
{
  if (itemNumber < 0 || itemNumber >= m_vecItems->Size())
    return;
  CFileItemPtr pItem = m_vecItems->Get(itemNumber);

  if (pItem->IsParentFolder())
  {
    // No context menu for ".." items
    return;
  }

  bool isDeletedRecording = false;

  CPVRRecordingPtr recording(pItem->GetPVRRecordingInfoTag());
  if (recording)
  {
    isDeletedRecording = recording->IsDeleted();

    if (isDeletedRecording)
    {
      if (m_vecItems->GetObjectCount() > 1)
        buttons.Add(CONTEXT_BUTTON_DELETE_ALL, 19292); /* Delete all permanently */
    }
  }

  if (!isDeletedRecording)
    CGUIWindowPVRBase::GetContextButtons(itemNumber, buttons);
}

bool CGUIWindowPVRRecordings::OnAction(const CAction &action)
{
  if (action.GetID() == ACTION_PARENT_DIR ||
      action.GetID() == ACTION_NAV_BACK)
  {
    CPVRRecordingsPath path(m_vecItems->GetPath());
    if (path.IsValid() && !path.IsRecordingsRoot())
    {
      GoParentFolder();
      return true;
    }
  }
  return CGUIWindowPVRBase::OnAction(action);
}

bool CGUIWindowPVRRecordings::OnContextButton(int itemNumber, CONTEXT_BUTTON button)
{
  if (itemNumber < 0 || itemNumber >= m_vecItems->Size())
    return false;
  CFileItemPtr pItem = m_vecItems->Get(itemNumber);

  return OnContextButtonDeleteAll(pItem.get(), button) ||
      CGUIMediaWindow::OnContextButton(itemNumber, button);
}

bool CGUIWindowPVRRecordings::Update(const std::string &strDirectory, bool updateFilterPath /* = true */)
{
  m_thumbLoader.StopThread();

  int iOldCount = m_vecItems->GetObjectCount();
  const std::string oldPath = m_vecItems->GetPath();

  bool bReturn = CGUIWindowPVRBase::Update(strDirectory);

  if (bReturn)
  {
    // TODO: does it make sense to show the non-deleted recordings, although user wants
    //       to see the deleted recordings? Or is this just another hack to avoid misbehavior
    //       of CGUIMediaWindow if it has no content?

    CSingleLock lock(m_critSection);

    /* empty list for deleted recordings */
    if (m_vecItems->GetObjectCount() == 0 && m_bShowDeletedRecordings)
    {
      /* show the normal recordings instead */
      m_bShowDeletedRecordings = false;
      lock.Leave();
      Update(GetDirectoryPath());
      return bReturn;
    }
  }

  if (bReturn && iOldCount > 0 && m_vecItems->GetObjectCount() == 0 && oldPath == m_vecItems->GetPath())
  {
    /* go to the parent folder if we're in a subdirectory and for instance just deleted the last item */
    const CPVRRecordingsPath path(m_vecItems->GetPath());
    if (path.IsValid() && !path.IsRecordingsRoot())
      GoParentFolder();
  }
  return bReturn;
}

void CGUIWindowPVRRecordings::UpdateButtons(void)
{
  bool bGroupRecordings = CServiceBroker::GetSettings().GetBool(CSettings::SETTING_PVRRECORD_GROUPRECORDINGS);
  SET_CONTROL_SELECTED(GetID(), CONTROL_BTNGROUPITEMS, bGroupRecordings);

  CGUIRadioButtonControl *btnShowDeleted = (CGUIRadioButtonControl*) GetControl(CONTROL_BTNSHOWDELETED);
  if (btnShowDeleted)
  {
    btnShowDeleted->SetVisible(m_bRadio ? g_PVRRecordings->HasDeletedRadioRecordings() : g_PVRRecordings->HasDeletedTVRecordings());
    btnShowDeleted->SetSelected(m_bShowDeletedRecordings);
  }

  CGUIWindowPVRBase::UpdateButtons();
  SET_CONTROL_LABEL(CONTROL_LABEL_HEADER1, m_bShowDeletedRecordings ? g_localizeStrings.Get(19179) : ""); /* Deleted recordings trash */

  const CPVRRecordingsPath path(m_vecItems->GetPath());
  SET_CONTROL_LABEL(CONTROL_LABEL_HEADER2, bGroupRecordings && path.IsValid() ? path.GetUnescapedDirectoryPath() : "");
}

bool CGUIWindowPVRRecordings::OnMessage(CGUIMessage &message)
{
  bool bReturn = false;
  switch (message.GetMessage())
  {
    case GUI_MSG_CLICKED:
      if (message.GetSenderId() == m_viewControl.GetCurrentControl())
      {
        int iItem = m_viewControl.GetSelectedItem();
        if (iItem >= 0 && iItem < m_vecItems->Size())
        {
          const CFileItemPtr item(m_vecItems->Get(iItem));
          switch (message.GetParam1())
          {
            case ACTION_SELECT_ITEM:
            case ACTION_MOUSE_LEFT_CLICK:
            case ACTION_PLAY:
            {
              const CPVRRecordingsPath path(m_vecItems->GetPath());
              if (path.IsValid() && path.IsRecordingsRoot() && item->IsParentFolder())
              {
                // handle special 'go home' item.
                g_windowManager.ActivateWindow(WINDOW_HOME);
                bReturn = true;
                break;
              }

              if (item->m_bIsFolder)
              {
                // recording folders and ".." folders in subfolders are handled by base class.
                bReturn = false;
                break;
              }

              if (message.GetParam1() == ACTION_PLAY)
              {
                CPVRGUIActions::GetInstance().PlayRecording(item, true /* check resume */);
                bReturn = true;
              }
              else
              {
                switch (CServiceBroker::GetSettings().GetInt(CSettings::SETTING_MYVIDEOS_SELECTACTION))
                {
                  case SELECT_ACTION_CHOOSE:
                    OnPopupMenu(iItem);
                    bReturn = true;
                    break;
                  case SELECT_ACTION_PLAY_OR_RESUME:
                    CPVRGUIActions::GetInstance().PlayRecording(item, true /* check resume */);
                    bReturn = true;
                    break;
                  case SELECT_ACTION_RESUME:
                    CPVRGUIActions::GetInstance().ResumePlayRecording(item, true /* fall back to play if no resume possible */);
                    bReturn = true;
                    break;
                  case SELECT_ACTION_INFO:
                    CPVRGUIActions::GetInstance().ShowRecordingInfo(item);
                    bReturn = true;
                    break;
                  default:
                    bReturn = false;
                    break;
                }
              }
              break;
            }
            case ACTION_CONTEXT_MENU:
            case ACTION_MOUSE_RIGHT_CLICK:
              OnPopupMenu(iItem);
              bReturn = true;
              break;
            case ACTION_SHOW_INFO:
              CPVRGUIActions::GetInstance().ShowRecordingInfo(item);
              bReturn = true;
              break;
            case ACTION_DELETE_ITEM:
              CPVRGUIActions::GetInstance().DeleteRecording(item);
              bReturn = true;
              break;
            default:
              bReturn = false;
              break;
          }
        }
      }
      else if (message.GetSenderId() == CONTROL_BTNGROUPITEMS)
      {
        CServiceBroker::GetSettings().ToggleBool(CSettings::SETTING_PVRRECORD_GROUPRECORDINGS);
        CServiceBroker::GetSettings().Save();
        Refresh(true);
      }
      else if (message.GetSenderId() == CONTROL_BTNSHOWDELETED)
      {
        CGUIRadioButtonControl *radioButton = (CGUIRadioButtonControl*) GetControl(CONTROL_BTNSHOWDELETED);
        if (radioButton)
        {
          m_bShowDeletedRecordings = radioButton->IsSelected();
          Update(GetDirectoryPath());
        }
        bReturn = true;
      }
      break;
    case GUI_MSG_REFRESH_LIST:
      switch(message.GetParam1())
      {
        case ObservableMessageTimers:
        case ObservableMessageEpg:
        case ObservableMessageEpgContainer:
        case ObservableMessageEpgActiveItem:
        case ObservableMessageCurrentItem:
        {
          SetInvalid();
          break;
        }
        case ObservableMessageRecordings:
        case ObservableMessageTimersReset:
        {
          Refresh(true);
          break;
        }
      }
      break;
  }

  return bReturn || CGUIWindowPVRBase::OnMessage(message);
}

bool CGUIWindowPVRRecordings::OnContextButtonDeleteAll(CFileItem *item, CONTEXT_BUTTON button)
{
  if (button == CONTEXT_BUTTON_DELETE_ALL)
  {
    CPVRGUIActions::GetInstance().DeleteAllRecordingsFromTrash();
    return true;
  }

  return false;
}

void CGUIWindowPVRRecordings::OnPrepareFileItems(CFileItemList& items)
{
  if (items.IsEmpty())
    return;

  CFileItemList files;
  VECFILEITEMS vecItems = items.GetList();
  for (VECFILEITEMS::const_iterator it = vecItems.begin(); it != vecItems.end(); ++it)
  {
    if (!(*it)->m_bIsFolder)
      files.Add((*it));
  }

  if (!files.IsEmpty())
  {
    if (m_database.Open())
    {
      CGUIWindowVideoNav::LoadVideoInfo(files, m_database, false);
      m_database.Close();
    }
    m_thumbLoader.Load(files);
  }

  CGUIWindowPVRBase::OnPrepareFileItems(items);
}
