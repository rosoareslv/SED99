#pragma once
/*
 *      Copyright (C) 2017 Team Kodi
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

#include "FileItem.h"
#include "video/jobs/VideoLibraryJob.h"

/*!
 \brief Video library job implementation for resetting a resume point.
 */
class CVideoLibraryResetResumePointJob : public CVideoLibraryJob
{
public:
  /*!
   \brief Creates a new job for resetting a given item's resume point.

   \param[in] item Item for that the resume point shall be reset.
  */
  CVideoLibraryResetResumePointJob(const CFileItemPtr item);
  ~CVideoLibraryResetResumePointJob() override = default;

  const char *GetType() const override { return "CVideoLibraryResetResumePointJob"; }
  bool operator==(const CJob* job) const override;

protected:
  bool Work(CVideoDatabase &db) override;

private:
  CFileItemPtr m_item;
};
