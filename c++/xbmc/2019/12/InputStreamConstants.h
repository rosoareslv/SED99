/*
 *  Copyright (C) 2017-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#define STREAM_PROPERTY_INPUTSTREAMCLASS  "inputstreamclass" /*!< @brief the name of the inputstream add-on that should be used by Kodi to play the stream denoted by STREAM_PROPERTY_STREAMURL. Leave blank to use Kodi's built-in playing capabilities or to allow ffmpeg to handle directly set to STREAM_PROPERTY_VALUE_INPUTSTREAMFFMPEG. */
#define STREAM_PROPERTY_ISREALTIMESTREAM "isrealtimestream" /*!< @brief "true" to denote that the stream that should be played is a realtime stream. Any other value indicates that this is not a realtime stream.*/
#define STREAM_PROPERTY_VALUE_INPUTSTREAMFFMPEG  "inputstream.ffmpeg" /*!< @brief special value for STREAM_PROPERTY_INPUTSTREAMCLASS to use ffmpeg to directly play a stream URL. */
