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
/*!
 \file MusicDatabase.h
\brief
*/
#pragma once
#include <utility>
#include <vector>

#include "addons/Scraper.h"
#include "Album.h"
#include "dbwrappers/Database.h"
#include "MusicDbUrl.h"
#include "utils/SortUtils.h"

class CArtist;
class CFileItem;

namespace dbiplus
{
  class field_value;
  typedef std::vector<field_value> sql_record;
}

#include <set>
#include <string>

// return codes of Cleaning up the Database
// numbers are strings from strings.xml
#define ERROR_OK     317
#define ERROR_CANCEL    0
#define ERROR_DATABASE    315
#define ERROR_REORG_SONGS   319
#define ERROR_REORG_ARTIST   321
#define ERROR_REORG_OTHER   323
#define ERROR_REORG_PATH   325
#define ERROR_REORG_ALBUM   327
#define ERROR_WRITING_CHANGES  329
#define ERROR_COMPRESSING   332

#define NUM_SONGS_BEFORE_COMMIT 500

/*!
 \ingroup music
 \brief A set of std::string objects, used for CMusicDatabase
 \sa ISETPATHS, CMusicDatabase
 */
typedef std::set<std::string> SETPATHS;

/*!
 \ingroup music
 \brief The SETPATHS iterator
 \sa SETPATHS, CMusicDatabase
 */
typedef std::set<std::string>::iterator ISETPATHS;

class CGUIDialogProgress;
class CFileItemList;

/*!
 \ingroup music
 \brief Class to store and read tag information

 CMusicDatabase can be used to read and store
 tag information for faster access. It is based on
 sqlite (http://www.sqlite.org).

 Here is the database layout:
  \image html musicdatabase.png

 \sa CAlbum, CSong, VECSONGS, CMapSong, VECARTISTS, VECALBUMS, VECGENRES
 */
class CMusicDatabase : public CDatabase
{
  friend class DatabaseUtils;
  friend class TestDatabaseUtilsHelper;

public:
  CMusicDatabase(void);
  ~CMusicDatabase(void) override;

  bool Open() override;
  bool CommitTransaction() override;
  void EmptyCache();
  void Clean();
  int  Cleanup(bool bShowProgress=true);
  bool LookupCDDBInfo(bool bRequery=false);
  void DeleteCDDBInfo();

  /////////////////////////////////////////////////
  // Song CRUD
  /////////////////////////////////////////////////
  /*! \brief Add a song to the database
   \param idAlbum [in] the database ID of the album for the song
   \param strTitle [in] the title of the song (required to be non-empty)
   \param strMusicBrainzTrackID [in] the MusicBrainz track ID of the song
   \param strPathAndFileName [in] the path and filename to the song
   \param strComment [in] the ids of the added songs
   \param strMood [in] the mood of the added song
   \param strThumb [in] the ids of the added songs
   \param artistDisp [in] the assembled artist name(s) display string
   \param artistSort [in] the artist name(s) sort string
   \param genres [in] a vector of genres to which this song belongs
   \param iTrack [in] the track number and disc number of the song
   \param iDuration [in] the duration of the song
   \param iYear [in] the year of the song
   \param iTimesPlayed [in] the number of times the song has been played
   \param iStartOffset [in] the start offset of the song (when using a single audio file with a .cue)
   \param iEndOffset [in] the end offset of the song (when using a single audio file with .cue)
   \param dtLastPlayed [in] the time the song was last played
   \param rating [in] a rating for the song
   \param userrating [in] a userrating (my rating) for the song
   \param votes [in] a vote counter for the song rating
   \param replayGain [in] album and track replaygain and peak values
   \return the id of the song
   */
  int AddSong(const int idAlbum,
              const std::string& strTitle,
              const std::string& strMusicBrainzTrackID,
              const std::string& strPathAndFileName,
              const std::string& strComment,
              const std::string& strMood,
              const std::string& strThumb,
              const std::string &artistDisp, const std::string &artistSort,
              const std::vector<std::string>& genres,
              int iTrack, int iDuration, int iYear,
              const int iTimesPlayed, int iStartOffset, int iEndOffset,
              const CDateTime& dtLastPlayed, float rating, int userrating, int votes,
              const ReplayGain& replayGain);
  bool GetSong(int idSong, CSong& song);

  /*! \brief Update a song in the database.

   NOTE: This function assumes that song.artist contains the artist string to be concatenated.
         Most internal functions should instead use the long-form function as the artist string
         should be constructed from the artist credits.
         This function will eventually be demised.

   \param idSong  the database ID of the song to update
   \param song the song
   \return the id of the song.
   */
  int UpdateSong(int idSong, const CSong &song);

  /*! \brief Update a song in the database
   \param idSong [in] the database ID of the song to update
   \param strTitle [in] the title of the song (required to be non-empty)
   \param strMusicBrainzTrackID [in] the MusicBrainz track ID of the song
   \param strPathAndFileName [in] the path and filename to the song
   \param strComment [in] the ids of the added songs
   \param strMood [in] the mood of the added song
   \param strThumb [in] the ids of the added songs
   \param artistDisp [in] the artist name(s) display string
   \param artistSort [in] the artist name(s) sort string
   \param genres [in] a vector of genres to which this song belongs
   \param iTrack [in] the track number and disc number of the song
   \param iDuration [in] the duration of the song
   \param iYear [in] the year of the song
   \param iTimesPlayed [in] the number of times the song has been played
   \param iStartOffset [in] the start offset of the song (when using a single audio file with a .cue)
   \param iEndOffset [in] the end offset of the song (when using a single audio file with .cue)
   \param dtLastPlayed [in] the time the song was last played
   \param rating [in] a rating for the song
   \param userrating [in] a userrating (my rating) for the song
   \param votes [in] a vote counter for the song rating
   \param replayGain [in] album and track replaygain and peak values
   \return the id of the song
   */
  int UpdateSong(int idSong,
                 const std::string& strTitle, const std::string& strMusicBrainzTrackID,
                 const std::string& strPathAndFileName, const std::string& strComment,
                 const std::string& strMood, const std::string& strThumb,
                 const std::string &artistDisp, const std::string &artistSort,
                 const std::vector<std::string>& genres,
                 int iTrack, int iDuration, int iYear,
                 int iTimesPlayed, int iStartOffset, int iEndOffset,
                 const CDateTime& dtLastPlayed, float rating, int userrating, int votes, const ReplayGain& replayGain);

  //// Misc Song
  bool GetSongByFileName(const std::string& strFileName, CSong& song, int startOffset = 0);
  bool GetSongsByPath(const std::string& strPath, MAPSONGS& songs, bool bAppendToMap = false);
  bool Search(const std::string& search, CFileItemList &items);
  bool RemoveSongsFromPath(const std::string &path, MAPSONGS& songs, bool exact=true);
  bool SetSongUserrating(const std::string &filePath, int userrating);
  bool SetSongVotes(const std::string &filePath, int votes);
  int  GetSongByArtistAndAlbumAndTitle(const std::string& strArtist, const std::string& strAlbum, const std::string& strTitle);

  /////////////////////////////////////////////////
  // Album
  /////////////////////////////////////////////////
  /*! \brief Add an album and all its songs to the database
  \param album the album to add
  \return the id of the album
  */
  bool AddAlbum(CAlbum& album);

  /*! \brief Update an album and all its nested entities (artists, songs etc)
   \param album the album to update
   \return true or false
   */
  bool UpdateAlbum(CAlbum& album);

  /*! \brief Add an album to the database
   \param strAlbum the album title
   \param strMusicBrainzAlbumID the Musicbrainz Id
   \param strArtist the album artist name(s) display string
   \param strArtistSort the album artist name(s) sort string
   \param strGenre the album genre(s)
   \param year the year
   \param strRecordLabel the recording label
   \param strType album type (Musicbrainz release type e.g. "Broadcast, Soundtrack, live"), 
   \param bCompilation if the album is a compilation
   \param releaseType "album" or "single"
   \return the id of the album
   */
  int  AddAlbum(const std::string& strAlbum, const std::string& strMusicBrainzAlbumID,
                const std::string& strReleaseGroupMBID,
                const std::string& strArtist, const std::string& strArtistSort, 
                const std::string& strGenre, int year,
                const std::string& strRecordLabel, const std::string& strType,
                bool bCompilation, CAlbum::ReleaseType releaseType);

  /*! \brief retrieve an album, optionally with all songs.
   \param idAlbum the database id of the album.
   \param album [out] the album to fill.
   \param getSongs whether or not to retrieve songs, defaults to true.
   \return true if the album is retrieved, false otherwise.
   */
  bool GetAlbum(int idAlbum, CAlbum& album, bool getSongs = true);
  int  UpdateAlbum(int idAlbum, const CAlbum &album);
  int  UpdateAlbum(int idAlbum,
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
                   bool bScrapedMBID);
  bool ClearAlbumLastScrapedTime(int idAlbum);
  bool HasAlbumBeenScraped(int idAlbum);

  /////////////////////////////////////////////////
  // Audiobook
  /////////////////////////////////////////////////
  bool AddAudioBook(const CFileItem& item);
  bool SetResumeBookmarkForAudioBook(const CFileItem& item, int bookmark);
  bool GetResumeBookmarkForAudioBook(const std::string& path, int& bookmark);

  /*! \brief Checks if the given path is inside a folder that has already been scanned into the library
   \param path the path we want to check
   */
  bool InsideScannedPath(const std::string& path);

  //// Misc Album
  int  GetAlbumIdByPath(const std::string& path);
  bool GetAlbumFromSong(int idSong, CAlbum &album);
  int  GetAlbumByName(const std::string& strAlbum, const std::string& strArtist="");
  int  GetAlbumByName(const std::string& strAlbum, const std::vector<std::string>& artist);
  std::string GetAlbumById(int id);
  bool SetAlbumUserrating(const int idAlbum, int userrating);

  /////////////////////////////////////////////////
  // Artist CRUD
  /////////////////////////////////////////////////
  bool UpdateArtist(const CArtist& artist);

  int  AddArtist(const std::string& strArtist, const std::string& strMusicBrainzArtistID, const std::string& strSortName, bool bScrapedMBID = false);
  int  AddArtist(const std::string& strArtist, const std::string& strMusicBrainzArtistID, bool bScrapedMBID = false);
  bool GetArtist(int idArtist, CArtist& artist, bool fetchAll = true);
  bool GetArtistExists(int idArtist);
  int  UpdateArtist(int idArtist,
                    const std::string& strArtist, const std::string& strSortName,
                    const std::string& strMusicBrainzArtistID, bool bScrapedMBID,
                    const std::string& strBorn, const std::string& strFormed,
                    const std::string& strGenres, const std::string& strMoods,
                    const std::string& strStyles, const std::string& strInstruments,
                    const std::string& strBiography, const std::string& strDied,
                    const std::string& strDisbanded, const std::string& strYearsActive,
                    const std::string& strImage, const std::string& strFanart);
  bool UpdateArtistScrapedMBID(int idArtist, const std::string& strMusicBrainzArtistID);
  bool GetTranslateBlankArtist() { return m_translateBlankArtist; }
  void SetTranslateBlankArtist(bool translate) { m_translateBlankArtist = translate; }
  bool HasArtistBeenScraped(int idArtist);
  bool ClearArtistLastScrapedTime(int idArtist);
  int  AddArtistDiscography(int idArtist, const std::string& strAlbum, const std::string& strYear);
  bool DeleteArtistDiscography(int idArtist);

  std::string GetArtistById(int id);
  int GetArtistByName(const std::string& strArtist);
  std::string GetRoleById(int id);

  /*! \brief Propagate artist sort name into the concatenated artist sort name strings
  held for songs and albums 
  \param int idArtist to propagate sort name for, -1 means all artists
  */
  bool UpdateArtistSortNames(int idArtist = -1);

  /////////////////////////////////////////////////
  // Paths
  /////////////////////////////////////////////////
  int AddPath(const std::string& strPath);

  bool GetPaths(std::set<std::string> &paths);
  bool SetPathHash(const std::string &path, const std::string &hash);
  bool GetPathHash(const std::string &path, std::string &hash);
  bool GetAlbumPath(int idAlbum, std::string &path);
  bool GetArtistPath(int idArtist, std::string &path);

  /////////////////////////////////////////////////
  // Genres
  /////////////////////////////////////////////////
  int AddGenre(const std::string& strGenre);
  std::string GetGenreById(int id);
  int GetGenreByName(const std::string& strGenre);

  /////////////////////////////////////////////////
  // Link tables
  /////////////////////////////////////////////////
  bool AddAlbumArtist(int idArtist, int idAlbum, std::string strArtist, int iOrder);
  bool GetAlbumsByArtist(int idArtist, std::vector<int>& albums);
  bool GetArtistsByAlbum(int idAlbum, CFileItem* item);
  bool DeleteAlbumArtistsByAlbum(int idAlbum);

  int AddRole(const std::string &strRole);
  bool AddSongArtist(int idArtist, int idSong, const std::string& strRole, const std::string& strArtist, int iOrder);
  bool AddSongArtist(int idArtist, int idSong, int idRole, const std::string& strArtist, int iOrder);
  int  AddSongContributor(int idSong, const std::string& strRole, const std::string& strArtist, const std::string &strSort);
  void AddSongContributors(int idSong, const VECMUSICROLES& contributors, const std::string &strSort);
  int GetRoleByName(const std::string& strRole);
  bool GetRolesByArtist(int idArtist, CFileItem* item);
  bool GetSongsByArtist(int idArtist, std::vector<int>& songs);
  bool GetArtistsBySong(int idSong, std::vector<int>& artists);
  bool DeleteSongArtistsBySong(int idSong);

  bool AddSongGenre(int idGenre, int idSong, int iOrder);
  bool GetGenresBySong(int idSong, std::vector<int>& genres);

  bool AddAlbumGenre(int idGenre, int idAlbum, int iOrder);
  bool GetGenresByAlbum(int idAlbum, std::vector<int>& genres);
  bool DeleteAlbumGenresByAlbum(int idAlbum);

  bool GetGenresByArtist(int idArtist, CFileItem* item);
  bool GetIsAlbumArtist(int idArtist, CFileItem* item);

  /////////////////////////////////////////////////
  // Top 100
  /////////////////////////////////////////////////
  bool GetTop100(const std::string& strBaseDir, CFileItemList& items);
  bool GetTop100Albums(VECALBUMS& albums);
  bool GetTop100AlbumSongs(const std::string& strBaseDir, CFileItemList& item);

  /////////////////////////////////////////////////
  // Recently added
  /////////////////////////////////////////////////
  bool GetRecentlyAddedAlbums(VECALBUMS& albums, unsigned int limit=0);
  bool GetRecentlyAddedAlbumSongs(const std::string& strBaseDir, CFileItemList& item, unsigned int limit=0);
  bool GetRecentlyPlayedAlbums(VECALBUMS& albums);
  bool GetRecentlyPlayedAlbumSongs(const std::string& strBaseDir, CFileItemList& item);

  /////////////////////////////////////////////////
  // Compilations
  /////////////////////////////////////////////////
  bool GetCompilationAlbums(const std::string& strBaseDir, CFileItemList& items);
  bool GetCompilationSongs(const std::string& strBaseDir, CFileItemList& items);
  int  GetCompilationAlbumsCount();

  int GetSinglesCount();

  int GetArtistCountForRole(int role);
  int GetArtistCountForRole(const std::string& strRole);
  
  /*! \brief Increment the playcount of an item
   Increments the playcount and updates the last played date
   \param item CFileItem to increment the playcount for
   */
  void IncrementPlayCount(const CFileItem &item);
  bool CleanupOrphanedItems();

  /////////////////////////////////////////////////
  // VIEWS
  /////////////////////////////////////////////////
  bool GetGenresNav(const std::string& strBaseDir, CFileItemList& items, const Filter &filter = Filter(), bool countOnly = false);
  bool GetYearsNav(const std::string& strBaseDir, CFileItemList& items, const Filter &filter = Filter());
  bool GetRolesNav(const std::string& strBaseDir, CFileItemList& items, const Filter &filter = Filter());
  bool GetArtistsNav(const std::string& strBaseDir, CFileItemList& items, bool albumArtistsOnly = false, int idGenre = -1, int idAlbum = -1, int idSong = -1, const Filter &filter = Filter(), const SortDescription &sortDescription = SortDescription(), bool countOnly = false);
  bool GetCommonNav(const std::string &strBaseDir, const std::string &table, const std::string &labelField, CFileItemList &items, const Filter &filter /* = Filter() */, bool countOnly /* = false */);
  bool GetAlbumTypesNav(const std::string &strBaseDir, CFileItemList &items, const Filter &filter = Filter(), bool countOnly = false);
  bool GetMusicLabelsNav(const std::string &strBaseDir, CFileItemList &items, const Filter &filter = Filter(), bool countOnly = false);
  bool GetAlbumsNav(const std::string& strBaseDir, CFileItemList& items, int idGenre = -1, int idArtist = -1, const Filter &filter = Filter(), const SortDescription &sortDescription = SortDescription(), bool countOnly = false);
  bool GetAlbumsByYear(const std::string &strBaseDir, CFileItemList& items, int year);
  bool GetSongsNav(const std::string& strBaseDir, CFileItemList& items, int idGenre, int idArtist,int idAlbum, const SortDescription &sortDescription = SortDescription());
  bool GetSongsByYear(const std::string& baseDir, CFileItemList& items, int year);
  bool GetSongsByWhere(const std::string &baseDir, const Filter &filter, CFileItemList& items, const SortDescription &sortDescription = SortDescription());
  bool GetSongsFullByWhere(const std::string &baseDir, const Filter &filter, CFileItemList& items, const SortDescription &sortDescription = SortDescription(), bool artistData = false);
  bool GetAlbumsByWhere(const std::string &baseDir, const Filter &filter, CFileItemList &items, const SortDescription &sortDescription = SortDescription(), bool countOnly = false);
  bool GetAlbumsByWhere(const std::string &baseDir, const Filter &filter, VECALBUMS& albums, int& total, const SortDescription &sortDescription = SortDescription(), bool countOnly = false);
  bool GetArtistsByWhere(const std::string& strBaseDir, const Filter &filter, CFileItemList& items, const SortDescription &sortDescription = SortDescription(), bool countOnly = false);
  bool GetRandomSong(CFileItem* item, int& idSong, const Filter &filter);
  int GetSongsCount(const Filter &filter = Filter());
  unsigned int GetSongIDs(const Filter &filter, std::vector<std::pair<int,int> > &songIDs);
  bool GetFilter(CDbUrl &musicUrl, Filter &filter, SortDescription &sorting) override;

  /////////////////////////////////////////////////
  // Scraper
  /////////////////////////////////////////////////
  bool SetScraper(int id, const CONTENT_TYPE &content, const ADDON::ScraperPtr scraper);
  bool SetScraperAll(const std::string& strBaseDir, const ADDON::ScraperPtr scraper);
  bool GetScraper(int id, const CONTENT_TYPE &content, ADDON::ScraperPtr& scraper);
  
  /*! \brief Check whether a given scraper is in use.
   \param scraperID the scraper to check for.
   \return true if the scraper is in use, false otherwise.
   */
  bool ScraperInUse(const std::string &scraperID) const;

  /////////////////////////////////////////////////
  // Filters
  /////////////////////////////////////////////////
  bool GetItems(const std::string &strBaseDir, CFileItemList &items, const Filter &filter = Filter(), const SortDescription &sortDescription = SortDescription());
  bool GetItems(const std::string &strBaseDir, const std::string &itemType, CFileItemList &items, const Filter &filter = Filter(), const SortDescription &sortDescription = SortDescription());
  std::string GetItemById(const std::string &itemType, int id);

  /////////////////////////////////////////////////
  // XML
  /////////////////////////////////////////////////
  void ExportToXML(const std::string &xmlFile, bool singleFile = false, bool images=false, bool overwrite=false);
  void ImportFromXML(const std::string &xmlFile);

  /////////////////////////////////////////////////
  // Properties
  /////////////////////////////////////////////////
  void SetPropertiesForFileItem(CFileItem& item);
  static void SetPropertiesFromArtist(CFileItem& item, const CArtist& artist);
  static void SetPropertiesFromAlbum(CFileItem& item, const CAlbum& album);

  /////////////////////////////////////////////////
  // Art
  /////////////////////////////////////////////////
  bool SaveAlbumThumb(int idAlbum, const std::string &thumb);
  /*! \brief Sets art for a database item.
   Sets a single piece of art for a database item.
   \param mediaId the id in the media (song/artist/album) table.
   \param mediaType the type of media, which corresponds to the table the item resides in (song/artist/album).
   \param artType the type of art to set, e.g. "thumb", "fanart"
   \param url the url to the art (this is the original url, not a cached url).
   \sa GetArtForItem
   */
  void SetArtForItem(int mediaId, const std::string &mediaType, const std::string &artType, const std::string &url);

  /*! \brief Sets art for a database item.
   Sets multiple pieces of art for a database item.
   \param mediaId the id in the media (song/artist/album) table.
   \param mediaType the type of media, which corresponds to the table the item resides in (song/artist/album).
   \param art a map of <type, url> where type is "thumb", "fanart", etc. and url is the original url of the art.
   \sa GetArtForItem
   */
  void SetArtForItem(int mediaId, const std::string &mediaType, const std::map<std::string, std::string> &art);

  /*! \brief Fetch art for a database item.
   Fetches multiple pieces of art for a database item.
   \param mediaId the id in the media (song/artist/album) table.
   \param mediaType the type of media, which corresponds to the table the item resides in (song/artist/album).
   \param art [out] a map of <type, url> where type is "thumb", "fanart", etc. and url is the original url of the art.
   \return true if art is retrieved, false if no art is found.
   \sa SetArtForItem
   */
  bool GetArtForItem(int mediaId, const std::string &mediaType, std::map<std::string, std::string> &art);

  /*! \brief Fetch art for a database item.
   Fetches a single piece of art for a database item.
   \param mediaId the id in the media (song/artist/album) table.
   \param mediaType the type of media, which corresponds to the table the item resides in (song/artist/album).
   \param artType the type of art to retrieve, eg "thumb", "fanart".
   \return the original URL to the piece of art, if available.
   \sa SetArtForItem
   */
  std::string GetArtForItem(int mediaId, const std::string &mediaType, const std::string &artType);

  /*! \brief Fetch artist art for a song or album item.
   Fetches the art associated with the primary artist for the song or album.
   \param mediaId the id in the media (song/album) table.
   \param mediaType the type of media, which corresponds to the table the item resides in (song/album).
   \param art [out] the art map <type, url> of artist art.
   \return true if artist art is found, false otherwise.
   \sa GetArtForItem
   */
  bool GetArtistArtForItem(int mediaId, const std::string &mediaType, std::map<std::string, std::string> &art);

  /*! \brief Fetch artist art for a song or album item.
   Fetches a single piece of art associated with the primary artist for the song or album.
   \param mediaId the id in the media (song/album) table.
   \param mediaType the type of media, which corresponds to the table the item resides in (song/album).
   \param artType the type of art to retrieve, eg "thumb", "fanart".
   \return the original URL to the piece of art, if available.
   \sa GetArtForItem
   */
  std::string GetArtistArtForItem(int mediaId, const std::string &mediaType, const std::string &artType);

  /////////////////////////////////////////////////
  // Tag Scan Version
  /////////////////////////////////////////////////
  /*! \brief Check if music files need all tags rescanning regardless of file being unchanged 
  because the tag processing has changed (which may happen without db version changes) since they
  where last scanned.
  \return -1 if an error occured, 0 if no scan is needed, or the version number of tags if not the same as current.
  */
  virtual int GetMusicNeedsTagScan();

  /*! \brief Set minimum version number of db needed when tag data scanned from music files
  \param version the version number of db
  */
  void SetMusicNeedsTagScan(int version);

  /*! \brief Set the version number of tag data 
  \param version the version number of db when tags last scanned, 0 (default) means current db version
  */
  void SetMusicTagScanVersion(int version = 0);

protected:
  std::map<std::string, int> m_genreCache;
  std::map<std::string, int> m_pathCache;
  
  void CreateTables() override;
  void CreateAnalytics() override;
  int GetMinSchemaVersion() const override { return 32; }
  int GetSchemaVersion() const override;

  const char *GetBaseDBName() const override { return "MyMusic"; };

private:
  /*! \brief (Re)Create the generic database views for songs and albums
   */
  virtual void CreateViews();

  CSong GetSongFromDataset();
  CSong GetSongFromDataset(const dbiplus::sql_record* const record, int offset = 0);
  CArtist GetArtistFromDataset(dbiplus::Dataset* pDS, int offset = 0, bool needThumb = true);
  CArtist GetArtistFromDataset(const dbiplus::sql_record* const record, int offset = 0, bool needThumb = true);
  CAlbum GetAlbumFromDataset(dbiplus::Dataset* pDS, int offset = 0, bool imageURL = false);
  CAlbum GetAlbumFromDataset(const dbiplus::sql_record* const record, int offset = 0, bool imageURL = false);
  CArtistCredit GetArtistCreditFromDataset(const dbiplus::sql_record* const record, int offset = 0);
  CMusicRole GetArtistRoleFromDataset(const dbiplus::sql_record* const record, int offset = 0);
  /*! \brief Updates the dateAdded field in the song table for the file
  with the given songId and the given path based on the files modification date
  \param songId id of the song in the song table
  \param strFileNameAndPath path to the file
  */
  void UpdateFileDateAdded(int songId, const std::string& strFileNameAndPath);
  void GetFileItemFromDataset(CFileItem* item, const CMusicDbUrl &baseUrl);
  void GetFileItemFromDataset(const dbiplus::sql_record* const record, CFileItem* item, const CMusicDbUrl &baseUrl);
  void GetFileItemFromArtistCredits(VECARTISTCREDITS& artistCredits, CFileItem* item);
  bool CleanupSongs();
  bool CleanupSongsByIds(const std::string &strSongIds);
  bool CleanupPaths();
  bool CleanupAlbums();
  bool CleanupArtists();
  bool CleanupGenres();
  bool CleanupInfoSettings();
  bool CleanupRoles();
  void UpdateTables(int version) override;
  bool SearchArtists(const std::string& search, CFileItemList &artists);
  bool SearchAlbums(const std::string& search, CFileItemList &albums);
  bool SearchSongs(const std::string& strSearch, CFileItemList &songs);
  int GetSongIDFromPath(const std::string &filePath);

  bool m_translateBlankArtist;

  // Fields should be ordered as they
  // appear in the songview
  static enum _SongFields
  {
    song_idSong=0,
    song_strArtists,
    song_strArtistSort,
    song_strGenres,
    song_strTitle,
    song_iTrack,
    song_iDuration,
    song_iYear,
    song_strFileName,
    song_strMusicBrainzTrackID,
    song_iTimesPlayed,
    song_iStartOffset,
    song_iEndOffset,
    song_lastplayed,
    song_rating,
    song_userrating,
    song_votes,
    song_comment,
    song_idAlbum,
    song_strAlbum,
    song_strPath,
    song_bCompilation,
    song_strAlbumArtists,
    song_strAlbumArtistSort,
    song_strAlbumReleaseType,
    song_mood,
    song_dateAdded,
    song_strReplayGain,
    song_enumCount // end of the enum, do not add past here
  } SongFields;

  // Fields should be ordered as they
  // appear in the albumview
  static enum _AlbumFields
  {
    album_idAlbum=0,
    album_strAlbum,
    album_strMusicBrainzAlbumID,
    album_strReleaseGroupMBID,
    album_strArtists,
    album_strArtistSort,
    album_strGenres,
    album_iYear,
    album_strMoods,
    album_strStyles,
    album_strThemes,
    album_strReview,
    album_strLabel,
    album_strType,
    album_strThumbURL,
    album_fRating,
    album_iUserrating,
    album_iVotes,
    album_bCompilation,
    album_bScrapedMBID,
    album_lastScraped,
    album_iTimesPlayed,
    album_strReleaseType,
    album_dtDateAdded,
    album_dtLastPlayed,
    album_enumCount // end of the enum, do not add past here
  } AlbumFields;

  // Fields should be ordered as they
  // appear in the songartistview/albumartistview
  static enum _ArtistCreditFields
  {
    // used for GetAlbum to get the cascaded album/song artist credits
    artistCredit_idEntity = 0,  // can be idSong or idAlbum depending on context
    artistCredit_idArtist,
    artistCredit_idRole,
    artistCredit_strRole,
    artistCredit_strArtist,
    artistCredit_strSortName,
    artistCredit_strMusicBrainzArtistID,
    artistCredit_iOrder,
    artistCredit_enumCount
  } ArtistCreditFields;

  // Fields should be ordered as they
  // appear in the artistview
  static enum _ArtistFields
  {
    artist_idArtist=0,
    artist_strArtist,
    artist_strSortName,
    artist_strMusicBrainzArtistID,
    artist_strBorn,
    artist_strFormed,
    artist_strGenres,
    artist_strMoods,
    artist_strStyles,
    artist_strInstruments,
    artist_strBiography,
    artist_strDied,
    artist_strDisbanded,
    artist_strYearsActive,
    artist_strImage,
    artist_strFanart,
    artist_bScrapedMBID,
    artist_lastScraped,
    artist_dtDateAdded,
    artist_enumCount // end of the enum, do not add past here
  } ArtistFields;

};
