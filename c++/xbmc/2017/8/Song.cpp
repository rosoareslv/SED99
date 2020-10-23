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

#include "Song.h"
#include "music/tags/MusicInfoTag.h"
#include "utils/Variant.h"
#include "utils/StringUtils.h"
#include "utils/log.h"
#include "FileItem.h"
#include "settings/AdvancedSettings.h"

using namespace MUSIC_INFO;

CSong::CSong(CFileItem& item)
{
  CMusicInfoTag& tag = *item.GetMusicInfoTag();
  SYSTEMTIME stTime;
  tag.GetReleaseDate(stTime);
  strTitle = tag.GetTitle();
  genre = tag.GetGenre();
  std::vector<std::string> artist = tag.GetArtist();
  std::vector<std::string> musicBrainzArtistHints = tag.GetMusicBrainzArtistHints();
  strArtistDesc = tag.GetArtistString();
  strArtistSort = tag.GetArtistSort();
  m_strComposerSort = tag.GetComposerSort();
  //Split the artist sort string to try and get sort names for individual artists
  std::vector<std::string> artistSort = StringUtils::Split(strArtistSort, g_advancedSettings.m_musicItemSeparator);

  if (!tag.GetMusicBrainzArtistID().empty())
  { // Have musicbrainz artist info, so use it

    // Vector of possible separators in the order least likely to be part of artist name
    const std::vector<std::string> separators{ " feat. ", " ft. ", " Feat. "," Ft. ", ";", ":", "|", "#", "/", " with ", ",", "&" };

    // Establish tag consistency - do the number of musicbrainz ids and number of names in hints or artist match
    if (tag.GetMusicBrainzArtistID().size() != musicBrainzArtistHints.size() &&
        tag.GetMusicBrainzArtistID().size() != artist.size())
    {
      // Tags mis-match - report it and then try to fix
      CLog::Log(LOGDEBUG, "Mis-match in song file tags: %i mbid %i names %s %s", 
        (int)tag.GetMusicBrainzArtistID().size(), (int)artist.size(), strTitle.c_str(), strArtistDesc.c_str());
      /*
        Most likely we have no hints and a single artist name like "Artist1 feat. Artist2"
        or "Composer; Conductor, Orchestra, Soloist" or "Artist1/Artist2" where the
        expected single item separator (default = space-slash-space) as not been used.
        Ampersand (&), comma and slash (no spaces) are poor delimiters as could be in name
        e.g. "AC/DC", "Earth, Wind & Fire", but here treat them as such in attempt to find artist names.
        When there are hints but count not match mbid they could be poorly formatted using unexpected
        separators so attempt to split them. Or we could have more hints or artist names than
        musicbrainz id so ignore them but raise warning.
      */
      // Do hints exist yet mis-match
      if (musicBrainzArtistHints.size() > 0 &&
        musicBrainzArtistHints.size() != tag.GetMusicBrainzArtistID().size())
      {
        if (artist.size() == tag.GetMusicBrainzArtistID().size())
          // Artist name count matches, use that as hints
          musicBrainzArtistHints = artist;
        else if (musicBrainzArtistHints.size() < tag.GetMusicBrainzArtistID().size())
        { // Try splitting the hints until have matching number
          musicBrainzArtistHints = StringUtils::SplitMulti(musicBrainzArtistHints, separators, tag.GetMusicBrainzArtistID().size());
        }
        else
          // Extra hints, discard them.
          musicBrainzArtistHints.resize(tag.GetMusicBrainzArtistID().size());
      }
      // Do hints not exist or still mis-match, try artists
      if (musicBrainzArtistHints.size() != tag.GetMusicBrainzArtistID().size())
        musicBrainzArtistHints = artist;
      // Still mis-match, try splitting the hints (now artists) until have matching number
      if (musicBrainzArtistHints.size() < tag.GetMusicBrainzArtistID().size())
      {
        musicBrainzArtistHints = StringUtils::SplitMulti(musicBrainzArtistHints, separators, tag.GetMusicBrainzArtistID().size());
      }
    }
    else
    { // Either hints or artist names (or both) matches number of musicbrainz id
      // If hints mis-match, use artists
      if (musicBrainzArtistHints.size() != tag.GetMusicBrainzArtistID().size())
        musicBrainzArtistHints = tag.GetArtist();
    }

    // Try to get number of artist sort names and musicbrainz ids to match. Split sort names 
    // further using multiple possible delimiters, over single separator applied in Tag loader
    if (artistSort.size() != tag.GetMusicBrainzArtistID().size())
      artistSort = StringUtils::SplitMulti(artistSort, { ";", ":", "|", "#" });

    for (size_t i = 0; i < tag.GetMusicBrainzArtistID().size(); i++)
    {
      std::string artistId = tag.GetMusicBrainzArtistID()[i];
      std::string artistName;
      /*
       We try and get the corresponding artist name from the hints list.
       Having already attempted to make the number of hints match, if they
       still don't then use musicbrainz id as the name and hope later on we
       can update that entry.
      */
      if (i < musicBrainzArtistHints.size())
        artistName = musicBrainzArtistHints[i];
      else
        artistName = artistId;

      // Use artist sort name providing we have as many as we have mbid, 
      // otherwise something is wrong with them so ignore and leave blank
      if (artistSort.size() == tag.GetMusicBrainzArtistID().size())
        artistCredits.emplace_back(StringUtils::Trim(artistName), StringUtils::Trim(artistSort[i]), artistId);
      else
        artistCredits.emplace_back(StringUtils::Trim(artistName), "", artistId);
    }
  }
  else
  { // No musicbrainz artist ids, so fill in directly
    // Separate artist names further, if possible, and trim blank space.
    if (musicBrainzArtistHints.size() > tag.GetArtist().size())
      // Make use of hints (ARTISTS tag), when present, to separate artist names
      artist = musicBrainzArtistHints;
    else
      // Split artist names further using multiple possible delimiters, over single separator applied in Tag loader
      artist = StringUtils::SplitMulti(artist, g_advancedSettings.m_musicArtistSeparators);

    if (artistSort.size() != artist.size())
      // Split artist sort names further using multiple possible delimiters, over single separator applied in Tag loader
      artistSort = StringUtils::SplitMulti(artistSort, { ";", ":", "|", "#" });

    for (size_t i = 0; i < artist.size(); i++)
    {
      artistCredits.emplace_back(StringUtils::Trim(artist[i]));
      // Set artist sort name providing we have as many as we have artists, 
      // otherwise something is wrong with them so ignore rather than guess.
      if (artistSort.size() == artist.size())
        artistCredits.back().SetSortName(StringUtils::Trim(artistSort[i]));
    }
  }
  strAlbum = tag.GetAlbum();
  m_albumArtist = tag.GetAlbumArtist();
  // Separate album artist names further, if possible, and trim blank space.
  if (tag.GetMusicBrainzAlbumArtistHints().size() > m_albumArtist.size())
    // Make use of hints (ALBUMARTISTS tag), when present, to separate artist names
    m_albumArtist = tag.GetMusicBrainzAlbumArtistHints();
  else
    // Split album artist names further using multiple possible delimiters, over single separator applied in Tag loader
    m_albumArtist = StringUtils::SplitMulti(m_albumArtist, g_advancedSettings.m_musicArtistSeparators);
  for (auto artistname : m_albumArtist)
    StringUtils::Trim(artistname);
  m_strAlbumArtistSort = tag.GetAlbumArtistSort();

  strMusicBrainzTrackID = tag.GetMusicBrainzTrackID();
  m_musicRoles = tag.GetContributors();
  strComment = tag.GetComment();
  strCueSheet = tag.GetCueSheet();
  strMood = tag.GetMood();
  rating = tag.GetRating();
  userrating = tag.GetUserrating();
  votes = tag.GetVotes();
  iYear = stTime.wYear;
  iTrack = tag.GetTrackAndDiscNumber();
  iDuration = tag.GetDuration();
  strRecordLabel = tag.GetRecordLabel();
  strAlbumType = tag.GetMusicBrainzReleaseType();
  bCompilation = tag.GetCompilation();
  embeddedArt = tag.GetCoverArtInfo();
  strFileName = tag.GetURL().empty() ? item.GetPath() : tag.GetURL();
  dateAdded = tag.GetDateAdded();
  replayGain = tag.GetReplayGain();
  strThumb = item.GetUserMusicThumb(true);
  iStartOffset = item.m_lStartOffset;
  iEndOffset = item.m_lEndOffset;
  idSong = -1;
  iTimesPlayed = 0;
  idAlbum = -1;
}

CSong::CSong()
{
  Clear();
}

void CSong::MergeScrapedSong(const CSong& source, bool override)
{
  // Merge when MusicBrainz Track ID match (checked in CAlbum::MergeScrapedAlbum)
  if ((override && !source.strTitle.empty()) || strTitle.empty())
    strTitle = source.strTitle;
  if ((override && source.iTrack != 0) || iTrack == 0)
    iTrack = source.iTrack;
  if (override)
  {
    artistCredits = source.artistCredits; // Replace artists and store mbid returned by scraper   
    strArtistDesc.clear();  // @todo: set artist display string e.g. "artist1 feat. artist2" when scraped 
  }
}

void CSong::Serialize(CVariant& value) const
{
  value["filename"] = strFileName;
  value["title"] = strTitle;
  value["artist"] = GetArtist();
  value["artistsort"] = GetArtistSort();  // a string for the song not vector of values for each artist
  value["album"] = strAlbum;
  value["albumartist"] = GetAlbumArtist();
  value["genre"] = genre;
  value["duration"] = iDuration;
  value["track"] = iTrack;
  value["year"] = iYear;
  value["musicbrainztrackid"] = strMusicBrainzTrackID;
  value["comment"] = strComment;
  value["mood"] = strMood;
  value["rating"] = rating;
  value["userrating"] = userrating;
  value["votes"] = votes;
  value["timesplayed"] = iTimesPlayed;
  value["lastplayed"] = lastPlayed.IsValid() ? lastPlayed.GetAsDBDateTime() : "";
  value["dateadded"] = dateAdded.IsValid() ? dateAdded.GetAsDBDateTime() : "";
  value["albumid"] = idAlbum;
}

void CSong::Clear()
{
  strFileName.clear();
  strTitle.clear();
  strAlbum.clear();
  strArtistSort.clear();
  strArtistDesc.clear();
  m_albumArtist.clear();
  m_strAlbumArtistSort.clear();
  genre.clear();
  strThumb.clear();
  strMusicBrainzTrackID.clear();
  m_musicRoles.clear();
  strComment.clear();
  strMood.clear();
  rating = 0;
  userrating = 0;
  votes = 0;
  iTrack = 0;
  iDuration = 0;
  iYear = 0;
  iStartOffset = 0;
  iEndOffset = 0;
  idSong = -1;
  iTimesPlayed = 0;
  lastPlayed.Reset();
  dateAdded.Reset();
  idAlbum = -1;
  bCompilation = false;
  embeddedArt.clear();
  replayGain = ReplayGain();
}
const std::vector<std::string> CSong::GetArtist() const
{
  //Get artist names as vector from artist credits
  std::vector<std::string> songartists;
  for (VECARTISTCREDITS::const_iterator artistCredit = artistCredits.begin(); artistCredit != artistCredits.end(); ++artistCredit)
  {
    songartists.push_back(artistCredit->GetArtist());
  }
  //When artist credits have not been populated attempt to build an artist vector from the description string
  //This is a temporary fix, in the longer term other areas should query the song_artist table and populate
  //artist credits. Note that splitting the string may not give the same artists as held in the song_artist table
  if (songartists.empty() && !strArtistDesc.empty())
    songartists = StringUtils::Split(strArtistDesc, g_advancedSettings.m_musicItemSeparator);
  return songartists;
}

const std::string CSong::GetArtistSort() const
{
  //The stored artist sort name string takes precidence but a
  //value could be created from individual sort names held in artistcredits
  if (!strArtistSort.empty())
    return strArtistSort;
  std::vector<std::string> artistvector;
  for (auto artistcredit: artistCredits)
    if (!artistcredit.GetSortName().empty())
      artistvector.emplace_back(artistcredit.GetSortName());
  std::string artistString;
  if (!artistvector.empty())
    artistString = StringUtils::Join(artistvector, "; ");
  return artistString;
}

const std::vector<std::string> CSong::GetMusicBrainzArtistID() const
{
  //Get artist MusicBrainz IDs as vector from artist credits
  std::vector<std::string> musicBrainzID;
  for (VECARTISTCREDITS::const_iterator artistCredit = artistCredits.begin(); artistCredit != artistCredits.end(); ++artistCredit)
  {
    musicBrainzID.push_back(artistCredit->GetMusicBrainzArtistID());
  }
  return musicBrainzID;
}

const std::string CSong::GetArtistString() const
{
  //Artist description may be different from the artists in artistcredits (see ARTISTS tag processing)
  //but is takes precedence as a string because artistcredits is not always filled during processing
  if (!strArtistDesc.empty())
    return strArtistDesc;
  std::vector<std::string> artistvector;
  for (VECARTISTCREDITS::const_iterator i = artistCredits.begin(); i != artistCredits.end(); ++i)
    artistvector.push_back(i->GetArtist());
  std::string artistString;
  if (!artistvector.empty())
    artistString = StringUtils::Join(artistvector, g_advancedSettings.m_musicItemSeparator);
  return artistString;
}

const std::vector<int> CSong::GetArtistIDArray() const
{
  // Get song artist IDs for json rpc
  std::vector<int> artistids;
  for (VECARTISTCREDITS::const_iterator artistCredit = artistCredits.begin(); artistCredit != artistCredits.end(); ++artistCredit)
    artistids.push_back(artistCredit->GetArtistId());
  return artistids;
}

void CSong::AppendArtistRole(const CMusicRole& musicRole)
{
  m_musicRoles.push_back(musicRole);
}

bool CSong::HasArt() const
{
  if (!strThumb.empty()) return true;
  if (!embeddedArt.empty()) return true;
  return false;
}

bool CSong::ArtMatches(const CSong &right) const
{
  return (right.strThumb == strThumb &&
          embeddedArt.matches(right.embeddedArt));
}
