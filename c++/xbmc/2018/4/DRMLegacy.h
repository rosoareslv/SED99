/*
 *      Copyright (C) 2005-2013 Team XBMC
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

#pragma once

#include "DRMUtils.h"

class CDRMLegacy : public CDRMUtils
{
public:
  CDRMLegacy() = default;
  ~CDRMLegacy() { DestroyDrm(); };
  virtual void FlipPage(struct gbm_bo *bo, bool rendered, bool videoLayer) override;
  virtual bool SetVideoMode(RESOLUTION_INFO res, struct gbm_bo *bo) override;
  virtual bool InitDrm() override;

private:
  bool WaitingForFlip();
  bool QueueFlip(struct gbm_bo *bo);
  static void PageFlipHandler(int fd, unsigned int frame, unsigned int sec,
                              unsigned int usec, void *data);
};
