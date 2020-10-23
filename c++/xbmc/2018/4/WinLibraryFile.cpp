/*
 *      Copyright (C) 2011-2013 Team XBMC
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

#include "WinLibraryFile.h"
#include "WinLibraryDirectory.h"
#include "platform/win10/AsyncHelpers.h"
#include "platform/win32/CharsetConverter.h"
#include "platform/win32/WIN32Util.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "URL.h"

#include <collection.h>
#include <string>
#include <robuffer.h>

using namespace XFILE;
using namespace KODI::PLATFORM::WINDOWS;
using namespace Windows::Foundation;
using namespace Windows::Security::Cryptography;
using namespace Windows::Storage;
using namespace Windows::Storage::AccessCache;
using namespace Windows::Storage::Search;
using namespace Windows::Storage::Streams;
using namespace Windows::Foundation::Collections;

byte* GetUnderlyingBuffer(IBuffer^ buf)
{
  if (!buf)
    return nullptr;

  Microsoft::WRL::ComPtr<IBufferByteAccess> bufferByteAccess;
  if (SUCCEEDED(reinterpret_cast<IUnknown*>(buf)->QueryInterface(IID_PPV_ARGS(&bufferByteAccess))))
  {
    byte* raw_buffer;
    if (SUCCEEDED(bufferByteAccess->Buffer(&raw_buffer)))
      return raw_buffer;
  }
  return nullptr;
}

CWinLibraryFile::CWinLibraryFile() = default;
CWinLibraryFile::~CWinLibraryFile(void) = default;

bool CWinLibraryFile::IsValid(const CURL & url)
{
  return CWinLibraryDirectory::IsValid(url)
    && !url.GetFileName().empty()
    && !URIUtils::HasSlashAtEnd(url.GetFileName(), false);
}

bool CWinLibraryFile::Open(const CURL& url)
{
  return OpenIntenal(url, FileAccessMode::Read);
}

bool CWinLibraryFile::OpenForWrite(const CURL& url, bool bOverWrite)
{
  return OpenIntenal(url, FileAccessMode::ReadWrite);
}

void CWinLibraryFile::Close()
{
  if (m_fileStream != nullptr)
  {
    // see https://docs.microsoft.com/en-us/uwp/api/windows.storage.streams.irandomaccessstream
    // m_fileStream->Close(); // where it is? 
    m_fileStream = nullptr;
  }
  if (m_sFile)
    m_sFile = nullptr;
}

ssize_t CWinLibraryFile::Read(void* lpBuf, size_t uiBufSize)
{
  if (!m_fileStream)
    return -1;

  uint32_t bytesToRead = static_cast<uint32_t>(uiBufSize);
  IBuffer^ buf = ref new Buffer(bytesToRead);
  Wait(m_fileStream->ReadAsync(buf, bytesToRead, InputStreamOptions::None));

  memcpy(lpBuf, GetUnderlyingBuffer(buf), buf->Length);
  return static_cast<intptr_t>(buf->Length);
}

ssize_t CWinLibraryFile::Write(const void* lpBuf, size_t uiBufSize)
{
  if (!m_fileStream || !m_allowWrite)
    return -1;

  auto arrByte = ref new Platform::Array<uint8_t>((uint8_t*)(lpBuf), static_cast<uint32_t>(uiBufSize));
  IBuffer^ buf = CryptographicBuffer::CreateFromByteArray(arrByte);

  uint32_t result = Wait(m_fileStream->WriteAsync(buf));
  return static_cast<intptr_t>(result);
}

int64_t CWinLibraryFile::Seek(int64_t iFilePosition, int iWhence)
{
  if (m_fileStream != nullptr)
  {
    int64_t pos = iFilePosition;
    if (iWhence == SEEK_CUR)
      pos += m_fileStream->Position;
    else if (iWhence == SEEK_END)
      pos += m_fileStream->Size;

    uint64_t seekTo;
    if (pos < 0)
      seekTo = 0;
    else if (static_cast<uint64_t>(pos) > m_fileStream->Size)
      seekTo = m_fileStream->Size;
    else
      seekTo = static_cast<uint64_t>(pos);

    m_fileStream->Seek(seekTo);
    return GetPosition();
  }
  return -1;
}

int CWinLibraryFile::Truncate(int64_t toSize)
{
  // not allowed
  return -1;
}

int64_t CWinLibraryFile::GetPosition()
{
  if (m_fileStream)
    return static_cast<int64_t>(m_fileStream->Position);

  return -1;
}

int64_t CWinLibraryFile::GetLength()
{
  if (m_fileStream)
    return m_fileStream->Size;

  return 0;
}

void CWinLibraryFile::Flush()
{
  if (m_fileStream)
    m_fileStream->FlushAsync();
}

bool CWinLibraryFile::Delete(const CURL & url)
{
  bool success = false;
  auto file = GetFile(url);
  if (file)
  {
    try
    {
      Wait(file->DeleteAsync());
    }
    catch(Platform::Exception^)
    { 
      return false;
    }
    return true;
  }
  return false;
}

bool CWinLibraryFile::Rename(const CURL & urlCurrentName, const CURL & urlNewName)
{
  if (!IsValid(urlNewName))
    return false;

  auto currFile = GetFile(urlCurrentName);
  if (currFile)
  {
    auto destFile = GetFile(urlNewName);
    if (destFile)
    {
      // replace exiting
      try
      {
        Wait(currFile->MoveAndReplaceAsync(destFile));
      }
      catch (Platform::Exception^)
      {
        return false;
      }
      return true;
    }

    // move
    CURL defFolder = CURL(urlNewName.GetWithoutFilename());
    StorageFolder^ destFolder = CWinLibraryDirectory::GetFolder(defFolder);
    if (destFolder)
    {
      try
      {
        Wait(currFile->MoveAsync(destFolder));
      }
      catch (Platform::Exception^)
      {
        return false;
      }
      return true;
    }
  }
  return false;
}

bool CWinLibraryFile::SetHidden(const CURL& url, bool hidden)
{
  return false;
}

bool CWinLibraryFile::Exists(const CURL& url)
{
  return GetFile(url) != nullptr;
}

int CWinLibraryFile::Stat(const CURL& url, struct __stat64* statData)
{
  auto file = GetFile(url);
  return Stat(file, statData);
}

int CWinLibraryFile::Stat(struct __stat64* statData)
{
  return Stat(m_sFile, statData);
}

bool CWinLibraryFile::IsInAccessList(const CURL& url)
{
  // skip local folder and installation folder
  using KODI::PLATFORM::WINDOWS::FromW;

  auto localFolder = Windows::Storage::ApplicationData::Current->LocalFolder;
  std::string path = FromW(localFolder->Path->Data(), localFolder->Path->Length());
  if (StringUtils::StartsWithNoCase(url.Get(), path))
    return false;

  auto appFolder = Windows::ApplicationModel::Package::Current->InstalledLocation;
  path = FromW(appFolder->Path->Data(), appFolder->Path->Length());
  if (StringUtils::StartsWithNoCase(url.Get(), path))
    return false;

  return IsInList(url, StorageApplicationPermissions::FutureAccessList)
      || IsInList(url, StorageApplicationPermissions::MostRecentlyUsedList);
}

bool CWinLibraryFile::OpenIntenal(const CURL &url, FileAccessMode mode)
{
  try
  {
    if (mode == FileAccessMode::Read)
    {
      m_sFile = GetFile(url);
    }
    else if (mode == FileAccessMode::ReadWrite)
    { 
      auto destFolder = CURL(URIUtils::GetParentPath(url.Get()));
      auto folder = CWinLibraryDirectory::GetFolder(destFolder);
      if (folder)
      {
        std::wstring fileNameW = ToW(url.GetFileNameWithoutPath());
        Platform::String^ strRT = ref new Platform::String(fileNameW.c_str());
        m_sFile = Wait(folder->CreateFileAsync(strRT, CreationCollisionOption::ReplaceExisting));
        if (m_sFile)
          m_allowWrite = true;
      }
    }

    if (m_sFile)
      m_fileStream = Wait(m_sFile->OpenAsync(mode));
  }
  catch (Platform::Exception^ ex)
  {
    std::string error = FromW(std::wstring(ex->Message->Data()));
    CLog::LogF(LOGERROR, "an exception occurs while openning a file '%s' (mode: %s) : %s"
                       , url.GetRedacted().c_str()
                       , mode == FileAccessMode::Read ? "r" : "rw"
                       , error.c_str());
    return false;
  }

  return m_fileStream != nullptr;
}

StorageFile^ CWinLibraryFile::GetFile(const CURL& url)
{
  // check that url is library url
  if (CWinLibraryDirectory::IsValid(url))
  {
    StorageFolder^ rootFolder = CWinLibraryDirectory::GetRootFolder(url);
    if (!rootFolder)
      return nullptr;

    std::string filePath = URIUtils::FixSlashesAndDups(url.GetFileName(), '\\');
    if (url.GetHostName() == "removable")
    {
      // here filePath has the form e\path\file.ext where first segment is 
      // drive letter, we should make path form like regular e:\path\file.ext
      auto index = filePath.find('\\');
      if (index == std::string::npos)
      {
        CLog::LogF(LOGDEBUG, "wrong file path '%s'", url.GetRedacted().c_str());
        return nullptr;
      }

      filePath = filePath.insert(index, 1, ':');
    }

    try
    {
      std::wstring wpath = ToW(filePath);
      auto item = Wait(rootFolder->TryGetItemAsync(ref new Platform::String(wpath.c_str())));
      if (item && item->IsOfType(StorageItemTypes::File))
        return dynamic_cast<StorageFile^>(item);

      return nullptr;
    }
    catch (Platform::Exception^ ex)
    {
      std::string error = FromW(std::wstring(ex->Message->Data()));
      CLog::LogF(LOGERROR, "unable to get file '%s' with error %s"
                         , url.GetRedacted().c_str()
                         , error.c_str());
    }
  }
  else if (url.GetProtocol() == "file" || url.GetProtocol().empty())
  {
    // check that a file in feature access list or most rescently used list
    // search in FAL
    IStorageItemAccessList^ list = StorageApplicationPermissions::FutureAccessList;
    Platform::String^ token = GetTokenFromList(url, list);
    if (!token || token->IsEmpty())
    {
      // serach in MRU list
      IStorageItemAccessList^ list = StorageApplicationPermissions::MostRecentlyUsedList;
      token = GetTokenFromList(url, list);
    }
    if (token && !token->IsEmpty())
      return Wait(list->GetFileAsync(token));
  }

  return nullptr;
}

bool CWinLibraryFile::IsInList(const CURL& url, IStorageItemAccessList^ list)
{
  Platform::String^ token = GetTokenFromList(url, list);
  return token != nullptr && !token->IsEmpty();
}

Platform::String^ CWinLibraryFile::GetTokenFromList(const CURL& url, IStorageItemAccessList^ list)
{
  AccessListEntryView^ listview = list->Entries;
  if (listview->Size == 0)
    return nullptr;

  using KODI::PLATFORM::WINDOWS::ToW;
  std::string filePath = url.Get();
  std::wstring filePathW = ToW(filePath);
  Platform::String^ itemKey = ref new Platform::String(filePathW.c_str());

  for (uint32_t i = 0; i < listview->Size; i++)
  {
    auto listEntry = listview->GetAt(i);
    if (listEntry.Metadata->Equals(itemKey))
    {
      return listEntry.Token;
    }
  }

  return nullptr;
}

int CWinLibraryFile::Stat(Windows::Storage::StorageFile^ file, struct __stat64* statData)
{
  if (!statData)
    return -1;

  if (!file)
    return -1;

  /* set st_gid */
  statData->st_gid = 0; // UNIX group ID is always zero on Win32
  /* set st_uid */
  statData->st_uid = 0; // UNIX user ID is always zero on Win32
  /* set st_ino */
  statData->st_ino = 0; // inode number is not implemented on Win32

  auto propertyList = ref new Platform::Collections::Vector<Platform::String^>();
  propertyList->Append("System.DateAccessed");
  propertyList->Append("System.DateCreated");
  propertyList->Append("System.DateModified");
  propertyList->Append("System.Size");

  auto requestedProps = Wait(file->Properties->RetrievePropertiesAsync(propertyList));

  auto dateAccessed = requestedProps->Lookup("System.DateAccessed");
  if (dateAccessed)
  {
    statData->st_atime = CWIN32Util::fileTimeToTimeT(static_cast<Windows::Foundation::DateTime>(dateAccessed).UniversalTime);
  }
  auto dateCreated = requestedProps->Lookup("System.DateCreated");
  if (dateCreated)
  {
    statData->st_ctime = CWIN32Util::fileTimeToTimeT(static_cast<Windows::Foundation::DateTime>(dateCreated).UniversalTime);
  }
  auto dateModified = requestedProps->Lookup("System.DateModified");
  if (dateModified)
  {
    statData->st_mtime = CWIN32Util::fileTimeToTimeT(static_cast<Windows::Foundation::DateTime>(dateModified).UniversalTime);
  }
  auto fileSize = requestedProps->Lookup("System.Size");
  if (fileSize)
  {
    /* set st_size */
    statData->st_size = static_cast<unsigned long long>(fileSize);
  }

  statData->st_dev = 0;
  statData->st_rdev = statData->st_dev;
  /* set st_nlink */
  statData->st_nlink = 1;
  /* set st_mode */
  statData->st_mode = _S_IREAD; // only read permission for file from library
  // copy user RWX rights to group rights
  statData->st_mode |= (statData->st_mode & (_S_IREAD | _S_IWRITE | _S_IEXEC)) >> 3;
  // copy user RWX rights to other rights
  statData->st_mode |= (statData->st_mode & (_S_IREAD | _S_IWRITE | _S_IEXEC)) >> 6;

  return 0;
}
