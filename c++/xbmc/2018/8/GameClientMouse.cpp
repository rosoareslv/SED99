/*
 *  Copyright (C) 2015-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "GameClientMouse.h"
#include "GameClientInput.h"
#include "addons/kodi-addon-dev-kit/include/kodi/kodi_game_types.h"
#include "games/addons/GameClient.h"
#include "input/mouse/interfaces/IMouseInputProvider.h"
#include "input/Key.h"
#include "utils/log.h"

#include <utility>

using namespace KODI;
using namespace GAME;

CGameClientMouse::CGameClientMouse(const CGameClient &gameClient,
                                   std::string controllerId,
                                   const KodiToAddonFuncTable_Game &dllStruct,
                                   MOUSE::IMouseInputProvider *inputProvider) :
  m_gameClient(gameClient),
  m_controllerId(std::move(controllerId)),
  m_dllStruct(dllStruct),
  m_inputProvider(inputProvider)
{
  inputProvider->RegisterMouseHandler(this, false);
}

CGameClientMouse::~CGameClientMouse()
{
  m_inputProvider->UnregisterMouseHandler(this);
}

std::string CGameClientMouse::ControllerID(void) const
{
  return m_controllerId;
}

bool CGameClientMouse::OnMotion(const std::string& relpointer, int dx, int dy)
{
  // Only allow activated input in fullscreen game
  if (!m_gameClient.Input().AcceptsInput())
  {
    return false;
  }

  bool bHandled = false;

  const std::string controllerId = ControllerID();

  game_input_event event;

  event.type            = GAME_INPUT_EVENT_RELATIVE_POINTER;
  event.controller_id   = m_controllerId.c_str();
  event.port_type       = GAME_PORT_MOUSE;
  event.port_address    = ""; // Not used
  event.feature_name    = relpointer.c_str();
  event.rel_pointer.x   = dx;
  event.rel_pointer.y   = dy;

  try
  {
    bHandled = m_dllStruct.InputEvent(&event);
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "GAME: %s: exception caught in InputEvent()", m_gameClient.ID().c_str());
  }

  return bHandled;
}

bool CGameClientMouse::OnButtonPress(const std::string& button)
{
  // Only allow activated input in fullscreen game
  if (!m_gameClient.Input().AcceptsInput())
  {
    return false;
  }

  bool bHandled = false;

  game_input_event event;

  event.type                   = GAME_INPUT_EVENT_DIGITAL_BUTTON;
  event.controller_id          = m_controllerId.c_str();
  event.port_type              = GAME_PORT_MOUSE;
  event.port_address           = ""; // Not used
  event.feature_name           = button.c_str();
  event.digital_button.pressed = true;

  try
  {
    bHandled = m_dllStruct.InputEvent(&event);
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "GAME: %s: exception caught in InputEvent()", m_gameClient.ID().c_str());
  }

  return bHandled;
}

void CGameClientMouse::OnButtonRelease(const std::string& button)
{
  game_input_event event;

  event.type                   = GAME_INPUT_EVENT_DIGITAL_BUTTON;
  event.controller_id          = m_controllerId.c_str();
  event.port_type              = GAME_PORT_MOUSE;
  event.port_address           = ""; // Not used
  event.feature_name           = button.c_str();
  event.digital_button.pressed = false;

  try
  {
    m_dllStruct.InputEvent(&event);
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "GAME: %s: exception caught in InputEvent()", m_gameClient.ID().c_str());
  }
}
