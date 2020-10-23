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

#include "dialogs/GUIDialogOK.h"
#include "dialogs/GUIDialogProgress.h"
#include "epg/EpgContainer.h"
#include "guilib/GUIWindowManager.h"
#include "input/Key.h"
#include "utils/URIUtils.h"
#include "utils/Variant.h"

#include "pvr/PVRGUIActions.h"
#include "pvr/PVRManager.h"
#include "pvr/addons/PVRClients.h"
#include "pvr/channels/PVRChannelGroupsContainer.h"
#include "pvr/dialogs/GUIDialogPVRGuideSearch.h"

#include "GUIWindowPVRSearch.h"

using namespace PVR;
using namespace EPG;

CGUIWindowPVRSearch::CGUIWindowPVRSearch(bool bRadio) :
  CGUIWindowPVRBase(bRadio, bRadio ? WINDOW_RADIO_SEARCH : WINDOW_TV_SEARCH, "MyPVRSearch.xml"),
  m_bSearchConfirmed(false)
{
}

void CGUIWindowPVRSearch::GetContextButtons(int itemNumber, CContextButtons &buttons)
{
  if (itemNumber < 0 || itemNumber >= m_vecItems->Size())
    return;

  buttons.Add(CONTEXT_BUTTON_CLEAR, 19232);               /* Clear search results */

  CGUIWindowPVRBase::GetContextButtons(itemNumber, buttons);
}

void CGUIWindowPVRSearch::OnWindowLoaded()
{
  CGUIMediaWindow::OnWindowLoaded();
  m_searchfilter.Reset();
}

bool CGUIWindowPVRSearch::OnContextButton(int itemNumber, CONTEXT_BUTTON button)
{
  if (itemNumber < 0 || itemNumber >= m_vecItems->Size())
    return false;
  CFileItemPtr pItem = m_vecItems->Get(itemNumber);

  return OnContextButtonClear(pItem.get(), button) ||
      CGUIMediaWindow::OnContextButton(itemNumber, button);
}

bool CGUIWindowPVRSearch::FindSimilar(const CFileItemPtr &item)
{
  m_searchfilter.Reset();

  // construct the search term
  if (item->IsEPG())
    m_searchfilter.m_strSearchTerm = "\"" + item->GetEPGInfoTag()->Title() + "\"";
  else if (item->IsPVRChannel())
  {
    const CEpgInfoTagPtr tag(item->GetPVRChannelInfoTag()->GetEPGNow());
    if (tag)
      m_searchfilter.m_strSearchTerm = "\"" + tag->Title() + "\"";
  }
  else if (item->IsUsablePVRRecording())
    m_searchfilter.m_strSearchTerm = "\"" + item->GetPVRRecordingInfoTag()->m_strTitle + "\"";
  else if (item->IsPVRTimer())
  {
    const CPVRTimerInfoTagPtr info(item->GetPVRTimerInfoTag());
    const CEpgInfoTagPtr tag(info->GetEpgInfoTag());
    if (tag)
      m_searchfilter.m_strSearchTerm = "\"" + tag->Title() + "\"";
    else
      m_searchfilter.m_strSearchTerm = "\"" + info->m_strTitle + "\"";
  }
  m_bSearchConfirmed = true;
  Refresh(true);
  return true;
}

void CGUIWindowPVRSearch::OnPrepareFileItems(CFileItemList &items)
{
  bool bAddSpecialSearchItem = items.IsEmpty();

  if (m_bSearchConfirmed)
  {
    bAddSpecialSearchItem = true;

    items.Clear();
    CGUIDialogProgress* dlgProgress = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
    if (dlgProgress)
    {
      dlgProgress->SetHeading(CVariant{194}); // "Searching..."
      dlgProgress->SetText(CVariant{m_searchfilter.m_strSearchTerm});
      dlgProgress->Open();
      dlgProgress->Progress();
    }

    //! @todo should we limit the find similar search to the selected group?
    g_EpgContainer.GetEPGSearch(items, m_searchfilter);

    if (dlgProgress)
      dlgProgress->Close();

    if (items.IsEmpty())
      CGUIDialogOK::ShowAndGetInput(CVariant{194},  // "Searching..."
                                    CVariant{284}); // "No results found"
  }

  if (bAddSpecialSearchItem)
  {
    CFileItemPtr item(new CFileItem("pvr://guide/searchresults/search/", true));
    item->SetLabel(g_localizeStrings.Get(19140)); // "Search..."
    item->SetLabelPreformated(true);
    item->SetSpecialSort(SortSpecialOnTop);
    items.Add(item);
  }
}

bool CGUIWindowPVRSearch::OnMessage(CGUIMessage &message)
{
  if (message.GetMessage() == GUI_MSG_CLICKED)
  {
    if (message.GetSenderId() == m_viewControl.GetCurrentControl())
    {
      int iItem = m_viewControl.GetSelectedItem();
      if (iItem >= 0 && iItem < m_vecItems->Size())
      {
        CFileItemPtr pItem = m_vecItems->Get(iItem);

        /* process actions */
        switch (message.GetParam1())
        {
          case ACTION_SHOW_INFO:
          case ACTION_SELECT_ITEM:
          case ACTION_MOUSE_LEFT_CLICK:
          {
            if (URIUtils::PathEquals(pItem->GetPath(), "pvr://guide/searchresults/search/"))
              OpenDialogSearch();
            else
               CPVRGUIActions::GetInstance().ShowEPGInfo(pItem);
            return true;
          }

          case ACTION_CONTEXT_MENU:
          case ACTION_MOUSE_RIGHT_CLICK:
            OnPopupMenu(iItem);
            return true;

          case ACTION_RECORD:
            CPVRGUIActions::GetInstance().ToggleTimer(pItem);
            return true;
        }
      }
    }
  }

  return CGUIWindowPVRBase::OnMessage(message);
}

bool CGUIWindowPVRSearch::OnContextButtonClear(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_CLEAR)
  {
    bReturn = true;

    m_bSearchConfirmed = false;
    m_searchfilter.Reset();

    Refresh(true);
  }

  return bReturn;
}

void CGUIWindowPVRSearch::OpenDialogSearch()
{
  CGUIDialogPVRGuideSearch* dlgSearch = (CGUIDialogPVRGuideSearch*)g_windowManager.GetWindow(WINDOW_DIALOG_PVR_GUIDE_SEARCH);

  if (!dlgSearch)
    return;

  dlgSearch->SetFilterData(&m_searchfilter);

  /* Set channel type filter */
  m_searchfilter.m_bIsRadio = m_bRadio;

  /* Open dialog window */
  dlgSearch->Open();

  if (dlgSearch->IsConfirmed())
  {
    m_bSearchConfirmed = true;
    Refresh(true);
  }
}
