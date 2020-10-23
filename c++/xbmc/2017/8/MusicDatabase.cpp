/*
 *      Copyright (C) 2005-2015 Team Kodi
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
 *  along with Kodi; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "MusicDatabase.h"

#include "addons/Addon.h"
#include "addons/AddonManager.h"
#include "addons/AddonSystemSettings.h"
#include "addons/Scraper.h"
#include "Album.h"
#include "Application.h"
#include "ServiceBroker.h"
#include "Artist.h"
#include "dbwrappers/dataset.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "dialogs/GUIDialogOK.h"
#include "dialogs/GUIDialogProgress.h"
#include "dialogs/GUIDialogSelect.h"
#include "FileItem.h"
#include "filesystem/DirectoryCache.h"
#include "filesystem/File.h"
#include "filesystem/MusicDatabaseDirectory/DirectoryNode.h"
#include "filesystem/MusicDatabaseDirectory/QueryParams.h"
#include "guiinfo/GUIInfoLabels.h"
#include "GUIInfoManager.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "interfaces/AnnouncementManager.h"
#include "messaging/helpers/DialogHelper.h"
#include "music/tags/MusicInfoTag.h"
#include "network/cddb.h"
#include "network/Network.h"
#include "playlists/SmartPlayList.h"
#include "profiles/ProfilesManager.h"
#include "settings/AdvancedSettings.h"
#include "settings/MediaSettings.h"
#include "settings/Settings.h"
#include "Song.h"
#include "storage/MediaManager.h"
#include "system.h"
#include "TextureCache.h"
#include "threads/SystemClock.h"
#include "URL.h"
#include "utils/FileUtils.h"
#include "utils/LegacyPathTranslation.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/XMLUtils.h"

using namespace XFILE;
using namespace MUSICDATABASEDIRECTORY;
using namespace KODI::MESSAGING;
using namespace MUSIC_INFO;

using ADDON::AddonPtr;
using KODI::MESSAGING::HELPERS::DialogResponse;

#define RECENTLY_PLAYED_LIMIT 25
#define MIN_FULL_SEARCH_LENGTH 3

#ifdef HAS_DVD_DRIVE
using namespace CDDB;
using namespace MEDIA_DETECT;
#endif

static void AnnounceRemove(const std::string& content, int id)
{
  CVariant data;
  data["type"] = content;
  data["id"] = id;
  if (g_application.IsMusicScanning())
    data["transaction"] = true;
  ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::AudioLibrary, "xbmc", "OnRemove", data);
}

static void AnnounceUpdate(const std::string& content, int id, bool added = false)
{
  CVariant data;
  data["type"] = content;
  data["id"] = id;
  if (g_application.IsMusicScanning())
    data["transaction"] = true;
  if (added)
    data["added"] = true;
  ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::AudioLibrary, "xbmc", "OnUpdate", data);
}

CMusicDatabase::CMusicDatabase(void)
{
  m_translateBlankArtist = true;
}

CMusicDatabase::~CMusicDatabase(void)
{
  EmptyCache();
}

bool CMusicDatabase::Open()
{
  return CDatabase::Open(g_advancedSettings.m_databaseMusic);
}

void CMusicDatabase::CreateTables()
{
  CLog::Log(LOGINFO, "create artist table");
  m_pDS->exec("CREATE TABLE artist ( idArtist integer primary key, "
              " strArtist varchar(256), strMusicBrainzArtistID text, "
              " strSortName text, "
              " strBorn text, strFormed text, strGenres text, strMoods text, "
              " strStyles text, strInstruments text, strBiography text, "
              " strDied text, strDisbanded text, strYearsActive text, "
              " strImage text, strFanart text, "
              " lastScraped varchar(20) default NULL, "
              " bScrapedMBID INTEGER NOT NULL DEFAULT 0, "
              " idInfoSetting INTEGER NOT NULL DEFAULT 0)");
  // Create missing artist tag artist [Missing].
  std::string strSQL = PrepareSQL("INSERT INTO artist (idArtist, strArtist, strSortName, strMusicBrainzArtistID) "
     "VALUES( %i, '%s', '%s', '%s' )",
    BLANKARTIST_ID, BLANKARTIST_NAME.c_str(),
    BLANKARTIST_NAME.c_str(), BLANKARTIST_FAKEMUSICBRAINZID.c_str());
  m_pDS->exec(strSQL);

  CLog::Log(LOGINFO, "create album table");
  m_pDS->exec("CREATE TABLE album (idAlbum integer primary key, "
              " strAlbum varchar(256), strMusicBrainzAlbumID text, "
              " strReleaseGroupMBID text, "
              " strArtistDisp text, strArtistSort text, strGenres text, "
              " iYear integer, "
              " bCompilation integer not null default '0', "
              " strMoods text, strStyles text, strThemes text, "
              " strReview text, strImage text, strLabel text, "
              " strType text, "
              " fRating FLOAT NOT NULL DEFAULT 0, "
              " iVotes INTEGER NOT NULL DEFAULT 0, "
              " iUserrating INTEGER NOT NULL DEFAULT 0, "
              " lastScraped varchar(20) default NULL, "
              " bScrapedMBID INTEGER NOT NULL DEFAULT 0, "
              " strReleaseType text, "
              " idInfoSetting INTEGER NOT NULL DEFAULT 0)");

  CLog::Log(LOGINFO, "create audiobook table");
  m_pDS->exec("CREATE TABLE audiobook (idBook integer primary key, "
              " strBook varchar(256), strAuthor text,"
              " bookmark integer, file text,"
              " dateAdded varchar (20) default NULL)");

  CLog::Log(LOGINFO, "create album_artist table");
  m_pDS->exec("CREATE TABLE album_artist (idArtist integer, idAlbum integer, iOrder integer, strArtist text)");
  CLog::Log(LOGINFO, "create album_genre table");
  m_pDS->exec("CREATE TABLE album_genre (idGenre integer, idAlbum integer, iOrder integer)");

  CLog::Log(LOGINFO, "create genre table");
  m_pDS->exec("CREATE TABLE genre (idGenre integer primary key, strGenre varchar(256))");
  CLog::Log(LOGINFO, "create path table");
  m_pDS->exec("CREATE TABLE path (idPath integer primary key, strPath varchar(512), strHash text)");
  CLog::Log(LOGINFO, "create song table");
  m_pDS->exec("CREATE TABLE song (idSong integer primary key, "
              " idAlbum integer, idPath integer, "
              " strArtistDisp text, strArtistSort text, strGenres text, strTitle varchar(512), "
              " iTrack integer, iDuration integer, iYear integer, "
              " strFileName text, strMusicBrainzTrackID text, "
              " iTimesPlayed integer, iStartOffset integer, iEndOffset integer, "
              " lastplayed varchar(20) default NULL, "
              " rating FLOAT NOT NULL DEFAULT 0, votes INTEGER NOT NULL DEFAULT 0, "
              " userrating INTEGER NOT NULL DEFAULT 0, "
              " comment text, mood text, strReplayGain text, dateAdded text)");
  CLog::Log(LOGINFO, "create song_artist table");
  m_pDS->exec("CREATE TABLE song_artist (idArtist integer, idSong integer, idRole integer, iOrder integer, strArtist text)");
  CLog::Log(LOGINFO, "create song_genre table");
  m_pDS->exec("CREATE TABLE song_genre (idGenre integer, idSong integer, iOrder integer)");

  CLog::Log(LOGINFO, "create role table");
  m_pDS->exec("CREATE TABLE role (idRole integer primary key, strRole text)");
  m_pDS->exec("INSERT INTO role(idRole, strRole) VALUES (1, 'Artist')");   //Default role

  CLog::Log(LOGINFO, "create infosetting table");
  m_pDS->exec("CREATE TABLE infosetting (idSetting INTEGER PRIMARY KEY, strScraperPath TEXT, strSettings TEXT)");

  CLog::Log(LOGINFO, "create discography table");
  m_pDS->exec("CREATE TABLE discography (idArtist integer, strAlbum text, strYear text)");

  CLog::Log(LOGINFO, "create art table");
  m_pDS->exec("CREATE TABLE art(art_id INTEGER PRIMARY KEY, media_id INTEGER, media_type TEXT, type TEXT, url TEXT)");

  CLog::Log(LOGINFO, "create versiontagscan table");
  m_pDS->exec("CREATE TABLE versiontagscan (idVersion integer, iNeedsScan integer)");
  m_pDS->exec(PrepareSQL("INSERT INTO versiontagscan (idVersion, iNeedsScan) values(%i, 0)", GetSchemaVersion()));
}

void CMusicDatabase::CreateAnalytics()
{
  CLog::Log(LOGINFO, "%s - creating indices", __FUNCTION__);
  m_pDS->exec("CREATE INDEX idxAlbum ON album(strAlbum(255))");
  m_pDS->exec("CREATE INDEX idxAlbum_1 ON album(bCompilation)");
  m_pDS->exec("CREATE UNIQUE INDEX idxAlbum_2 ON album(strMusicBrainzAlbumID(36))");
  m_pDS->exec("CREATE INDEX idxAlbum_3 ON album(idInfoSetting)");

  m_pDS->exec("CREATE UNIQUE INDEX idxAlbumArtist_1 ON album_artist ( idAlbum, idArtist )");
  m_pDS->exec("CREATE UNIQUE INDEX idxAlbumArtist_2 ON album_artist ( idArtist, idAlbum )");

  m_pDS->exec("CREATE UNIQUE INDEX idxAlbumGenre_1 ON album_genre ( idAlbum, idGenre )");
  m_pDS->exec("CREATE UNIQUE INDEX idxAlbumGenre_2 ON album_genre ( idGenre, idAlbum )");

  m_pDS->exec("CREATE INDEX idxGenre ON genre(strGenre(255))");

  m_pDS->exec("CREATE INDEX idxArtist ON artist(strArtist(255))");
  m_pDS->exec("CREATE UNIQUE INDEX idxArtist1 ON artist(strMusicBrainzArtistID(36))");
  m_pDS->exec("CREATE INDEX idxArtist_2 ON artist(idInfoSetting)");

  m_pDS->exec("CREATE INDEX idxPath ON path(strPath(255))");

  m_pDS->exec("CREATE INDEX idxSong ON song(strTitle(255))");
  m_pDS->exec("CREATE INDEX idxSong1 ON song(iTimesPlayed)");
  m_pDS->exec("CREATE INDEX idxSong2 ON song(lastplayed)");
  m_pDS->exec("CREATE INDEX idxSong3 ON song(idAlbum)");
  m_pDS->exec("CREATE INDEX idxSong6 ON song( idPath, strFileName(255) )");
  //Musicbrainz Track ID is not unique on an album, recordings are sometimes repeated e.g. "[silence]" or on a disc set
  m_pDS->exec("CREATE UNIQUE INDEX idxSong7 ON song( idAlbum, iTrack, strMusicBrainzTrackID(36) )");

  m_pDS->exec("CREATE UNIQUE INDEX idxSongArtist_1 ON song_artist ( idSong, idArtist, idRole )");
  m_pDS->exec("CREATE INDEX idxSongArtist_2 ON song_artist ( idSong, idRole )");
  m_pDS->exec("CREATE INDEX idxSongArtist_3 ON song_artist ( idArtist, idRole )");
  m_pDS->exec("CREATE INDEX idxSongArtist_4 ON song_artist ( idRole )");

  m_pDS->exec("CREATE UNIQUE INDEX idxSongGenre_1 ON song_genre ( idSong, idGenre )");
  m_pDS->exec("CREATE UNIQUE INDEX idxSongGenre_2 ON song_genre ( idGenre, idSong )");

  m_pDS->exec("CREATE INDEX idxRole on role(strRole(255))");

  m_pDS->exec("CREATE INDEX idxDiscography_1 ON discography ( idArtist )");

  m_pDS->exec("CREATE INDEX ix_art ON art(media_id, media_type(20), type(20))");

  CLog::Log(LOGINFO, "create triggers");
  m_pDS->exec("CREATE TRIGGER tgrDeleteAlbum AFTER delete ON album FOR EACH ROW BEGIN"
              "  DELETE FROM song WHERE song.idAlbum = old.idAlbum;"
              "  DELETE FROM album_artist WHERE album_artist.idAlbum = old.idAlbum;"
              "  DELETE FROM album_genre WHERE album_genre.idAlbum = old.idAlbum;"
              "  DELETE FROM art WHERE media_id=old.idAlbum AND media_type='album';"
              " END");
  m_pDS->exec("CREATE TRIGGER tgrDeleteArtist AFTER delete ON artist FOR EACH ROW BEGIN"
              "  DELETE FROM album_artist WHERE album_artist.idArtist = old.idArtist;"
              "  DELETE FROM song_artist WHERE song_artist.idArtist = old.idArtist;"
              "  DELETE FROM discography WHERE discography.idArtist = old.idArtist;"
              "  DELETE FROM art WHERE media_id=old.idArtist AND media_type='artist';"
              " END");
  m_pDS->exec("CREATE TRIGGER tgrDeleteSong AFTER delete ON song FOR EACH ROW BEGIN"
              "  DELETE FROM song_artist WHERE song_artist.idSong = old.idSong;"
              "  DELETE FROM song_genre WHERE song_genre.idSong = old.idSong;"
              "  DELETE FROM art WHERE media_id=old.idSong AND media_type='song';"
              " END");
  
  // we create views last to ensure all indexes are rolled in
  CreateViews();
}

void CMusicDatabase::CreateViews()
{
  CLog::Log(LOGINFO, "create song view");
  m_pDS->exec("CREATE VIEW songview AS SELECT "
              "        song.idSong AS idSong, "
              "        song.strArtistDisp AS strArtists,"
              "        song.strArtistSort AS strArtistSort,"
              "        song.strGenres AS strGenres,"
              "        strTitle, "
              "        iTrack, iDuration, "
              "        song.iYear AS iYear, "
              "        strFileName, "
              "        strMusicBrainzTrackID, "
              "        iTimesPlayed, iStartOffset, iEndOffset, "
              "        lastplayed, "
              "        song.rating, "
              "        song.userrating, "
              "        song.votes, "
              "        comment, "
              "        song.idAlbum AS idAlbum, "
              "        strAlbum, "
              "        strPath, "
              "        album.bCompilation AS bCompilation,"
              "        album.strArtistDisp AS strAlbumArtists,"
              "        album.strArtistSort AS strAlbumArtistSort,"
              "        album.strReleaseType AS strAlbumReleaseType,"
              "        song.mood as mood,"
              "        song.dateAdded as dateAdded, "
              "        song.strReplayGain "
              "FROM song"
              "  JOIN album ON"
              "    song.idAlbum=album.idAlbum"
              "  JOIN path ON"
              "    song.idPath=path.idPath");

  CLog::Log(LOGINFO, "create album view");
  m_pDS->exec("CREATE VIEW albumview AS SELECT "
              "        album.idAlbum AS idAlbum, "
              "        strAlbum, "
              "        strMusicBrainzAlbumID, "
              "        strReleaseGroupMBID, "
              "        album.strArtistDisp AS strArtists, "
              "        album.strArtistSort AS strArtistSort, "
              "        album.strGenres AS strGenres, "
              "        album.iYear AS iYear, "
              "        album.strMoods AS strMoods, "
              "        album.strStyles AS strStyles, "
              "        strThemes, "
              "        strReview, "
              "        strLabel, "
              "        strType, "
              "        album.strImage as strImage, "
              "        album.fRating, "
              "        album.iUserrating, "
              "        album.iVotes, "
              "        bCompilation, "
              "        bScrapedMBID,"
              "        lastScraped,"
              "        (SELECT AVG(song.iTimesPlayed) FROM song WHERE song.idAlbum = album.idAlbum) AS iTimesPlayed, "
              "        strReleaseType, "
              "        (SELECT MAX(song.dateAdded) FROM song WHERE song.idAlbum = album.idAlbum) AS dateAdded, "
              "        (SELECT MAX(song.lastplayed) FROM song WHERE song.idAlbum = album.idAlbum) AS lastplayed "
              "FROM album"
              );

  CLog::Log(LOGINFO, "create artist view");
  m_pDS->exec("CREATE VIEW artistview AS SELECT"
              "  idArtist, strArtist, strSortName, "
              "  strMusicBrainzArtistID, "
              "  strBorn, strFormed, strGenres,"
              "  strMoods, strStyles, strInstruments, "
              "  strBiography, strDied, strDisbanded, "
              "  strYearsActive, strImage, strFanart, "
              "  bScrapedMBID, lastScraped, "
              "  (SELECT MAX(song.dateAdded) FROM song_artist INNER JOIN song ON song.idSong = song_artist.idSong "
              "  WHERE song_artist.idArtist = artist.idArtist) AS dateAdded "
              "FROM artist");

  CLog::Log(LOGINFO, "create albumartist view");
  m_pDS->exec("CREATE VIEW albumartistview AS SELECT"
              "  album_artist.idAlbum AS idAlbum, "
              "  album_artist.idArtist AS idArtist, "
              "  0 AS idRole, "
              "  'AlbumArtist' AS strRole, "
              "  artist.strArtist AS strArtist, "
              "  artist.strSortName AS strSortName,"
              "  artist.strMusicBrainzArtistID AS strMusicBrainzArtistID, "
              "  album_artist.iOrder AS iOrder "
              "FROM album_artist "
              "JOIN artist ON "
              "     album_artist.idArtist = artist.idArtist");

  CLog::Log(LOGINFO, "create songartist view");
  m_pDS->exec("CREATE VIEW songartistview AS SELECT"
              "  song_artist.idSong AS idSong, "
              "  song_artist.idArtist AS idArtist, "
              "  song_artist.idRole AS idRole, "
              "  role.strRole AS strRole, "
              "  artist.strArtist AS strArtist, "
              "  artist.strSortName AS strSortName,"
              "  artist.strMusicBrainzArtistID AS strMusicBrainzArtistID, "
              "  song_artist.iOrder AS iOrder "
              "FROM song_artist "
              "JOIN artist ON "
              "     song_artist.idArtist = artist.idArtist "
              "JOIN role ON "
              "     song_artist.idRole = role.idRole");
}

bool CMusicDatabase::AddAlbum(CAlbum& album)
{
  BeginTransaction();

  album.idAlbum = AddAlbum(album.strAlbum,
                           album.strMusicBrainzAlbumID,
                           album.strReleaseGroupMBID,
                           album.GetAlbumArtistString(),
                           album.GetAlbumArtistSort(),
                           album.GetGenreString(),
                           album.iYear,
                           album.strLabel, album.strType,
                           album.bCompilation, album.releaseType);

  // Add the album artists
  if (album.artistCredits.empty())
    AddAlbumArtist(BLANKARTIST_ID, album.idAlbum, BLANKARTIST_NAME, 0); // Album must have at least one artist so set artist to [Missing]
  for (auto artistCredit = album.artistCredits.begin(); artistCredit != album.artistCredits.end(); ++artistCredit)
  {
    artistCredit->idArtist = AddArtist(artistCredit->GetArtist(), artistCredit->GetMusicBrainzArtistID(), artistCredit->GetSortName());
    AddAlbumArtist(artistCredit->idArtist,
                   album.idAlbum,
                   artistCredit->GetArtist(),
                   std::distance(album.artistCredits.begin(), artistCredit));
  }

  for (auto song = album.songs.begin(); song != album.songs.end(); ++song)
  {
    song->idAlbum = album.idAlbum;

    song->idSong = AddSong(song->idAlbum,
                           song->strTitle, song->strMusicBrainzTrackID,
                           song->strFileName, song->strComment,
                           song->strMood, song->strThumb,
                           song->GetArtistString(), 
                           song->GetArtistSort(),
                           song->genre,
                           song->iTrack, song->iDuration, song->iYear,
                           song->iTimesPlayed, song->iStartOffset,
                           song->iEndOffset,
                           song->lastPlayed,
                           song->rating,
                           song->userrating,
                           song->votes,
                           song->replayGain);

    if (song->artistCredits.empty())    
      AddSongArtist(BLANKARTIST_ID, song->idSong, ROLE_ARTIST, BLANKARTIST_NAME, 0); // Song must have at least one artist so set artist to [Missing]
    
    for (auto artistCredit = song->artistCredits.begin(); artistCredit != song->artistCredits.end(); ++artistCredit)
    {
      artistCredit->idArtist = AddArtist(artistCredit->GetArtist(),
                                         artistCredit->GetMusicBrainzArtistID(),
                                         artistCredit->GetSortName());
      AddSongArtist(artistCredit->idArtist,
                    song->idSong,
                    ROLE_ARTIST,
                    artistCredit->GetArtist(), // we don't have song artist breakdowns from scrapers, yet
                    std::distance(song->artistCredits.begin(), artistCredit));
    }
    // Having added artist credits (maybe with MBID) add the other contributing artists (no MBID)
    // and use COMPOSERSORT tag data to provide sort names for artists that are composers
    AddSongContributors(song->idSong, song->GetContributors(), song->GetComposerSort());
  }

  for (const auto &albumArt : album.art)
    SetArtForItem(album.idAlbum, MediaTypeAlbum, albumArt.first, albumArt.second);

  CommitTransaction();
  return true;
}

bool CMusicDatabase::UpdateAlbum(CAlbum& album)
{
  BeginTransaction();

  UpdateAlbum(album.idAlbum,
              album.strAlbum, album.strMusicBrainzAlbumID,
              album.strReleaseGroupMBID,
              album.GetAlbumArtistString(), album.GetAlbumArtistSort(),
              album.GetGenreString(),
              StringUtils::Join(album.moods, g_advancedSettings.m_musicItemSeparator).c_str(),
              StringUtils::Join(album.styles, g_advancedSettings.m_musicItemSeparator).c_str(),
              StringUtils::Join(album.themes, g_advancedSettings.m_musicItemSeparator).c_str(),
              album.strReview,
              album.thumbURL.m_xml.c_str(),
              album.strLabel, album.strType,
              album.fRating, album.iUserrating, album.iVotes, album.iYear, album.bCompilation, album.releaseType,
              album.bScrapedMBID);

  if (!album.bArtistSongMerge)
  {
    // Album artist(s) already exist and names are not changing, but may have scraped Musicbrainz ids to add
    for (const auto &artistCredit : album.artistCredits)
      UpdateArtistScrapedMBID(artistCredit.GetArtistId(), artistCredit.GetMusicBrainzArtistID());
  }
  else
  {
    // Replace the album artists with those scraped
    DeleteAlbumArtistsByAlbum(album.idAlbum);
    if (album.artistCredits.empty())
      AddAlbumArtist(BLANKARTIST_ID, album.idAlbum, BLANKARTIST_NAME, 0); // Album must have at least one artist so set artist to [Missing]
    for (auto artistCredit = album.artistCredits.begin(); artistCredit != album.artistCredits.end(); ++artistCredit)
    {
      artistCredit->idArtist = AddArtist(artistCredit->GetArtist(),
        artistCredit->GetMusicBrainzArtistID(), artistCredit->GetSortName(), true);
      AddAlbumArtist(artistCredit->idArtist,
        album.idAlbum,
        artistCredit->GetArtist(),
        std::distance(album.artistCredits.begin(), artistCredit));
    }
    // Replace the songs with those scraped
    for (auto &song : album.songs)
    {
      UpdateSong(song.idSong,
        song.strTitle,
        song.strMusicBrainzTrackID,
        song.strFileName,
        song.strComment,
        song.strMood,
        song.strThumb,
        song.GetArtistString(),
        song.GetArtistSort(),
        song.genre,
        song.iTrack,
        song.iDuration,
        song.iYear,
        song.iTimesPlayed,
        song.iStartOffset,
        song.iEndOffset,
        song.lastPlayed,
        song.rating,
        song.userrating,
        song.votes,
        song.replayGain);
      //Replace song artists and contributors
      DeleteSongArtistsBySong(song.idSong);
      if (song.artistCredits.empty())
        AddSongArtist(BLANKARTIST_ID, song.idSong, ROLE_ARTIST, BLANKARTIST_NAME, 0); // Song must have at least one artist so set artist to [Missing]
      for (auto artistCredit = song.artistCredits.begin(); artistCredit != song.artistCredits.end(); ++artistCredit)
      {
        artistCredit->idArtist = AddArtist(artistCredit->GetArtist(),
          artistCredit->GetMusicBrainzArtistID(), artistCredit->GetSortName());
        AddSongArtist(artistCredit->idArtist,
          song.idSong,
          ROLE_ARTIST,
          artistCredit->GetArtist(),
          std::distance(song.artistCredits.begin(), artistCredit));
      }
      // Having added artist credits (maybe with MBID) add the other contributing artists (MBID unknown)
      // and use COMPOSERSORT tag data to provide sort names for artists that are composers
      AddSongContributors(song.idSong, song.GetContributors(), song.GetComposerSort());
    }
  }

  if (!album.art.empty())
    SetArtForItem(album.idAlbum, MediaTypeAlbum, album.art);

  CommitTransaction();
  return true;
}

int CMusicDatabase::AddSong(const int idAlbum,
                            const std::string& strTitle, const std::string& strMusicBrainzTrackID,
                            const std::string& strPathAndFileName, const std::string& strComment,
                            const std::string& strMood, const std::string& strThumb,
                            const std::string &artistDisp, const std::string &artistSort,
                            const std::vector<std::string>& genres,
                            int iTrack, int iDuration, int iYear,
                            const int iTimesPlayed, int iStartOffset, int iEndOffset,
                            const CDateTime& dtLastPlayed, float rating, int userrating, int votes,
                            const ReplayGain& replayGain)
{
  int idSong = -1;
  std::string strSQL;
  try
  {
    // We need at least the title
    if (strTitle.empty())
      return -1;

    if (NULL == m_pDB.get()) return -1;
    if (NULL == m_pDS.get()) return -1;

    std::string strPath, strFileName;
    URIUtils::Split(strPathAndFileName, strPath, strFileName);
    int idPath = AddPath(strPath);

    if (!strMusicBrainzTrackID.empty())
      strSQL = PrepareSQL("SELECT idSong FROM song WHERE idAlbum = %i AND iTrack=%i AND strMusicBrainzTrackID = '%s'",
                          idAlbum, 
                          iTrack,
                          strMusicBrainzTrackID.c_str());
    else
      strSQL = PrepareSQL("SELECT idSong FROM song WHERE idAlbum=%i AND strFileName='%s' AND strTitle='%s' AND iTrack=%i AND strMusicBrainzTrackID IS NULL",
                          idAlbum,
                          strFileName.c_str(),
                          strTitle.c_str(),
                          iTrack);

    if (!m_pDS->query(strSQL))
      return -1;

    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      strSQL=PrepareSQL("INSERT INTO song ("
                                          "idSong,idAlbum,idPath,strArtistDisp,strGenres,"
                                          "strTitle,iTrack,iDuration,iYear,strFileName,"
                                          "strMusicBrainzTrackID, strArtistSort, "
                                          "iTimesPlayed,iStartOffset, "
                                          "iEndOffset,lastplayed,rating,userrating,votes,comment,mood,strReplayGain"
                        ") values (NULL, %i, %i, '%s', '%s', '%s', %i, %i, %i, '%s'",
                    idAlbum,
                    idPath,
                    artistDisp.c_str(),
                    StringUtils::Join(genres, g_advancedSettings.m_musicItemSeparator).c_str(),
                    strTitle.c_str(),
                    iTrack, iDuration, iYear,
                    strFileName.c_str());

      if (strMusicBrainzTrackID.empty())
        strSQL += PrepareSQL(",NULL");
      else
        strSQL += PrepareSQL(",'%s'", strMusicBrainzTrackID.c_str());
      if (artistSort.empty())
        strSQL += PrepareSQL(",NULL");
      else
        strSQL += PrepareSQL(",'%s'", artistSort.c_str());

      if (dtLastPlayed.IsValid())
        strSQL += PrepareSQL(",%i,%i,%i,'%s', %.1f, %i, %i, '%s','%s', '%s')",
                      iTimesPlayed, iStartOffset, iEndOffset, dtLastPlayed.GetAsDBDateTime().c_str(), rating, userrating, votes, 
                      strComment.c_str(), strMood.c_str(), replayGain.Get().c_str());
      else
        strSQL += PrepareSQL(",%i,%i,%i,NULL, %.1f, %i, %i,'%s', '%s', '%s')",
                      iTimesPlayed, iStartOffset, iEndOffset, rating, userrating, votes, strComment.c_str(), strMood.c_str(), replayGain.Get().c_str());
      m_pDS->exec(strSQL);
      idSong = (int)m_pDS->lastinsertid();
    }
    else
    {
      idSong = m_pDS->fv("idSong").get_asInt();
      m_pDS->close();
      UpdateSong( idSong, strTitle, strMusicBrainzTrackID, strPathAndFileName, strComment, strMood, strThumb, 
                  artistDisp, artistSort, genres, iTrack, iDuration, iYear, iTimesPlayed, iStartOffset, iEndOffset, 
                  dtLastPlayed, rating, userrating, votes, replayGain);
    }

    if (!strThumb.empty())
      SetArtForItem(idSong, MediaTypeSong, "thumb", strThumb);

    unsigned int index = 0;
    for (const auto &i : genres)
    {
      // index will be wrong for albums, but ordering is not all that relevant
      // for genres anyway
      int idGenre = AddGenre(i);
      AddSongGenre(idGenre, idSong, index);
      AddAlbumGenre(idGenre, idAlbum, index++);
    }

    UpdateFileDateAdded(idSong, strPathAndFileName);

    AnnounceUpdate(MediaTypeSong, idSong, true);
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "musicdatabase:unable to addsong (%s)", strSQL.c_str());
  }
  return idSong;
}

bool CMusicDatabase::GetSong(int idSong, CSong& song)
{
  try
  {
    song.Clear();

    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string strSQL=PrepareSQL("SELECT songview.*,songartistview.* FROM songview "
                                 " JOIN songartistview ON songview.idSong = songartistview.idSong "
                                 " WHERE songview.idSong = %i "
                                 " ORDER BY songartistview.idRole, songartistview.iOrder", idSong);

    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return false;
    }

    int songArtistOffset = song_enumCount;

    song = GetSongFromDataset(m_pDS.get()->get_sql_record());
    while (!m_pDS->eof())
    {
      const dbiplus::sql_record* const record = m_pDS.get()->get_sql_record();

      int idSongArtistRole = record->at(songArtistOffset + artistCredit_idRole).get_asInt();
      if (idSongArtistRole == ROLE_ARTIST)
        song.artistCredits.emplace_back(GetArtistCreditFromDataset(record, songArtistOffset));
      else 
        song.AppendArtistRole(GetArtistRoleFromDataset(record, songArtistOffset));

      m_pDS->next();
    }
    m_pDS->close(); // cleanup recordset data
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%i) failed", __FUNCTION__, idSong);
  }

  return false;
}

int CMusicDatabase::UpdateSong(int idSong, const CSong &song)
{
  return UpdateSong(idSong,
                    song.strTitle,
                    song.strMusicBrainzTrackID,
                    song.strFileName,
                    song.strComment,
                    song.strMood,
                    song.strThumb,
                    song.GetArtistString(), // NOTE: Don't call this function internally!!!
                    song.GetArtistSort(),
                    song.genre,
                    song.iTrack,
                    song.iDuration,
                    song.iYear,
                    song.iTimesPlayed,
                    song.iStartOffset,
                    song.iEndOffset,
                    song.lastPlayed,
                    song.rating,
                    song.userrating,
                    song.votes,
                    song.replayGain);
}

int CMusicDatabase::UpdateSong(int idSong,
                               const std::string& strTitle, const std::string& strMusicBrainzTrackID,
                               const std::string& strPathAndFileName, const std::string& strComment,
                               const std::string& strMood, const std::string& strThumb,
                               const std::string &artistDisp, const std::string &artistSort,
                               const std::vector<std::string>& genres,
                               int iTrack, int iDuration, int iYear,
                               int iTimesPlayed, int iStartOffset, int iEndOffset,
                               const CDateTime& dtLastPlayed, float rating, int userrating, int votes, 
                               const ReplayGain& replayGain)
{
  if (idSong < 0)
    return -1;

  std::string strSQL;
  std::string strPath, strFileName;
  URIUtils::Split(strPathAndFileName, strPath, strFileName);
  int idPath = AddPath(strPath);

  strSQL = PrepareSQL("UPDATE song SET idPath = %i, strArtistDisp = '%s', strGenres = '%s', "
      " strTitle = '%s', iTrack = %i, iDuration = %i, iYear = %i, strFileName = '%s'",
      idPath,
      artistDisp.c_str(),
      StringUtils::Join(genres, g_advancedSettings.m_musicItemSeparator).c_str(),
      strTitle.c_str(),
      iTrack, iDuration, iYear,
      strFileName.c_str());
  if (strMusicBrainzTrackID.empty())
    strSQL += PrepareSQL(", strMusicBrainzTrackID = NULL");
  else
    strSQL += PrepareSQL(", strMusicBrainzTrackID = '%s'", strMusicBrainzTrackID.c_str());
  if (artistSort.empty())
    strSQL += PrepareSQL(", strArtistSort = NULL");
  else
    strSQL += PrepareSQL(", strArtistSort = '%s'", artistSort.c_str());
  

  if (dtLastPlayed.IsValid())
    strSQL += PrepareSQL(", iTimesPlayed = %i, iStartOffset = %i, iEndOffset = %i, lastplayed = '%s', rating = %.1f, userrating = %i, votes = %i, comment = '%s', mood = '%s', strReplayGain = '%s'",
                         iTimesPlayed, iStartOffset, iEndOffset, dtLastPlayed.GetAsDBDateTime().c_str(), rating, userrating, votes, strComment.c_str(), strMood.c_str(), replayGain.Get().c_str());
  else
    strSQL += PrepareSQL(", iTimesPlayed = %i, iStartOffset = %i, iEndOffset = %i, lastplayed = NULL, rating = %.1f, userrating = %i, votes = %i, comment = '%s', mood = '%s', strReplayGain = '%s'",
                         iTimesPlayed, iStartOffset, iEndOffset, rating, userrating, votes, strComment.c_str(), strMood.c_str(), replayGain.Get().c_str());
  strSQL += PrepareSQL(" WHERE idSong = %i", idSong);

  bool status = ExecuteQuery(strSQL);

  UpdateFileDateAdded(idSong, strPathAndFileName);

  if (status)
    AnnounceUpdate(MediaTypeSong, idSong);
  return idSong;
}

int CMusicDatabase::AddAlbum(const std::string& strAlbum, const std::string& strMusicBrainzAlbumID,
                             const std::string& strReleaseGroupMBID,
                             const std::string& strArtist, const std::string& strArtistSort, 
                             const std::string& strGenre, int year,
                             const std::string& strRecordLabel, const std::string& strType,
                             bool bCompilation, CAlbum::ReleaseType releaseType)
{
  std::string strSQL;
  try
  {
    if (NULL == m_pDB.get()) return -1;
    if (NULL == m_pDS.get()) return -1;

    if (!strMusicBrainzAlbumID.empty())
      strSQL = PrepareSQL("SELECT * FROM album WHERE strMusicBrainzAlbumID = '%s'",
                        strMusicBrainzAlbumID.c_str());
    else
      strSQL = PrepareSQL("SELECT * FROM album WHERE strArtistDisp LIKE '%s' AND strAlbum LIKE '%s' AND strMusicBrainzAlbumID IS NULL",
                          strArtist.c_str(),
                          strAlbum.c_str());
    m_pDS->query(strSQL);

    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      // doesnt exists, add it
      strSQL = PrepareSQL("INSERT INTO album (idAlbum, strAlbum, strArtistDisp, strGenres, iYear, "
        "strLabel, strType, bCompilation, strReleaseType, strMusicBrainzAlbumID, strReleaseGroupMBID, strArtistSort) "
        "values( NULL, '%s', '%s', '%s', %i, '%s', '%s', %i, '%s'",
        strAlbum.c_str(),
        strArtist.c_str(),
        strGenre.c_str(),
        year,
        strRecordLabel.c_str(),
        strType.c_str(),
        bCompilation,
        CAlbum::ReleaseTypeToString(releaseType).c_str());

      if (strMusicBrainzAlbumID.empty())
        strSQL += PrepareSQL(", NULL");
      else
        strSQL += PrepareSQL(",'%s'", strMusicBrainzAlbumID.c_str());
      if (strReleaseGroupMBID.empty())
        strSQL += PrepareSQL(", NULL");
      else
        strSQL += PrepareSQL(",'%s'", strReleaseGroupMBID.c_str());
      if (strArtistSort.empty())
        strSQL += PrepareSQL(", NULL");
      else
        strSQL += PrepareSQL(", '%s'", strArtistSort.c_str());
      strSQL += ")";
      m_pDS->exec(strSQL);

      return (int)m_pDS->lastinsertid();
    }
    else
    {
      /* Exists in our database and being re-scanned from tags, so we should update it as the details
         may have changed.

         Note that for multi-folder albums this will mean the last folder scanned will have the information
         stored for it.  Most values here should be the same across all songs anyway, but it does mean
         that if there's any inconsistencies then only the last folders information will be taken.

         We make sure we clear out the link tables (album artists, album genres) and we reset
         the last scraped time to make sure that online metadata is re-fetched. */
      int idAlbum = m_pDS->fv("idAlbum").get_asInt();
      m_pDS->close();

      strSQL = "UPDATE album SET ";
      if (!strMusicBrainzAlbumID.empty())   
        strSQL += PrepareSQL("strAlbum = '%s', strArtistDisp = '%s', ", strAlbum.c_str(), strArtist.c_str());
      if (strReleaseGroupMBID.empty())
        strSQL += PrepareSQL(" strReleaseGroupMBID = NULL,");
      else
        strSQL += PrepareSQL(" strReleaseGroupMBID ='%s', ", strReleaseGroupMBID.c_str());
      if (strArtistSort.empty())
        strSQL += PrepareSQL(" strArtistSort = NULL");
      else
        strSQL += PrepareSQL(" strArtistSort = '%s'", strArtistSort.c_str());
      
      strSQL += PrepareSQL(", strGenres = '%s', iYear=%i, strLabel = '%s', strType = '%s', "
        "bCompilation=%i, strReleaseType = '%s', lastScraped = NULL WHERE idAlbum=%i",
        strGenre.c_str(),
        year,
        strRecordLabel.c_str(),
        strType.c_str(),
        bCompilation,
        CAlbum::ReleaseTypeToString(releaseType).c_str(),
        idAlbum);
      m_pDS->exec(strSQL);
      DeleteAlbumArtistsByAlbum(idAlbum);
      DeleteAlbumGenresByAlbum(idAlbum);
      return idAlbum;
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed with query (%s)", __FUNCTION__, strSQL.c_str());
  }

  return -1;
}

int  CMusicDatabase::UpdateAlbum(int idAlbum,
                                 const std::string& strAlbum, const std::string& strMusicBrainzAlbumID,
                                 const std::string& strReleaseGroupMBID,
                                 const std::string& strArtist, const std::string& strArtistSort,
                                 const std::string& strGenre,
                                 const std::string& strMoods, const std::string& strStyles,
                                 const std::string& strThemes, const std::string& strReview,
                                 const std::string& strImage, const std::string& strLabel,
                                 const std::string& strType,
                                 float fRating, int iUserrating, int iVotes, int iYear, bool bCompilation,
                                 CAlbum::ReleaseType releaseType,
                                 bool bScrapedMBID)
{
  if (idAlbum < 0)
    return -1;

  std::string strSQL;
  strSQL = PrepareSQL("UPDATE album SET "
                      " strAlbum = '%s', strArtistDisp = '%s', strGenres = '%s', "
                      " strMoods = '%s', strStyles = '%s', strThemes = '%s', "
                      " strReview = '%s', strImage = '%s', strLabel = '%s', "
                      " strType = '%s', fRating = %f, iUserrating = %i, iVotes = %i,"
                      " iYear = %i, bCompilation = %i, strReleaseType = '%s', "
                      " lastScraped = '%s', bScrapedMBID = %i",
                      strAlbum.c_str(), strArtist.c_str(), strGenre.c_str(),
                      strMoods.c_str(), strStyles.c_str(), strThemes.c_str(),
                      strReview.c_str(), strImage.c_str(), strLabel.c_str(),
                      strType.c_str(), fRating, iUserrating, iVotes,
                      iYear, bCompilation,
                      CAlbum::ReleaseTypeToString(releaseType).c_str(),
                      CDateTime::GetCurrentDateTime().GetAsDBDateTime().c_str(),
                      bScrapedMBID);
  if (strMusicBrainzAlbumID.empty())
    strSQL += PrepareSQL(", strMusicBrainzAlbumID = NULL");
  else
    strSQL += PrepareSQL(", strMusicBrainzAlbumID = '%s'", strMusicBrainzAlbumID.c_str());
  if (strReleaseGroupMBID.empty())
    strSQL += PrepareSQL(", strReleaseGroupMBID = NULL");
  else
    strSQL += PrepareSQL(", strReleaseGroupMBID = '%s'", strReleaseGroupMBID.c_str());
  if (strArtistSort.empty())
    strSQL += PrepareSQL(", strArtistSort = NULL");
  else
    strSQL += PrepareSQL(", strArtistSort = '%s'", strArtistSort.c_str());

  strSQL += PrepareSQL(" WHERE idAlbum = %i", idAlbum);

  bool status = ExecuteQuery(strSQL);
  if (status)
    AnnounceUpdate(MediaTypeAlbum, idAlbum);
  return idAlbum;
}

bool CMusicDatabase::GetAlbum(int idAlbum, CAlbum& album, bool getSongs /* = true */)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    if (idAlbum == -1)
      return false; // not in the database

    //Get album, song and album song info data using separate queries/datasets because we can have 
    //multiple roles per artist for songs and that makes a single combined join impractical
    //Get album data
    std::string sql;
    sql = PrepareSQL("SELECT albumview.*,albumartistview.* "
      " FROM albumview "
      " JOIN albumartistview ON albumview.idAlbum = albumartistview.idAlbum "
      " WHERE albumview.idAlbum = %ld "
      " ORDER BY albumartistview.iOrder", idAlbum);

    CLog::Log(LOGDEBUG, "%s", sql.c_str());
    if (!m_pDS->query(sql)) return false;
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return false;
    }

    int albumArtistOffset = album_enumCount;

    album = GetAlbumFromDataset(m_pDS.get()->get_sql_record(), 0, true); // true to grab and parse the imageURL
    while (!m_pDS->eof())
    {
      const dbiplus::sql_record* const record = m_pDS->get_sql_record();

      // Album artists always have role = 0 (idRole and strRole columns are in albumartistview to match columns of songartistview)
      // so there is only one row in the result set for each artist credit.      
      album.artistCredits.push_back(GetArtistCreditFromDataset(record, albumArtistOffset));
     
      m_pDS->next();
    }
    m_pDS->close(); // cleanup recordset data

    //Get song data
    if (getSongs)
    {
      sql = PrepareSQL("SELECT songview.*, songartistview.*"
        " FROM songview "
        " JOIN songartistview ON songview.idSong = songartistview.idSong "
        " WHERE songview.idAlbum = %ld "
        " ORDER BY songview.iTrack, songartistview.idRole, songartistview.iOrder", idAlbum);

      CLog::Log(LOGDEBUG, "%s", sql.c_str());
      if (!m_pDS->query(sql)) return false;
      if (m_pDS->num_rows() == 0)  //Album with no songs
      {
        m_pDS->close();
        return false;
      }

      int songArtistOffset = song_enumCount;
      std::set<int> songs;
      while (!m_pDS->eof())
      {
        const dbiplus::sql_record* const record = m_pDS->get_sql_record();

        int idSong = record->at(song_idSong).get_asInt();  //Same as songartist.idSong by join
        if (songs.find(idSong) == songs.end())
        {
          album.songs.emplace_back(GetSongFromDataset(record));
          songs.insert(idSong);
        }

        int idSongArtistRole = record->at(songArtistOffset + artistCredit_idRole).get_asInt();
        //By query order song is the last one appended to the album song vector.                
        if (idSongArtistRole == ROLE_ARTIST)
          album.songs.back().artistCredits.emplace_back(GetArtistCreditFromDataset(record, songArtistOffset));
        else 
          album.songs.back().AppendArtistRole(GetArtistRoleFromDataset(record, songArtistOffset));

        m_pDS->next();
      }
      m_pDS->close(); // cleanup recordset data
    }

    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%i) failed", __FUNCTION__, idAlbum);
  }

  return false;
}

bool CMusicDatabase::ClearAlbumLastScrapedTime(int idAlbum)
{
  std::string strSQL = PrepareSQL("UPDATE album SET lastScraped = NULL WHERE idAlbum = %i", idAlbum);
  return ExecuteQuery(strSQL);
}

bool CMusicDatabase::HasAlbumBeenScraped(int idAlbum)
{
  std::string strSQL = PrepareSQL("SELECT idAlbum FROM album WHERE idAlbum = %i AND lastScraped IS NULL", idAlbum);
  return GetSingleValue(strSQL).empty();
}

int CMusicDatabase::AddGenre(const std::string& strGenre1)
{
  std::string strSQL;
  try
  {
    std::string strGenre = strGenre1;
    StringUtils::Trim(strGenre);

    if (strGenre.empty())
      strGenre=g_localizeStrings.Get(13205); // Unknown

    if (NULL == m_pDB.get()) return -1;
    if (NULL == m_pDS.get()) return -1;

    auto it = m_genreCache.find(strGenre);
    if (it != m_genreCache.end())
      return it->second;


    strSQL=PrepareSQL("select * from genre where strGenre like '%s'", strGenre.c_str());
    m_pDS->query(strSQL);
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      // doesnt exists, add it
      strSQL=PrepareSQL("insert into genre (idGenre, strGenre) values( NULL, '%s' )", strGenre.c_str());
      m_pDS->exec(strSQL);

      int idGenre = (int)m_pDS->lastinsertid();
      m_genreCache.insert(std::pair<std::string, int>(strGenre1, idGenre));
      return idGenre;
    }
    else
    {
      int idGenre = m_pDS->fv("idGenre").get_asInt();
      m_genreCache.insert(std::pair<std::string, int>(strGenre1, idGenre));
      m_pDS->close();
      return idGenre;
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "musicdatabase:unable to addgenre (%s)", strSQL.c_str());
  }

  return -1;
}

bool CMusicDatabase::UpdateArtist(const CArtist& artist)
{
  UpdateArtist(artist.idArtist,
               artist.strArtist, artist.strSortName,
               artist.strMusicBrainzArtistID, artist.bScrapedMBID,
               artist.strBorn, artist.strFormed,
               StringUtils::Join(artist.genre, g_advancedSettings.m_musicItemSeparator),
               StringUtils::Join(artist.moods, g_advancedSettings.m_musicItemSeparator),
               StringUtils::Join(artist.styles, g_advancedSettings.m_musicItemSeparator),
               StringUtils::Join(artist.instruments, g_advancedSettings.m_musicItemSeparator),
               artist.strBiography, artist.strDied,
               artist.strDisbanded,
               StringUtils::Join(artist.yearsActive, g_advancedSettings.m_musicItemSeparator).c_str(),
               artist.thumbURL.m_xml.c_str(),
               artist.fanart.m_xml.c_str());

  DeleteArtistDiscography(artist.idArtist);
  for (const auto &disc : artist.discography)
  {
    AddArtistDiscography(artist.idArtist, disc.first, disc.second);
  }

  return true;
}

int CMusicDatabase::AddArtist(const std::string& strArtist, const std::string& strMusicBrainzArtistID, const std::string& strSortName, bool bScrapedMBID /* = false*/)
{
  std::string strSQL;
  int idArtist = AddArtist(strArtist, strMusicBrainzArtistID, bScrapedMBID);
  if (idArtist < 0 || strSortName.empty())
    return idArtist;
  
  /* Artist sort name always taken as the first value provided that is different from name, so only  
     update when current sort name is blank. If a new sortname the same as name is provided then
     clear any sortname currently held.
  */ 

  try
  {
    if (NULL == m_pDB.get()) return -1;
    if (NULL == m_pDS.get()) return -1;

    strSQL = PrepareSQL("SELECT strArtist, strSortName FROM artist WHERE idArtist = %i", idArtist);
    m_pDS->query(strSQL);
    if (m_pDS->num_rows() != 1)
    {
      m_pDS->close();
      return -1;
    }
    std::string strArtistName, strArtistSort;
    strArtistName = m_pDS->fv("strArtist").get_asString();
    strArtistSort = m_pDS->fv("strSortName").get_asString();
    m_pDS->close();

    if (!strArtistSort.empty())    
    {
      if (strSortName.compare(strArtistName) == 0)
        m_pDS->exec(PrepareSQL("UPDATE artist SET strSortName = NULL WHERE idArtist = %i", idArtist));
    }
    else if (strSortName.compare(strArtistName) != 0)
        m_pDS->exec(PrepareSQL("UPDATE artist SET strSortName = '%s' WHERE idArtist = %i", strSortName.c_str(), idArtist));

    return idArtist;
  }

  catch (...)
  {
    CLog::Log(LOGERROR, "musicdatabase:unable to addartist with sortname (%s)", strSQL.c_str());
  }

  return -1;
}

int CMusicDatabase::AddArtist(const std::string& strArtist, const std::string& strMusicBrainzArtistID, bool bScrapedMBID /* = false*/)
{
  std::string strSQL;
  try
  {
    if (NULL == m_pDB.get()) return -1;
    if (NULL == m_pDS.get()) return -1;

    // 1) MusicBrainz
    if (!strMusicBrainzArtistID.empty())
    {
      // 1.a) Match on a MusicBrainz ID
      strSQL = PrepareSQL("SELECT idArtist, strArtist FROM artist WHERE strMusicBrainzArtistID = '%s'",
        strMusicBrainzArtistID.c_str());
      m_pDS->query(strSQL);
      if (m_pDS->num_rows() > 0)
      {
        int idArtist = (int)m_pDS->fv("idArtist").get_asInt();
        bool update = m_pDS->fv("strArtist").get_asString().compare(strMusicBrainzArtistID) == 0;
        m_pDS->close();
        if (update)
        {
          strSQL = PrepareSQL("UPDATE artist SET strArtist = '%s' WHERE idArtist = %i", strArtist.c_str(), idArtist);
          m_pDS->exec(strSQL);
          m_pDS->close();
        }
        return idArtist;
      }
      m_pDS->close();


      // 1.b) No match on MusicBrainz ID. Look for a previously added artist with no MusicBrainz ID
      //     and update that if it exists.
      strSQL = PrepareSQL("SELECT idArtist FROM artist WHERE strArtist LIKE '%s' AND strMusicBrainzArtistID IS NULL", strArtist.c_str());
      m_pDS->query(strSQL);
      if (m_pDS->num_rows() > 0)
      {
        int idArtist = (int)m_pDS->fv("idArtist").get_asInt();
        m_pDS->close();
        // 1.b.a) We found an artist by name but with no MusicBrainz ID set, update it and assume it is our artist, flag when mbid scraped
        strSQL = PrepareSQL("UPDATE artist SET strArtist = '%s', strMusicBrainzArtistID = '%s', bScrapedMBID = %i WHERE idArtist = %i",
          strArtist.c_str(),
          strMusicBrainzArtistID.c_str(),
          bScrapedMBID,
          idArtist);
        m_pDS->exec(strSQL);
        return idArtist;
      }

      // 2) No MusicBrainz - search for any artist (MB ID or non) with the same name.
      //    With MusicBrainz IDs this could return multiple artists and is non-determinstic
      //    Always pick the first artist ID returned by the DB to return.
    }
    else
    {
      strSQL = PrepareSQL("SELECT idArtist FROM artist WHERE strArtist LIKE '%s'",
        strArtist.c_str());

      m_pDS->query(strSQL);
      if (m_pDS->num_rows() > 0)
      {
        int idArtist = (int)m_pDS->fv("idArtist").get_asInt();
        m_pDS->close();
        return idArtist;
      }
      m_pDS->close();
    }

    // 3) No artist exists at all - add it, flagging when has scraped mbid
    if (strMusicBrainzArtistID.empty())
      strSQL = PrepareSQL("INSERT INTO artist (idArtist, strArtist, strMusicBrainzArtistID) VALUES( NULL, '%s', NULL )",
        strArtist.c_str());
    else
      strSQL = PrepareSQL("INSERT INTO artist (idArtist, strArtist, strMusicBrainzArtistID, bScrapedMBID) VALUES( NULL, '%s', '%s', %i )",
        strArtist.c_str(),
        strMusicBrainzArtistID.c_str(),
        bScrapedMBID);

    m_pDS->exec(strSQL);
    int idArtist = (int)m_pDS->lastinsertid();
    return idArtist;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "musicdatabase:unable to addartist (%s)", strSQL.c_str());
  }

  return -1;
}

int  CMusicDatabase::UpdateArtist(int idArtist,
                                  const std::string& strArtist, const std::string& strSortName,
                                  const std::string& strMusicBrainzArtistID, const bool bScrapedMBID,
                                  const std::string& strBorn, const std::string& strFormed,
                                  const std::string& strGenres, const std::string& strMoods,
                                  const std::string& strStyles, const std::string& strInstruments,
                                  const std::string& strBiography, const std::string& strDied,
                                  const std::string& strDisbanded, const std::string& strYearsActive,
                                  const std::string& strImage, const std::string& strFanart)
{
  CScraperUrl thumbURL;
  CFanart fanart;
  if (idArtist < 0)
    return -1;

  std::string strSQL;
  strSQL = PrepareSQL("UPDATE artist SET "
                      " strArtist = '%s', "
                      " strBorn = '%s', strFormed = '%s', strGenres = '%s', "
                      " strMoods = '%s', strStyles = '%s', strInstruments = '%s', "
                      " strBiography = '%s', strDied = '%s', strDisbanded = '%s', "
                      " strYearsActive = '%s', strImage = '%s', strFanart = '%s', "
                      " lastScraped = '%s', bScrapedMBID = %i",
                      strArtist.c_str(), 
                      /* strSortName.c_str(),*/
                      /* strMusicBrainzArtistID.c_str(), */
                      strBorn.c_str(), strFormed.c_str(), strGenres.c_str(),
                      strMoods.c_str(), strStyles.c_str(), strInstruments.c_str(),
                      strBiography.c_str(), strDied.c_str(), strDisbanded.c_str(),
                      strYearsActive.c_str(), strImage.c_str(), strFanart.c_str(),
                      CDateTime::GetCurrentDateTime().GetAsDBDateTime().c_str(),
                      bScrapedMBID);
  if (strMusicBrainzArtistID.empty())
    strSQL += PrepareSQL(", strMusicBrainzArtistID = NULL");
  else
    strSQL += PrepareSQL(", strMusicBrainzArtistID = '%s'", strMusicBrainzArtistID.c_str());
  if (strSortName.empty())
    strSQL += PrepareSQL(", strSortName = NULL");
  else
    strSQL += PrepareSQL(", strSortName = '%s'", strSortName.c_str());

  strSQL += PrepareSQL(" WHERE idArtist = %i", idArtist);

  bool status = ExecuteQuery(strSQL);
  if (status)
    AnnounceUpdate(MediaTypeArtist, idArtist);
  return idArtist;
}

bool CMusicDatabase::UpdateArtistScrapedMBID(int idArtist, const std::string& strMusicBrainzArtistID)
{
  if (strMusicBrainzArtistID.empty() || idArtist < 0)
    return false;

  // Set scraped artist Musicbrainz ID for a previously added artist with no MusicBrainz ID
  std::string strSQL;
  strSQL = PrepareSQL("UPDATE artist SET strMusicBrainzArtistID = '%s', bScrapedMBID = 1 "
    "WHERE idArtist = %i AND strMusicBrainzArtistID IS NULL", 
    strMusicBrainzArtistID.c_str(), idArtist);

  bool status = ExecuteQuery(strSQL);
  if (status)
  {
    AnnounceUpdate(MediaTypeArtist, idArtist);
    return true;
  }
  return false;
}

bool CMusicDatabase::GetArtist(int idArtist, CArtist &artist, bool fetchAll /* = false */)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    if (idArtist == -1)
      return false; // not in the database

    std::string strSQL;
    if (fetchAll)
      strSQL = PrepareSQL("SELECT * FROM artistview LEFT JOIN discography ON artistview.idArtist = discography.idArtist WHERE artistview.idArtist = %i", idArtist);
    else
      strSQL = PrepareSQL("SELECT * FROM artistview WHERE artistview.idArtist = %i", idArtist);

    if (!m_pDS->query(strSQL)) return false;
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return false;
    }

    int discographyOffset = artist_enumCount;

    artist.discography.clear();
    artist = GetArtistFromDataset(m_pDS.get()->get_sql_record(), 0, fetchAll);
    if (fetchAll)
    {
      while (!m_pDS->eof())
      {
        const dbiplus::sql_record* const record = m_pDS.get()->get_sql_record();

        artist.discography.emplace_back(record->at(discographyOffset + 1).get_asString(), record->at(discographyOffset + 2).get_asString());
        m_pDS->next();
      }
    }
    m_pDS->close(); // cleanup recordset data
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%i) failed", __FUNCTION__, idArtist);
  }

  return false;
}

bool CMusicDatabase::GetArtistExists(int idArtist)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string strSQL = PrepareSQL("SELECT 1 FROM artist WHERE artist.idArtist = %i LIMIT 1", idArtist);

    if (!m_pDS->query(strSQL)) return false;
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return false;
    }    
    m_pDS->close(); // cleanup recordset data
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%i) failed", __FUNCTION__, idArtist);
  }

  return false;
}

bool CMusicDatabase::HasArtistBeenScraped(int idArtist)
{
  std::string strSQL = PrepareSQL("SELECT idArtist FROM artist WHERE idArtist = %i AND lastScraped IS NULL", idArtist);
  return GetSingleValue(strSQL).empty();
}

bool CMusicDatabase::ClearArtistLastScrapedTime(int idArtist)
{
  std::string strSQL = PrepareSQL("UPDATE artist SET lastScraped = NULL WHERE idArtist = %i", idArtist);
  return ExecuteQuery(strSQL);
}

int CMusicDatabase::AddArtistDiscography(int idArtist, const std::string& strAlbum, const std::string& strYear)
{
  std::string strSQL=PrepareSQL("INSERT INTO discography (idArtist, strAlbum, strYear) values(%i, '%s', '%s')",
                               idArtist,
                               strAlbum.c_str(),
                               strYear.c_str());
  return ExecuteQuery(strSQL);
}

bool CMusicDatabase::DeleteArtistDiscography(int idArtist)
{
  std::string strSQL = PrepareSQL("DELETE FROM discography WHERE idArtist = %i", idArtist);
  return ExecuteQuery(strSQL);
}

int CMusicDatabase::AddRole(const std::string &strRole)
{
  int idRole = -1;
  std::string strSQL;

  try
  {
    if (NULL == m_pDB.get()) return -1;
    if (NULL == m_pDS.get()) return -1;
    strSQL = PrepareSQL("SELECT idRole FROM role WHERE strRole LIKE '%s'", strRole.c_str());
    m_pDS->query(strSQL);
    if (m_pDS->num_rows() > 0)
      idRole = m_pDS->fv("idRole").get_asInt();
    m_pDS->close();

    if (idRole < 0)
    {
      strSQL = PrepareSQL("INSERT INTO role (strRole) VALUES ('%s')", strRole.c_str());
      m_pDS->exec(strSQL);
      idRole = static_cast<int>(m_pDS->lastinsertid());
      m_pDS->close();
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "musicdatabase:unable to AddRole (%s)", strSQL.c_str());
  }
  return idRole;
}

bool CMusicDatabase::AddSongArtist(int idArtist, int idSong, const std::string& strRole, const std::string& strArtist, int iOrder)
{
  int idRole = AddRole(strRole);
  return AddSongArtist(idArtist, idSong, idRole, strArtist, iOrder);
}

bool CMusicDatabase::AddSongArtist(int idArtist, int idSong, int idRole, const std::string& strArtist, int iOrder)
{
  std::string strSQL;
  strSQL = PrepareSQL("replace into song_artist (idArtist, idSong, idRole, strArtist, iOrder) values(%i,%i,%i,'%s',%i)",
    idArtist, idSong, idRole, strArtist.c_str(), iOrder);
  return ExecuteQuery(strSQL);
}

int CMusicDatabase::AddSongContributor(int idSong, const std::string& strRole, const std::string& strArtist, const std::string &strSort)
{
  if (strArtist.empty())
    return -1;

  std::string strSQL;
  try
  {
    if (NULL == m_pDB.get()) return -1;
    if (NULL == m_pDS.get()) return -1;

    int idArtist = -1;
    // Add artist. As we only have name (no MBID) first try to identify artist from song 
    // as they may have already been added with a different role (including MBID).
    strSQL = PrepareSQL("SELECT idArtist FROM song_artist WHERE idSong = %i AND strArtist LIKE '%s' ", idSong, strArtist.c_str());
    m_pDS->query(strSQL);
    if (m_pDS->num_rows() > 0)
      idArtist = m_pDS->fv("idArtist").get_asInt();
    m_pDS->close();

    if (idArtist < 0)
      idArtist = AddArtist(strArtist, "", strSort);

    // Add to song_artist table
    AddSongArtist(idArtist, idSong, strRole, strArtist, 0);

    return idArtist;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "musicdatabase:unable to AddSongContributor (%s)", strSQL.c_str());
  }

  return -1;
}

void CMusicDatabase::AddSongContributors(int idSong, const VECMUSICROLES& contributors, const std::string &strSort)
{
  std::vector<std::string> composerSort;
  size_t countComposer = 0;
  if (!strSort.empty())
  {
    composerSort = StringUtils::Split(strSort, g_advancedSettings.m_musicItemSeparator);
  }
  
  for (const auto &credit : contributors)
  {
    std::string strSortName;
    //Identify composer sort name if we have it
    if (countComposer < composerSort.size())
    {
      if (credit.GetRoleDesc().compare("Composer") == 0)
      {
        strSortName = composerSort[countComposer];
        countComposer++;
      }
    }
    AddSongContributor(idSong, credit.GetRoleDesc(), credit.GetArtist(), strSortName);
  }
}

int CMusicDatabase::GetRoleByName(const std::string& strRole)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string strSQL;
    strSQL = PrepareSQL("SELECT idRole FROM role WHERE strRole like '%s'", strRole.c_str());
    // run query
    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound != 1)
    {
      m_pDS->close();
      return -1;
    }
    return m_pDS->fv("idRole").get_asInt();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return -1;

}

bool CMusicDatabase::GetRolesByArtist(int idArtist, CFileItem* item)
{
  try
  {
    std::string strSQL = PrepareSQL("SELECT DISTINCT song_artist.idRole, Role.strRole FROM song_artist JOIN role ON "
                                    " song_artist.idRole = role.idRole WHERE idArtist = %i ORDER BY song_artist.idRole ASC", idArtist);
    if (!m_pDS->query(strSQL))
      return false;
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return true;
    }
    
    CVariant artistRoles(CVariant::VariantTypeArray);
    
    while (!m_pDS->eof())
    {
      CVariant roleObj;
      roleObj["role"] = m_pDS->fv("strRole").get_asString();
      roleObj["roleid"] = m_pDS->fv("idrole").get_asInt();
      artistRoles.push_back(roleObj);
      m_pDS->next();
    }
    m_pDS->close();

    item->SetProperty("roles", artistRoles);
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%i) failed", __FUNCTION__, idArtist);
  }
  return false;
}

bool CMusicDatabase::DeleteSongArtistsBySong(int idSong)
{
  return ExecuteQuery(PrepareSQL("DELETE FROM song_artist WHERE idSong = %i", idSong));
}

bool CMusicDatabase::AddAlbumArtist(int idArtist, int idAlbum, std::string strArtist, int iOrder)
{
  std::string strSQL;
  strSQL = PrepareSQL("replace into album_artist (idArtist, idAlbum, strArtist, iOrder) values(%i,%i,'%s',%i)",
    idArtist, idAlbum, strArtist.c_str(), iOrder);
  return ExecuteQuery(strSQL);
}

bool CMusicDatabase::DeleteAlbumArtistsByAlbum(int idAlbum)
{
  return ExecuteQuery(PrepareSQL("DELETE FROM album_artist WHERE idAlbum = %i", idAlbum));
}

bool CMusicDatabase::AddSongGenre(int idGenre, int idSong, int iOrder)
{
  if (idGenre == -1 || idSong == -1)
    return true;

  std::string strSQL;
  strSQL=PrepareSQL("replace into song_genre (idGenre, idSong, iOrder) values(%i,%i,%i)",
                    idGenre, idSong, iOrder);
  return ExecuteQuery(strSQL);
};

bool CMusicDatabase::AddAlbumGenre(int idGenre, int idAlbum, int iOrder)
{
  if (idGenre == -1 || idAlbum == -1)
    return true;
  
  std::string strSQL;
  strSQL=PrepareSQL("replace into album_genre (idGenre, idAlbum, iOrder) values(%i,%i,%i)",
                    idGenre, idAlbum, iOrder);
  return ExecuteQuery(strSQL);
};

bool CMusicDatabase::DeleteAlbumGenresByAlbum(int idAlbum)
{
  return ExecuteQuery(PrepareSQL("DELETE FROM album_genre WHERE idAlbum = %i", idAlbum));
}

bool CMusicDatabase::GetAlbumsByArtist(int idArtist, std::vector<int> &albums)
{
  try 
  {
    std::string strSQL;
    strSQL = PrepareSQL("SELECT idAlbum  FROM album_artist WHERE idArtist = %i", idArtist);
    if (!m_pDS->query(strSQL)) 
      return false;
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return false;
    }
    
    while (!m_pDS->eof())
    {
      albums.push_back(m_pDS->fv("idAlbum").get_asInt());
      m_pDS->next();
    }
    m_pDS->close();
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%i) failed", __FUNCTION__, idArtist);
  }
  return false;
}

bool CMusicDatabase::GetArtistsByAlbum(int idAlbum, CFileItem* item)
{
  try 
  {
    std::string strSQL;
    
    strSQL = PrepareSQL("SELECT * FROM albumartistview WHERE idAlbum = %i", idAlbum);
    
    if (!m_pDS->query(strSQL)) 
      return false;
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return false;
    }

    // Get album artist credits
    VECARTISTCREDITS artistCredits;
    while (!m_pDS->eof())
    {
      artistCredits.emplace_back(GetArtistCreditFromDataset(m_pDS->get_sql_record(), 0));
      m_pDS->next();
    }
    m_pDS->close();
   
    // Populate item with song albumartist credits
    std::vector<std::string> musicBrainzID;
    std::vector<std::string> albumartists;
    CVariant artistidObj(CVariant::VariantTypeArray);
    for (const auto &artistCredit : artistCredits)
    {
      artistidObj.push_back(artistCredit.GetArtistId());
      albumartists.emplace_back(artistCredit.GetArtist());
      if (!artistCredit.GetMusicBrainzArtistID().empty())
        musicBrainzID.emplace_back(artistCredit.GetMusicBrainzArtistID());
    }
    item->GetMusicInfoTag()->SetAlbumArtist(albumartists);
    item->GetMusicInfoTag()->SetMusicBrainzAlbumArtistID(musicBrainzID);
    // Add song albumartistIds as separate property as not part of CMusicInfoTag
    item->SetProperty("albumartistid", artistidObj);

    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%i) failed", __FUNCTION__, idAlbum);
  }
  return false;
}

bool CMusicDatabase::GetSongsByArtist(int idArtist, std::vector<int> &songs)
{
  try 
  {
    std::string strSQL;
    //Restrict to Artists only, no other roles
    strSQL = PrepareSQL("SELECT idSong FROM song_artist WHERE idArtist = %i AND idRole = 1", idArtist);
    if (!m_pDS->query(strSQL)) 
      return false;
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return false;
    }
    
    while (!m_pDS->eof())
    {
      songs.push_back(m_pDS->fv("idSong").get_asInt());
      m_pDS->next();
    }
    m_pDS->close();
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%i) failed", __FUNCTION__, idArtist);
  }
  return false;
};

bool CMusicDatabase::GetArtistsBySong(int idSong, std::vector<int> &artists)
{
  try 
  {
    std::string strSQL;
    //Restrict to Artists only, no other roles
    strSQL = PrepareSQL("SELECT idArtist FROM song_artist WHERE idSong = %i AND idRole = 1", idSong);
    if (!m_pDS->query(strSQL)) 
      return false;
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return false;
    }

    while (!m_pDS->eof())
    {
      artists.push_back(m_pDS->fv("idArtist").get_asInt());
      m_pDS->next();
    }
    m_pDS->close();
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%i) failed", __FUNCTION__, idSong);
  }
  return false;
}

bool CMusicDatabase::GetGenresByArtist(int idArtist, CFileItem* item)
{
  try
  {
    std::string strSQL = PrepareSQL("SELECT DISTINCT song_genre.idGenre, genre.strGenre FROM "
      "song_artist JOIN song ON song_artist.idSong = song.idSong JOIN "
      "song_genre ON song.idSong = song_genre.idSong JOIN "
      "genre ON song_genre.idGenre = genre.idGenre "
      "WHERE song_artist.idArtist = %i ORDER BY song_genre.idGenre", idArtist);
    if (!m_pDS->query(strSQL))
      return false;
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return true;
    }

    CVariant artistSongGenres(CVariant::VariantTypeArray);

    while (!m_pDS->eof())
    {
      CVariant genreObj;
      genreObj["title"] = m_pDS->fv("strGenre").get_asString();
      genreObj["genreid"] = m_pDS->fv("idGenre").get_asInt();
      artistSongGenres.push_back(genreObj);
      m_pDS->next();
    }
    m_pDS->close();

    item->SetProperty("songgenres", artistSongGenres);
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%i) failed", __FUNCTION__, idArtist);
  }
  return false;
}

bool CMusicDatabase::GetGenresByAlbum(int idAlbum, std::vector<int>& genres)
{
  try
  {
    std::string strSQL = PrepareSQL("select idGenre from album_genre where idAlbum = %i ORDER BY iOrder ASC", idAlbum);
    if (!m_pDS->query(strSQL))
      return false;
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return true;
    }

    while (!m_pDS->eof())
    {
      genres.push_back(m_pDS->fv("idGenre").get_asInt());
      m_pDS->next();
    }
    m_pDS->close();

    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%i) failed", __FUNCTION__, idAlbum);
  }
  return false;
}

bool CMusicDatabase::GetGenresBySong(int idSong, std::vector<int>& genres)
{
  try
  {
    std::string strSQL = PrepareSQL("select idGenre from song_genre where idSong = %i ORDER BY iOrder ASC", idSong);
    if (!m_pDS->query(strSQL))
      return false;
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return true;
    }

    while (!m_pDS->eof())
    {
      genres.push_back(m_pDS->fv("idGenre").get_asInt());
      m_pDS->next();
    }
    m_pDS->close();

    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%i) failed", __FUNCTION__, idSong);
  }
  return false;
}

bool CMusicDatabase::GetIsAlbumArtist(int idArtist, CFileItem* item)
{
  try
  {    
    int countalbum = strtol(GetSingleValue("album_artist", "count(idArtist)", PrepareSQL("idArtist=%i", idArtist)).c_str(), NULL, 10);
    CVariant IsAlbumArtistObj(CVariant::VariantTypeBoolean);
    IsAlbumArtistObj = (countalbum > 0);
    item->SetProperty("isalbumartist", IsAlbumArtistObj);
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%i) failed", __FUNCTION__, idArtist);
  }
  return false;
}


int CMusicDatabase::AddPath(const std::string& strPath1)
{
  std::string strSQL;
  try
  {
    std::string strPath(strPath1);
    if (!URIUtils::HasSlashAtEnd(strPath))
      URIUtils::AddSlashAtEnd(strPath);

    if (NULL == m_pDB.get()) return -1;
    if (NULL == m_pDS.get()) return -1;

    auto it = m_pathCache.find(strPath);
    if (it != m_pathCache.end())
      return it->second;

    strSQL=PrepareSQL( "select * from path where strPath='%s'", strPath.c_str());
    m_pDS->query(strSQL);
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      // doesnt exists, add it
      strSQL=PrepareSQL("insert into path (idPath, strPath) values( NULL, '%s' )", strPath.c_str());
      m_pDS->exec(strSQL);

      int idPath = (int)m_pDS->lastinsertid();
      m_pathCache.insert(std::pair<std::string, int>(strPath, idPath));
      return idPath;
    }
    else
    {
      int idPath = m_pDS->fv("idPath").get_asInt();
      m_pathCache.insert(std::pair<std::string, int>(strPath, idPath));
      m_pDS->close();
      return idPath;
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "musicdatabase:unable to addpath (%s)", strSQL.c_str());
  }

  return -1;
}

CSong CMusicDatabase::GetSongFromDataset()
{
  return GetSongFromDataset(m_pDS->get_sql_record());
}

CSong CMusicDatabase::GetSongFromDataset(const dbiplus::sql_record* const record, int offset /* = 0 */)
{
  CSong song;
  song.idSong = record->at(offset + song_idSong).get_asInt();
  // Note this function does not populate artist credits, this must be done separately.
  // However artist names are held as a descriptive string
  song.strArtistDesc = record->at(offset + song_strArtists).get_asString();
  song.strArtistSort = record->at(offset + song_strArtistSort).get_asString();
  // Get the full genre string
  song.genre = StringUtils::Split(record->at(offset + song_strGenres).get_asString(), g_advancedSettings.m_musicItemSeparator);
  // and the rest...
  song.strAlbum = record->at(offset + song_strAlbum).get_asString();
  song.idAlbum = record->at(offset + song_idAlbum).get_asInt();
  song.iTrack = record->at(offset + song_iTrack).get_asInt() ;
  song.iDuration = record->at(offset + song_iDuration).get_asInt() ;
  song.iYear = record->at(offset + song_iYear).get_asInt() ;
  song.strTitle = record->at(offset + song_strTitle).get_asString();
  song.iTimesPlayed = record->at(offset + song_iTimesPlayed).get_asInt();
  song.lastPlayed.SetFromDBDateTime(record->at(offset + song_lastplayed).get_asString());
  song.dateAdded.SetFromDBDateTime(record->at(offset + song_dateAdded).get_asString());
  song.iStartOffset = record->at(offset + song_iStartOffset).get_asInt();
  song.iEndOffset = record->at(offset + song_iEndOffset).get_asInt();
  song.strMusicBrainzTrackID = record->at(offset + song_strMusicBrainzTrackID).get_asString();
  song.rating = record->at(offset + song_rating).get_asFloat();
  song.userrating = record->at(offset + song_userrating).get_asInt();
  song.votes = record->at(offset + song_votes).get_asInt();
  song.strComment = record->at(offset + song_comment).get_asString();
  song.strMood = record->at(offset + song_mood).get_asString();
  song.bCompilation = record->at(offset + song_bCompilation).get_asInt() == 1;
  // Replay gain data (needed for songs from cuesheets, both separate .cue files and embedded metadata)
  song.replayGain.Set(record->at(offset + song_strReplayGain).get_asString());  
  // Get filename with full path
  song.strFileName = URIUtils::AddFileToFolder(record->at(offset + song_strPath).get_asString(), record->at(offset + song_strFileName).get_asString());
  return song;
}

void CMusicDatabase::GetFileItemFromDataset(CFileItem* item, const CMusicDbUrl &baseUrl)
{
  GetFileItemFromDataset(m_pDS->get_sql_record(), item, baseUrl);
}

void CMusicDatabase::GetFileItemFromDataset(const dbiplus::sql_record* const record, CFileItem* item, const CMusicDbUrl &baseUrl)
{
  // get the artist string from songview (not the song_artist and artist tables)
  item->GetMusicInfoTag()->SetArtistDesc(record->at(song_strArtists).get_asString());
  // get the artist sort name string from songview (not the song_artist and artist tables)
  item->GetMusicInfoTag()->SetArtistSort(record->at(song_strArtistSort).get_asString());
  // and the full genre string
  item->GetMusicInfoTag()->SetGenre(record->at(song_strGenres).get_asString());
  // and the rest...
  item->GetMusicInfoTag()->SetAlbum(record->at(song_strAlbum).get_asString());
  item->GetMusicInfoTag()->SetAlbumId(record->at(song_idAlbum).get_asInt());
  item->GetMusicInfoTag()->SetTrackAndDiscNumber(record->at(song_iTrack).get_asInt());
  item->GetMusicInfoTag()->SetDuration(record->at(song_iDuration).get_asInt());
  item->GetMusicInfoTag()->SetDatabaseId(record->at(song_idSong).get_asInt(), MediaTypeSong);
  SYSTEMTIME stTime;
  stTime.wYear = (WORD)record->at(song_iYear).get_asInt();
  item->GetMusicInfoTag()->SetReleaseDate(stTime);
  item->GetMusicInfoTag()->SetTitle(record->at(song_strTitle).get_asString());
  item->SetLabel(record->at(song_strTitle).get_asString());
  item->m_lStartOffset = record->at(song_iStartOffset).get_asInt();
  item->SetProperty("item_start", item->m_lStartOffset);
  item->m_lEndOffset = record->at(song_iEndOffset).get_asInt();
  item->GetMusicInfoTag()->SetMusicBrainzTrackID(record->at(song_strMusicBrainzTrackID).get_asString());
  item->GetMusicInfoTag()->SetRating(record->at(song_rating).get_asFloat());
  item->GetMusicInfoTag()->SetUserrating(record->at(song_userrating).get_asInt());
  item->GetMusicInfoTag()->SetVotes(record->at(song_votes).get_asInt());
  item->GetMusicInfoTag()->SetComment(record->at(song_comment).get_asString());
  item->GetMusicInfoTag()->SetMood(record->at(song_mood).get_asString());
  item->GetMusicInfoTag()->SetPlayCount(record->at(song_iTimesPlayed).get_asInt());
  item->GetMusicInfoTag()->SetLastPlayed(record->at(song_lastplayed).get_asString());
  item->GetMusicInfoTag()->SetDateAdded(record->at(song_dateAdded).get_asString());
  std::string strRealPath = URIUtils::AddFileToFolder(record->at(song_strPath).get_asString(), record->at(song_strFileName).get_asString());
  item->GetMusicInfoTag()->SetURL(strRealPath);
  item->GetMusicInfoTag()->SetCompilation(record->at(song_bCompilation).get_asInt() == 1);
  // get the album artist string from songview (not the album_artist and artist tables)
  item->GetMusicInfoTag()->SetAlbumArtist(record->at(song_strAlbumArtists).get_asString());
  item->GetMusicInfoTag()->SetAlbumReleaseType(CAlbum::ReleaseTypeFromString(record->at(song_strAlbumReleaseType).get_asString()));
  // Replay gain data (needed for songs from cuesheets, both separate .cue files and embedded metadata)
  ReplayGain replaygain;
  replaygain.Set(record->at(song_strReplayGain).get_asString());
  item->GetMusicInfoTag()->SetReplayGain(replaygain);

  item->GetMusicInfoTag()->SetLoaded(true);
  // Get filename with full path
  if (!baseUrl.IsValid())
    item->SetPath(strRealPath);
  else
  {
    CMusicDbUrl itemUrl = baseUrl;
    std::string strFileName = record->at(song_strFileName).get_asString();
    std::string strExt = URIUtils::GetExtension(strFileName);
    std::string path = StringUtils::Format("%i%s", record->at(song_idSong).get_asInt(), strExt.c_str());
    itemUrl.AppendPath(path);
    item->SetPath(itemUrl.ToString());
  }
}

void CMusicDatabase::GetFileItemFromArtistCredits(VECARTISTCREDITS& artistCredits, CFileItem* item)
{
  // Populate fileitem with artists from vector of artist credits
  std::vector<std::string> musicBrainzID;
  std::vector<std::string> songartists;
  CVariant artistidObj(CVariant::VariantTypeArray);

  // When "missing tag" artist, it is the only artist when present.
  if (artistCredits.begin()->GetArtistId() == BLANKARTIST_ID)
  {
    artistidObj.push_back((int)BLANKARTIST_ID);
    songartists.push_back(StringUtils::Empty);
  }
  else
  {
    for (const auto &artistCredit : artistCredits)
    {
      artistidObj.push_back(artistCredit.GetArtistId());
      songartists.push_back(artistCredit.GetArtist());
      if (!artistCredit.GetMusicBrainzArtistID().empty())
        musicBrainzID.push_back(artistCredit.GetMusicBrainzArtistID());
    }
  }
  item->GetMusicInfoTag()->SetArtist(songartists); // Also sets ArtistDesc if empty from song.strArtist field
  item->GetMusicInfoTag()->SetMusicBrainzArtistID(musicBrainzID);
  // Add album artistIds as separate property as not part of CMusicInfoTag
  item->SetProperty("artistid", artistidObj);
}

CAlbum CMusicDatabase::GetAlbumFromDataset(dbiplus::Dataset* pDS, int offset /* = 0 */, bool imageURL /* = false*/)
{
  return GetAlbumFromDataset(pDS->get_sql_record(), offset, imageURL);
}

CAlbum CMusicDatabase::GetAlbumFromDataset(const dbiplus::sql_record* const record, int offset /* = 0 */, bool imageURL /* = false*/)
{
  CAlbum album;
  album.idAlbum = record->at(offset + album_idAlbum).get_asInt();
  album.strAlbum = record->at(offset + album_strAlbum).get_asString();
  if (album.strAlbum.empty())
    album.strAlbum = g_localizeStrings.Get(1050);
  album.strMusicBrainzAlbumID = record->at(offset + album_strMusicBrainzAlbumID).get_asString();
  album.strReleaseGroupMBID = record->at(offset + album_strReleaseGroupMBID).get_asString();
  album.strArtistDesc = record->at(offset + album_strArtists).get_asString();
  album.strArtistSort = record->at(offset + album_strArtistSort).get_asString();
  album.genre = StringUtils::Split(record->at(offset + album_strGenres).get_asString(), g_advancedSettings.m_musicItemSeparator);
  album.iYear = record->at(offset + album_iYear).get_asInt();
  if (imageURL)
    album.thumbURL.ParseString(record->at(offset + album_strThumbURL).get_asString());
  album.fRating = record->at(offset + album_fRating).get_asFloat();
  album.iUserrating = record->at(offset + album_iUserrating).get_asInt();
  album.iVotes = record->at(offset + album_iVotes).get_asInt();
  album.iYear = record->at(offset + album_iYear).get_asInt();
  album.strReview = record->at(offset + album_strReview).get_asString();
  album.styles = StringUtils::Split(record->at(offset + album_strStyles).get_asString(), g_advancedSettings.m_musicItemSeparator);
  album.moods = StringUtils::Split(record->at(offset + album_strMoods).get_asString(), g_advancedSettings.m_musicItemSeparator);
  album.themes = StringUtils::Split(record->at(offset + album_strThemes).get_asString(), g_advancedSettings.m_musicItemSeparator);
  album.strLabel = record->at(offset + album_strLabel).get_asString();
  album.strType = record->at(offset + album_strType).get_asString();
  album.bCompilation = record->at(offset + album_bCompilation).get_asInt() == 1;
  album.bScrapedMBID = record->at(offset + album_bScrapedMBID).get_asInt() == 1;
  album.strLastScraped = record->at(offset + album_lastScraped).get_asString();
  album.iTimesPlayed = record->at(offset + album_iTimesPlayed).get_asInt();
  album.SetReleaseType(record->at(offset + album_strReleaseType).get_asString());
  album.SetDateAdded(record->at(offset + album_dtDateAdded).get_asString());
  album.SetLastPlayed(record->at(offset + album_dtLastPlayed).get_asString());
  return album;
}

CArtistCredit CMusicDatabase::GetArtistCreditFromDataset(const dbiplus::sql_record* const record, int offset /* = 0 */)
{
  CArtistCredit artistCredit;
  artistCredit.idArtist = record->at(offset + artistCredit_idArtist).get_asInt();
  if (artistCredit.idArtist == BLANKARTIST_ID)
    artistCredit.m_strArtist = StringUtils::Empty;
  else
  {
    artistCredit.m_strArtist = record->at(offset + artistCredit_strArtist).get_asString();
    artistCredit.m_strMusicBrainzArtistID = record->at(offset + artistCredit_strMusicBrainzArtistID).get_asString();
  }
  return artistCredit;
}

CMusicRole CMusicDatabase::GetArtistRoleFromDataset(const dbiplus::sql_record* const record, int offset /* = 0 */)
{
  CMusicRole ArtistRole(record->at(offset + artistCredit_idRole).get_asInt(), 
                        record->at(offset + artistCredit_strRole).get_asString(),
                        record->at(offset + artistCredit_strArtist).get_asString(),
                        record->at(offset + artistCredit_idArtist).get_asInt());
  return ArtistRole;
}

CArtist CMusicDatabase::GetArtistFromDataset(dbiplus::Dataset* pDS, int offset /* = 0 */, bool needThumb /* = true */)
{
  return GetArtistFromDataset(pDS->get_sql_record(), offset, needThumb);
}

CArtist CMusicDatabase::GetArtistFromDataset(const dbiplus::sql_record* const record, int offset /* = 0 */, bool needThumb /* = true */)
{
  CArtist artist;
  artist.idArtist = record->at(offset + artist_idArtist).get_asInt();
  if (artist.idArtist == BLANKARTIST_ID && m_translateBlankArtist)
    artist.strArtist = g_localizeStrings.Get(38042);  //Missing artist tag in current language
  else
    artist.strArtist = record->at(offset + artist_strArtist).get_asString();
  artist.strSortName = record->at(offset + artist_strSortName).get_asString();
  artist.strMusicBrainzArtistID = record->at(offset + artist_strMusicBrainzArtistID).get_asString();
  artist.genre = StringUtils::Split(record->at(offset + artist_strGenres).get_asString(), g_advancedSettings.m_musicItemSeparator);
  artist.strBiography = record->at(offset + artist_strBiography).get_asString();
  artist.styles = StringUtils::Split(record->at(offset + artist_strStyles).get_asString(), g_advancedSettings.m_musicItemSeparator);
  artist.moods = StringUtils::Split(record->at(offset + artist_strMoods).get_asString(), g_advancedSettings.m_musicItemSeparator);
  artist.strBorn = record->at(offset + artist_strBorn).get_asString();
  artist.strFormed = record->at(offset + artist_strFormed).get_asString();
  artist.strDied = record->at(offset + artist_strDied).get_asString();
  artist.strDisbanded = record->at(offset + artist_strDisbanded).get_asString();
  artist.yearsActive = StringUtils::Split(record->at(offset + artist_strYearsActive).get_asString(), g_advancedSettings.m_musicItemSeparator);
  artist.instruments = StringUtils::Split(record->at(offset + artist_strInstruments).get_asString(), g_advancedSettings.m_musicItemSeparator);
  artist.bScrapedMBID = record->at(offset + artist_bScrapedMBID).get_asInt() == 1;
  artist.strLastScraped = record->at(offset + artist_lastScraped).get_asString();
  artist.SetDateAdded(record->at(offset + artist_dtDateAdded).get_asString());

  if (needThumb)
  {
    artist.fanart.m_xml = record->at(artist_strFanart).get_asString();
    artist.fanart.Unpack();
    artist.thumbURL.ParseString(record->at(artist_strImage).get_asString());
  }

  return artist;
}

bool CMusicDatabase::GetSongByFileName(const std::string& strFileNameAndPath, CSong& song, int startOffset)
{
  song.Clear();
  CURL url(strFileNameAndPath);

  if (url.IsProtocol("musicdb"))
  {
    std::string strFile = URIUtils::GetFileName(strFileNameAndPath);
    URIUtils::RemoveExtension(strFile);
    return GetSong(atol(strFile.c_str()), song);
  }

  if (NULL == m_pDB.get()) return false;
  if (NULL == m_pDS.get()) return false;

  std::string strPath, strFileName;
  URIUtils::Split(strFileNameAndPath, strPath, strFileName);
  URIUtils::AddSlashAtEnd(strPath);

  std::string strSQL = PrepareSQL("select idSong from songview "
                                 "where strFileName='%s' and strPath='%s'",
                                 strFileName.c_str(), strPath.c_str());
  if (startOffset)
    strSQL += PrepareSQL(" AND iStartOffset=%i", startOffset);

  int idSong = (int)strtol(GetSingleValue(strSQL).c_str(), NULL, 10);
  if (idSong > 0)
    return GetSong(idSong, song);

  return false;
}

int CMusicDatabase::GetAlbumIdByPath(const std::string& strPath)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string strSQL = PrepareSQL("SELECT DISTINCT idAlbum FROM song JOIN path ON song.idPath = path.idPath WHERE path.strPath='%s'", strPath.c_str());
    // run query
    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();

    int idAlbum = -1; // If no album is found, or more than one album is found then -1 is returned
    if (iRowsFound == 1)
      idAlbum = m_pDS->fv(0).get_asInt();
    
    m_pDS->close();

    return idAlbum;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%s) failed", __FUNCTION__, strPath.c_str());
  }

  return -1;
}

int CMusicDatabase::GetSongByArtistAndAlbumAndTitle(const std::string& strArtist, const std::string& strAlbum, const std::string& strTitle)
{
  try
  {
    std::string strSQL=PrepareSQL("select idSong from songview "
                                "where strArtists like '%s' and strAlbum like '%s' and "
                                "strTitle like '%s'",strArtist.c_str(),strAlbum.c_str(),strTitle.c_str());

    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return -1;
    }
    int lResult = m_pDS->fv(0).get_asInt();
    m_pDS->close(); // cleanup recordset data
    return lResult;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s (%s,%s,%s) failed", __FUNCTION__, strArtist.c_str(),strAlbum.c_str(),strTitle.c_str());
  }

  return -1;
}

bool CMusicDatabase::SearchArtists(const std::string& search, CFileItemList &artists)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string strVariousArtists = g_localizeStrings.Get(340).c_str();
    std::string strSQL;
    if (search.size() >= MIN_FULL_SEARCH_LENGTH)
      strSQL=PrepareSQL("select * from artist "
                                "where (strArtist like '%s%%' or strArtist like '%% %s%%') and strArtist <> '%s' "
                                , search.c_str(), search.c_str(), strVariousArtists.c_str() );
    else
      strSQL=PrepareSQL("select * from artist "
                                "where strArtist like '%s%%' and strArtist <> '%s' "
                                , search.c_str(), strVariousArtists.c_str() );

    if (!m_pDS->query(strSQL)) return false;
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return false;
    }

    std::string artistLabel(g_localizeStrings.Get(557)); // Artist
    while (!m_pDS->eof())
    {
      std::string path = StringUtils::Format("musicdb://artists/%i/", m_pDS->fv(0).get_asInt());
      CFileItemPtr pItem(new CFileItem(path, true));
      std::string label = StringUtils::Format("[%s] %s", artistLabel.c_str(), m_pDS->fv(1).get_asString().c_str());
      pItem->SetLabel(label);
      label = StringUtils::Format("A %s", m_pDS->fv(1).get_asString().c_str()); // sort label is stored in the title tag
      pItem->GetMusicInfoTag()->SetTitle(label);
      pItem->GetMusicInfoTag()->SetDatabaseId(m_pDS->fv(0).get_asInt(), MediaTypeArtist);
      artists.Add(pItem);
      m_pDS->next();
    }

    m_pDS->close(); // cleanup recordset data
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }

  return false;
}

bool CMusicDatabase::GetTop100(const std::string& strBaseDir, CFileItemList& items)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    CMusicDbUrl baseUrl;
    if (!strBaseDir.empty() && !baseUrl.FromString(strBaseDir))
      return false;

    std::string strSQL="select * from songview "
                      "where iTimesPlayed>0 "
                      "order by iTimesPlayed desc "
                      "limit 100";

    CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());
    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }
    items.Reserve(iRowsFound);
    while (!m_pDS->eof())
    {
      CFileItemPtr item(new CFileItem);
      GetFileItemFromDataset(item.get(), baseUrl);
      items.Add(item);
      m_pDS->next();
    }

    m_pDS->close(); // cleanup recordset data
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }

  return false;
}

bool CMusicDatabase::GetTop100Albums(VECALBUMS& albums)
{
  try
  {
    albums.erase(albums.begin(), albums.end());
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    // Get data from album and album_artist tables to fully populate albums
    std::string strSQL = "SELECT albumview.*, albumartistview.* FROM albumview "
      "JOIN albumartistview ON albumview.idAlbum = albumartistview.idAlbum "
      "WHERE albumartistview.idAlbum in "
      "(SELECT albumview.idAlbum FROM albumview "
      "WHERE albumview.strAlbum != '' AND albumview.iTimesPlayed>0 "
      "ORDER BY albumview.iTimesPlayed DESC LIMIT 100) "
      "ORDER BY albumview.iTimesPlayed DESC, albumartistview.iOrder";

    CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());
    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }

    int albumArtistOffset = album_enumCount;
    int albumId = -1;
    while (!m_pDS->eof())
    {
      const dbiplus::sql_record* const record = m_pDS->get_sql_record();

      if (albumId != record->at(album_idAlbum).get_asInt())
      { // New album
        albumId = record->at(album_idAlbum).get_asInt();
        albums.push_back(GetAlbumFromDataset(record));
      }
      // Get album artists
      albums.back().artistCredits.push_back(GetArtistCreditFromDataset(record, albumArtistOffset));
   
      m_pDS->next();
    }

    m_pDS->close(); // cleanup recordset data
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }

  return false;
}

bool CMusicDatabase::GetTop100AlbumSongs(const std::string& strBaseDir, CFileItemList& items)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    CMusicDbUrl baseUrl;
    if (!strBaseDir.empty() && baseUrl.FromString(strBaseDir))
      return false;

    std::string strSQL = StringUtils::Format("SELECT songview.*, albumview.* FROM songview JOIN albumview ON (songview.idAlbum = albumview.idAlbum) JOIN (SELECT song.idAlbum, SUM(song.iTimesPlayed) AS iTimesPlayedSum FROM song WHERE song.iTimesPlayed > 0 GROUP BY idAlbum ORDER BY iTimesPlayedSum DESC LIMIT 100) AS _albumlimit ON (songview.idAlbum = _albumlimit.idAlbum) ORDER BY _albumlimit.iTimesPlayedSum DESC");
    CLog::Log(LOGDEBUG,"GetTop100AlbumSongs() query: %s", strSQL.c_str());
    if (!m_pDS->query(strSQL)) return false;

    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }

    // get data from returned rows
    items.Reserve(iRowsFound);
    while (!m_pDS->eof())
    {
      CFileItemPtr item(new CFileItem);
      GetFileItemFromDataset(item.get(), baseUrl);
      items.Add(item);
      m_pDS->next();
    }

    // cleanup
    m_pDS->close();
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return false;
}

bool CMusicDatabase::GetRecentlyPlayedAlbums(VECALBUMS& albums)
{
  try
  {
    albums.erase(albums.begin(), albums.end());
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    // Get data from album and album_artist tables to fully populate albums
    std::string strSQL = PrepareSQL("SELECT albumview.*, albumartistview.* FROM "
      "(SELECT idAlbum FROM albumview WHERE albumview.lastplayed IS NOT NULL "
      "AND albumview.strReleaseType = '%s' "
      "ORDER BY albumview.lastplayed DESC LIMIT %u) as playedalbums "
      "JOIN albumview ON albumview.idAlbum = playedalbums.idAlbum "
      "JOIN albumartistview ON albumview.idAlbum = albumartistview.idAlbum "
      "ORDER BY albumview.lastplayed DESC, albumartistview.iorder ", 
      CAlbum::ReleaseTypeToString(CAlbum::Album).c_str(), RECENTLY_PLAYED_LIMIT);

    CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());
    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }

    int albumArtistOffset = album_enumCount;
    int albumId = -1;
    while (!m_pDS->eof())
    {
      const dbiplus::sql_record* const record = m_pDS->get_sql_record();

      if (albumId != record->at(album_idAlbum).get_asInt())
      { // New album
        albumId = record->at(album_idAlbum).get_asInt();
        albums.push_back(GetAlbumFromDataset(record));
      }
      // Get album artists
      albums.back().artistCredits.push_back(GetArtistCreditFromDataset(record, albumArtistOffset));

      m_pDS->next();
    }
    m_pDS->close(); // cleanup recordset data
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }

  return false;
}

bool CMusicDatabase::GetRecentlyPlayedAlbumSongs(const std::string& strBaseDir, CFileItemList& items)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    CMusicDbUrl baseUrl;
    if (!strBaseDir.empty() && !baseUrl.FromString(strBaseDir))
      return false;

    std::string strSQL = PrepareSQL("SELECT songview.*, songartistview.* FROM "
      "(SELECT idAlbum, lastPlayed FROM albumview WHERE albumview.lastplayed IS NOT NULL "
      "ORDER BY albumview.lastplayed DESC LIMIT %u) as playedalbums "
      "JOIN songview ON songview.idAlbum = playedalbums.idAlbum "
      "JOIN songartistview ON songview.idSong = songartistview.idSong "
      "ORDER BY playedalbums.lastplayed DESC,songartistview.idsong, songartistview.idRole, songartistview.iOrder",
      g_advancedSettings.m_iMusicLibraryRecentlyAddedItems);
    CLog::Log(LOGDEBUG,"GetRecentlyPlayedAlbumSongs() query: %s", strSQL.c_str());
    if (!m_pDS->query(strSQL)) return false;

    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }

    // Needs a separate query to determine number of songs to set items size.
    // Get songs from returned rows. Join means there is a row for every song artist
    // Gather artist credits, rather than append to item as go along, so can return array of artistIDs too
    int songArtistOffset = song_enumCount;
    int songId = -1;
    VECARTISTCREDITS artistCredits;
    while (!m_pDS->eof())
    {
      const dbiplus::sql_record* const record = m_pDS->get_sql_record();

      int idSongArtistRole = record->at(songArtistOffset + artistCredit_idRole).get_asInt();
      if (songId != record->at(song_idSong).get_asInt())
      { //New song
        if (songId > 0 && !artistCredits.empty())
        {
          //Store artist credits for previous song
          GetFileItemFromArtistCredits(artistCredits, items[items.Size() - 1].get());
          artistCredits.clear();
        }
        songId = record->at(song_idSong).get_asInt();
        CFileItemPtr item(new CFileItem);
        GetFileItemFromDataset(record, item.get(), baseUrl);
        items.Add(item);
      }
      // Get song artist credits and contributors
      if (idSongArtistRole == ROLE_ARTIST)
        artistCredits.push_back(GetArtistCreditFromDataset(record, songArtistOffset));
      else
        items[items.Size() - 1]->GetMusicInfoTag()->AppendArtistRole(GetArtistRoleFromDataset(record, songArtistOffset));

      m_pDS->next();
    }
    if (!artistCredits.empty())
    {
      //Store artist credits for final song
      GetFileItemFromArtistCredits(artistCredits, items[items.Size() - 1].get());
      artistCredits.clear();
    }

    // cleanup
    m_pDS->close();
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return false;
}

bool CMusicDatabase::GetRecentlyAddedAlbums(VECALBUMS& albums, unsigned int limit)
{
  try
  {
    albums.erase(albums.begin(), albums.end());
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    // Get data from album and album_artist tables to fully populate albums
    // Use idAlbum to determine the recently added albums 
    // (not "dateAdded" as this is file time stamp and nothing to do with when albums added to library)
    std::string strSQL = PrepareSQL("SELECT albumview.*, albumartistview.* FROM "
      "(SELECT idAlbum FROM album WHERE strAlbum != '' ORDER BY idAlbum DESC LIMIT %u) AS recentalbums "
      "JOIN albumview ON albumview.idAlbum = recentalbums.idAlbum "
      "JOIN albumartistview ON albumview.idAlbum = albumartistview.idAlbum "
      "ORDER BY albumview.idAlbum desc, albumartistview.iOrder ",
       limit ? limit : g_advancedSettings.m_iMusicLibraryRecentlyAddedItems);

    CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());
    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }

    int albumArtistOffset = album_enumCount;
    int albumId = -1;
    while (!m_pDS->eof())
    {
      const dbiplus::sql_record* const record = m_pDS->get_sql_record();

      if (albumId != record->at(album_idAlbum).get_asInt())
      { // New album
        albumId = record->at(album_idAlbum).get_asInt();
        albums.push_back(GetAlbumFromDataset(record));
      }
      // Get album artists
      albums.back().artistCredits.push_back(GetArtistCreditFromDataset(record, albumArtistOffset));

      m_pDS->next();
    }
    m_pDS->close(); // cleanup recordset data
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }

  return false;
}

bool CMusicDatabase::GetRecentlyAddedAlbumSongs(const std::string& strBaseDir, CFileItemList& items, unsigned int limit)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    CMusicDbUrl baseUrl;
    if (!strBaseDir.empty() && !baseUrl.FromString(strBaseDir))
      return false;

    // Get data from song and song_artist tables to fully populate songs
    // Use idAlbum to determine the recently added albums 
    // (not "dateAdded" as this is file time stamp and nothing to do with when albums added to library)
    std::string strSQL;
    strSQL = PrepareSQL("SELECT songview.*, songartistview.* FROM "
        "(SELECT idAlbum FROM album ORDER BY idAlbum DESC LIMIT %u) AS recentalbums " 
        "JOIN songview ON songview.idAlbum = recentalbums.idAlbum "
        "JOIN songartistview ON songview.idSong = songartistview.idSong "
        "ORDER BY songview.idAlbum DESC, songview.idSong, songartistview.idRole, songartistview.iOrder ",
        limit ? limit : g_advancedSettings.m_iMusicLibraryRecentlyAddedItems);
    CLog::Log(LOGDEBUG,"GetRecentlyAddedAlbumSongs() query: %s", strSQL.c_str());
    if (!m_pDS->query(strSQL)) return false;

    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }

    // Needs a separate query to determine number of songs to set items size.
    // Get songs from returned rows. Join means there is a row for every song artist
    int songArtistOffset = song_enumCount;
    int songId = -1;
    VECARTISTCREDITS artistCredits;
    while (!m_pDS->eof())
    {
      const dbiplus::sql_record* const record = m_pDS->get_sql_record();

      int idSongArtistRole = record->at(songArtistOffset + artistCredit_idRole).get_asInt();
      if (songId != record->at(song_idSong).get_asInt())
      { //New song
        if (songId > 0 && !artistCredits.empty())
        {
          //Store artist credits for previous song
          GetFileItemFromArtistCredits(artistCredits, items[items.Size() - 1].get());
          artistCredits.clear();
        }
        songId = record->at(song_idSong).get_asInt();
        CFileItemPtr item(new CFileItem);
        GetFileItemFromDataset(record, item.get(), baseUrl);
        items.Add(item);
      }
      // Get song artist credits and contributors
      if (idSongArtistRole == ROLE_ARTIST)
        artistCredits.push_back(GetArtistCreditFromDataset(record, songArtistOffset));
      else
        items[items.Size() - 1]->GetMusicInfoTag()->AppendArtistRole(GetArtistRoleFromDataset(record, songArtistOffset));

      m_pDS->next();
    }
    if (!artistCredits.empty())
    {
      //Store artist credits for final song
      GetFileItemFromArtistCredits(artistCredits, items[items.Size() - 1].get());
      artistCredits.clear();
    }

    // cleanup
    m_pDS->close();
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return false;
}

void CMusicDatabase::IncrementPlayCount(const CFileItem& item)
{
  try
  {
    if (NULL == m_pDB.get()) return;
    if (NULL == m_pDS.get()) return;

    int idSong = GetSongIDFromPath(item.GetPath());

    std::string sql=PrepareSQL("UPDATE song SET iTimesPlayed=iTimesPlayed+1, lastplayed=CURRENT_TIMESTAMP where idSong=%i", idSong);
    m_pDS->exec(sql);
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%s) failed", __FUNCTION__, item.GetPath().c_str());
  }
}

bool CMusicDatabase::GetSongsByPath(const std::string& strPath1, MAPSONGS& songs, bool bAppendToMap)
{
  std::string strPath(strPath1);
  try
  {
    if (!URIUtils::HasSlashAtEnd(strPath))
      URIUtils::AddSlashAtEnd(strPath);

    if (!bAppendToMap)
      songs.clear();

    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string strSQL=PrepareSQL("SELECT * FROM songview WHERE strPath='%s'", strPath.c_str()); 
    if (!m_pDS->query(strSQL)) return false;
    CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return false;
    }
    while (!m_pDS->eof())
    {
      CSong song = GetSongFromDataset();
      // For songs from cue sheets strFileName is not unique, so only 1st song gets added to song map
      songs.insert(std::make_pair(song.strFileName, song));
      m_pDS->next();
    }
    m_pDS->close(); // cleanup recordset data
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%s) failed", __FUNCTION__, strPath.c_str());
  }

  return false;
}

void CMusicDatabase::EmptyCache()
{
  m_genreCache.erase(m_genreCache.begin(), m_genreCache.end());
  m_pathCache.erase(m_pathCache.begin(), m_pathCache.end());
}

bool CMusicDatabase::Search(const std::string& search, CFileItemList &items)
{
  unsigned int time = XbmcThreads::SystemClockMillis();
  // first grab all the artists that match
  SearchArtists(search, items);
  CLog::Log(LOGDEBUG, "%s Artist search in %i ms",
            __FUNCTION__, XbmcThreads::SystemClockMillis() - time); time = XbmcThreads::SystemClockMillis();

  // then albums that match
  SearchAlbums(search, items);
  CLog::Log(LOGDEBUG, "%s Album search in %i ms",
            __FUNCTION__, XbmcThreads::SystemClockMillis() - time); time = XbmcThreads::SystemClockMillis();

  // and finally songs
  SearchSongs(search, items);
  CLog::Log(LOGDEBUG, "%s Songs search in %i ms",
            __FUNCTION__, XbmcThreads::SystemClockMillis() - time); time = XbmcThreads::SystemClockMillis();
  return true;
}

bool CMusicDatabase::SearchSongs(const std::string& search, CFileItemList &items)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    CMusicDbUrl baseUrl;
    if (!baseUrl.FromString("musicdb://songs/"))
      return false;

    std::string strSQL;
    if (search.size() >= MIN_FULL_SEARCH_LENGTH)
      strSQL=PrepareSQL("select * from songview where strTitle like '%s%%' or strTitle like '%% %s%%' limit 1000", search.c_str(), search.c_str());
    else
      strSQL=PrepareSQL("select * from songview where strTitle like '%s%%' limit 1000", search.c_str());

    if (!m_pDS->query(strSQL)) return false;
    if (m_pDS->num_rows() == 0) return false;

    std::string songLabel = g_localizeStrings.Get(179); // Song
    while (!m_pDS->eof())
    {
      CFileItemPtr item(new CFileItem);
      GetFileItemFromDataset(item.get(), baseUrl);
      items.Add(item);
      m_pDS->next();
    }

    m_pDS->close();
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }

  return false;
}

bool CMusicDatabase::SearchAlbums(const std::string& search, CFileItemList &albums)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string strSQL;
    if (search.size() >= MIN_FULL_SEARCH_LENGTH)
      strSQL=PrepareSQL("select * from albumview where strAlbum like '%s%%' or strAlbum like '%% %s%%'", search.c_str(), search.c_str());
    else
      strSQL=PrepareSQL("select * from albumview where strAlbum like '%s%%'", search.c_str());

    if (!m_pDS->query(strSQL)) return false;

    std::string albumLabel(g_localizeStrings.Get(558)); // Album
    while (!m_pDS->eof())
    {
      CAlbum album = GetAlbumFromDataset(m_pDS.get());
      std::string path = StringUtils::Format("musicdb://albums/%ld/", album.idAlbum);
      CFileItemPtr pItem(new CFileItem(path, album));
      std::string label = StringUtils::Format("[%s] %s", albumLabel.c_str(), album.strAlbum.c_str());
      pItem->SetLabel(label);
      label = StringUtils::Format("B %s", album.strAlbum.c_str()); // sort label is stored in the title tag
      pItem->GetMusicInfoTag()->SetTitle(label);
      albums.Add(pItem);
      m_pDS->next();
    }
    m_pDS->close(); // cleanup recordset data
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return false;
}

bool CMusicDatabase::CleanupSongsByIds(const std::string &strSongIds)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    // ok, now find all idSong's
    std::string strSQL=PrepareSQL("select * from song join path on song.idPath = path.idPath where song.idSong in %s", strSongIds.c_str());
    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }
    std::vector<std::string> songsToDelete;
    while (!m_pDS->eof())
    { // get the full song path
      std::string strFileName = URIUtils::AddFileToFolder(m_pDS->fv("path.strPath").get_asString(), m_pDS->fv("song.strFileName").get_asString());

      //  Special case for streams inside an ogg file. (oggstream)
      //  The last dir in the path is the ogg file that
      //  contains the stream, so test if its there
      if (URIUtils::HasExtension(strFileName, ".oggstream|.nsfstream"))
      {
        strFileName = URIUtils::GetDirectory(strFileName);
        // we are dropping back to a file, so remove the slash at end
        URIUtils::RemoveSlashAtEnd(strFileName);
      }

      if (!CFile::Exists(strFileName, false))
      { // file no longer exists, so add to deletion list
        songsToDelete.push_back(m_pDS->fv("song.idSong").get_asString());
      }
      m_pDS->next();
    }
    m_pDS->close();

    if (!songsToDelete.empty())
    {
      std::string strSongsToDelete = "(" + StringUtils::Join(songsToDelete, ",") + ")";
      // ok, now delete these songs + all references to them from the linked tables
      strSQL = "delete from song where idSong in " + strSongsToDelete;
      m_pDS->exec(strSQL);
      m_pDS->close();
    }
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "Exception in CMusicDatabase::CleanupSongsFromPaths()");
  }
  return false;
}

bool CMusicDatabase::CleanupSongs()
{
  try
  {
    // run through all songs and get all unique path ids
    int iLIMIT = 1000;
    for (int i=0;;i+=iLIMIT)
    {
      std::string strSQL=PrepareSQL("select song.idSong from song order by song.idSong limit %i offset %i",iLIMIT,i);
      if (!m_pDS->query(strSQL)) return false;
      int iRowsFound = m_pDS->num_rows();
      // keep going until no rows are left!
      if (iRowsFound == 0)
      {
        m_pDS->close();
        return true;
      }

      std::vector<std::string> songIds;
      while (!m_pDS->eof())
      {
        songIds.push_back(m_pDS->fv("song.idSong").get_asString());
        m_pDS->next();
      }
      m_pDS->close();
      std::string strSongIds = "(" + StringUtils::Join(songIds, ",") + ")";
      CLog::Log(LOGDEBUG,"Checking songs from song ID list: %s",strSongIds.c_str());
      if (!CleanupSongsByIds(strSongIds)) return false;
    }
    return true;
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "Exception in CMusicDatabase::CleanupSongs()");
  }
  return false;
}

bool CMusicDatabase::CleanupAlbums()
{
  try
  {
    // This must be run AFTER songs have been cleaned up
    // delete albums with no reference to songs
    std::string strSQL = "select * from album where album.idAlbum not in (select idAlbum from song)";
    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }

    std::vector<std::string> albumIds;
    while (!m_pDS->eof())
    {
      albumIds.push_back(m_pDS->fv("album.idAlbum").get_asString());
      m_pDS->next();
    }
    m_pDS->close();

    std::string strAlbumIds = "(" + StringUtils::Join(albumIds, ",") + ")";
    // ok, now we can delete them and the references in the linked tables
    strSQL = "delete from album where idAlbum in " + strAlbumIds;
    m_pDS->exec(strSQL);
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "Exception in CMusicDatabase::CleanupAlbums()");
  }
  return false;
}

bool CMusicDatabase::CleanupPaths()
{
  try
  {
    // needs to be done AFTER the songs and albums have been cleaned up.
    // we can happily delete any path that has no reference to a song
    // but we must keep all paths that have been scanned that may contain songs in subpaths

    // first create a temporary table of song paths
    m_pDS->exec("CREATE TEMPORARY TABLE songpaths (idPath integer, strPath varchar(512))\n");
    m_pDS->exec("INSERT INTO songpaths select idPath,strPath from path where idPath in (select idPath from song)\n");

    // grab all paths that aren't immediately connected with a song
    std::string sql = "select * from path where idPath not in (select idPath from song)";
    if (!m_pDS->query(sql)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }
    // and construct a list to delete
    std::vector<std::string> pathIds;
    while (!m_pDS->eof())
    {
      // anything that isn't a parent path of a song path is to be deleted
      std::string path = m_pDS->fv("strPath").get_asString();
      std::string sql = PrepareSQL("select count(idPath) from songpaths where SUBSTR(strPath,1,%i)='%s'", StringUtils::utf8_strlen(path.c_str()), path.c_str());
      if (m_pDS2->query(sql) && m_pDS2->num_rows() == 1 && m_pDS2->fv(0).get_asInt() == 0)
        pathIds.push_back(m_pDS->fv("idPath").get_asString()); // nothing found, so delete
      m_pDS2->close();
      m_pDS->next();
    }
    m_pDS->close();

    if (!pathIds.empty())
    {
      // do the deletion, and drop our temp table
      std::string deleteSQL = "DELETE FROM path WHERE idPath IN (" + StringUtils::Join(pathIds, ",") + ")";
      m_pDS->exec(deleteSQL);
    }
    m_pDS->exec("drop table songpaths");
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "Exception in CMusicDatabase::CleanupPaths() or was aborted");
  }
  return false;
}

bool CMusicDatabase::InsideScannedPath(const std::string& path)
{
  std::string sql = PrepareSQL("select idPath from path where SUBSTR(strPath,1,%i)='%s' LIMIT 1", path.size(), path.c_str());
  return !GetSingleValue(sql).empty();
}

bool CMusicDatabase::CleanupArtists()
{
  try
  {
    // (nested queries by Bobbin007)
    // must be executed AFTER the song, album and their artist link tables are cleaned.
    // Don't delete [Missing] the missing artist tag artist

    // Create temp table to avoid 1442 trigger hell on mysql
    m_pDS->exec("CREATE TEMPORARY TABLE tmp_delartists (idArtist integer)");
    m_pDS->exec("INSERT INTO tmp_delartists select idArtist from song_artist");
    m_pDS->exec("INSERT INTO tmp_delartists select idArtist from album_artist");
    m_pDS->exec(PrepareSQL("INSERT INTO tmp_delartists VALUES(%i)", BLANKARTIST_ID));
    // tmp_delartists contains duplicate ids, and on a large library with small changes can be very large.
    // To avoid MySQL hanging or timeout create a table of unique ids with primary key
    m_pDS->exec("CREATE TEMPORARY TABLE tmp_keep (idArtist INTEGER PRIMARY KEY)");
    m_pDS->exec("INSERT INTO tmp_keep SELECT DISTINCT idArtist from tmp_delartists");
    m_pDS->exec("DELETE FROM artist WHERE idArtist NOT IN (SELECT idArtist FROM tmp_keep)");
    // Tidy up temp tables
    m_pDS->exec("DROP TABLE tmp_delartists");
    m_pDS->exec("DROP TABLE tmp_keep");

    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "Exception in CMusicDatabase::CleanupArtists() or was aborted");
  }
  return false;
}

bool CMusicDatabase::CleanupGenres()
{
  try
  {
    // Cleanup orphaned genres (ie those that don't belong to a song or an album entry)
    // (nested queries by Bobbin007)
    // Must be executed AFTER the song, song_genre, album and album_genre tables have been cleaned.
    std::string strSQL = "delete from genre where idGenre not in (select idGenre from song_genre) and";
    strSQL += " idGenre not in (select idGenre from album_genre)";
    m_pDS->exec(strSQL);
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "Exception in CMusicDatabase::CleanupGenres() or was aborted");
  }
  return false;
}

bool CMusicDatabase::CleanupInfoSettings()
{
  try
  {
    // Cleanup orphaned info settings (ie those that don't belong to an album or artist entry)
    // Must be executed AFTER the album and artist tables have been cleaned.
    std::string strSQL = "DELETE FROM infosetting WHERE idSetting NOT IN (SELECT idInfoSetting FROM artist) " 
      "AND idSetting NOT IN (SELECT idInfoSetting FROM album)";
    m_pDS->exec(strSQL);
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "Exception in CMusicDatabase::CleanupInfoSettings() or was aborted");
  }
  return false;
}

bool CMusicDatabase::CleanupRoles()
{
  try
  {
    // Cleanup orphaned roles (ie those that don't belong to a song entry)
    // Must be executed AFTER the song, and song_artist tables have been cleaned.
    // Do not remove default role (ROLE_ARTIST)
    std::string strSQL = "DELETE FROM role WHERE idRole > 1 AND idRole NOT IN (SELECT idRole FROM song_artist)";
    m_pDS->exec(strSQL);
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "Exception in CMusicDatabase::CleanupRoles() or was aborted");
  }
  return false;
}

bool CMusicDatabase::CleanupOrphanedItems()
{
  // paths aren't cleaned up here - they're cleaned up in RemoveSongsFromPath()
  if (NULL == m_pDB.get()) return false;
  if (NULL == m_pDS.get()) return false;
  if (!CleanupAlbums()) return false;
  if (!CleanupArtists()) return false;
  if (!CleanupGenres()) return false;
  if (!CleanupRoles()) return false;
  if (!CleanupInfoSettings()) return false;
  return true;
}

int CMusicDatabase::Cleanup(bool bShowProgress /* = true */)
{
  if (NULL == m_pDB.get()) return ERROR_DATABASE;
  if (NULL == m_pDS.get()) return ERROR_DATABASE;

  int ret = ERROR_OK;
  CGUIDialogProgress* pDlgProgress = NULL;
  unsigned int time = XbmcThreads::SystemClockMillis();
  CLog::Log(LOGNOTICE, "%s: Starting musicdatabase cleanup ..", __FUNCTION__);
  ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::AudioLibrary, "xbmc", "OnCleanStarted");

  // first cleanup any songs with invalid paths
  if (bShowProgress)
  {
    pDlgProgress = g_windowManager.GetWindow<CGUIDialogProgress>(WINDOW_DIALOG_PROGRESS);
    if (pDlgProgress)
    {
      pDlgProgress->SetHeading(CVariant{700});
      pDlgProgress->SetLine(0, CVariant{""});
      pDlgProgress->SetLine(1, CVariant{318});
      pDlgProgress->SetLine(2, CVariant{330});
      pDlgProgress->SetPercentage(0);
      pDlgProgress->Open();
      pDlgProgress->ShowProgressBar(true);
    }
  }
  if (!CleanupSongs())
  {
    ret = ERROR_REORG_SONGS;
    goto error;
  }
  // then the albums that are not linked to a song or to album, or whose path is removed
  if (pDlgProgress)
  {
    pDlgProgress->SetLine(1, CVariant{326});
    pDlgProgress->SetPercentage(20);
    pDlgProgress->Progress();
  }
  if (!CleanupAlbums())
  {
    ret = ERROR_REORG_ALBUM;
    goto error;
  }
  // now the paths
  if (pDlgProgress)
  {
    pDlgProgress->SetLine(1, CVariant{324});
    pDlgProgress->SetPercentage(40);
    pDlgProgress->Progress();
  }
  if (!CleanupPaths())
  {
    ret = ERROR_REORG_PATH;
    goto error;
  }
  // and finally artists + genres
  if (pDlgProgress)
  {
    pDlgProgress->SetLine(1, CVariant{320});
    pDlgProgress->SetPercentage(60);
    pDlgProgress->Progress();
  }
  if (!CleanupArtists())
  {
    ret = ERROR_REORG_ARTIST;
    goto error;
  }
  //Genres, roles and info settings progess in one step
  if (pDlgProgress)
  {
    pDlgProgress->SetLine(1, CVariant{322});
    pDlgProgress->SetPercentage(80);
    pDlgProgress->Progress();
  }
  if (!CleanupGenres())
  {
    ret = ERROR_REORG_OTHER;
    goto error;
  }
  if (!CleanupRoles())
  {
    ret = ERROR_REORG_OTHER;
    goto error;
  }
  if (!CleanupInfoSettings())
  {
    ret = ERROR_REORG_OTHER;
    goto error;
  }
  // commit transaction
  if (pDlgProgress)
  {
    pDlgProgress->SetLine(1, CVariant{328});
    pDlgProgress->SetPercentage(90);
    pDlgProgress->Progress();
  }
  if (!CommitTransaction())
  {
    ret = ERROR_WRITING_CHANGES;
    goto error;
  }
  // and compress the database
  if (pDlgProgress)
  {
    pDlgProgress->SetLine(1, CVariant{331});
    pDlgProgress->SetPercentage(100);
    pDlgProgress->Progress();
    pDlgProgress->Close();
  }
  time = XbmcThreads::SystemClockMillis() - time;
  CLog::Log(LOGNOTICE, "%s: Cleaning musicdatabase done. Operation took %s", __FUNCTION__, StringUtils::SecondsToTimeString(time / 1000).c_str());
  ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::AudioLibrary, "xbmc", "OnCleanFinished");

  if (!Compress(false))
  {
    return ERROR_COMPRESSING;
  }
  return ERROR_OK;

error:
  RollbackTransaction();
  ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::AudioLibrary, "xbmc", "OnCleanFinished");
  return ret;
}

bool CMusicDatabase::LookupCDDBInfo(bool bRequery/*=false*/)
{
#ifdef HAS_DVD_DRIVE
  if (!CServiceBroker::GetSettings().GetBool(CSettings::SETTING_AUDIOCDS_USECDDB))
    return false;

  // check network connectivity
  if (!g_application.getNetwork().IsAvailable())
    return false;

  // Get information for the inserted disc
  CCdInfo* pCdInfo = g_mediaManager.GetCdInfo();
  if (pCdInfo == NULL)
    return false;

  // If the disc has no tracks, we are finished here.
  int nTracks = pCdInfo->GetTrackCount();
  if (nTracks <= 0)
    return false;

  //  Delete old info if any
  if (bRequery)
  {
    std::string strFile = StringUtils::Format("%x.cddb", pCdInfo->GetCddbDiscId());
    CFile::Delete(URIUtils::AddFileToFolder(CProfilesManager::GetInstance().GetCDDBFolder(), strFile));
  }

  // Prepare cddb
  Xcddb cddb;
  cddb.setCacheDir(CProfilesManager::GetInstance().GetCDDBFolder());

  // Do we have to look for cddb information
  if (pCdInfo->HasCDDBInfo() && !cddb.isCDCached(pCdInfo))
  {
    CGUIDialogProgress* pDialogProgress = g_windowManager.GetWindow<CGUIDialogProgress>(WINDOW_DIALOG_PROGRESS);
    CGUIDialogSelect *pDlgSelect = g_windowManager.GetWindow<CGUIDialogSelect>(WINDOW_DIALOG_SELECT);

    if (!pDialogProgress) return false;
    if (!pDlgSelect) return false;

    // Show progress dialog if we have to connect to freedb.org
    pDialogProgress->SetHeading(CVariant{255}); //CDDB
    pDialogProgress->SetLine(0, CVariant{""}); // Querying freedb for CDDB info
    pDialogProgress->SetLine(1, CVariant{256});
    pDialogProgress->SetLine(2, CVariant{""});
    pDialogProgress->ShowProgressBar(false);
    pDialogProgress->Open();

    // get cddb information
    if (!cddb.queryCDinfo(pCdInfo))
    {
      pDialogProgress->Close();
      int lasterror = cddb.getLastError();

      // Have we found more then on match in cddb for this disc,...
      if (lasterror == E_WAIT_FOR_INPUT)
      {
        // ...yes, show the matches found in a select dialog
        // and let the user choose an entry.
        pDlgSelect->Reset();
        pDlgSelect->SetHeading(CVariant{255});
        int i = 1;
        while (1)
        {
          std::string strTitle = cddb.getInexactTitle(i);
          if (strTitle == "") break;

          std::string strArtist = cddb.getInexactArtist(i);
          if (!strArtist.empty())
            strTitle += " - " + strArtist;

          pDlgSelect->Add(strTitle);
          i++;
        }
        pDlgSelect->Open();

        // Has the user selected a match...
        int iSelectedCD = pDlgSelect->GetSelectedItem();
        if (iSelectedCD >= 0)
        {
          // ...query cddb for the inexact match
          if (!cddb.queryCDinfo(pCdInfo, 1 + iSelectedCD))
            pCdInfo->SetNoCDDBInfo();
        }
        else
          pCdInfo->SetNoCDDBInfo();
      }
      else if (lasterror == E_NO_MATCH_FOUND)
      {
        pCdInfo->SetNoCDDBInfo();
      }
      else
      {
        pCdInfo->SetNoCDDBInfo();
        // ..no, an error occured, display it to the user
        std::string strErrorText = StringUtils::Format("[%d] %s", cddb.getLastError(), cddb.getLastErrorText());
        CGUIDialogOK::ShowAndGetInput(CVariant{255}, CVariant{257}, CVariant{std::move(strErrorText)}, CVariant{0});
      }
    } // if ( !cddb.queryCDinfo( pCdInfo ) )
    else
      pDialogProgress->Close();
  }

  // Filling the file items with cddb info happens in CMusicInfoTagLoaderCDDA

  return pCdInfo->HasCDDBInfo();
#else
  return false;
#endif
}

void CMusicDatabase::DeleteCDDBInfo()
{
#ifdef HAS_DVD_DRIVE
  CFileItemList items;
  if (!CDirectory::GetDirectory(CProfilesManager::GetInstance().GetCDDBFolder(), items, ".cddb", DIR_FLAG_NO_FILE_DIRS))
  {
    CGUIDialogOK::ShowAndGetInput(CVariant{313}, CVariant{426});
    return ;
  }
  // Show a selectdialog that the user can select the album to delete
  CGUIDialogSelect *pDlg = g_windowManager.GetWindow<CGUIDialogSelect>(WINDOW_DIALOG_SELECT);
  if (pDlg)
  {
    pDlg->SetHeading(CVariant{g_localizeStrings.Get(181)});
    pDlg->Reset();

    std::map<ULONG, std::string> mapCDDBIds;
    for (int i = 0; i < items.Size(); ++i)
    {
      if (items[i]->m_bIsFolder)
        continue;

      std::string strFile = URIUtils::GetFileName(items[i]->GetPath());
      strFile.erase(strFile.size() - 5, 5);
      ULONG lDiscId = strtoul(strFile.c_str(), NULL, 16);
      Xcddb cddb;
      cddb.setCacheDir(CProfilesManager::GetInstance().GetCDDBFolder());

      if (!cddb.queryCache(lDiscId))
        continue;

      std::string strDiskTitle, strDiskArtist;
      cddb.getDiskTitle(strDiskTitle);
      cddb.getDiskArtist(strDiskArtist);

      std::string str;
      if (strDiskArtist.empty())
        str = strDiskTitle;
      else
        str = strDiskTitle + " - " + strDiskArtist;

      pDlg->Add(str);
      mapCDDBIds.insert(std::pair<ULONG, std::string>(lDiscId, str));
    }

    pDlg->Sort();
    pDlg->Open();

    // and wait till user selects one
    int iSelectedAlbum = pDlg->GetSelectedItem();
    if (iSelectedAlbum < 0)
    {
      mapCDDBIds.erase(mapCDDBIds.begin(), mapCDDBIds.end());
      return ;
    }

    std::string strSelectedAlbum = pDlg->GetSelectedFileItem()->GetLabel();
    for (const auto &i : mapCDDBIds)
    {
      if (i.second == strSelectedAlbum)
      {
        std::string strFile = StringUtils::Format("%x.cddb", (unsigned int) i.first);
        CFile::Delete(URIUtils::AddFileToFolder(CProfilesManager::GetInstance().GetCDDBFolder(), strFile));
        break;
      }
    }
    mapCDDBIds.erase(mapCDDBIds.begin(), mapCDDBIds.end());
  }
#endif
}

void CMusicDatabase::Clean()
{
  // If we are scanning for music info in the background,
  // other writing access to the database is prohibited.
  if (g_application.IsMusicScanning())
  {
    CGUIDialogOK::ShowAndGetInput(CVariant{189}, CVariant{14057});
    return;
  }
  
  if (HELPERS::ShowYesNoDialogText(CVariant{313}, CVariant{333}) == DialogResponse::YES)
  {
    CMusicDatabase musicdatabase;
    if (musicdatabase.Open())
    {
      int iReturnString = musicdatabase.Cleanup();
      musicdatabase.Close();

      if (iReturnString != ERROR_OK)
      {
        CGUIDialogOK::ShowAndGetInput(CVariant{313}, CVariant{iReturnString});
      }
    }
  }
}

bool CMusicDatabase::GetGenresNav(const std::string& strBaseDir, CFileItemList& items, const Filter &filter /* = Filter() */, bool countOnly /* = false */)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    // get primary genres for songs - could be simplified to just SELECT * FROM genre?
    std::string strSQL = "SELECT %s FROM genre ";

    Filter extFilter = filter;
    CMusicDbUrl musicUrl;
    SortDescription sorting;
    if (!musicUrl.FromString(strBaseDir) || !GetFilter(musicUrl, extFilter, sorting))
      return false;

    // if there are extra WHERE conditions we might need access
    // to songview or albumview for these conditions
    if (!extFilter.where.empty())
    {
      if (extFilter.where.find("artistview") != std::string::npos)
        extFilter.AppendJoin("JOIN song_genre ON song_genre.idGenre = genre.idGenre JOIN songview ON songview.idSong = song_genre.idSong "
                             "JOIN song_artist ON song_artist.idSong = songview.idSong JOIN artistview ON artistview.idArtist = song_artist.idArtist");
      else if (extFilter.where.find("songview") != std::string::npos)
        extFilter.AppendJoin("JOIN song_genre ON song_genre.idGenre = genre.idGenre JOIN songview ON songview.idSong = song_genre.idSong");
      else if (extFilter.where.find("albumview") != std::string::npos)
        extFilter.AppendJoin("JOIN album_genre ON album_genre.idGenre = genre.idGenre JOIN albumview ON albumview.idAlbum = album_genre.idAlbum");

      extFilter.AppendGroup("genre.idGenre");
    }
    extFilter.AppendWhere("genre.strGenre != ''");

    if (countOnly)
    {
      extFilter.fields = "COUNT(DISTINCT genre.idGenre)";
      extFilter.group.clear();
      extFilter.order.clear();
    }

    std::string strSQLExtra;
    if (!BuildSQL(strSQLExtra, extFilter, strSQLExtra))
      return false;

    strSQL = PrepareSQL(strSQL.c_str(), !extFilter.fields.empty() && extFilter.fields.compare("*") != 0 ? extFilter.fields.c_str() : "genre.*") + strSQLExtra;

    // run query
    CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());

    if (!m_pDS->query(strSQL))
      return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }

    if (countOnly)
    {
      CFileItemPtr pItem(new CFileItem());
      pItem->SetProperty("total", iRowsFound == 1 ? m_pDS->fv(0).get_asInt() : iRowsFound);
      items.Add(pItem);

      m_pDS->close();
      return true;
    }

    // get data from returned rows
    while (!m_pDS->eof())
    {
      CFileItemPtr pItem(new CFileItem(m_pDS->fv("genre.strGenre").get_asString()));
      pItem->GetMusicInfoTag()->SetGenre(m_pDS->fv("genre.strGenre").get_asString());
      pItem->GetMusicInfoTag()->SetDatabaseId(m_pDS->fv("genre.idGenre").get_asInt(), "genre");

      CMusicDbUrl itemUrl = musicUrl;
      std::string strDir = StringUtils::Format("%i/", m_pDS->fv("genre.idGenre").get_asInt());
      itemUrl.AppendPath(strDir);
      pItem->SetPath(itemUrl.ToString());

      pItem->m_bIsFolder = true;
      items.Add(pItem);

      m_pDS->next();
    }

    // cleanup
    m_pDS->close();

    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return false;
}

bool CMusicDatabase::GetYearsNav(const std::string& strBaseDir, CFileItemList& items, const Filter &filter /* = Filter() */)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    Filter extFilter = filter;
    CMusicDbUrl musicUrl;
    SortDescription sorting;
    if (!musicUrl.FromString(strBaseDir) || !GetFilter(musicUrl, extFilter, sorting))
      return false;

    // get years from album list
    std::string strSQL = "SELECT DISTINCT albumview.iYear FROM albumview ";
    extFilter.AppendWhere("albumview.iYear <> 0");

    if (!BuildSQL(strSQL, extFilter, strSQL))
      return false;

    // run query
    CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());
    if (!m_pDS->query(strSQL))
      return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }

    // get data from returned rows
    while (!m_pDS->eof())
    {
      CFileItemPtr pItem(new CFileItem(m_pDS->fv(0).get_asString()));
      SYSTEMTIME stTime;
      stTime.wYear = (WORD)m_pDS->fv(0).get_asInt();
      pItem->GetMusicInfoTag()->SetReleaseDate(stTime);

      CMusicDbUrl itemUrl = musicUrl;
      std::string strDir = StringUtils::Format("%i/", m_pDS->fv(0).get_asInt());
      itemUrl.AppendPath(strDir);
      pItem->SetPath(itemUrl.ToString());

      pItem->m_bIsFolder = true;
      items.Add(pItem);

      m_pDS->next();
    }

    // cleanup
    m_pDS->close();

    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return false;
}

bool CMusicDatabase::GetRolesNav(const std::string& strBaseDir, CFileItemList& items, const Filter &filter /* = Filter() */)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    Filter extFilter = filter;
    CMusicDbUrl musicUrl;
    SortDescription sorting;
    if (!musicUrl.FromString(strBaseDir) || !GetFilter(musicUrl, extFilter, sorting))
      return false;

    // get roles with artists having that role
    std::string strSQL = "SELECT DISTINCT role.idRole, role.strRole FROM role "
                         "JOIN song_artist ON song_artist.idRole = role.idRole ";

    if (!BuildSQL(strSQL, extFilter, strSQL))
      return false;

    // run query
    CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());
    if (!m_pDS->query(strSQL))
      return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }

    // get data from returned rows
    while (!m_pDS->eof())
    {
      std::string labelValue = m_pDS->fv("role.strRole").get_asString();
      CFileItemPtr pItem(new CFileItem(labelValue));
      pItem->GetMusicInfoTag()->SetTitle(labelValue);
      pItem->GetMusicInfoTag()->SetDatabaseId(m_pDS->fv("role.idRole").get_asInt(), "role");
      CMusicDbUrl itemUrl = musicUrl;
      std::string strDir = StringUtils::Format("%i/", m_pDS->fv("role.idRole").get_asInt());
      itemUrl.AppendPath(strDir);
      itemUrl.AddOption("roleid", m_pDS->fv("role.idRole").get_asInt());
      pItem->SetPath(itemUrl.ToString());

      pItem->m_bIsFolder = true;
      items.Add(pItem);

      m_pDS->next();
    }

    // cleanup
    m_pDS->close();

    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return false;
}

bool CMusicDatabase::GetAlbumsByYear(const std::string& strBaseDir, CFileItemList& items, int year)
{
  CMusicDbUrl musicUrl;
  if (!musicUrl.FromString(strBaseDir))
    return false;

  musicUrl.AddOption("year", year);
  musicUrl.AddOption("show_singles", true); // allow singles to be listed
  
  Filter filter;
  return GetAlbumsByWhere(musicUrl.ToString(), filter, items);
}

bool CMusicDatabase::GetCommonNav(const std::string &strBaseDir, const std::string &table, const std::string &labelField, CFileItemList &items, const Filter &filter /* = Filter() */, bool countOnly /* = false */)
{
  if (NULL == m_pDB.get()) return false;
  if (NULL == m_pDS.get()) return false;

  if (table.empty() || labelField.empty())
    return false;
  
  try
  {
    Filter extFilter = filter;
    std::string strSQL = "SELECT %s FROM " + table + " ";
    extFilter.AppendGroup(labelField);
    extFilter.AppendWhere(labelField + " != ''");
    
    if (countOnly)
    {
      extFilter.fields = "COUNT(DISTINCT " + labelField + ")";
      extFilter.group.clear();
      extFilter.order.clear();
    }
    
    // Do prepare before add where as it could contain a LIKE statement with wild card that upsets format
    // e.g. LIKE '%symphony%' would be taken as a %s format argument
    strSQL = PrepareSQL(strSQL, !extFilter.fields.empty() ? extFilter.fields.c_str() : labelField.c_str());

    CMusicDbUrl musicUrl;
    if (!BuildSQL(strBaseDir, strSQL, extFilter, strSQL, musicUrl))
      return false;
    
    // run query
    CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());
    if (!m_pDS->query(strSQL))
      return false;
    
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound <= 0)
    {
      m_pDS->close();
      return false;
    }
    
    if (countOnly)
    {
      CFileItemPtr pItem(new CFileItem());
      pItem->SetProperty("total", iRowsFound == 1 ? m_pDS->fv(0).get_asInt() : iRowsFound);
      items.Add(pItem);
      
      m_pDS->close();
      return true;
    }
    
    // get data from returned rows
    while (!m_pDS->eof())
    {
      std::string labelValue = m_pDS->fv(labelField.c_str()).get_asString();
      CFileItemPtr pItem(new CFileItem(labelValue));
      
      CMusicDbUrl itemUrl = musicUrl;
      std::string strDir = StringUtils::Format("%s/", labelValue.c_str());
      itemUrl.AppendPath(strDir);
      pItem->SetPath(itemUrl.ToString());
      
      pItem->m_bIsFolder = true;
      items.Add(pItem);
      
      m_pDS->next();
    }
    
    // cleanup
    m_pDS->close();
    
    return true;
  }
  catch (...)
  {
    m_pDS->close();
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  
  return false;
}

bool CMusicDatabase::GetAlbumTypesNav(const std::string &strBaseDir, CFileItemList &items, const Filter &filter /* = Filter() */, bool countOnly /* = false */)
{
  return GetCommonNav(strBaseDir, "albumview", "albumview.strType", items, filter, countOnly);
}

bool CMusicDatabase::GetMusicLabelsNav(const std::string &strBaseDir, CFileItemList &items, const Filter &filter /* = Filter() */, bool countOnly /* = false */)
{
  return GetCommonNav(strBaseDir, "albumview", "albumview.strLabel", items, filter, countOnly);
}

bool CMusicDatabase::GetArtistsNav(const std::string& strBaseDir, CFileItemList& items, bool albumArtistsOnly /* = false */, int idGenre /* = -1 */, int idAlbum /* = -1 */, int idSong /* = -1 */, const Filter &filter /* = Filter() */, const SortDescription &sortDescription /* = SortDescription() */, bool countOnly /* = false */)
{
  if (NULL == m_pDB.get()) return false;
  if (NULL == m_pDS.get()) return false;
  try
  {
    unsigned int time = XbmcThreads::SystemClockMillis();

    CMusicDbUrl musicUrl;
    if (!musicUrl.FromString(strBaseDir))
      return false;

    if (idGenre > 0)
      musicUrl.AddOption("genreid", idGenre);
    else if (idAlbum > 0)
      musicUrl.AddOption("albumid", idAlbum);
    else if (idSong > 0)
      musicUrl.AddOption("songid", idSong);

    // Override albumArtistsOnly parameter (usually externally set to SETTING_MUSICLIBRARY_SHOWCOMPILATIONARTISTS)
    // when local option already present in music URL thus allowing it to be an option in custom nodes
    if (!musicUrl.HasOption("albumartistsonly"))
      musicUrl.AddOption("albumartistsonly", albumArtistsOnly);

    bool result = GetArtistsByWhere(musicUrl.ToString(), filter, items, sortDescription, countOnly);
    CLog::Log(LOGDEBUG,"Time to retrieve artists from dataset = %i", XbmcThreads::SystemClockMillis() - time);

    return result;
  }
  catch (...)
  {
    m_pDS->close();
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return false;
}

bool CMusicDatabase::GetArtistsByWhere(const std::string& strBaseDir, const Filter &filter, CFileItemList& items, const SortDescription &sortDescription /* = SortDescription() */, bool countOnly /* = false */)
{
  if (NULL == m_pDB.get()) return false;
  if (NULL == m_pDS.get()) return false;

  try
  {
    int total = -1;

    std::string strSQL = "SELECT %s FROM artistview ";

    Filter extFilter = filter;
    CMusicDbUrl musicUrl;
    SortDescription sorting = sortDescription;
    if (!musicUrl.FromString(strBaseDir) || !GetFilter(musicUrl, extFilter, sorting))
      return false;

    // if there are extra WHERE conditions we might need access
    // to songview or albumview for these conditions
    if (!extFilter.where.empty())
    {
      bool extended = false;
      if (extFilter.where.find("songview") != std::string::npos)
      {
        extended = true;
        extFilter.AppendJoin("JOIN song_artist ON song_artist.idArtist = artistview.idArtist JOIN songview ON songview.idSong = song_artist.idSong");
      }
      else if (extFilter.where.find("albumview") != std::string::npos)
      {
        extended = true;
        extFilter.AppendJoin("JOIN album_artist ON album_artist.idArtist = artistview.idArtist JOIN albumview ON albumview.idAlbum = album_artist.idAlbum");
      }

      if (extended)
        extFilter.AppendGroup("artistview.idArtist");
    }
    
    if (countOnly)
    {
      extFilter.fields = "COUNT(DISTINCT artistview.idArtist)";
      extFilter.group.clear();
      extFilter.order.clear();
    }

    std::string strSQLExtra;
    if (!BuildSQL(strSQLExtra, extFilter, strSQLExtra))
      return false;

    // Apply the limiting directly here if there's no special sorting but limiting
    if (extFilter.limit.empty() &&
        sortDescription.sortBy == SortByNone &&
       (sortDescription.limitStart > 0 || sortDescription.limitEnd > 0))
    {
      total = (int)strtol(GetSingleValue(PrepareSQL(strSQL, "COUNT(1)") + strSQLExtra, m_pDS).c_str(), NULL, 10);
      strSQLExtra += DatabaseUtils::BuildLimitClause(sortDescription.limitEnd, sortDescription.limitStart);
    }

    strSQL = PrepareSQL(strSQL.c_str(), !extFilter.fields.empty() && extFilter.fields.compare("*") != 0 ? extFilter.fields.c_str() : "artistview.*") + strSQLExtra;

    // run query
    CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());
    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }

    if (countOnly)
    {
      CFileItemPtr pItem(new CFileItem());
      pItem->SetProperty("total", iRowsFound == 1 ? m_pDS->fv(0).get_asInt() : iRowsFound);
      items.Add(pItem);

      m_pDS->close();
      return true;
    }

    // store the total value of items as a property
    if (total < iRowsFound)
      total = iRowsFound;
    items.SetProperty("total", total);
    
    DatabaseResults results;
    results.reserve(iRowsFound);
    if (!SortUtils::SortFromDataset(sortDescription, MediaTypeArtist, m_pDS, results))
      return false;

    // get data from returned rows
    items.Reserve(results.size());
    const dbiplus::query_data &data = m_pDS->get_result_set().records;
    for (const auto &i : results)
    {
      unsigned int targetRow = (unsigned int)i.at(FieldRow).asInteger();
      const dbiplus::sql_record* const record = data.at(targetRow);
      
      try
      {
        CArtist artist = GetArtistFromDataset(record, false);
        CFileItemPtr pItem(new CFileItem(artist));

        CMusicDbUrl itemUrl = musicUrl;
        std::string path = StringUtils::Format("%ld/", artist.idArtist);
        itemUrl.AppendPath(path);
        pItem->SetPath(itemUrl.ToString());

        pItem->GetMusicInfoTag()->SetDatabaseId(artist.idArtist, MediaTypeArtist);
        pItem->SetIconImage("DefaultArtist.png");

        SetPropertiesFromArtist(*pItem, artist);
        items.Add(pItem);
      }
      catch (...)
      {
        m_pDS->close();
        CLog::Log(LOGERROR, "%s - out of memory getting listing (got %i)", __FUNCTION__, items.Size());
      }
    }

    // cleanup
    m_pDS->close();

    return true;
  }
  catch (...)
  {
    m_pDS->close();
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return false;
}

bool CMusicDatabase::GetAlbumFromSong(int idSong, CAlbum &album)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string strSQL = PrepareSQL("select albumview.* from song join albumview on song.idAlbum = albumview.idAlbum where song.idSong='%i'", idSong);
    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound != 1)
    {
      m_pDS->close();
      return false;
    }

    album = GetAlbumFromDataset(m_pDS.get());

    m_pDS->close();
    return true;

  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return false;
}

bool CMusicDatabase::GetAlbumsNav(const std::string& strBaseDir, CFileItemList& items, int idGenre /* = -1 */, int idArtist /* = -1 */, const Filter &filter /* = Filter() */, const SortDescription &sortDescription /* = SortDescription() */, bool countOnly /* = false */)
{
  CMusicDbUrl musicUrl;
  if (!musicUrl.FromString(strBaseDir))
    return false;

  // where clause
  if (idGenre > 0)
    musicUrl.AddOption("genreid", idGenre);

  if (idArtist > 0)
    musicUrl.AddOption("artistid", idArtist);

  return GetAlbumsByWhere(musicUrl.ToString(), filter, items, sortDescription, countOnly);
}

bool CMusicDatabase::GetAlbumsByWhere(const std::string &baseDir, const Filter &filter, CFileItemList &items, const SortDescription &sortDescription /* = SortDescription() */, bool countOnly /* = false */)
{
  if (m_pDB.get() == NULL || m_pDS.get() == NULL)
    return false;

  try
  {
    int total = -1;

    std::string strSQL = "SELECT %s FROM albumview ";

    Filter extFilter = filter;
    CMusicDbUrl musicUrl;
    SortDescription sorting = sortDescription;
    if (!musicUrl.FromString(baseDir) || !GetFilter(musicUrl, extFilter, sorting))
      return false;

    // if there are extra WHERE conditions we might need access
    // to songview for these conditions
    if (extFilter.where.find("songview") != std::string::npos)
    {
      extFilter.AppendJoin("JOIN songview ON songview.idAlbum = albumview.idAlbum");
      extFilter.AppendGroup("albumview.idAlbum");
    }

    std::string strSQLExtra;
    if (!BuildSQL(strSQLExtra, extFilter, strSQLExtra))
      return false;

    // Apply the limiting directly here if there's no special sorting but limiting
    if (extFilter.limit.empty() &&
        sortDescription.sortBy == SortByNone &&
       (sortDescription.limitStart > 0 || sortDescription.limitEnd > 0))
    {
      total = (int)strtol(GetSingleValue(PrepareSQL(strSQL, "COUNT(1)") + strSQLExtra, m_pDS).c_str(), NULL, 10);
      strSQLExtra += DatabaseUtils::BuildLimitClause(sortDescription.limitEnd, sortDescription.limitStart);
    }

    strSQL = PrepareSQL(strSQL, !filter.fields.empty() && filter.fields.compare("*") != 0 ? filter.fields.c_str() : "albumview.*") + strSQLExtra;

    CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());
    // run query
    unsigned int time = XbmcThreads::SystemClockMillis();
    if (!m_pDS->query(strSQL))
      return false;
    CLog::Log(LOGDEBUG, "%s - query took %i ms",
              __FUNCTION__, XbmcThreads::SystemClockMillis() - time); time = XbmcThreads::SystemClockMillis();

    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound <= 0)
    {
      m_pDS->close();
      return true;
    }

    // store the total value of items as a property
    if (total < iRowsFound)
      total = iRowsFound;
    items.SetProperty("total", total);

    if (countOnly)
    {
      CFileItemPtr pItem(new CFileItem());
      pItem->SetProperty("total", total);
      items.Add(pItem);

      m_pDS->close();
      return true;
    }
    
    DatabaseResults results;
    results.reserve(iRowsFound);
    if (!SortUtils::SortFromDataset(sortDescription, MediaTypeAlbum, m_pDS, results))
      return false;

    // get data from returned rows
    items.Reserve(results.size());
    const dbiplus::query_data &data = m_pDS->get_result_set().records;
    for (const auto &i : results)
    {
      unsigned int targetRow = (unsigned int)i.at(FieldRow).asInteger();
      const dbiplus::sql_record* const record = data.at(targetRow);
      
      try
      {
        CMusicDbUrl itemUrl = musicUrl;
        std::string path = StringUtils::Format("%i/", record->at(album_idAlbum).get_asInt());
        itemUrl.AppendPath(path);

        CFileItemPtr pItem(new CFileItem(itemUrl.ToString(), GetAlbumFromDataset(record)));
        pItem->SetIconImage("DefaultAlbumCover.png");
        items.Add(pItem);
      }
      catch (...)
      {
        m_pDS->close();
        CLog::Log(LOGERROR, "%s - out of memory getting listing (got %i)", __FUNCTION__, items.Size());
      }
    }

    // cleanup
    m_pDS->close();
    return true;
  }
  catch (...)
  {
    m_pDS->close();
    CLog::Log(LOGERROR, "%s (%s) failed", __FUNCTION__, filter.where.c_str());
  }
  return false;
}

bool CMusicDatabase::GetAlbumsByWhere(const std::string &baseDir, const Filter &filter, VECALBUMS& albums, int& total, const SortDescription &sortDescription /* = SortDescription() */, bool countOnly /* = false */)
{
  albums.erase(albums.begin(), albums.end());
  if (NULL == m_pDB.get()) return false;
  if (NULL == m_pDS.get()) return false;

  try
  {
    total = -1;

    Filter extFilter = filter;
    CMusicDbUrl musicUrl;
    SortDescription sorting = sortDescription;
    if (!musicUrl.FromString(baseDir) || !GetFilter(musicUrl, extFilter, sorting))
      return false;

    // if there are extra WHERE conditions we might need access
    // to songview for these conditions
    if (extFilter.where.find("songview") != std::string::npos)
    {
      extFilter.AppendJoin("JOIN songview ON songview.idAlbum = albumview.idAlbum");
      extFilter.AppendGroup("albumview.idAlbum");
    }

    std::string strSQLExtra;
    if (!BuildSQL(strSQLExtra, extFilter, strSQLExtra))
      return false;

    // Count and return number of albums that satisfy selection criteria
    total = (int)strtol(GetSingleValue("SELECT COUNT(1) FROM albumview " + strSQLExtra, m_pDS).c_str(), NULL, 10);
    if (countOnly)
      return true;

    // Apply the limiting directly here if there's no special sorting but limiting
    bool limited = extFilter.limit.empty() && sortDescription.sortBy == SortByNone &&
      (sortDescription.limitStart > 0 || sortDescription.limitEnd > 0);
    if (limited)
    {
      strSQLExtra += DatabaseUtils::BuildLimitClause(sortDescription.limitEnd, sortDescription.limitStart);
      albums.reserve(sortDescription.limitEnd - sortDescription.limitStart);
    }
    else
      albums.reserve(total);

    std::string strSQL;
    
    // Get data from album, album_artist and artist tables to fully populate albums with album artists
    // All albums have at least one artist so inner join sufficient
    if (limited)
      //Apply where clause and limits to albumview, then join as multiple records in result set per album
      strSQL = "SELECT av.*, albumartistview.* "
               "FROM (SELECT albumview.* FROM albumview " + strSQLExtra + ") AS av "
               "JOIN albumartistview ON albumartistview.idalbum = av.idalbum ";
    else
      strSQL = "SELECT albumview.*, albumartistview.* "
               "FROM albumview JOIN albumartistview ON albumartistview.idalbum = albumview.idalbum " + strSQLExtra;
    
    CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());
    // run query
    unsigned int time = XbmcThreads::SystemClockMillis();
    if (!m_pDS->query(strSQL))
      return false;
    CLog::Log(LOGDEBUG, "%s - query took %i ms",
      __FUNCTION__, XbmcThreads::SystemClockMillis() - time); time = XbmcThreads::SystemClockMillis();

    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound <= 0)
    {
      m_pDS->close();
      return true;
    }

    DatabaseResults results;
    results.reserve(iRowsFound);
    // Do not apply any limit when sorting as have join with albumartistview so limit would 
    // apply incorrectly (although when SortByNone limit already applied in SQL). 
    // Apply limits later to album list rather than dataset
    // But Artist order may be disturbed by sort???
    sorting = sortDescription;
    sorting.limitStart = 0;
    sorting.limitEnd = -1;
    if (!SortUtils::SortFromDataset(sorting, MediaTypeAlbum, m_pDS, results))
      return false;

    // Get albums from returned rows. Join means there is a row for every album artist
    int albumArtistOffset = album_enumCount;
    int albumId = -1;

    const dbiplus::query_data &data = m_pDS->get_result_set().records;
    for (const auto &i : results)
    {
      unsigned int targetRow = (unsigned int)i.at(FieldRow).asInteger();
      const dbiplus::sql_record* const record = data.at(targetRow);

      if (albumId != record->at(album_idAlbum).get_asInt())
      { // New album
        albumId = record->at(album_idAlbum).get_asInt();
        albums.emplace_back(GetAlbumFromDataset(record));
      }
      // Get artists
      albums.back().artistCredits.emplace_back(GetArtistCreditFromDataset(record, albumArtistOffset));
    }

    m_pDS->close(); // cleanup recordset data

    // Apply any limits to sorted albums
    if (sortDescription.sortBy != SortByNone && (sortDescription.limitStart > 0 || sortDescription.limitEnd > 0))
    {
      int limitEnd = sortDescription.limitEnd;
      if (sortDescription.limitStart > 0 && (size_t)sortDescription.limitStart < albums.size())
      {
        albums.erase(albums.begin(), albums.begin() + sortDescription.limitStart);
        limitEnd = sortDescription.limitEnd - sortDescription.limitStart;
      }
      if (limitEnd > 0 && (size_t)limitEnd < albums.size())
        albums.erase(albums.begin() + limitEnd, albums.end());
    }
    return true;
  }
  catch (...)
  {
    m_pDS->close();
    CLog::Log(LOGERROR, "%s (%s) failed", __FUNCTION__, filter.where.c_str());
  }
  return false;
}

bool CMusicDatabase::GetSongsFullByWhere(const std::string &baseDir, const Filter &filter, CFileItemList &items, const SortDescription &sortDescription /* = SortDescription() */, bool artistData /* = false*/)
{
  if (m_pDB.get() == NULL || m_pDS.get() == NULL)
    return false;

  try
  {
    unsigned int time = XbmcThreads::SystemClockMillis();
    int total = -1;

    Filter extFilter = filter;
    CMusicDbUrl musicUrl;
    SortDescription sorting = sortDescription;
    if (!musicUrl.FromString(baseDir) || !GetFilter(musicUrl, extFilter, sorting))
      return false;

    // if there are extra WHERE conditions we might need access
    // to songview for these conditions
    if (extFilter.where.find("albumview") != std::string::npos)
    {
      extFilter.AppendJoin("JOIN albumview ON albumview.idAlbum = songview.idAlbum");
      extFilter.AppendGroup("songview.idSong");
    }

    std::string strSQLExtra;
    if (!BuildSQL(strSQLExtra, extFilter, strSQLExtra))
      return false;
    
    // Count number of songs that satisfy selection criteria
    total = (int)strtol(GetSingleValue("SELECT COUNT(1) FROM songview " + strSQLExtra, m_pDS).c_str(), NULL, 10);

    // Apply any limiting directly in SQL if there is either no special sorting or random sort
    // When limited, random sort is also applied in SQL
    bool limitedInSQL = extFilter.limit.empty() && 
      (sortDescription.sortBy == SortByNone || sortDescription.sortBy == SortByRandom) &&
      (sortDescription.limitStart > 0 || sortDescription.limitEnd > 0);
    if (limitedInSQL)
    {
      if (sortDescription.sortBy == SortByRandom)
        strSQLExtra += PrepareSQL(" ORDER BY RANDOM()");
      strSQLExtra += DatabaseUtils::BuildLimitClause(sortDescription.limitEnd, sortDescription.limitStart);
    }

    std::string strSQL;
    if (artistData)
    { // Get data from song and song_artist tables to fully populate songs with artists
      // All songs now have at least one artist so inner join sufficient
      // Need guaranteed ordering for dataset processing to extract songs
      if (limitedInSQL)
        //Apply where clause, limits and random order to songview, then join as multiple records in result set per song
        strSQL = "SELECT sv.*, songartistview.* "
          "FROM (SELECT songview.* FROM songview " + strSQLExtra + ") AS sv "
          "JOIN songartistview ON songartistview.idsong = sv.idsong ";
      else
        strSQL = "SELECT songview.*, songartistview.* "
          "FROM songview JOIN songartistview ON songartistview.idsong = songview.idsong " + strSQLExtra;
      strSQL += " ORDER BY songartistview.idsong, songartistview.idRole, songartistview.iOrder";
    }
    else
      strSQL = "SELECT songview.* FROM songview " + strSQLExtra;

    CLog::Log(LOGDEBUG, "%s query = %s", __FUNCTION__, strSQL.c_str());
    // run query
    if (!m_pDS->query(strSQL))
      return false;

    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }

    // Store the total number of songs as a property
    items.SetProperty("total", total);

    DatabaseResults results;
    results.reserve(iRowsFound);
    // Avoid sorting with limits when have join with songartistview 
    // Limit when SortByNone already applied in SQL, 
    // apply sort later to fileitems list rather than dataset
    sorting = sortDescription;
    if (artistData && sortDescription.sortBy != SortByNone)
      sorting.sortBy = SortByNone;
    if (!SortUtils::SortFromDataset(sorting, MediaTypeSong, m_pDS, results))
      return false;

    // Get songs from returned rows. If join songartistview then there is a row for every artist
    items.Reserve(total);
    int songArtistOffset = song_enumCount;
    int songId = -1;
    VECARTISTCREDITS artistCredits;
    const dbiplus::query_data &data = m_pDS->get_result_set().records;
    int count = 0;
    for (const auto &i : results)
    {
      unsigned int targetRow = (unsigned int)i.at(FieldRow).asInteger();
      const dbiplus::sql_record* const record = data.at(targetRow);
      
      try
      {
        if (songId != record->at(song_idSong).get_asInt())
        { //New song
          if (songId > 0 && !artistCredits.empty())
          {
            //Store artist credits for previous song
            GetFileItemFromArtistCredits(artistCredits, items[items.Size()-1].get());
            artistCredits.clear();
          }
          songId = record->at(song_idSong).get_asInt();
          CFileItemPtr item(new CFileItem);
          GetFileItemFromDataset(record, item.get(), musicUrl);
          // HACK for sorting by database returned order
          item->m_iprogramCount = ++count;
          items.Add(item);
        }
        // Get song artist credits and contributors
        if (artistData)
        {
          int idSongArtistRole = record->at(songArtistOffset + artistCredit_idRole).get_asInt();
          if (idSongArtistRole == ROLE_ARTIST)
            artistCredits.push_back(GetArtistCreditFromDataset(record, songArtistOffset));
          else
            items[items.Size() - 1]->GetMusicInfoTag()->AppendArtistRole(GetArtistRoleFromDataset(record, songArtistOffset));
        }
      }
      catch (...)
      {
        m_pDS->close();
        CLog::Log(LOGERROR, "%s: out of memory loading query: %s", __FUNCTION__, filter.where.c_str());
        return (items.Size() > 0);
      }
    }
    if (!artistCredits.empty())
    {
      //Store artist credits for final song
      GetFileItemFromArtistCredits(artistCredits, items[items.Size() - 1].get());
      artistCredits.clear();
    }
    // cleanup
    m_pDS->close();

    // Finally do any sorting in items list we have not been able to do before in SQL or dataset,
    // that is when have join with songartistview and sorting other than random with limit
    if (artistData && sortDescription.sortBy != SortByNone && !(limitedInSQL && sortDescription.sortBy == SortByRandom))
      items.Sort(sortDescription);
     
    CLog::Log(LOGDEBUG, "%s(%s) - took %d ms", __FUNCTION__, filter.where.c_str(), XbmcThreads::SystemClockMillis() - time);
    return true;
  }
  catch (...)
  {
    // cleanup
    m_pDS->close();
    CLog::Log(LOGERROR, "%s(%s) failed", __FUNCTION__, filter.where.c_str());
  }
  return false;
}

bool CMusicDatabase::GetSongsByWhere(const std::string &baseDir, const Filter &filter, CFileItemList &items, const SortDescription &sortDescription /* = SortDescription() */)
{
  if (m_pDB.get() == NULL || m_pDS.get() == NULL)
    return false;

  try
  {
    int total = -1;

    std::string strSQL = "SELECT %s FROM songview ";

    Filter extFilter = filter;
    CMusicDbUrl musicUrl;
    SortDescription sorting = sortDescription;
    if (!musicUrl.FromString(baseDir) || !GetFilter(musicUrl, extFilter, sorting))
      return false;

    // if there are extra WHERE conditions we might need access
    // to songview for these conditions
    if (extFilter.where.find("albumview") != std::string::npos)
    {
      extFilter.AppendJoin("JOIN albumview ON albumview.idAlbum = songview.idAlbum");
      extFilter.AppendGroup("songview.idSong");
    }

    std::string strSQLExtra;
    if (!BuildSQL(strSQLExtra, extFilter, strSQLExtra))
      return false;

    // Apply the limiting directly here if there's no special sorting but limiting
    if (extFilter.limit.empty() &&
        sortDescription.sortBy == SortByNone &&
       (sortDescription.limitStart > 0 || sortDescription.limitEnd > 0))
    {
      total = (int)strtol(GetSingleValue(PrepareSQL(strSQL, "COUNT(1)") + strSQLExtra, m_pDS).c_str(), NULL, 10);
      strSQLExtra += DatabaseUtils::BuildLimitClause(sortDescription.limitEnd, sortDescription.limitStart);
    }

    strSQL = PrepareSQL(strSQL, !filter.fields.empty() && filter.fields.compare("*") != 0 ? filter.fields.c_str() : "songview.*") + strSQLExtra;

    CLog::Log(LOGDEBUG, "%s query = %s", __FUNCTION__, strSQL.c_str());
    // run query
    if (!m_pDS->query(strSQL))
      return false;

    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }

    // store the total value of items as a property
    if (total < iRowsFound)
      total = iRowsFound;
    items.SetProperty("total", total);
    
    DatabaseResults results;
    results.reserve(iRowsFound);
    if (!SortUtils::SortFromDataset(sortDescription, MediaTypeSong, m_pDS, results))
      return false;

    // get data from returned rows
    items.Reserve(results.size());
    const dbiplus::query_data &data = m_pDS->get_result_set().records;
    int count = 0;
    for (const auto &i : results)
    {
      unsigned int targetRow = (unsigned int)i.at(FieldRow).asInteger();
      const dbiplus::sql_record* const record = data.at(targetRow);
      
      try
      {
        CFileItemPtr item(new CFileItem);
        GetFileItemFromDataset(record, item.get(), musicUrl);
        // HACK for sorting by database returned order
        item->m_iprogramCount = ++count;
        items.Add(item);
      }
      catch (...)
      {
        m_pDS->close();
        CLog::Log(LOGERROR, "%s: out of memory loading query: %s", __FUNCTION__, filter.where.c_str());
        return (items.Size() > 0);
      }
    }

    // cleanup
    m_pDS->close();
    return true;
  }
  catch (...)
  {
    // cleanup
    m_pDS->close();
    CLog::Log(LOGERROR, "%s(%s) failed", __FUNCTION__, filter.where.c_str());
  }
  return false;
}

bool CMusicDatabase::GetSongsByYear(const std::string& baseDir, CFileItemList& items, int year)
{
  CMusicDbUrl musicUrl;
  if (!musicUrl.FromString(baseDir))
    return false;

  musicUrl.AddOption("year", year);
  
  Filter filter;
  return GetSongsFullByWhere(baseDir, filter, items, SortDescription(), true);
}

bool CMusicDatabase::GetSongsNav(const std::string& strBaseDir, CFileItemList& items, int idGenre, int idArtist, int idAlbum, const SortDescription &sortDescription /* = SortDescription() */)
{
  CMusicDbUrl musicUrl;
  if (!musicUrl.FromString(strBaseDir))
    return false;

  if (idAlbum > 0)
    musicUrl.AddOption("albumid", idAlbum);

  if (idGenre > 0)
    musicUrl.AddOption("genreid", idGenre);

  if (idArtist > 0)
    musicUrl.AddOption("artistid", idArtist);

  Filter filter;
  return GetSongsFullByWhere(musicUrl.ToString(), filter, items, sortDescription, true);
}

void CMusicDatabase::UpdateTables(int version)
{
  CLog::Log(LOGINFO, "%s - updating tables", __FUNCTION__);
  if (version < 34)
  {
    m_pDS->exec("ALTER TABLE artist ADD strMusicBrainzArtistID text\n");
    m_pDS->exec("ALTER TABLE album ADD strMusicBrainzAlbumID text\n");
    m_pDS->exec("CREATE TABLE song_new ( idSong integer primary key, idAlbum integer, idPath integer, strArtists text, strGenres text, strTitle varchar(512), iTrack integer, iDuration integer, iYear integer, dwFileNameCRC text, strFileName text, strMusicBrainzTrackID text, iTimesPlayed integer, iStartOffset integer, iEndOffset integer, idThumb integer, lastplayed varchar(20) default NULL, rating char default '0', comment text)\n");
    m_pDS->exec("INSERT INTO song_new ( idSong, idAlbum, idPath, strArtists, strTitle, iTrack, iDuration, iYear, dwFileNameCRC, strFileName, strMusicBrainzTrackID, iTimesPlayed, iStartOffset, iEndOffset, idThumb, lastplayed, rating, comment) SELECT idSong, idAlbum, idPath, strArtists, strTitle, iTrack, iDuration, iYear, dwFileNameCRC, strFileName, strMusicBrainzTrackID, iTimesPlayed, iStartOffset, iEndOffset, idThumb, lastplayed, rating, comment FROM song");
    
    m_pDS->exec("DROP TABLE song");
    m_pDS->exec("ALTER TABLE song_new RENAME TO song");
 
    m_pDS->exec("UPDATE song SET strMusicBrainzTrackID = NULL");
  }

  if (version < 36)
  {
    // translate legacy musicdb:// paths
    if (m_pDS->query("SELECT strPath FROM content"))
    {
      std::vector<std::string> contentPaths;
      while (!m_pDS->eof())
      {
        contentPaths.push_back(m_pDS->fv(0).get_asString());
        m_pDS->next();
      }
      m_pDS->close();

      for (const auto &originalPath : contentPaths)
      {
        std::string path = CLegacyPathTranslation::TranslateMusicDbPath(originalPath);
        m_pDS->exec(PrepareSQL("UPDATE content SET strPath='%s' WHERE strPath='%s'", path.c_str(), originalPath.c_str()));
      }
    }
  }

  if (version < 39)
  {
    m_pDS->exec("CREATE TABLE album_new "
                "(idAlbum integer primary key, "
                " strAlbum varchar(256), strMusicBrainzAlbumID text, "
                " strArtists text, strGenres text, "
                " iYear integer, idThumb integer, "
                " bCompilation integer not null default '0', "
                " strMoods text, strStyles text, strThemes text, "
                " strReview text, strImage text, strLabel text, "
                " strType text, "
                " iRating integer, "
                " lastScraped varchar(20) default NULL, "
                " dateAdded varchar (20) default NULL)");
    m_pDS->exec("INSERT INTO album_new "
                "(idAlbum, "
                " strAlbum, strMusicBrainzAlbumID, "
                " strArtists, strGenres, "
                " iYear, idThumb, "
                " bCompilation, "
                " strMoods, strStyles, strThemes, "
                " strReview, strImage, strLabel, "
                " strType, "
                " iRating) "
                " SELECT "
                " album.idAlbum, "
                " strAlbum, strMusicBrainzAlbumID, "
                " strArtists, strGenres, "
                " album.iYear, idThumb, "
                " bCompilation, "
                " strMoods, strStyles, strThemes, "
                " strReview, strImage, strLabel, "
                " strType, iRating "
                " FROM album LEFT JOIN albuminfo ON album.idAlbum = albuminfo.idAlbum");
    m_pDS->exec("UPDATE albuminfosong SET idAlbumInfo = (SELECT idAlbum FROM albuminfo WHERE albuminfo.idAlbumInfo = albuminfosong.idAlbumInfo)");
    m_pDS->exec(PrepareSQL("UPDATE album_new SET lastScraped='%s' WHERE idAlbum IN (SELECT idAlbum FROM albuminfo)", CDateTime::GetCurrentDateTime().GetAsDBDateTime().c_str()));
    m_pDS->exec("DROP TABLE album");
    m_pDS->exec("DROP TABLE albuminfo");
    m_pDS->exec("ALTER TABLE album_new RENAME TO album");
  }
  if (version < 40)
  {
    m_pDS->exec("CREATE TABLE artist_new ( idArtist integer primary key, "
                " strArtist varchar(256), strMusicBrainzArtistID text, "
                " strBorn text, strFormed text, strGenres text, strMoods text, "
                " strStyles text, strInstruments text, strBiography text, "
                " strDied text, strDisbanded text, strYearsActive text, "
                " strImage text, strFanart text, "
                " lastScraped varchar(20) default NULL, "
                " dateAdded varchar (20) default NULL)");
    m_pDS->exec("INSERT INTO artist_new "
                "(idArtist, strArtist, strMusicBrainzArtistID, "
                " strBorn, strFormed, strGenres, strMoods, "
                " strStyles , strInstruments , strBiography , "
                " strDied, strDisbanded, strYearsActive, "
                " strImage, strFanart) "
                " SELECT "
                " artist.idArtist, "
                " strArtist, strMusicBrainzArtistID, "
                " strBorn, strFormed, strGenres, strMoods, "
                " strStyles, strInstruments, strBiography, "
                " strDied, strDisbanded, strYearsActive, "
                " strImage, strFanart "
                " FROM artist "
                " LEFT JOIN artistinfo ON artist.idArtist = artistinfo.idArtist");
    m_pDS->exec(PrepareSQL("UPDATE artist_new SET lastScraped='%s' WHERE idArtist IN (SELECT idArtist FROM artistinfo)", CDateTime::GetCurrentDateTime().GetAsDBDateTime().c_str()));
    m_pDS->exec("DROP TABLE artist");
    m_pDS->exec("DROP TABLE artistinfo");
    m_pDS->exec("ALTER TABLE artist_new RENAME TO artist");
  }
  if (version < 42)
  {
    m_pDS->exec("ALTER TABLE album_artist ADD strArtist text\n");
    m_pDS->exec("ALTER TABLE song_artist ADD strArtist text\n");
    // populate these
    std::string sql = "select idArtist,strArtist from artist";
    m_pDS->query(sql);
    while (!m_pDS->eof())
    {
      m_pDS2->exec(PrepareSQL("UPDATE song_artist SET strArtist='%s' where idArtist=%i", m_pDS->fv(1).get_asString().c_str(), m_pDS->fv(0).get_asInt()));
      m_pDS2->exec(PrepareSQL("UPDATE album_artist SET strArtist='%s' where idArtist=%i", m_pDS->fv(1).get_asString().c_str(), m_pDS->fv(0).get_asInt()));
      m_pDS->next();
    }
  }
  if (version < 48)
  { // null out columns that are no longer used
    m_pDS->exec("UPDATE song SET dwFileNameCRC=NULL, idThumb=NULL");
    m_pDS->exec("UPDATE album SET idThumb=NULL");
  }
  if (version < 49)
  {
    m_pDS->exec("CREATE TABLE cue (idPath integer, strFileName text, strCuesheet text)");
  }
  if (version < 50)
  {
    // add a new column strReleaseType for albums
    m_pDS->exec("ALTER TABLE album ADD strReleaseType text\n");

    // set strReleaseType based on album name
    m_pDS->exec(PrepareSQL("UPDATE album SET strReleaseType = '%s' WHERE strAlbum IS NOT NULL AND strAlbum <> ''", CAlbum::ReleaseTypeToString(CAlbum::Album).c_str()));
    m_pDS->exec(PrepareSQL("UPDATE album SET strReleaseType = '%s' WHERE strAlbum IS NULL OR strAlbum = ''", CAlbum::ReleaseTypeToString(CAlbum::Single).c_str()));
  }
  if (version < 51)
  {
    m_pDS->exec("ALTER TABLE song ADD mood text\n");
  }
  if (version < 53)
  {
    m_pDS->exec("ALTER TABLE song ADD dateAdded text");
  }
  if (version < 54)
  {
      //Remove dateAdded from artist table
      m_pDS->exec("CREATE TABLE artist_new ( idArtist integer primary key, "
              " strArtist varchar(256), strMusicBrainzArtistID text, "
              " strBorn text, strFormed text, strGenres text, strMoods text, "
              " strStyles text, strInstruments text, strBiography text, "
              " strDied text, strDisbanded text, strYearsActive text, "
              " strImage text, strFanart text, "
              " lastScraped varchar(20) default NULL)");
      m_pDS->exec("INSERT INTO artist_new "
          "(idArtist, strArtist, strMusicBrainzArtistID, "
          " strBorn, strFormed, strGenres, strMoods, "
          " strStyles , strInstruments , strBiography , "
          " strDied, strDisbanded, strYearsActive, "
          " strImage, strFanart, lastScraped) "
          " SELECT "
          " idArtist, "
          " strArtist, strMusicBrainzArtistID, "
          " strBorn, strFormed, strGenres, strMoods, "
          " strStyles, strInstruments, strBiography, "
          " strDied, strDisbanded, strYearsActive, "
          " strImage, strFanart, lastScraped "
          " FROM artist");
      m_pDS->exec("DROP TABLE artist");
      m_pDS->exec("ALTER TABLE artist_new RENAME TO artist");

      //Remove dateAdded from album table
      m_pDS->exec("CREATE TABLE album_new (idAlbum integer primary key, "
              " strAlbum varchar(256), strMusicBrainzAlbumID text, "
              " strArtists text, strGenres text, "
              " iYear integer, idThumb integer, "
              " bCompilation integer not null default '0', "
              " strMoods text, strStyles text, strThemes text, "
              " strReview text, strImage text, strLabel text, "
              " strType text, "
              " iRating integer, "
              " lastScraped varchar(20) default NULL, "
              " strReleaseType text)");
      m_pDS->exec("INSERT INTO album_new "
          "(idAlbum, "
          " strAlbum, strMusicBrainzAlbumID, "
          " strArtists, strGenres, "
          " iYear, idThumb, "
          " bCompilation, "
          " strMoods, strStyles, strThemes, "
          " strReview, strImage, strLabel, "
          " strType, iRating, lastScraped, "
          " strReleaseType) "
          " SELECT "
          " album.idAlbum, "
          " strAlbum, strMusicBrainzAlbumID, "
          " strArtists, strGenres, "
          " iYear, idThumb, "
          " bCompilation, "
          " strMoods, strStyles, strThemes, "
          " strReview, strImage, strLabel, "
          " strType, iRating, lastScraped, "
          " strReleaseType"
          " FROM album");
      m_pDS->exec("DROP TABLE album");
      m_pDS->exec("ALTER TABLE album_new RENAME TO album");
   }
   if (version < 55)
   {
     m_pDS->exec("DROP TABLE karaokedata");
   }
   if (version < 57)
   {
     m_pDS->exec("ALTER TABLE song ADD userrating INTEGER NOT NULL DEFAULT 0");
     m_pDS->exec("UPDATE song SET rating = 0 WHERE rating < 0 or rating IS NULL");
     m_pDS->exec("UPDATE song SET userrating = rating * 2");
     m_pDS->exec("UPDATE song SET rating = 0");
     m_pDS->exec("CREATE TABLE song_new (idSong INTEGER PRIMARY KEY, "
       " idAlbum INTEGER, idPath INTEGER, "
       " strArtists TEXT, strGenres TEXT, strTitle VARCHAR(512), "
       " iTrack INTEGER, iDuration INTEGER, iYear INTEGER, "
       " dwFileNameCRC TEXT, "
       " strFileName TEXT, strMusicBrainzTrackID TEXT, "
       " iTimesPlayed INTEGER, iStartOffset INTEGER, iEndOffset INTEGER, "
       " idThumb INTEGER, "
       " lastplayed VARCHAR(20) DEFAULT NULL, "
       " rating FLOAT DEFAULT 0, "
       " userrating INTEGER DEFAULT 0, "
       " comment TEXT, mood TEXT, dateAdded TEXT)");
     m_pDS->exec("INSERT INTO song_new "
       "(idSong, "
       " idAlbum, idPath, "
       " strArtists, strGenres, strTitle, "
       " iTrack, iDuration, iYear, "
       " dwFileNameCRC, "
       " strFileName, strMusicBrainzTrackID, "
       " iTimesPlayed, iStartOffset, iEndOffset, "
       " idThumb, "
       " lastplayed,"
       " rating, userrating, "
       " comment, mood, dateAdded)"
       " SELECT "
       " idSong, "
       " idAlbum, idPath, "
       " strArtists, strGenres, strTitle, "
       " iTrack, iDuration, iYear, "
       " dwFileNameCRC, "
       " strFileName, strMusicBrainzTrackID, "
       " iTimesPlayed, iStartOffset, iEndOffset, "
       " idThumb, "
       " lastplayed,"
       " rating, "
       " userrating, "
       " comment, mood, dateAdded"
       " FROM song");
     m_pDS->exec("DROP TABLE song");
     m_pDS->exec("ALTER TABLE song_new RENAME TO song");

     m_pDS->exec("ALTER TABLE album ADD iUserrating INTEGER NOT NULL DEFAULT 0");
     m_pDS->exec("UPDATE album SET iRating = 0 WHERE iRating < 0 or iRating IS NULL");
     m_pDS->exec("CREATE TABLE album_new (idAlbum INTEGER PRIMARY KEY, "
       " strAlbum VARCHAR(256), strMusicBrainzAlbumID TEXT, "
       " strArtists TEXT, strGenres TEXT, "
       " iYear INTEGER, idThumb INTEGER, "
       " bCompilation INTEGER NOT NULL DEFAULT '0', "
       " strMoods TEXT, strStyles TEXT, strThemes TEXT, "
       " strReview TEXT, strImage TEXT, strLabel TEXT, "
       " strType TEXT, "
       " fRating FLOAT NOT NULL DEFAULT 0, "
       " iUserrating INTEGER NOT NULL DEFAULT 0, "
       " lastScraped VARCHAR(20) DEFAULT NULL, "
       " strReleaseType TEXT)");
     m_pDS->exec("INSERT INTO album_new "
       "(idAlbum, "
       " strAlbum, strMusicBrainzAlbumID, "
       " strArtists, strGenres, "
       " iYear, idThumb, "
       " bCompilation, "
       " strMoods, strStyles, strThemes, "
       " strReview, strImage, strLabel, "
       " strType, "
       " fRating, "
       " iUserrating, "
       " lastScraped, "
       " strReleaseType)"
       " SELECT "
       " idAlbum, "
       " strAlbum, strMusicBrainzAlbumID, "
       " strArtists, strGenres, "
       " iYear, idThumb, "
       " bCompilation, "
       " strMoods, strStyles, strThemes, "
       " strReview, strImage, strLabel, "
       " strType, "
       " iRating, "
       " iUserrating, "
       " lastScraped, "
       " strReleaseType"
       " FROM album");
     m_pDS->exec("DROP TABLE album");
     m_pDS->exec("ALTER TABLE album_new RENAME TO album");

     m_pDS->exec("ALTER TABLE album ADD iVotes INTEGER NOT NULL DEFAULT 0");
     m_pDS->exec("ALTER TABLE song ADD votes INTEGER NOT NULL DEFAULT 0");
   }
  if (version < 58)
  {
     m_pDS->exec("UPDATE album SET fRating = fRating * 2");
  }
  if (version < 59)
  {
    m_pDS->exec("CREATE TABLE role (idRole integer primary key, strRole text)");
    m_pDS->exec("INSERT INTO role(idRole, strRole) VALUES (1, 'Artist')"); //Default Role

    //Remove strJoinPhrase, boolFeatured from song_artist table and add idRole
    m_pDS->exec("CREATE TABLE song_artist_new (idArtist integer, idSong integer, idRole integer, iOrder integer, strArtist text)");
    m_pDS->exec("INSERT INTO song_artist_new (idArtist, idSong, idRole, iOrder, strArtist) "
      "SELECT idArtist, idSong, 1 as idRole, iOrder, strArtist FROM song_artist");
    m_pDS->exec("DROP TABLE song_artist");
    m_pDS->exec("ALTER TABLE song_artist_new RENAME TO song_artist");

    //Remove strJoinPhrase, boolFeatured from album_artist table
    m_pDS->exec("CREATE TABLE album_artist_new (idArtist integer, idAlbum integer, iOrder integer, strArtist text)");
    m_pDS->exec("INSERT INTO album_artist_new (idArtist, idAlbum, iOrder, strArtist) "
      "SELECT idArtist, idAlbum, iOrder, strArtist FROM album_artist");
    m_pDS->exec("DROP TABLE album_artist");
    m_pDS->exec("ALTER TABLE album_artist_new RENAME TO album_artist");
  }
  if (version < 60)
  { 
    // From now on artist ID = 1 will be an artificial artist [Missing] use for songs that
    // do not have an artist tag to ensure all songs in the library have at least one artist.
    std::string strSQL;
    if (GetArtistExists(BLANKARTIST_ID))
    { 
      // When BLANKARTIST_ID (=1) is already in use, move the record
      try
      { //No mbid index yet, so can have record for artist twice even with mbid
        strSQL = PrepareSQL("INSERT INTO artist SELECT null, "
          "strArtist, strMusicBrainzArtistID, "
          "strBorn, strFormed, strGenres, strMoods, "
          "strStyles, strInstruments, strBiography, "
          "strDied, strDisbanded, strYearsActive, "
          "strImage, strFanart, lastScraped "
          "FROM artist WHERE artist.idArtist = %i", BLANKARTIST_ID);
        m_pDS->exec(strSQL);
        int idArtist = (int)m_pDS->lastinsertid();
        //No triggers, so can delete artist without effecting other tables.
        strSQL = PrepareSQL("DELETE FROM artist WHERE artist.idArtist = %i", BLANKARTIST_ID);
        m_pDS->exec(strSQL);

        // Update related tables with the new artist ID
        // Indices have been dropped making transactions very slow, so create appropriate temp indices     
        m_pDS->exec("CREATE INDEX idxSongArtist2 ON song_artist ( idArtist )");
        m_pDS->exec("CREATE INDEX idxAlbumArtist2 ON album_artist ( idArtist )");
        m_pDS->exec("CREATE INDEX idxDiscography ON discography ( idArtist )");
        m_pDS->exec("CREATE INDEX ix_art ON art ( media_id, media_type(20) )");
        strSQL = PrepareSQL("UPDATE song_artist SET idArtist = %i WHERE idArtist = %i", idArtist, BLANKARTIST_ID);
        m_pDS->exec(strSQL);
        strSQL = PrepareSQL("UPDATE album_artist SET idArtist = %i WHERE idArtist = %i", idArtist, BLANKARTIST_ID);
        m_pDS->exec(strSQL);
        strSQL = PrepareSQL("UPDATE art SET media_id = %i WHERE media_id = %i AND media_type='artist'", idArtist, BLANKARTIST_ID);
        m_pDS->exec(strSQL);
        strSQL = PrepareSQL("UPDATE discography SET idArtist = %i WHERE idArtist = %i", idArtist, BLANKARTIST_ID);
        m_pDS->exec(strSQL);
        // Drop temp indices
        m_pDS->exec("DROP INDEX idxSongArtist2 ON song_artist");
        m_pDS->exec("DROP INDEX idxAlbumArtist2 ON album_artist");
        m_pDS->exec("DROP INDEX idxDiscography ON discography");
        m_pDS->exec("DROP INDEX ix_art ON art");
      }
      catch (...)
      {
        CLog::Log(LOGERROR, "Moving existing artist to add missing tag artist has failed");
      }
    }

    // Create missing artist tag artist [Missing].
    // Fake MusicbrainzId assures uniqueness and avoids updates from scanned songs
    strSQL = PrepareSQL("INSERT INTO artist (idArtist, strArtist, strMusicBrainzArtistID) VALUES( %i, '%s', '%s' )",
      BLANKARTIST_ID, BLANKARTIST_NAME.c_str(), BLANKARTIST_FAKEMUSICBRAINZID.c_str());
    m_pDS->exec(strSQL);

    // Indices have been dropped making transactions very slow, so create temp index
    m_pDS->exec("CREATE INDEX idxSongArtist1 ON song_artist ( idSong, idRole )");
    m_pDS->exec("CREATE INDEX idxAlbumArtist1 ON album_artist ( idAlbum )");

    // Ensure all songs have at least one artist, set those without to [Missing] 
    strSQL = "SELECT count(idSong) FROM song "
             "WHERE NOT EXISTS(SELECT idSong FROM song_artist "
             "WHERE song_artist.idsong = song.idsong AND song_artist.idRole = 1)";
    int numsongs = strtol(GetSingleValue(strSQL).c_str(), NULL, 10);
    if (numsongs > 0)
    { 
      CLog::Log(LOGDEBUG, "%i songs have no artist, setting artist to [Missing]", numsongs);
      // Insert song_artist records for songs that don't have any
      try
      {
        strSQL = PrepareSQL("INSERT INTO song_artist(idArtist, idSong, idRole, strArtist, iOrder) "
          "SELECT %i, idSong, %i, '%s', 0 FROM song "
          "WHERE NOT EXISTS(SELECT idSong FROM song_artist "
          "WHERE song_artist.idsong = song.idsong AND song_artist.idRole = %i)", 
          BLANKARTIST_ID, ROLE_ARTIST, BLANKARTIST_NAME.c_str(), ROLE_ARTIST);
        ExecuteQuery(strSQL);
      }
      catch (...)
      {
        CLog::Log(LOGERROR, "Setting missing artist for songs without an artist has failed");
      }
    }
    
    // Ensure all albums have at least one artist, set those without to [Missing]
    strSQL = "SELECT count(idAlbum) FROM album "
      "WHERE NOT EXISTS(SELECT idAlbum FROM album_artist "
      "WHERE album_artist.idAlbum = album.idAlbum)";
    int numalbums = strtol(GetSingleValue(strSQL).c_str(), NULL, 10);
    if (numalbums > 0)
    {
      CLog::Log(LOGDEBUG, "%i albums have no artist, setting artist to [Missing]", numalbums);
      // Insert album_artist records for albums that don't have any
      try
      {
        strSQL = PrepareSQL("INSERT INTO album_artist(idArtist, idAlbum, strArtist, iOrder) "
          "SELECT %i, idAlbum, '%s', 0 FROM album "
          "WHERE NOT EXISTS(SELECT idAlbum FROM album_artist "
          "WHERE album_artist.idAlbum = album.idAlbum)", 
          BLANKARTIST_ID, BLANKARTIST_NAME.c_str());
        ExecuteQuery(strSQL);
      }
      catch (...)
      {
        CLog::Log(LOGERROR, "Setting artist missing for albums without an artist has failed");
      }
    }
    //Remove temp indices, full analytics for database created later
    m_pDS->exec("DROP INDEX idxSongArtist1 ON song_artist");
    m_pDS->exec("DROP INDEX idxAlbumArtist1 ON album_artist");
  }
  if (version < 61)
  {
    // Create versiontagscan table
    m_pDS->exec("CREATE TABLE versiontagscan (idVersion integer, iNeedsScan integer)");
    m_pDS->exec("INSERT INTO versiontagscan (idVersion, iNeedsScan) values(0, 0)");
  }
  if (version < 62)
  {
    CLog::Log(LOGINFO, "create audiobook table");
    m_pDS->exec("CREATE TABLE audiobook (idBook integer primary key, "
        " strBook varchar(256), strAuthor text,"
        " bookmark integer, file text,"
        " dateAdded varchar (20) default NULL)");
  }
  if (version < 63)
  {
    // Add strSortName to Artist table
    m_pDS->exec("ALTER TABLE artist ADD strSortName text\n");

    //Remove idThumb (column unused since v47), rename strArtists and add strArtistSort to album table
    m_pDS->exec("CREATE TABLE album_new (idAlbum integer primary key, "
      " strAlbum varchar(256), strMusicBrainzAlbumID text, "
      " strArtistDisp text, strArtistSort text, strGenres text, "
      " iYear integer, bCompilation integer not null default '0', "
      " strMoods text, strStyles text, strThemes text, "
      " strReview text, strImage text, strLabel text, "
      " strType text, "
      " fRating FLOAT NOT NULL DEFAULT 0, "
      " iUserrating INTEGER NOT NULL DEFAULT 0, "
      " lastScraped varchar(20) default NULL, "
      " strReleaseType text, "
      " iVotes INTEGER NOT NULL DEFAULT 0)");
    m_pDS->exec("INSERT INTO album_new "
      "(idAlbum, "
      " strAlbum, strMusicBrainzAlbumID, "
      " strArtistDisp, strArtistSort, strGenres, "
      " iYear, bCompilation, "
      " strMoods, strStyles, strThemes, "
      " strReview, strImage, strLabel, "
      " strType, "
      " fRating, iUserrating, iVotes, "
      " lastScraped, "
      " strReleaseType)"
      " SELECT "
      " idAlbum, "
      " strAlbum, strMusicBrainzAlbumID, "
      " strArtists, NULL, strGenres, "
      " iYear, bCompilation, "
      " strMoods, strStyles, strThemes, "
      " strReview, strImage, strLabel, "
      " strType, "
      " fRating, iUserrating, iVotes, "
      " lastScraped, "
      " strReleaseType"
      " FROM album");
    m_pDS->exec("DROP TABLE album");
    m_pDS->exec("ALTER TABLE album_new RENAME TO album");

    //Remove dwFileNameCRC, idThumb (columns unused since v47), rename strArtists and add strArtistSort to song table
    m_pDS->exec("CREATE TABLE song_new (idSong INTEGER PRIMARY KEY, "
      " idAlbum INTEGER, idPath INTEGER, "
      " strArtistDisp TEXT, strArtistSort TEXT, strGenres TEXT, strTitle VARCHAR(512), "
      " iTrack INTEGER, iDuration INTEGER, iYear INTEGER, "
      " strFileName TEXT, strMusicBrainzTrackID TEXT, "
      " iTimesPlayed INTEGER, iStartOffset INTEGER, iEndOffset INTEGER, "
      " lastplayed VARCHAR(20) DEFAULT NULL, "
      " rating FLOAT NOT NULL DEFAULT 0, votes INTEGER NOT NULL DEFAULT 0, "
      " userrating INTEGER NOT NULL DEFAULT 0, "
      " comment TEXT, mood TEXT, dateAdded TEXT)");
    m_pDS->exec("INSERT INTO song_new "
      "(idSong, "
      " idAlbum, idPath, "
      " strArtistDisp, strArtistSort, strGenres, strTitle, "
      " iTrack, iDuration, iYear, "
      " strFileName, strMusicBrainzTrackID, "
      " iTimesPlayed, iStartOffset, iEndOffset, "
      " lastplayed,"
      " rating, userrating, votes, "
      " comment, mood, dateAdded)"
      " SELECT "
      " idSong, "
      " idAlbum, idPath, "
      " strArtists, NULL, strGenres, strTitle, "
      " iTrack, iDuration, iYear, "
      " strFileName, strMusicBrainzTrackID, "
      " iTimesPlayed, iStartOffset, iEndOffset, "
      " lastplayed,"
      " rating, userrating, votes, "
      " comment, mood, dateAdded"
      " FROM song");
    m_pDS->exec("DROP TABLE song");
    m_pDS->exec("ALTER TABLE song_new RENAME TO song");
  }
  if (version < 65)
  {
    // Remove cue table
    m_pDS->exec("DROP TABLE cue");
    // Add strReplayGain to song table
    m_pDS->exec("ALTER TABLE song ADD strReplayGain TEXT\n");
  }
  if (version < 66)
  {
    // Add a new columns strReleaseGroupMBID, bScrapedMBID for albums
    m_pDS->exec("ALTER TABLE album ADD bScrapedMBID INTEGER NOT NULL DEFAULT 0\n");
    m_pDS->exec("ALTER TABLE album ADD strReleaseGroupMBID TEXT \n");
    // Add a new column bScrapedMBID for artists
    m_pDS->exec("ALTER TABLE artist ADD bScrapedMBID INTEGER NOT NULL DEFAULT 0\n");
  }
  if (version < 67)
  {
    // Add infosetting table
    m_pDS->exec("CREATE TABLE infosetting (idSetting INTEGER PRIMARY KEY, strScraperPath TEXT, strSettings TEXT)");
    // Add a new column for setting to album and artist tables
    m_pDS->exec("ALTER TABLE artist ADD idInfoSetting INTEGER NOT NULL DEFAULT 0\n");
    m_pDS->exec("ALTER TABLE album ADD idInfoSetting INTEGER NOT NULL DEFAULT 0\n");

    // Attempt to get album and artist specific scraper settings from the content table, extracting ids from path
    m_pDS->exec("CREATE TABLE content_temp(id INTEGER PRIMARY KEY, idItem INTEGER, strContent text, "
      "strScraperPath text, strSettings text)");
    try
    {
      m_pDS->exec("INSERT INTO content_temp(idItem, strContent, strScraperPath, strSettings) "
        "SELECT SUBSTR(strPath, 19, LENGTH(strPath) - 19) + 0 AS idItem, strContent, strScraperPath, strSettings "
        "FROM content WHERE strContent = 'artists' AND strPath LIKE 'musicdb://artists/_%/' ORDER BY idItem"
        );
    }
    catch (...)
    {
      CLog::Log(LOGERROR, "Migrating specific artist scraper settings has failed, settings not transfered");
    }
    try
    {
      m_pDS->exec("INSERT INTO content_temp (idItem, strContent, strScraperPath, strSettings ) "
        "SELECT SUBSTR(strPath, 18, LENGTH(strPath) - 18) + 0 AS idItem, strContent, strScraperPath, strSettings "
        "FROM content WHERE strContent = 'albums' AND strPath LIKE 'musicdb://albums/_%/' ORDER BY idItem"
      );
    }
    catch (...)
    {
      CLog::Log(LOGERROR, "Migrating specific album scraper settings has failed, settings not transfered");
    }
    try
    {
      m_pDS->exec("INSERT INTO infosetting(idSetting, strScraperPath, strSettings) "
        "SELECT id, strScraperPath, strSettings FROM content_temp");
      m_pDS->exec("UPDATE artist SET idInfoSetting = " 
        "(SELECT id FROM content_temp WHERE strContent = 'artists' AND idItem = idArtist) "
        "WHERE EXISTS(SELECT 1 FROM content_temp WHERE strContent = 'artists' AND idItem = idArtist) ");
      m_pDS->exec("UPDATE album SET idInfoSetting = "
        "(SELECT id FROM content_temp WHERE strContent = 'albums' AND idItem = idAlbum) "
        "WHERE EXISTS(SELECT 1 FROM content_temp WHERE strContent = 'albums' AND idItem = idAlbum) ");
    }
    catch (...)
    {
      CLog::Log(LOGERROR, "Migrating album and artist scraper settings has failed, settings not transfered");
    }
    m_pDS->exec("DROP TABLE content_temp");

    // Remove content table
    m_pDS->exec("DROP TABLE content");
    // Remove albuminfosong table
    m_pDS->exec("DROP TABLE albuminfosong");
  }
  // Set the verion of tag scanning required. 
  // Not every schema change requires the tags to be rescanned, set to the highest schema version 
  // that needs this. Forced rescanning (of music files that have not changed since they were 
  // previously scanned) also accommodates any changes to the way tags are processed 
  // e.g. read tags that were not processed by previous versions.
  // The original db version when the tags were scanned, and the minimal db version needed are 
  // later used to determine if a forced rescan should be prompted
  
  // The last schema change needing forced rescanning was 60.
  // Mostly because of the new tags processed by v17 rather than a schema change.
  SetMusicNeedsTagScan(60);

  // After all updates, store the original db version. 
  // This indicates the version of tag processing that was used to populate db
  SetMusicTagScanVersion(version);
}

int CMusicDatabase::GetSchemaVersion() const
{
  return 67;
}

int CMusicDatabase::GetMusicNeedsTagScan()
{
  try
  {
    if (NULL == m_pDB.get()) return -1;
    if (NULL == m_pDS.get()) return -1;

    std::string sql = "SELECT * FROM versiontagscan";
    if (!m_pDS->query(sql)) return -1;

    if (m_pDS->num_rows() != 1)
    {
      m_pDS->close();
      return -1;
    }

    int idVersion = m_pDS->fv("idVersion").get_asInt();
    int iNeedsScan = m_pDS->fv("iNeedsScan").get_asInt();
    m_pDS->close();
    if (idVersion < iNeedsScan)
      return idVersion;
    else
      return 0;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return -1;
}

void CMusicDatabase::SetMusicNeedsTagScan(int version)
{
  m_pDS->exec(PrepareSQL("UPDATE versiontagscan SET iNeedsScan=%i", version));
}

void CMusicDatabase::SetMusicTagScanVersion(int version /* = 0 */)
{
  if (version == 0)
    m_pDS->exec(PrepareSQL("UPDATE versiontagscan SET idVersion=%i", GetSchemaVersion()));
  else
    m_pDS->exec(PrepareSQL("UPDATE versiontagscan SET idVersion=%i", version));
}

unsigned int CMusicDatabase::GetSongIDs(const Filter &filter, std::vector<std::pair<int,int> > &songIDs)
{
  try
  {
    if (NULL == m_pDB.get()) return 0;
    if (NULL == m_pDS.get()) return 0;

    std::string strSQL = "select idSong from songview ";
    if (!CDatabase::BuildSQL(strSQL, filter, strSQL))
      return false;

    if (!m_pDS->query(strSQL)) return 0;
    songIDs.clear();
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return 0;
    }
    songIDs.reserve(m_pDS->num_rows());
    while (!m_pDS->eof())
    {
      songIDs.push_back(std::make_pair<int,int>(1,m_pDS->fv(song_idSong).get_asInt()));
      m_pDS->next();
    }    // cleanup
    m_pDS->close();
    return songIDs.size();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%s) failed", __FUNCTION__, filter.where.c_str());
  }
  return 0;
}

int CMusicDatabase::GetSongsCount(const Filter &filter)
{
  try
  {
    if (NULL == m_pDB.get()) return 0;
    if (NULL == m_pDS.get()) return 0;

    std::string strSQL = "select count(idSong) as NumSongs from songview ";
    if (!CDatabase::BuildSQL(strSQL, filter, strSQL))
      return false;

    if (!m_pDS->query(strSQL)) return false;
    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return 0;
    }

    int iNumSongs = m_pDS->fv("NumSongs").get_asInt();
    // cleanup
    m_pDS->close();
    return iNumSongs;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%s) failed", __FUNCTION__, filter.where.c_str());
  }
  return 0;
}

bool CMusicDatabase::GetAlbumPath(int idAlbum, std::string& path)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS2.get()) return false;

    path.clear();

    std::string strSQL=PrepareSQL("select strPath from song join path on song.idPath = path.idPath where song.idAlbum=%ld", idAlbum);
    if (!m_pDS2->query(strSQL)) return false;
    int iRowsFound = m_pDS2->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS2->close();
      return false;
    }

    // if this returns more than one path, we just grab the first one.  It's just for determining where to obtain + place
    // a local thumbnail
    path = m_pDS2->fv("strPath").get_asString();

    m_pDS2->close(); // cleanup recordset data
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%i) failed", __FUNCTION__, idAlbum);
  }

  return false;
}

bool CMusicDatabase::SaveAlbumThumb(int idAlbum, const std::string& strThumb)
{
  SetArtForItem(idAlbum, MediaTypeAlbum, "thumb", strThumb);
  //! @todo We should prompt the user to update the art for songs
  std::string sql = PrepareSQL("UPDATE art"
                              " SET url='-'"
                              " WHERE media_type='song'"
                              " AND type='thumb'"
                              " AND media_id IN"
                              " (SELECT idSong FROM song WHERE idAlbum=%ld)", idAlbum);
  ExecuteQuery(sql);
  return true;
}

bool CMusicDatabase::GetArtistPath(int idArtist, std::string &basePath)
{
  basePath.clear();
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS2.get()) return false;

    // find all albums from this artist, and all the paths to the songs from those albums
    std::string strSQL=PrepareSQL("SELECT strPath"
                                 "  FROM album_artist"
                                 "  JOIN song "
                                 "    ON album_artist.idAlbum = song.idAlbum"
                                 "  JOIN path"
                                 "    ON song.idPath = path.idPath"
                                 " WHERE album_artist.idArtist = %i"
                                 " GROUP BY song.idPath", idArtist);

    // run query
    if (!m_pDS2->query(strSQL)) return false;
    int iRowsFound = m_pDS2->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS2->close();
      return false;
    }

    // special case for single path - assume that we're in an artist/album/songs filesystem
    if (iRowsFound == 1)
    {
      URIUtils::GetParentPath(m_pDS2->fv("strPath").get_asString(), basePath);
      m_pDS2->close();
      return true;
    }

    // find the common path (if any) to these albums
    while (!m_pDS2->eof())
    {
      std::string path = m_pDS2->fv("strPath").get_asString();
      if (basePath.empty())
        basePath = path;
      else
        URIUtils::GetCommonPath(basePath,path);

      m_pDS2->next();
    }

    // cleanup
    m_pDS2->close();
    return true;

  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return false;
}

int CMusicDatabase::GetArtistByName(const std::string& strArtist)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string strSQL=PrepareSQL("select idArtist from artist where artist.strArtist like '%s'", strArtist.c_str());

    // run query
    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound != 1)
    {
      m_pDS->close();
      return -1;
    }
    int lResult = m_pDS->fv("artist.idArtist").get_asInt();
    m_pDS->close();
    return lResult;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return -1;
}

int CMusicDatabase::GetAlbumByName(const std::string& strAlbum, const std::string& strArtist)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string strSQL;
    if (strArtist.empty())
      strSQL=PrepareSQL("SELECT idAlbum FROM album WHERE album.strAlbum LIKE '%s'", strAlbum.c_str());
    else
      strSQL=PrepareSQL("SELECT album.idAlbum FROM album WHERE album.strAlbum LIKE '%s' AND album.strArtistDisp LIKE '%s'", strAlbum.c_str(),strArtist.c_str());
    // run query
    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound != 1)
    {
      m_pDS->close();
      return -1;
    }
    return m_pDS->fv("album.idAlbum").get_asInt();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return -1;
}

int CMusicDatabase::GetAlbumByName(const std::string& strAlbum, const std::vector<std::string>& artist)
{
  return GetAlbumByName(strAlbum, StringUtils::Join(artist, g_advancedSettings.m_musicItemSeparator));
}

std::string CMusicDatabase::GetGenreById(int id)
{
  return GetSingleValue("genre", "strGenre", PrepareSQL("idGenre=%i", id));
}

std::string CMusicDatabase::GetArtistById(int id)
{
  return GetSingleValue("artist", "strArtist", PrepareSQL("idArtist=%i", id));
}

std::string CMusicDatabase::GetRoleById(int id)
{
  return GetSingleValue("role", "strRole", PrepareSQL("idRole=%i", id));
}

bool CMusicDatabase::UpdateArtistSortNames(int idArtist /*=-1*/)
{
  // Propagate artist sort names into concatenated artist sort name string for songs and albums
  std::string strSQL;
  // MySQL syntax for GROUP_CONCAT is different from that in SQLite
  bool bisMySQL = StringUtils::EqualsNoCase(g_advancedSettings.m_databaseMusic.type, "mysql");
  
  BeginMultipleExecute();   
  if (bisMySQL)
    strSQL = "UPDATE album SET strArtistSort =  "
      "(SELECT GROUP_CONCAT("
      "CASE WHEN artist.strSortName IS NULL THEN artist.strArtist "
      "ELSE artist.strSortName END "
      "ORDER BY album_artist.idAlbum, album_artist.iOrder "
      "SEPARATOR '; ') as val "
      "FROM album_artist JOIN artist on artist.idArtist = album_artist.idArtist "
      "WHERE album_artist.idAlbum = album.idAlbum GROUP BY idAlbum) "
      "WHERE album.strArtistSort = '' OR album.strArtistSort is NULL";
  else
    strSQL = "UPDATE album SET strArtistSort = "
      "(SELECT GROUP_CONCAT(val, '; ') "
      "FROM(SELECT album_artist.idAlbum, "
      "CASE WHEN artist.strSortName IS NULL THEN artist.strArtist "
      "ELSE artist.strSortName END as val "
      "FROM album_artist JOIN artist on artist.idArtist = album_artist.idArtist "
      "WHERE album_artist.idAlbum = album.idAlbum "
      "ORDER BY album_artist.idAlbum, album_artist.iOrder) GROUP BY idAlbum) "
      "WHERE album.strArtistSort = '' OR album.strArtistSort is NULL";
  if (idArtist > 0)
    strSQL += PrepareSQL(" AND EXISTS (SELECT 1 FROM album_artist WHERE album_artist.idArtist = %ld "
      "AND album_artist.idAlbum = album.idAlbum)", idArtist);
  ExecuteQuery(strSQL);
  CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());

  
  if (bisMySQL)
    strSQL = "UPDATE song SET strArtistSort = "
      "(SELECT GROUP_CONCAT("
      "CASE WHEN artist.strSortName IS NULL THEN artist.strArtist "
      "ELSE artist.strSortName END "
      "ORDER BY song_artist.idSong, song_artist.iOrder "
      "SEPARATOR '; ') as val "
      "FROM song_artist JOIN artist on artist.idArtist = song_artist.idArtist "
      "WHERE song_artist.idSong = song.idSong AND song_artist.idRole = 1 GROUP BY idSong) "
      "WHERE song.strArtistSort = ''  OR song.strArtistSort is NULL";
  else
    strSQL = "UPDATE song SET strArtistSort = "
      "(SELECT GROUP_CONCAT(val, '; ') "
      "FROM(SELECT song_artist.idSong, "
      "CASE WHEN artist.strSortName IS NULL THEN artist.strArtist "
      "ELSE artist.strSortName END as val "
      "FROM song_artist JOIN artist on artist.idArtist = song_artist.idArtist "
      "WHERE song_artist.idSong = song.idSong AND song_artist.idRole = 1 "
      "ORDER BY song_artist.idSong, song_artist.iOrder) GROUP BY idSong) "
      "WHERE song.strArtistSort = ''  OR song.strArtistSort is NULL ";
  if (idArtist > 0)
    strSQL += PrepareSQL(" AND EXISTS (SELECT 1 FROM song_artist WHERE song_artist.idArtist = %ld "
      "AND song_artist.idSong = song.idSong AND song_artist.idRole = 1)", idArtist);
  ExecuteQuery(strSQL);
  CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());

  //Restore nulls where strArtistSort = strArtistDisp
  strSQL = "UPDATE album SET strArtistSort = Null WHERE strArtistSort = strArtistDisp";
  if (idArtist > 0)
    strSQL += PrepareSQL(" AND EXISTS (SELECT 1 FROM album_artist WHERE album_artist.idArtist = %ld "
      "AND album_artist.idAlbum = album.idAlbum)", idArtist);
  ExecuteQuery(strSQL);
  CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());
  strSQL = "UPDATE song SET strArtistSort = Null WHERE strArtistSort = strArtistDisp";
  if (idArtist > 0)
    strSQL += PrepareSQL(" AND EXISTS (SELECT 1 FROM song_artist WHERE song_artist.idArtist = %ld "
        "AND song_artist.idSong = song.idSong AND song_artist.idRole = 1)", idArtist);
  ExecuteQuery(strSQL);
  CLog::Log(LOGDEBUG, "%s query: %s", __FUNCTION__, strSQL.c_str());
 
  if (CommitMultipleExecute())
    return true;
  else
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__); 
  return false;
}

std::string CMusicDatabase::GetAlbumById(int id)
{
  return GetSingleValue("album", "strAlbum", PrepareSQL("idAlbum=%i", id));
}

int CMusicDatabase::GetGenreByName(const std::string& strGenre)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string strSQL;
    strSQL=PrepareSQL("select idGenre from genre where genre.strGenre like '%s'", strGenre.c_str());
    // run query
    if (!m_pDS->query(strSQL)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound != 1)
    {
      m_pDS->close();
      return -1;
    }
    return m_pDS->fv("genre.idGenre").get_asInt();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return -1;
}

bool CMusicDatabase::GetRandomSong(CFileItem* item, int& idSong, const Filter &filter)
{
  try
  {
    idSong = -1;

    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    // Get a random song that matches filter criteria (which may exclude some songs)
    // We don't use PrepareSQL here, as the WHERE clause is already formatted but must
    // use songview as that is what the WHERE clause has as reference table
    std::string strSQL = "SELECT idSong FROM songview ";
    Filter extFilter = filter;
    extFilter.AppendOrder(PrepareSQL("RANDOM()"));
    extFilter.limit = "1";
    if (!CDatabase::BuildSQL(strSQL, extFilter, strSQL))
      return false;
    if (!m_pDS->query(strSQL))
      return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound != 1)
    {
      m_pDS->close();
      return false;
    }
    idSong = m_pDS->fv("songview.idSong").get_asInt();
    m_pDS->close();

    // Fetch the full song details, including contributors
    std::string baseDir = StringUtils::Format("musicdb://songs/?songid=%d", idSong);
    CFileItemList items;
    GetSongsFullByWhere(baseDir, Filter(), items, SortDescription(), true);
    if (items.Size() > 0)
    {
      *item = *items[0];
      return true;
    }
    return false;
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "%s(%s) failed", __FUNCTION__, filter.where.c_str());
  }
  return false;
}

bool CMusicDatabase::GetCompilationAlbums(const std::string& strBaseDir, CFileItemList& items)
{
  CMusicDbUrl musicUrl;
  if (!musicUrl.FromString(strBaseDir))
    return false;

  musicUrl.AddOption("compilation", true);
  
  Filter filter;
  return GetAlbumsByWhere(musicUrl.ToString(), filter, items);
}

bool CMusicDatabase::GetCompilationSongs(const std::string& strBaseDir, CFileItemList& items)
{
  CMusicDbUrl musicUrl;
  if (!musicUrl.FromString(strBaseDir))
    return false;

  musicUrl.AddOption("compilation", true);

  Filter filter;
  return GetSongsFullByWhere(musicUrl.ToString(), filter, items, SortDescription(), true);
}

int CMusicDatabase::GetCompilationAlbumsCount()
{
  return strtol(GetSingleValue("album", "count(idAlbum)", "bCompilation = 1").c_str(), NULL, 10);
}

int CMusicDatabase::GetSinglesCount()
{
  CDatabase::Filter filter(PrepareSQL("songview.idAlbum IN (SELECT idAlbum FROM album WHERE strReleaseType = '%s')", CAlbum::ReleaseTypeToString(CAlbum::Single).c_str()));
  return GetSongsCount(filter);
}

int CMusicDatabase::GetArtistCountForRole(int role)
{
  std::string strSQL = PrepareSQL("SELECT COUNT(DISTINCT idartist) FROM song_artist WHERE song_artist.idRole = %i", role);
  return strtol(GetSingleValue(strSQL).c_str(), NULL, 10);
}

int CMusicDatabase::GetArtistCountForRole(const std::string& strRole)
{
  std::string strSQL = PrepareSQL("SELECT COUNT(DISTINCT idartist) FROM song_artist JOIN role ON song_artist.idRole = role.idRole WHERE role.strRole LIKE '%s'", strRole.c_str());
  return strtol(GetSingleValue(strSQL).c_str(), NULL, 10);
}

bool CMusicDatabase::SetPathHash(const std::string &path, const std::string &hash)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    if (hash.empty())
    { // this is an empty folder - we need only add it to the path table
      // if the path actually exists
      if (!CDirectory::Exists(path))
        return false;
    }
    int idPath = AddPath(path);
    if (idPath < 0) return false;

    std::string strSQL=PrepareSQL("update path set strHash='%s' where idPath=%ld", hash.c_str(), idPath);
    m_pDS->exec(strSQL);

    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s (%s, %s) failed", __FUNCTION__, path.c_str(), hash.c_str());
  }

  return false;
}

bool CMusicDatabase::GetPathHash(const std::string &path, std::string &hash)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string strSQL=PrepareSQL("select strHash from path where strPath='%s'", path.c_str());
    m_pDS->query(strSQL);
    if (m_pDS->num_rows() == 0)
      return false;
    hash = m_pDS->fv("strHash").get_asString();
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s (%s) failed", __FUNCTION__, path.c_str());
  }

  return false;
}

bool CMusicDatabase::RemoveSongsFromPath(const std::string &path1, MAPSONGS& songs, bool exact)
{
  // We need to remove all songs from this path, as their tags are going
  // to be re-read.  We need to remove all songs from the song table + all links to them
  // from the song link tables (as otherwise if a song is added back
  // to the table with the same idSong, these tables can't be cleaned up properly later)

  //! @todo SQLite probably doesn't allow this, but can we rely on that??

  // We don't need to remove orphaned albums at this point as in AddAlbum() we check
  // first whether the album has already been read during this scan, and if it hasn't
  // we check whether it's in the table and update accordingly at that point, removing the entries from
  // the album link tables.  The only failure point for this is albums
  // that span multiple folders, where just the files in one folder have been changed.  In this case
  // any linked fields that are only in the files that haven't changed will be removed.  Clearly
  // the primary albumartist still matches (as that's what we looked up based on) so is this really
  // an issue?  I don't think it is, as those artists will still have links to the album via the songs
  // which is generally what we rely on, so the only failure point is albumartist lookup.  In this
  // case, it will return only things in the album_artist table from the newly updated songs (and
  // only if they have additional artists).  I think the effect of this is minimal at best, as ALL
  // songs in the album should have the same albumartist!

  // we also remove the path at this point as it will be added later on if the
  // path still exists.
  // After scanning we then remove the orphaned artists, genres and thumbs.

  // Note: when used to remove all songs from a path and its subpath (exact=false), this
  // does miss archived songs.
  std::string path(path1);
  try
  {
    if (!URIUtils::HasSlashAtEnd(path))
      URIUtils::AddSlashAtEnd(path);

    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string where;
    if (exact)
      where = PrepareSQL(" where strPath='%s'", path.c_str());
    else
      where = PrepareSQL(" where SUBSTR(strPath,1,%i)='%s'", StringUtils::utf8_strlen(path.c_str()), path.c_str());
    std::string sql = "select * from songview" + where;
    if (!m_pDS->query(sql)) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound > 0)
    {
      std::vector<std::string> songIds;
      while (!m_pDS->eof())
      {
        CSong song = GetSongFromDataset();
        song.strThumb = GetArtForItem(song.idSong, MediaTypeSong, "thumb");
        songs.insert(std::make_pair(song.strFileName, song));
        songIds.push_back(PrepareSQL("%i", song.idSong));
        m_pDS->next();
      }
      m_pDS->close();

      //! @todo move this below the m_pDS->exec block, once UPnP doesn't rely on this anymore
      for (const auto &song : songs)
        AnnounceRemove(MediaTypeSong, song.second.idSong);

      // and delete all songs, and anything linked to them
      sql = "delete from song where idSong in (" + StringUtils::Join(songIds, ",") + ")";
      m_pDS->exec(sql);
    }
    // and remove the path as well (it'll be re-added later on with the new hash if it's non-empty)
    sql = "delete from path" + where;
    m_pDS->exec(sql);
    return iRowsFound > 0;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s (%s) failed", __FUNCTION__, path.c_str());
  }
  return false;
}

bool CMusicDatabase::GetPaths(std::set<std::string> &paths)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    paths.clear();

    // find all paths
    if (!m_pDS->query("select strPath from path")) return false;
    int iRowsFound = m_pDS->num_rows();
    if (iRowsFound == 0)
    {
      m_pDS->close();
      return true;
    }
    while (!m_pDS->eof())
    {
      paths.insert(m_pDS->fv("strPath").get_asString());
      m_pDS->next();
    }
    m_pDS->close();
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
  }
  return false;
}

bool CMusicDatabase::SetSongUserrating(const std::string &filePath, int userrating)
{
  try
  {
    if (filePath.empty()) return false;
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    int songID = GetSongIDFromPath(filePath);
    if (-1 == songID) return false;

    std::string sql = PrepareSQL("UPDATE song SET userrating='%i' WHERE idSong = %i", userrating, songID);
    m_pDS->exec(sql);
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s (%s,%i) failed", __FUNCTION__, filePath.c_str(), userrating);
  }
  return false;
}

bool CMusicDatabase::SetAlbumUserrating(const int idAlbum, int userrating)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    if (-1 == idAlbum) return false;

    std::string sql = PrepareSQL("UPDATE album SET iUserrating='%i' WHERE idAlbum = %i", userrating, idAlbum);
    m_pDS->exec(sql);
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s (%i,%i) failed", __FUNCTION__, idAlbum, userrating);
  }
  return false;
}

bool CMusicDatabase::SetSongVotes(const std::string &filePath, int votes)
{
  try
  {
    if (filePath.empty()) return false;
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    int songID = GetSongIDFromPath(filePath);
    if (-1 == songID) return false;

    std::string sql = PrepareSQL("UPDATE song SET votes='%i' WHERE idSong = %i", votes, songID);
    m_pDS->exec(sql);
    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s (%s,%i) failed", __FUNCTION__, filePath.c_str(), votes);
  }
  return false;
}

int CMusicDatabase::GetSongIDFromPath(const std::string &filePath)
{
  // grab the where string to identify the song id
  CURL url(filePath);
  if (url.IsProtocol("musicdb"))
  {
    std::string strFile=URIUtils::GetFileName(filePath);
    URIUtils::RemoveExtension(strFile);
    return atol(strFile.c_str());
  }
  // hit the db
  try
  {
    if (NULL == m_pDB.get()) return -1;
    if (NULL == m_pDS.get()) return -1;

    std::string strPath, strFileName;
    URIUtils::Split(filePath, strPath, strFileName);
    URIUtils::AddSlashAtEnd(strPath);

    std::string sql = PrepareSQL("select idSong from song join path on song.idPath = path.idPath where song.strFileName='%s' and path.strPath='%s'", strFileName.c_str(), strPath.c_str());
    if (!m_pDS->query(sql)) return -1;

    if (m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return -1;
    }

    int songID = m_pDS->fv("idSong").get_asInt();
    m_pDS->close();
    return songID;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s (%s) failed", __FUNCTION__, filePath.c_str());
  }
  return -1;
}

bool CMusicDatabase::CommitTransaction()
{
  if (CDatabase::CommitTransaction())
  { // number of items in the db has likely changed, so reset the infomanager cache
    g_infoManager.SetLibraryBool(LIBRARY_HAS_MUSIC, GetSongsCount() > 0);
    return true;
  }
  return false;
}

bool CMusicDatabase::SetScraperAll(const std::string & strBaseDir, const ADDON::ScraperPtr scraper)
{
  if (NULL == m_pDB.get()) return false;
  if (NULL == m_pDS.get()) return false;
  std::string strSQL;
  int idSetting = -1;
  try
  {
    CONTENT_TYPE content = CONTENT_NONE;

    // Build where clause from virtual path
    Filter extFilter;
    CMusicDbUrl musicUrl;
    SortDescription sorting;
    if (!musicUrl.FromString(strBaseDir) || !GetFilter(musicUrl, extFilter, sorting))
      return false;

    std::string itemType = musicUrl.GetType();
    if (StringUtils::EqualsNoCase(itemType, "artists"))
    {
      content = CONTENT_ARTISTS;
    }
    else if (StringUtils::EqualsNoCase(itemType, "albums"))
    {
      content = CONTENT_ALBUMS;
    }
    else
      return false;  //Only artists and albums have info settings
    
    std::string strSQLWhere;
    if (!BuildSQL(strSQLWhere, extFilter, strSQLWhere))
      return false;

    // Replace view names with table names
    StringUtils::Replace(strSQLWhere, "artistview", "artist");
    StringUtils::Replace(strSQLWhere, "albumview", "album");

    BeginTransaction();
    // Clear current scraper settings (0 => default scraper used)
    if (content == CONTENT_ARTISTS)
      strSQL = "UPDATE artist SET idInfoSetting = %i ";
    else
      strSQL = "UPDATE album SET idInfoSetting = %i ";
    strSQL = PrepareSQL(strSQL, 0) + strSQLWhere;
    m_pDS->exec(strSQL);

    //Remove orphaned settings
    CleanupInfoSettings();

    if (scraper)
    {
      // Add new info setting
      strSQL = "INSERT INTO infosetting (strScraperPath, strSettings) values ('%s','%s')";
      strSQL = PrepareSQL(strSQL, scraper->ID().c_str(), scraper->GetPathSettings().c_str());
      m_pDS->exec(strSQL);
      idSetting = static_cast<int>(m_pDS->lastinsertid());

      if (content == CONTENT_ARTISTS)
        strSQL = "UPDATE artist SET idInfoSetting = %i ";
      else
        strSQL = "UPDATE album SET idInfoSetting = %i ";
      strSQL = PrepareSQL(strSQL, idSetting) + strSQLWhere;
      m_pDS->exec(strSQL);
    }
    CommitTransaction();
    return true;
  }
  catch (...)
  {
    RollbackTransaction();
    CLog::Log(LOGERROR, "%s - (%s, %s) failed", __FUNCTION__, strBaseDir.c_str(), strSQL.c_str());
  }
  return false;
}

bool CMusicDatabase::SetScraper(int id, const CONTENT_TYPE &content, const ADDON::ScraperPtr scraper)
{
  if (NULL == m_pDB.get()) return false;
  if (NULL == m_pDS.get()) return false;
  std::string strSQL;
  int idSetting = -1;
  try
  { 
    BeginTransaction();
    // Fetch current info settings for item, 0 => default is used
    if (content == CONTENT_ARTISTS)
      strSQL = "SELECT idInfoSetting FROM artist WHERE idArtist = %i";
    else 
      strSQL = "SELECT idInfoSetting FROM album WHERE idAlbum = %i";
    strSQL = PrepareSQL(strSQL, id);
    m_pDS->query(strSQL);
    if (m_pDS->num_rows() > 0)
      idSetting = m_pDS->fv("idInfoSetting").get_asInt();
    m_pDS->close();

    if (idSetting < 1)
    { // Add new info setting
      strSQL = "INSERT INTO infosetting (strScraperPath, strSettings) values ('%s','%s')";
      strSQL = PrepareSQL(strSQL, scraper->ID().c_str(), scraper->GetPathSettings().c_str());
      m_pDS->exec(strSQL);
      idSetting = static_cast<int>(m_pDS->lastinsertid());

      if (content == CONTENT_ARTISTS)
        strSQL = "UPDATE artist SET idInfoSetting = %i WHERE idArtist = %i";
      else
        strSQL = "UPDATE album SET idInfoSetting = %i WHERE idAlbum = %i";
      strSQL = PrepareSQL(strSQL, idSetting, id);
      m_pDS->exec(strSQL);
    }
    else
    {  // Update info setting
      strSQL = "UPDATE infosetting SET strScraperPath = '%s', strSettings = '%s' WHERE idSetting = %i";
      strSQL = PrepareSQL(strSQL, scraper->ID().c_str(), scraper->GetPathSettings().c_str(), idSetting);
      m_pDS->exec(strSQL);
    }
    CommitTransaction();
    return true;
  }
  catch (...)
  {
    RollbackTransaction();
    CLog::Log(LOGERROR, "%s - (%i, %s) failed", __FUNCTION__, id, strSQL.c_str());
  }
  return false;
}

bool CMusicDatabase::GetScraper(int id, const CONTENT_TYPE &content, ADDON::ScraperPtr& scraper)
{
  std::string scraperUUID;
  std::string strSettings;
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string strSQL;
    strSQL = "SELECT strScraperPath, strSettings FROM infosetting JOIN ";
    if (content == CONTENT_ARTISTS)
      strSQL = strSQL + "artist ON artist.idInfoSetting = infosetting.idSetting WHERE artist.idArtist = %i";
    else
      strSQL = strSQL + "album ON album.idInfoSetting = infosetting.idSetting WHERE album.idAlbum = %i";
    strSQL = PrepareSQL(strSQL, id);
    m_pDS->query(strSQL);
    if (!m_pDS->eof())
    { // try and ascertain scraper
      scraperUUID = m_pDS->fv("strScraperPath").get_asString();
      strSettings = m_pDS->fv("strSettings").get_asString();

      // Use pre configured or default scraper
      ADDON::AddonPtr addon;
      if (!scraperUUID.empty() && ADDON::CAddonMgr::GetInstance().GetAddon(scraperUUID, addon) && addon)
      {
        scraper = std::dynamic_pointer_cast<ADDON::CScraper>(addon);
        if (scraper)
          // Set settings 
          scraper->SetPathSettings(content, strSettings);
      }
    }
    m_pDS->close();

    if (!scraper)
    { // use default music scraper instead
      ADDON::AddonPtr addon;
      if(ADDON::CAddonSystemSettings::GetInstance().GetActive(ADDON::ScraperTypeFromContent(content), addon))
      {
        scraper = std::dynamic_pointer_cast<ADDON::CScraper>(addon);
        return scraper != NULL;
      }
      else
        return false;
    }

    return true;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s -(%i, %s %s) failed", __FUNCTION__, id, scraperUUID.c_str(), strSettings.c_str());
  }
  return false;
}

bool CMusicDatabase::ScraperInUse(const std::string &scraperID) const
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;

    std::string sql = PrepareSQL("SELECT COUNT(1) FROM infosetting WHERE strScraperPath='%s'",scraperID.c_str());
    if (!m_pDS->query(sql) || m_pDS->num_rows() == 0)
    {
      m_pDS->close();
      return false;
    }
    bool found = m_pDS->fv(0).get_asInt() > 0;
    m_pDS->close();
    return found;
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%s) failed", __FUNCTION__, scraperID.c_str());
  }
  return false;
}

bool CMusicDatabase::GetItems(const std::string &strBaseDir, CFileItemList &items, const Filter &filter /* = Filter() */, const SortDescription &sortDescription /* = SortDescription() */)
{
  CMusicDbUrl musicUrl;
  if (!musicUrl.FromString(strBaseDir))
    return false;

  return GetItems(strBaseDir, musicUrl.GetType(), items, filter, sortDescription);
}

bool CMusicDatabase::GetItems(const std::string &strBaseDir, const std::string &itemType, CFileItemList &items, const Filter &filter /* = Filter() */, const SortDescription &sortDescription /* = SortDescription() */)
{
  if (StringUtils::EqualsNoCase(itemType, "genres"))
    return GetGenresNav(strBaseDir, items, filter);
  else if (StringUtils::EqualsNoCase(itemType, "years"))
    return GetYearsNav(strBaseDir, items, filter);
  else if (StringUtils::EqualsNoCase(itemType, "roles"))
    return GetRolesNav(strBaseDir, items, filter);
  else if (StringUtils::EqualsNoCase(itemType, "artists"))
    return GetArtistsNav(strBaseDir, items, !CServiceBroker::GetSettings().GetBool(CSettings::SETTING_MUSICLIBRARY_SHOWCOMPILATIONARTISTS), -1, -1, -1, filter, sortDescription);
  else if (StringUtils::EqualsNoCase(itemType, "albums"))
    return GetAlbumsByWhere(strBaseDir, filter, items, sortDescription);
  else if (StringUtils::EqualsNoCase(itemType, "songs"))
    return GetSongsFullByWhere(strBaseDir, filter, items, sortDescription, true);

  return false;
}

std::string CMusicDatabase::GetItemById(const std::string &itemType, int id)
{
  if (StringUtils::EqualsNoCase(itemType, "genres"))
    return GetGenreById(id);
  else if (StringUtils::EqualsNoCase(itemType, "years"))
    return StringUtils::Format("%d", id);
  else if (StringUtils::EqualsNoCase(itemType, "artists"))
    return GetArtistById(id);
  else if (StringUtils::EqualsNoCase(itemType, "albums"))
    return GetAlbumById(id);
  else if (StringUtils::EqualsNoCase(itemType, "roles"))
    return GetRoleById(id);

  return "";
}

void CMusicDatabase::ExportToXML(const std::string &xmlFile, bool singleFile, bool images, bool overwrite)
{
  int iFailCount = 0;
  CGUIDialogProgress *progress=NULL;
  try
  {
    if (NULL == m_pDB.get()) return;
    if (NULL == m_pDS.get()) return;
    if (NULL == m_pDS2.get()) return;

    // find all albums
    std::vector<int> albumIds;
    std::string sql = "select idAlbum FROM album WHERE lastScraped IS NOT NULL";
    m_pDS->query(sql);

    int total = m_pDS->num_rows();
    int current = 0;

    albumIds.reserve(total);
    while (!m_pDS->eof())
    {
      albumIds.push_back(m_pDS->fv("idAlbum").get_asInt());
      m_pDS->next();
    }
    m_pDS->close();

    progress = g_windowManager.GetWindow<CGUIDialogProgress>(WINDOW_DIALOG_PROGRESS);
    if (progress)
    {
      progress->SetHeading(CVariant{20196});
      progress->SetLine(0, CVariant{650});
      progress->SetLine(1, CVariant{""});
      progress->SetLine(2, CVariant{""});
      progress->SetPercentage(0);
      progress->Open();
      progress->ShowProgressBar(true);
    }

    // create our xml document
    CXBMCTinyXML xmlDoc;
    TiXmlDeclaration decl("1.0", "UTF-8", "yes");
    xmlDoc.InsertEndChild(decl);
    TiXmlNode *pMain = NULL;
    if (!singleFile)
      pMain = &xmlDoc;
    else
    {
      TiXmlElement xmlMainElement("musicdb");
      pMain = xmlDoc.InsertEndChild(xmlMainElement);
    }
    for (const auto &albumId : albumIds)
    {
      CAlbum album;
      GetAlbum(albumId, album);
      std::string strPath;
      GetAlbumPath(albumId, strPath);
      album.Save(pMain, "album", strPath);
      if (!singleFile)
      {
        if (!CDirectory::Exists(strPath))
          CLog::Log(LOGDEBUG, "%s - Not exporting item %s as it does not exist", __FUNCTION__, strPath.c_str());
        else
        {
          std::string nfoFile = URIUtils::AddFileToFolder(strPath, "album.nfo");
          if (overwrite || !CFile::Exists(nfoFile))
          {
            if (!xmlDoc.SaveFile(nfoFile))
            {
              CLog::Log(LOGERROR, "%s: Album nfo export failed! ('%s')", __FUNCTION__, nfoFile.c_str());
              CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, g_localizeStrings.Get(20302), nfoFile);
              iFailCount++;
            }
          }

          if (images)
          {
            std::string thumb = GetArtForItem(album.idAlbum, MediaTypeAlbum, "thumb");
            std::string imagePath = URIUtils::AddFileToFolder(strPath, "folder.jpg");
            if (!thumb.empty() && (overwrite || !CFile::Exists(imagePath)))
              CTextureCache::GetInstance().Export(thumb, imagePath);
          }
          xmlDoc.Clear();
          TiXmlDeclaration decl("1.0", "UTF-8", "yes");
          xmlDoc.InsertEndChild(decl);
        }
      }

      if ((current % 50) == 0 && progress)
      {
        progress->SetLine(1, CVariant{album.strAlbum});
        progress->SetPercentage(current * 100 / total);
        progress->Progress();
        if (progress->IsCanceled())
        {
          progress->Close();
          m_pDS->close();
          return;
        }
      }
      current++;
    }

    // find all artists
    std::vector<int> artistIds;
    std::string artistSQL = "SELECT idArtist FROM artist where lastScraped IS NOT NULL";
    m_pDS->query(artistSQL);
    total = m_pDS->num_rows();
    current = 0;
    artistIds.reserve(total);
    while (!m_pDS->eof())
    {
      artistIds.push_back(m_pDS->fv("idArtist").get_asInt());
      m_pDS->next();
    }
    m_pDS->close();

    for (const auto &artistId : artistIds)
    {
      CArtist artist;
      GetArtist(artistId, artist);
      std::string strPath;
      GetArtistPath(artist.idArtist,strPath);
      artist.Save(pMain, "artist", strPath);

      std::map<std::string, std::string> artwork;
      if (GetArtForItem(artist.idArtist, MediaTypeArtist, artwork) && singleFile)
      { // append to the XML
        TiXmlElement additionalNode("art");
        for (const auto &i : artwork)
          XMLUtils::SetString(&additionalNode, i.first.c_str(), i.second);
        pMain->LastChild()->InsertEndChild(additionalNode);
      }
      if (!singleFile)
      {
        if (!CDirectory::Exists(strPath))
          CLog::Log(LOGDEBUG, "%s - Not exporting item %s as it does not exist", __FUNCTION__, strPath.c_str());
        else
        {
          std::string nfoFile = URIUtils::AddFileToFolder(strPath, "artist.nfo");
          if (overwrite || !CFile::Exists(nfoFile))
          {
            if (!xmlDoc.SaveFile(nfoFile))
            {
              CLog::Log(LOGERROR, "%s: Artist nfo export failed! ('%s')", __FUNCTION__, nfoFile.c_str());
              CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, g_localizeStrings.Get(20302), nfoFile);
              iFailCount++;
            }
          }

          if (images && !artwork.empty())
          {
            std::string savedThumb = URIUtils::AddFileToFolder(strPath,"folder.jpg");
            std::string savedFanart = URIUtils::AddFileToFolder(strPath,"fanart.jpg");
            if (artwork.find("thumb") != artwork.end() && (overwrite || !CFile::Exists(savedThumb)))
              CTextureCache::GetInstance().Export(artwork["thumb"], savedThumb);
            if (artwork.find("fanart") != artwork.end() && (overwrite || !CFile::Exists(savedFanart)))
              CTextureCache::GetInstance().Export(artwork["fanart"], savedFanart);
          }
          xmlDoc.Clear();
          TiXmlDeclaration decl("1.0", "UTF-8", "yes");
          xmlDoc.InsertEndChild(decl);
        }
      }

      if ((current % 50) == 0 && progress)
      {
        progress->SetLine(1, CVariant{artist.strArtist});
        progress->SetPercentage(current * 100 / total);
        progress->Progress();
        if (progress->IsCanceled())
        {
          progress->Close();
          m_pDS->close();
          return;
        }
      }
      current++;
    }

    xmlDoc.SaveFile(xmlFile);

    CVariant data;
    if (singleFile)
    {
      data["file"] = xmlFile;
      if (iFailCount > 0)
        data["failcount"] = iFailCount;
    }
    ANNOUNCEMENT::CAnnouncementManager::GetInstance().Announce(ANNOUNCEMENT::AudioLibrary, "xbmc", "OnExport", data);
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
    iFailCount++;
  }

  if (progress)
    progress->Close();

  if (iFailCount > 0)
    CGUIDialogOK::ShowAndGetInput(CVariant{20196}, CVariant{StringUtils::Format(g_localizeStrings.Get(15011).c_str(), iFailCount)});
}

void CMusicDatabase::ImportFromXML(const std::string &xmlFile)
{
  CGUIDialogProgress *progress = g_windowManager.GetWindow<CGUIDialogProgress>(WINDOW_DIALOG_PROGRESS);
  try
  {
    if (NULL == m_pDB.get()) return;
    if (NULL == m_pDS.get()) return;

    CXBMCTinyXML xmlDoc;
    if (!xmlDoc.LoadFile(xmlFile))
      return;

    TiXmlElement *root = xmlDoc.RootElement();
    if (!root) return;

    if (progress)
    {
      progress->SetHeading(CVariant{20197});
      progress->SetLine(0, CVariant{649});
      progress->SetLine(1, CVariant{330});
      progress->SetLine(2, CVariant{""});
      progress->SetPercentage(0);
      progress->Open();
      progress->ShowProgressBar(true);
    }

    TiXmlElement *entry = root->FirstChildElement();
    int current = 0;
    int total = 0;
    // first count the number of items...
    while (entry)
    {
      if (strnicmp(entry->Value(), "artist", 6)==0 ||
          strnicmp(entry->Value(), "album", 5)==0)
        total++;
      entry = entry->NextSiblingElement();
    }

    BeginTransaction();
    entry = root->FirstChildElement();
    while (entry)
    {
      std::string strTitle;
      if (strnicmp(entry->Value(), "artist", 6) == 0)
      {
        CArtist importedArtist;
        importedArtist.Load(entry);
        strTitle = importedArtist.strArtist;
        int idArtist = GetArtistByName(importedArtist.strArtist);
        if (idArtist > -1)
        {
          CArtist artist;
          GetArtist(idArtist, artist);
          artist.MergeScrapedArtist(importedArtist, true);
          UpdateArtist(artist);
        }

        current++;
      }
      else if (strnicmp(entry->Value(), "album", 5) == 0)
      {
        CAlbum importedAlbum;
        importedAlbum.Load(entry);
        strTitle = importedAlbum.strAlbum;
        int idAlbum = GetAlbumByName(importedAlbum.strAlbum, importedAlbum.GetAlbumArtistString());
        if (idAlbum > -1)
        {
          CAlbum album;
          GetAlbum(idAlbum, album, true);
          album.MergeScrapedAlbum(importedAlbum, true);
          UpdateAlbum(album); //Will replace song artists if present in xml
        }

        current++;
      }
      entry = entry ->NextSiblingElement();
      if (progress && total)
      {
        progress->SetPercentage(current * 100 / total);
        progress->SetLine(2, CVariant{std::move(strTitle)});
        progress->Progress();
        if (progress->IsCanceled())
        {
          progress->Close();
          RollbackTransaction();
          return;
        }
      }
    }
    CommitTransaction();

    g_infoManager.ResetLibraryBools();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s failed", __FUNCTION__);
    RollbackTransaction();
  }
  if (progress)
    progress->Close();
}

void CMusicDatabase::SetPropertiesFromArtist(CFileItem& item, const CArtist& artist)
{
  item.SetProperty("artist_instrument", StringUtils::Join(artist.instruments, g_advancedSettings.m_musicItemSeparator));
  item.SetProperty("artist_instrument_array", artist.instruments);
  item.SetProperty("artist_style", StringUtils::Join(artist.styles, g_advancedSettings.m_musicItemSeparator));
  item.SetProperty("artist_style_array", artist.styles);
  item.SetProperty("artist_mood", StringUtils::Join(artist.moods, g_advancedSettings.m_musicItemSeparator));
  item.SetProperty("artist_mood_array", artist.moods);
  item.SetProperty("artist_born", artist.strBorn);
  item.SetProperty("artist_formed", artist.strFormed);
  item.SetProperty("artist_description", artist.strBiography);
  item.SetProperty("artist_genre", StringUtils::Join(artist.genre, g_advancedSettings.m_musicItemSeparator));
  item.SetProperty("artist_genre_array", artist.genre);
  item.SetProperty("artist_died", artist.strDied);
  item.SetProperty("artist_disbanded", artist.strDisbanded);
  item.SetProperty("artist_yearsactive", StringUtils::Join(artist.yearsActive, g_advancedSettings.m_musicItemSeparator));
  item.SetProperty("artist_yearsactive_array", artist.yearsActive);
}

void CMusicDatabase::SetPropertiesFromAlbum(CFileItem& item, const CAlbum& album)
{
  item.SetProperty("album_description", album.strReview);
  item.SetProperty("album_theme", StringUtils::Join(album.themes, g_advancedSettings.m_musicItemSeparator));
  item.SetProperty("album_theme_array", album.themes);
  item.SetProperty("album_mood", StringUtils::Join(album.moods, g_advancedSettings.m_musicItemSeparator));
  item.SetProperty("album_mood_array", album.moods);
  item.SetProperty("album_style", StringUtils::Join(album.styles, g_advancedSettings.m_musicItemSeparator));
  item.SetProperty("album_style_array", album.styles);
  item.SetProperty("album_type", album.strType);
  item.SetProperty("album_label", album.strLabel);
  item.SetProperty("album_artist", album.GetAlbumArtistString());
  item.SetProperty("album_artist_array", album.GetAlbumArtist());
  item.SetProperty("album_genre", StringUtils::Join(album.genre, g_advancedSettings.m_musicItemSeparator));
  item.SetProperty("album_genre_array", album.genre);
  item.SetProperty("album_title", album.strAlbum);
  if (album.fRating > 0)
    item.SetProperty("album_rating", album.fRating);
  if (album.iUserrating > 0)
    item.SetProperty("album_userrating", album.iUserrating);
  if (album.iVotes > 0)
    item.SetProperty("album_votes", album.iVotes);
  item.SetProperty("album_releasetype", CAlbum::ReleaseTypeToString(album.releaseType));
}

void CMusicDatabase::SetPropertiesForFileItem(CFileItem& item)
{
  if (!item.HasMusicInfoTag())
    return;
  int idArtist = GetArtistByName(item.GetMusicInfoTag()->GetArtistString());
  if (idArtist > -1)
  {
    CArtist artist;
    if (GetArtist(idArtist, artist))
      SetPropertiesFromArtist(item,artist);
  }
  int idAlbum = item.GetMusicInfoTag()->GetAlbumId();
  if (idAlbum <= 0)
    idAlbum = GetAlbumByName(item.GetMusicInfoTag()->GetAlbum(),
                             item.GetMusicInfoTag()->GetArtistString());
  if (idAlbum > -1)
  {
    CAlbum album;
    if (GetAlbum(idAlbum, album, false))
      SetPropertiesFromAlbum(item,album);
  }
}

void CMusicDatabase::SetArtForItem(int mediaId, const std::string &mediaType, const std::map<std::string, std::string> &art)
{
  for (const auto &i : art)
    SetArtForItem(mediaId, mediaType, i.first, i.second);
}

void CMusicDatabase::SetArtForItem(int mediaId, const std::string &mediaType, const std::string &artType, const std::string &url)
{
  try
  {
    if (NULL == m_pDB.get()) return;
    if (NULL == m_pDS.get()) return;

    // don't set <foo>.<bar> art types - these are derivative types from parent items
    if (artType.find('.') != std::string::npos)
      return;

    std::string sql = PrepareSQL("SELECT art_id FROM art WHERE media_id=%i AND media_type='%s' AND type='%s'", mediaId, mediaType.c_str(), artType.c_str());
    m_pDS->query(sql);
    if (!m_pDS->eof())
    { // update
      int artId = m_pDS->fv(0).get_asInt();
      m_pDS->close();
      sql = PrepareSQL("UPDATE art SET url='%s' where art_id=%d", url.c_str(), artId);
      m_pDS->exec(sql);
    }
    else
    { // insert
      m_pDS->close();
      sql = PrepareSQL("INSERT INTO art(media_id, media_type, type, url) VALUES (%d, '%s', '%s', '%s')", mediaId, mediaType.c_str(), artType.c_str(), url.c_str());
      m_pDS->exec(sql);
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%d, '%s', '%s', '%s') failed", __FUNCTION__, mediaId, mediaType.c_str(), artType.c_str(), url.c_str());
  }
}

bool CMusicDatabase::GetArtForItem(int mediaId, const std::string &mediaType, std::map<std::string, std::string> &art)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS2.get()) return false; // using dataset 2 as we're likely called in loops on dataset 1

    std::string sql = PrepareSQL("SELECT type,url FROM art WHERE media_id=%i AND media_type='%s'", mediaId, mediaType.c_str());
    m_pDS2->query(sql);
    while (!m_pDS2->eof())
    {
      art.insert(std::make_pair(m_pDS2->fv(0).get_asString(), m_pDS2->fv(1).get_asString()));
      m_pDS2->next();
    }
    m_pDS2->close();
    return !art.empty();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%d) failed", __FUNCTION__, mediaId);
  }
  return false;
}

std::string CMusicDatabase::GetArtForItem(int mediaId, const std::string &mediaType, const std::string &artType)
{
  std::string query = PrepareSQL("SELECT url FROM art WHERE media_id=%i AND media_type='%s' AND type='%s'", mediaId, mediaType.c_str(), artType.c_str());
  return GetSingleValue(query, m_pDS2);
}

bool CMusicDatabase::GetArtistArtForItem(int mediaId, const std::string &mediaType, std::map<std::string, std::string> &art)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS2.get()) return false; // using dataset 2 as we're likely called in loops on dataset 1

    std::string sql;
    if (mediaType == MediaTypeAlbum)
      sql = PrepareSQL("SELECT type, url FROM art WHERE media_id=(SELECT idArtist FROM album_artist "
                       "WHERE idAlbum=%i AND iOrder=0) AND media_type='artist'", 
                       mediaId);
    else
      //Select first "artist" only from song_artist, no other roles.
      sql = PrepareSQL("SELECT type, url FROM art WHERE media_id=(SELECT idArtist FROM song_artist "
                       "WHERE idSong=%i AND idRole=%i AND iOrder=0) AND media_type='artist'", 
                       mediaId, ROLE_ARTIST);
    m_pDS2->query(sql);
    while (!m_pDS2->eof())
    {
      art.insert(std::make_pair(m_pDS2->fv(0).get_asString(), m_pDS2->fv(1).get_asString()));
      m_pDS2->next();
    }
    m_pDS2->close();
    return !art.empty();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s(%d) failed", __FUNCTION__, mediaId);
  }
  return false;
}

std::string CMusicDatabase::GetArtistArtForItem(int mediaId, const std::string &mediaType, const std::string &artType)
{
  std::string query;
  if (mediaType == MediaTypeAlbum)
    query = PrepareSQL("SELECT url FROM art WHERE media_id=(SELECT idArtist FROM album_artist "
                       "WHERE idAlbum=%i AND iOrder=0) AND media_type='artist' AND type='%s'", 
                       mediaId, artType.c_str());
  else
    //Select first "artist" only from song_artist, no other roles.
    query = PrepareSQL("SELECT url FROM art WHERE media_id=(SELECT idArtist FROM song_artist "
                       "WHERE idSong=%i AND idRole=%i AND iOrder=0) AND media_type='artist' AND type='%s'", 
                       mediaId, ROLE_ARTIST, artType.c_str());
  return GetSingleValue(query, m_pDS2);
}

bool CMusicDatabase::GetFilter(CDbUrl &musicUrl, Filter &filter, SortDescription &sorting)
{
  if (!musicUrl.IsValid())
    return false;

  std::string type = musicUrl.GetType();
  const CUrlOptions::UrlOptions& options = musicUrl.GetOptions();

  // Check for playlist rules first, they may contain role criteria
  bool hasRoleRules = false;
  auto option = options.find("xsp");
  if (option != options.end())
  {
    CSmartPlaylist xsp;
    if (!xsp.LoadFromJson(option->second.asString()))
      return false;

    std::set<std::string> playlists;
    std::string xspWhere;
    xspWhere = xsp.GetWhereClause(*this, playlists);
    hasRoleRules = xsp.GetType() == "artists" && xspWhere.find("song_artist.idRole = role.idRole") != xspWhere.npos;

    // check if the filter playlist matches the item type
    if (xsp.GetType() == type ||
      (xsp.GetGroup() == type && !xsp.IsGroupMixed()))
    {
      filter.AppendWhere(xspWhere);

      if (xsp.GetLimit() > 0)
        sorting.limitEnd = xsp.GetLimit();
      if (xsp.GetOrder() != SortByNone)
        sorting.sortBy = xsp.GetOrder();
      sorting.sortOrder = xsp.GetOrderAscending() ? SortOrderAscending : SortOrderDescending;
      if (CServiceBroker::GetSettings().GetBool(CSettings::SETTING_FILELISTS_IGNORETHEWHENSORTING))
        sorting.sortAttributes = SortAttributeIgnoreArticle;
    }
  }

  //Process role options, common to artist and album type filtering
  int idRole = 1; // Default restrict song_artist to "artists" only, no other roles.
  option = options.find("roleid");
  if (option != options.end())
    idRole = static_cast<int>(option->second.asInteger());
  else
  {
    option = options.find("role");
    if (option != options.end())
    {
      if (option->second.asString() == "all" || option->second.asString() == "%")
        idRole = -1000; //All roles
      else
        idRole = GetRoleByName(option->second.asString());
    }
  }
  if (hasRoleRules)
  {
    // Get Role from role rule(s) here.
    // But that requires much change, so for now get all roles as better than none
    idRole = -1000; //All roles
  }
  
  std::string strRoleSQL; //Role < 0 means all roles, otherwise filter by role
  if(idRole > 0) strRoleSQL = PrepareSQL(" AND song_artist.idRole = %i ", idRole);

  int idArtist = -1, idGenre = -1, idAlbum = -1, idSong = -1;
  bool albumArtistsOnly = false;
  std::string artistname;

  // Process albumartistsonly option
  option = options.find("albumartistsonly");
  if (option != options.end())
    albumArtistsOnly = option->second.asBoolean();

  // Process genre option
  option = options.find("genreid");
  if (option != options.end())
    idGenre = static_cast<int>(option->second.asInteger());
  else
  {
    option = options.find("genre");
    if (option != options.end())
      idGenre = GetGenreByName(option->second.asString());
  }

  // Process album option
  option = options.find("albumid");
  if (option != options.end())
    idAlbum = static_cast<int>(option->second.asInteger());
  else
  {
    option = options.find("album");
    if (option != options.end())
      idAlbum = GetAlbumByName(option->second.asString());
  }

  // Process artist option
  option = options.find("artistid");
  if (option != options.end())
    idArtist = static_cast<int>(option->second.asInteger());
  else
  {
    option = options.find("artist");
    if (option != options.end())
    {
      idArtist = GetArtistByName(option->second.asString());
      if (idArtist == -1)
      {// not found with that name, or more than one found as artist name is not unique
        artistname = option->second.asString();
      }
    }
  }

  //  Process song option
  option = options.find("songid");
  if (option != options.end())
    idSong = static_cast<int>(option->second.asInteger());

  if (type == "artists")
  {
    if (!hasRoleRules)
    { // Not an "artists" smart playlist with roles rules, so get filter from options
      if (idArtist > 0)
        filter.AppendWhere(PrepareSQL("artistview.idArtist = %d", idArtist));
      else if (idAlbum > 0)
        filter.AppendWhere(PrepareSQL("artistview.idArtist IN (SELECT album_artist.idArtist FROM album_artist "
          "WHERE album_artist.idAlbum = %i)", idAlbum));
      else if (idSong > 0)
      {
        filter.AppendWhere(PrepareSQL("artistview.idArtist IN (SELECT song_artist.idArtist FROM song_artist "
          "WHERE song_artist.idSong = %i %s)", idSong, strRoleSQL.c_str()));
      }
      else
      { // Artists can be only album artists, so for all artists (with linked albums or songs) 
        // we need to check both album_artist and song_artist tables.
        // Role is determined from song_artist table, so even if looking for album artists only
        // we can check those that have a specific role e.g. which album artist is a composer
        // of songs in that album, from entries in the song_artist table.
        // Role < -1 is used to indicate that all roles are wanted.
        // When not album artists only and a specific role wanted then only the song_artist table is checked.
        // When album artists only and role = 1 (an "artist") then only the album_artist table is checked.
        std::string albumArtistSQL, songArtistSQL;
        ExistsSubQuery albumArtistSub("album_artist", "album_artist.idArtist = artistview.idArtist");
        ExistsSubQuery songArtistSub("song_artist", "song_artist.idArtist = artistview.idArtist");
        if (idRole > 0)
          songArtistSub.AppendWhere(PrepareSQL("song_artist.idRole = %i", idRole));
        if (idGenre > 0)
        {
          songArtistSub.AppendJoin("JOIN song_genre ON song_genre.idSong = song_artist.idSong");
          songArtistSub.AppendWhere(PrepareSQL("song_genre.idGenre = %i", idGenre));
        }
        if (idRole <= 1 && idGenre > 0)
        {// Check genre of songs of album using nested subquery
          std::string strGenre = PrepareSQL("EXISTS(SELECT 1 FROM song JOIN song_genre ON song_genre.idSong = song.idSong "
            "WHERE song.idAlbum = album_artist.idAlbum AND song_genre.idGenre = %i)", idGenre);
          albumArtistSub.AppendWhere(strGenre);
        }
        if (idRole > 1 && albumArtistsOnly)
        { // Album artists only with role, check AND in album_artist for album of song
          // using nested subquery correlated with album_artist
          songArtistSub.AppendJoin("JOIN song ON song.idSong = song_artist.idSong");
          songArtistSub.param = "song_artist.idArtist = album_artist.idArtist";
          songArtistSub.AppendWhere("song.idAlbum = album_artist.idAlbum");
          songArtistSub.BuildSQL(songArtistSQL);
          albumArtistSub.AppendWhere(songArtistSQL);
          albumArtistSub.BuildSQL(albumArtistSQL);
          filter.AppendWhere(albumArtistSQL);
        }
        else
        {
          songArtistSub.BuildSQL(songArtistSQL);
          albumArtistSub.BuildSQL(albumArtistSQL);
          if (idRole < 0 || (idRole == 1 && !albumArtistsOnly))
          { // Artist contributing to songs, any role, check OR album artist too
            // as artists can be just album artists but not song artists
            filter.AppendWhere(songArtistSQL + " OR " + albumArtistSQL);
          }
          else if (idRole > 1)
          {
            // Artist contributes that role (not albmartistsonly as already handled)
            filter.AppendWhere(songArtistSQL);
          }
          else // idRole = 1 and albumArtistsOnly
          { // Only look at album artists, not albums where artist features on songs
            filter.AppendWhere(albumArtistSQL);
          }
        }
      }
    }
    // remove the null string
    filter.AppendWhere("artistview.strArtist != ''");

    // and the various artist entry if applicable
    if (!albumArtistsOnly)
    {
      std::string strVariousArtists = g_localizeStrings.Get(340);
      filter.AppendWhere(PrepareSQL("artistview.strArtist <> '%s'", strVariousArtists.c_str()));
    }
  }
  else if (type == "albums")
  {
    option = options.find("year");
    if (option != options.end())
      filter.AppendWhere(PrepareSQL("albumview.iYear = %i", static_cast<int>(option->second.asInteger())));
    
    option = options.find("compilation");
    if (option != options.end())
      filter.AppendWhere(PrepareSQL("albumview.bCompilation = %i", option->second.asBoolean() ? 1 : 0));

    // Process artist, role and genre options together as song subquery to filter those
    // albums that have songs with both that artist and genre
    std::string albumArtistSQL, songArtistSQL, genreSQL;
    ExistsSubQuery genreSub("song", "song.idAlbum = album_artist.idAlbum");
    genreSub.AppendJoin("JOIN song_genre ON song_genre.idSong = song.idSong");
    genreSub.AppendWhere(PrepareSQL("song_genre.idGenre = %i", idGenre));
    ExistsSubQuery albumArtistSub("album_artist", "album_artist.idAlbum = albumview.idAlbum");
    ExistsSubQuery songArtistSub("song_artist", "song.idAlbum = albumview.idAlbum");
    songArtistSub.AppendJoin("JOIN song ON song.idSong = song_artist.idSong");

    if (idArtist > 0)
    {
      songArtistSub.AppendWhere(PrepareSQL("song_artist.idArtist = %i", idArtist));
      albumArtistSub.AppendWhere(PrepareSQL("album_artist.idArtist = %i", idArtist));
    }
    else if (!artistname.empty())
    { // Artist name is not unique, so could get albums or songs from more than one.
      songArtistSub.AppendJoin("JOIN artist ON artist.idArtist = song_artist.idArtist");
      songArtistSub.AppendWhere(PrepareSQL("artist.strArtist like '%s'", artistname.c_str()));
      
      albumArtistSub.AppendJoin("JOIN artist ON artist.idArtist = song_artist.idArtist");
      albumArtistSub.AppendWhere(PrepareSQL("artist.strArtist like '%s'", artistname.c_str()));
    }
    if (idRole > 0)
      songArtistSub.AppendWhere(PrepareSQL("song_artist.idRole = %i", idRole));
    if (idGenre > 0)
    {
      songArtistSub.AppendJoin("JOIN song_genre ON song_genre.idSong = song.idSong");
      songArtistSub.AppendWhere(PrepareSQL("song_genre.idGenre = %i", idGenre));
    }

    if (idArtist > 0 || !artistname.empty())
    {
      if (idRole <= 1 && idGenre > 0)
      { // Check genre of songs of album using nested subquery
        genreSub.BuildSQL(genreSQL);
        albumArtistSub.AppendWhere(genreSQL);
      }
      if (idRole > 1 && albumArtistsOnly)
      {  // Album artists only with role, check AND in album_artist for same song
         // using nested subquery correlated with album_artist
         songArtistSub.param = "song.idAlbum = album_artist.idAlbum";
         songArtistSub.BuildSQL(songArtistSQL);
         albumArtistSub.AppendWhere(songArtistSQL);
         albumArtistSub.BuildSQL(albumArtistSQL);
         filter.AppendWhere(albumArtistSQL);
      }
      else
      {
        songArtistSub.BuildSQL(songArtistSQL);
        albumArtistSub.BuildSQL(albumArtistSQL);
        if (idRole < 0 || (idRole == 1 && !albumArtistsOnly))  
        { // Artist contributing to songs, any role, check OR album artist too
          // as artists can be just album artists but not song artists
          filter.AppendWhere(songArtistSQL + " OR " + albumArtistSQL);
        }
        else if (idRole > 1)
        { // Albums with songs where artist contributes that role (not albmartistsonly as already handled)
          filter.AppendWhere(songArtistSQL);
        }
        else // idRole = 1 and albumArtistsOnly
        { // Only look at album artists, not albums where artist features on songs
          // This may want to be a separate option so you can choose to see all the albums where that artist 
          // appears on one or more songs without having to list all song artists in the artists node.
          filter.AppendWhere(albumArtistSQL);
        }
      }
    }
    else
    { // No artist given 
      if (idGenre > 0)
      { // Have genre option but not artist
        genreSub.param = "song.idAlbum = albumview.idAlbum";
        genreSub.BuildSQL(genreSQL);
        filter.AppendWhere(genreSQL);
      }
      // Exclude any single albums (aka empty tagged albums)
      // This causes "albums"  media filter artist selection to only offer album artists       
      option = options.find("show_singles");
      if (option == options.end() || !option->second.asBoolean())
        filter.AppendWhere(PrepareSQL("albumview.strReleaseType = '%s'", CAlbum::ReleaseTypeToString(CAlbum::Album).c_str()));
    }
  }
  else if (type == "songs" || type == "singles")
  {
    option = options.find("singles");
    if (option != options.end())
      filter.AppendWhere(PrepareSQL("songview.idAlbum %sIN (SELECT idAlbum FROM album WHERE strReleaseType = '%s')",
                                    option->second.asBoolean() ? "" : "NOT ",
                                    CAlbum::ReleaseTypeToString(CAlbum::Single).c_str()));

    option = options.find("year");
    if (option != options.end())
      filter.AppendWhere(PrepareSQL("songview.iYear = %i", static_cast<int>(option->second.asInteger())));
    
    option = options.find("compilation");
    if (option != options.end())
      filter.AppendWhere(PrepareSQL("songview.bCompilation = %i", option->second.asBoolean() ? 1 : 0));
    
    if (idSong > 0)
      filter.AppendWhere(PrepareSQL("songview.idSong = %i", idSong));

    if (idAlbum > 0)
      filter.AppendWhere(PrepareSQL("songview.idAlbum = %i", idAlbum));

    if (idGenre > 0)
      filter.AppendWhere(PrepareSQL("songview.idSong IN (SELECT song_genre.idSong FROM song_genre WHERE song_genre.idGenre = %i)", idGenre));

    std::string songArtistClause, albumArtistClause;
    if (idArtist > 0)
    {
      songArtistClause = PrepareSQL("EXISTS (SELECT 1 FROM song_artist "
        "WHERE song_artist.idSong = songview.idSong AND song_artist.idArtist = %i %s)",
        idArtist, strRoleSQL.c_str());
      albumArtistClause = PrepareSQL("EXISTS (SELECT 1 FROM album_artist "
        "WHERE album_artist.idAlbum = songview.idAlbum AND album_artist.idArtist = %i)",
        idArtist);
    }
    else if (!artistname.empty())
    {  // Artist name is not unique, so could get songs from more than one.
      songArtistClause = PrepareSQL("EXISTS (SELECT 1 FROM song_artist JOIN artist ON artist.idArtist = song_artist.idArtist "
        "WHERE song_artist.idSong = songview.idSong AND artist.strArtist like '%s' %s)",
        artistname.c_str(), strRoleSQL.c_str());
      albumArtistClause = PrepareSQL("EXISTS (SELECT 1 FROM album_artist JOIN artist ON artist.idArtist = album_artist.idArtist "
        "WHERE album_artist.idAlbum = songview.idAlbum AND artist.strArtist like '%s')",
        artistname.c_str());
    }

    // Process artist name or id option
    if (!songArtistClause.empty())
    {
      if (idRole < 0) // Artist contributes to songs, any roles OR is album artist
        filter.AppendWhere("(" + songArtistClause + " OR " + albumArtistClause + ")");
      else if (idRole > 1)
      {
        if (albumArtistsOnly)  //Album artists only with role, check AND in album_artist for same song
          filter.AppendWhere("(" + songArtistClause + " AND " + albumArtistClause + ")");
        else // songs where artist contributes that role.
          filter.AppendWhere(songArtistClause);
      }
      else
      {
        if (albumArtistsOnly) // Only look at album artists, not where artist features on songs            
          filter.AppendWhere(albumArtistClause);
        else // Artist is song artist or album artist
          filter.AppendWhere("(" + songArtistClause + " OR " + albumArtistClause + ")");
      }
    }
  }

  option = options.find("filter");
  if (option != options.end())
  {
    CSmartPlaylist xspFilter;
    if (!xspFilter.LoadFromJson(option->second.asString()))
      return false;

    // check if the filter playlist matches the item type
    if (xspFilter.GetType() == type)
    {
      std::set<std::string> playlists;
      filter.AppendWhere(xspFilter.GetWhereClause(*this, playlists));
    }
    // remove the filter if it doesn't match the item type
    else
      musicUrl.RemoveOption("filter");
  }

  return true;
}

void CMusicDatabase::UpdateFileDateAdded(int songId, const std::string& strFileNameAndPath)
{
  if (songId < 0 || strFileNameAndPath.empty())
    return;

  CDateTime dateAdded;
  try
  {
    if (NULL == m_pDB.get()) return;
    if (NULL == m_pDS.get()) return;

    // 1 preferring to use the files mtime(if it's valid) and only using the file's ctime if the mtime isn't valid
    if (g_advancedSettings.m_iMusicLibraryDateAdded == 1)
      dateAdded = CFileUtils::GetModificationDate(strFileNameAndPath, false);
    //2 using the newer datetime of the file's mtime and ctime
    else if (g_advancedSettings.m_iMusicLibraryDateAdded == 2)
      dateAdded = CFileUtils::GetModificationDate(strFileNameAndPath, true);
    //0 using the current datetime if non of the above matches or one returns an invalid datetime
    if (!dateAdded.IsValid())
      dateAdded = CDateTime::GetCurrentDateTime();

    m_pDS->exec(PrepareSQL("UPDATE song SET dateAdded='%s' WHERE idSong=%d", dateAdded.GetAsDBDateTime().c_str(), songId));
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "%s (%s, %s) failed", __FUNCTION__, CURL::GetRedacted(strFileNameAndPath).c_str(), dateAdded.GetAsDBDateTime().c_str());
  }
}

bool CMusicDatabase::AddAudioBook(const CFileItem& item)
{
  std::string strSQL = PrepareSQL("INSERT INTO audiobook (idBook,strBook,strAuthor,bookmark,file,dateAdded) VALUES (NULL,'%s','%s',%i,'%s','%s')",
                                 item.GetMusicInfoTag()->GetAlbum().c_str(),
                                 item.GetMusicInfoTag()->GetArtist()[0].c_str(), 0,
                                 item.GetPath().c_str(),
                                 CDateTime::GetCurrentDateTime().GetAsDBDateTime().c_str());
  return ExecuteQuery(strSQL);
}

bool CMusicDatabase::SetResumeBookmarkForAudioBook(const CFileItem& item, int bookmark)
{
  std::string strSQL = PrepareSQL("select bookmark from audiobook where file='%s'",
                                 item.GetPath().c_str());
  if (!m_pDS->query(strSQL.c_str()) || m_pDS->num_rows() == 0)
  {
    if (!AddAudioBook(item))
      return false;
  }

  strSQL = PrepareSQL("UPDATE audiobook SET bookmark=%i WHERE file='%s'",
                      bookmark, item.GetPath().c_str());

  return ExecuteQuery(strSQL);
}

bool CMusicDatabase::GetResumeBookmarkForAudioBook(const std::string& path, int& bookmark)
{
  std::string strSQL = PrepareSQL("SELECT bookmark FROM audiobook WHERE file='%s'",
                                 path.c_str());
  if (!m_pDS->query(strSQL.c_str()) || m_pDS->num_rows() == 0)
    return false;

  bookmark = m_pDS->fv(0).get_asInt();
  return true;
}
