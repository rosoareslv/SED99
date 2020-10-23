#pragma once

/*
 *      Copyright (C) 2005-2013 Team XBMC
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

#include "view/GUIViewState.h"

class CGUIViewStateWindowMusic : public CGUIViewState
{
public:
  explicit CGUIViewStateWindowMusic(const CFileItemList& items) : CGUIViewState(items) {}
protected:
  VECSOURCES& GetSources() override;
  int GetPlaylist() override;
  bool AutoPlayNextItem() override;
  std::string GetLockType() override;
  std::string GetExtensions() override;
};

class CGUIViewStateMusicSearch : public CGUIViewStateWindowMusic
{
public:
  explicit CGUIViewStateMusicSearch(const CFileItemList& items);

protected:
  void SaveViewState() override;
};

class CGUIViewStateMusicDatabase : public CGUIViewStateWindowMusic
{
public:
  explicit CGUIViewStateMusicDatabase(const CFileItemList& items);

protected:
  void SaveViewState() override;
};

class CGUIViewStateMusicSmartPlaylist : public CGUIViewStateWindowMusic
{
public:
  explicit CGUIViewStateMusicSmartPlaylist(const CFileItemList& items);

protected:
  void SaveViewState() override;
};

class CGUIViewStateMusicPlaylist : public CGUIViewStateWindowMusic
{
public:
  explicit CGUIViewStateMusicPlaylist(const CFileItemList& items);

protected:
  void SaveViewState() override;
};

class CGUIViewStateWindowMusicNav : public CGUIViewStateWindowMusic
{
public:
  explicit CGUIViewStateWindowMusicNav(const CFileItemList& items);

protected:
  void SaveViewState() override;
  VECSOURCES& GetSources() override;

private:
  void AddOnlineShares();
};

class CGUIViewStateWindowMusicPlaylist : public CGUIViewStateWindowMusic
{
public:
  explicit CGUIViewStateWindowMusicPlaylist(const CFileItemList& items);

protected:
  void SaveViewState() override;
  int GetPlaylist() override;
  bool AutoPlayNextItem() override;
  bool HideParentDirItems() override;
  VECSOURCES& GetSources() override;
};
