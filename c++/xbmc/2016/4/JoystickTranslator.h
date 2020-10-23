/*
 *      Copyright (C) 2015-2016 Team Kodi
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
 *  along with this Program; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */
#pragma once

#include "JoystickTypes.h"

namespace JOYSTICK
{
  class CJoystickTranslator
  {
  public:
    /*!
     * \brief Translate a hat state to a string representation
     *
     * \param state The hat state
     *
     * \return A capitalized string representation, or "RELEASED" if the hat is centered.
     */
    static const char* HatStateToString(HAT_STATE state);

    /*!
     * \brief Get the semi-axis direction containing the specified position
     *
     * \param position The position of the axis
     *
     * \return POSITIVE, NEGATIVE, or UNKNOWN if position is 0
     */
    static SEMIAXIS_DIRECTION PositionToSemiAxisDirection(float position);

    /*!
     * \brief Get the closest cardinal direction to the given vector
     *
     * Ties are resolved in the clockwise direction: (0.5, 0.5) will resolve to
     * RIGHT.
     *
     * \param x  The x component of the vector
     * \param y  The y component of the vector
     *
     * \return The closest cardinal directon (up, down, right or left), or unknown
     *         if x and y are both 0.
     */
    static CARDINAL_DIRECTION VectorToCardinalDirection(float x, float y);
  };
}
