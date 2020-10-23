/*
 *      Copyright (C) 2013 Team XBMC
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

#include "GenericTouchActionHandler.h"

#include <cmath>

#include "messaging/ApplicationMessenger.h"
#include "guilib/GUIWindowManager.h"
#include "input/Key.h"
#include "windowing/WinEvents.h"

using namespace KODI::MESSAGING;

CGenericTouchActionHandler &CGenericTouchActionHandler::GetInstance()
{
  static CGenericTouchActionHandler sTouchAction;
  return sTouchAction;
}

void CGenericTouchActionHandler::OnTouchAbort()
{
  sendEvent(ACTION_GESTURE_ABORT, 0.0f, 0.0f);
}

bool CGenericTouchActionHandler::OnSingleTouchStart(float x, float y)
{
  focusControl(x, y);

  return true;
}

bool CGenericTouchActionHandler::OnSingleTouchHold(float x, float y)
{
  return true;
}

bool CGenericTouchActionHandler::OnSingleTouchMove(float x, float y, float offsetX, float offsetY, float velocityX, float velocityY)
{
  return true;
}

bool CGenericTouchActionHandler::OnSingleTouchEnd(float x, float y)
{
  return true;
}

bool CGenericTouchActionHandler::OnMultiTouchDown(float x, float y, int32_t pointer)
{
  return true;
}

bool CGenericTouchActionHandler::OnMultiTouchHold(float x, float y, int32_t pointers /* = 2 */)
{
  return true;
}

bool CGenericTouchActionHandler::OnMultiTouchMove(float x, float y, float offsetX, float offsetY, float velocityX, float velocityY, int32_t pointer)
{
  return true;
}

bool CGenericTouchActionHandler::OnMultiTouchUp(float x, float y, int32_t pointer)
{
  return true;
}

bool CGenericTouchActionHandler::OnTouchGestureStart(float x, float y)
{
  sendEvent(ACTION_GESTURE_BEGIN, x, y, 0.0f, 0.0f);

  return true;
}

bool CGenericTouchActionHandler::OnTouchGesturePan(float x, float y, float offsetX, float offsetY, float velocityX, float velocityY)
{
  sendEvent(ACTION_GESTURE_PAN, x, y, offsetX, offsetY, velocityX, velocityY);

  return true;
}

bool CGenericTouchActionHandler::OnTouchGestureEnd(float x, float y, float offsetX, float offsetY, float velocityX, float velocityY)
{
  sendEvent(ACTION_GESTURE_END, velocityX, velocityY, x, y, offsetX, offsetY);

  return true;
}

void CGenericTouchActionHandler::OnTap(float x, float y, int32_t pointers /* = 1 */)
{
  if (pointers <= 0 || pointers > 10)
    return;

  sendEvent(ACTION_TOUCH_TAP, x, y, 0.0f, 0.0f, 0.0f, 0.0f, pointers);
}

void CGenericTouchActionHandler::OnLongPress(float x, float y, int32_t pointers /* = 1 */)
{
  if (pointers <= 0 || pointers > 10)
    return;

  sendEvent(ACTION_TOUCH_LONGPRESS, x, y, 0.0f, 0.0f, 0.0f, 0.0f, pointers);
}

void CGenericTouchActionHandler::OnSwipe(TouchMoveDirection direction, float xDown, float yDown, float xUp, float yUp, float velocityX, float velocityY, int32_t pointers /* = 1 */)
{
  if (pointers <= 0 || pointers > 10)
    return;

  int actionId = 0;
  if (direction == TouchMoveDirectionLeft)
    actionId = ACTION_GESTURE_SWIPE_LEFT;
  else if (direction == TouchMoveDirectionRight)
    actionId = ACTION_GESTURE_SWIPE_RIGHT;
  else if (direction == TouchMoveDirectionUp)
    actionId = ACTION_GESTURE_SWIPE_UP;
  else if (direction == TouchMoveDirectionDown)
    actionId = ACTION_GESTURE_SWIPE_DOWN;
  else
    return;

  sendEvent(actionId, xUp, yUp, velocityX, velocityY, xDown, yDown, pointers);
}

void CGenericTouchActionHandler::OnZoomPinch(float centerX, float centerY, float zoomFactor)
{
  sendEvent(ACTION_GESTURE_ZOOM, centerX, centerY, zoomFactor, 0.0f);
}

void CGenericTouchActionHandler::OnRotate(float centerX, float centerY, float angle)
{
  sendEvent(ACTION_GESTURE_ROTATE, centerX, centerY, angle, 0.0f);
}

int CGenericTouchActionHandler::QuerySupportedGestures(float x, float y)
{
  CGUIMessage msg(GUI_MSG_GESTURE_NOTIFY, 0, 0, static_cast<int> (std::round(x)), static_cast<int> (std::round(y)));
  if (!g_windowManager.SendMessage(msg))
    return 0;

  int result = 0;
  if (msg.GetPointer())
  {
    int *p = static_cast<int*>(msg.GetPointer());
    msg.SetPointer(nullptr);
    result = *p;
    delete p;
  }
  return result;
}

void CGenericTouchActionHandler::sendEvent(int actionId, float x, float y, float x2 /* = 0.0f */, float y2 /* = 0.0f */, float x3, float y3, int pointers /* = 1 */)
{
  XBMC_Event newEvent{XBMC_TOUCH};
  
  newEvent.touch.action = actionId;
  newEvent.touch.x = x;
  newEvent.touch.y = y;
  newEvent.touch.x2 = x2;
  newEvent.touch.y2 = y2;
  newEvent.touch.x3 = x3;
  newEvent.touch.y3 = y3;
  newEvent.touch.pointers = pointers;

  CWinEvents::MessagePush(&newEvent);
}

void CGenericTouchActionHandler::focusControl(float x, float y)
{
  XBMC_Event newEvent{XBMC_SETFOCUS};

  newEvent.focus.x = static_cast<int> (std::round(x));
  newEvent.focus.y = static_cast<int> (std::round(y));

  CWinEvents::MessagePush(&newEvent);
}
