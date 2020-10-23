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

#include "Artist.h"
#include "utils/XMLUtils.h"
#include "settings/AdvancedSettings.h"

#include <algorithm>

void CArtist::MergeScrapedArtist(const CArtist& source, bool override /* = true */)
{
  /*   
  Initial scraping of artist information when the mbid is derived from tags is done directly
  using that ID, otherwise the lookup is based on name and can mis-identify the artist
  (many have same name). It is useful to store the scraped mbid, but we need to be
  able to correct any mistakes. Hence a manual refresh of artist information uses either 
  the mbid is derived from tags or the artist name, not any previously scraped mbid.

   A Musicbrainz artist ID derived from music file tags is always taken as accurate and so can
   not be overwritten by a scraped value. When the artist does not already have an mbid or has
   a previously scraped mbid, merge the new scraped value, flagging it as being from the 
   scraper rather than derived from music file tags.
   */
  if (!source.strMusicBrainzArtistID.empty() && (strMusicBrainzArtistID.empty() || bScrapedMBID))
  {
    strMusicBrainzArtistID = source.strMusicBrainzArtistID;
    bScrapedMBID = true;
  }

  if ((override && !source.strArtist.empty()) || strArtist.empty())
    strArtist = source.strArtist;

  if ((override && !source.strSortName.empty()) || strSortName.empty())
    strSortName = source.strSortName;

  genre = source.genre;
  strBiography = source.strBiography;
  styles = source.styles;
  moods = source.moods;
  instruments = source.instruments;
  strBorn = source.strBorn;
  strFormed = source.strFormed;
  strDied = source.strDied;
  strDisbanded = source.strDisbanded;
  yearsActive = source.yearsActive;
  thumbURL = source.thumbURL;
  fanart = source.fanart;
  discography = source.discography;
}


bool CArtist::Load(const TiXmlElement *artist, bool append, bool prioritise)
{
  if (!artist) return false;
  if (!append)
    Reset();

  XMLUtils::GetString(artist,                "name", strArtist);
  XMLUtils::GetString(artist, "musicBrainzArtistID", strMusicBrainzArtistID);
  XMLUtils::GetString(artist,            "sortname", strSortName);
  XMLUtils::GetStringArray(artist,       "genre", genre, prioritise, g_advancedSettings.m_musicItemSeparator);
  XMLUtils::GetStringArray(artist,       "style", styles, prioritise, g_advancedSettings.m_musicItemSeparator);
  XMLUtils::GetStringArray(artist,        "mood", moods, prioritise, g_advancedSettings.m_musicItemSeparator);
  XMLUtils::GetStringArray(artist, "yearsactive", yearsActive, prioritise, g_advancedSettings.m_musicItemSeparator);
  XMLUtils::GetStringArray(artist, "instruments", instruments, prioritise, g_advancedSettings.m_musicItemSeparator);

  XMLUtils::GetString(artist,      "born", strBorn);
  XMLUtils::GetString(artist,    "formed", strFormed);
  XMLUtils::GetString(artist, "biography", strBiography);
  XMLUtils::GetString(artist,      "died", strDied);
  XMLUtils::GetString(artist, "disbanded", strDisbanded);

  size_t iThumbCount = thumbURL.m_url.size();
  std::string xmlAdd = thumbURL.m_xml;

  const TiXmlElement* thumb = artist->FirstChildElement("thumb");
  while (thumb)
  {
    thumbURL.ParseElement(thumb);
    if (prioritise)
    {
      std::string temp;
      temp << *thumb;
      xmlAdd = temp+xmlAdd;
    }
    thumb = thumb->NextSiblingElement("thumb");
  }
  // prefix thumbs from nfos
  if (prioritise && iThumbCount && iThumbCount != thumbURL.m_url.size())
  {
    rotate(thumbURL.m_url.begin(),
           thumbURL.m_url.begin()+iThumbCount, 
           thumbURL.m_url.end());
    thumbURL.m_xml = xmlAdd;
  }
  const TiXmlElement* node = artist->FirstChildElement("album");
  while (node)
  {
    const TiXmlNode* title = node->FirstChild("title");
    if (title && title->FirstChild())
    {
      std::string strTitle = title->FirstChild()->Value();
      std::string strYear;
      const TiXmlNode* year = node->FirstChild("year");
      if (year && year->FirstChild())
        strYear = year->FirstChild()->Value();
      discography.push_back(make_pair(strTitle,strYear));
    }
    node = node->NextSiblingElement("album");
  }

  // fanart
  const TiXmlElement *fanart2 = artist->FirstChildElement("fanart");
  if (fanart2)
  {
    // we prefix to handle mixed-mode nfo's with fanart set
    if (prioritise)
    {
      std::string temp;
      temp << *fanart2;
      fanart.m_xml = temp+fanart.m_xml;
    }
    else
      fanart.m_xml << *fanart2;
    fanart.Unpack();
  }

  return true;
}

bool CArtist::Save(TiXmlNode *node, const std::string &tag, const std::string& strPath)
{
  if (!node) return false;

  // we start with a <tag> tag
  TiXmlElement artistElement(tag.c_str());
  TiXmlNode *artist = node->InsertEndChild(artistElement);

  if (!artist) return false;

  XMLUtils::SetString(artist,                      "name", strArtist);
  XMLUtils::SetString(artist,       "musicBrainzArtistID", strMusicBrainzArtistID);
  XMLUtils::SetString(artist,                  "sortname", strSortName);
  XMLUtils::SetStringArray(artist,                "genre", genre);
  XMLUtils::SetStringArray(artist,                "style", styles);
  XMLUtils::SetStringArray(artist,                 "mood", moods);
  XMLUtils::SetStringArray(artist,          "yearsactive", yearsActive);
  XMLUtils::SetStringArray(artist,          "instruments", instruments);
  XMLUtils::SetString(artist,                      "born", strBorn);
  XMLUtils::SetString(artist,                    "formed", strFormed);
  XMLUtils::SetString(artist,                 "biography", strBiography);
  XMLUtils::SetString(artist,                      "died", strDied);
  XMLUtils::SetString(artist,                 "disbanded", strDisbanded);
  if (!thumbURL.m_xml.empty())
  {
    CXBMCTinyXML doc;
    doc.Parse(thumbURL.m_xml);
    const TiXmlNode* thumb = doc.FirstChild("thumb");
    while (thumb)
    {
      artist->InsertEndChild(*thumb);
      thumb = thumb->NextSibling("thumb");
    }
  }
  XMLUtils::SetString(artist,        "path", strPath);
  if (fanart.m_xml.size())
  {
    CXBMCTinyXML doc;
    doc.Parse(fanart.m_xml);
    artist->InsertEndChild(*doc.RootElement());
  }

  // albums
  for (std::vector<std::pair<std::string,std::string> >::const_iterator it = discography.begin(); it != discography.end(); ++it)
  {
    // add a <album> tag
    TiXmlElement cast("album");
    TiXmlNode *node = artist->InsertEndChild(cast);
    TiXmlElement title("title");
    TiXmlNode *titleNode = node->InsertEndChild(title);
    TiXmlText name(it->first);
    titleNode->InsertEndChild(name);
    TiXmlElement year("year");
    TiXmlNode *yearNode = node->InsertEndChild(year);
    TiXmlText name2(it->second);
    yearNode->InsertEndChild(name2);
  }

  return true;
}

void CArtist::SetDateAdded(const std::string& strDateAdded)
{
  dateAdded.SetFromDBDateTime(strDateAdded);
}

