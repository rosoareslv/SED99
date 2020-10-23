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

#include "gtest/gtest.h"

#include "TestBasicEnvironment.h"
#include "TestUtils.h"

#include "threads/Thread.h"
#include "commons/ilog.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  CXBMCTestUtils::Instance().ParseArgs(argc, argv);

  if (!testing::AddGlobalTestEnvironment(new TestBasicEnvironment()))
  {
    fprintf(stderr, "Unable to add basic test environment.\n");
    exit(EXIT_FAILURE);
  }
  int ret = RUN_ALL_TESTS();

  return ret;
}
