/*
 *      Copyright (C) 2014-2017 Team Kodi
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

#include "GUIConfigurationWizard.h"
#include "games/controllers/dialogs/GUIDialogAxisDetection.h"
#include "games/controllers/guicontrols/GUIFeatureButton.h"
#include "games/controllers/Controller.h"
#include "games/controllers/ControllerFeature.h"
#include "input/joysticks/IButtonMap.h"
#include "input/joysticks/IButtonMapCallback.h"
#include "input/joysticks/JoystickUtils.h"
#include "input/keyboard/KeymapActionMap.h"
#include "input/InputManager.h"
#include "input/IKeymap.h"
#include "peripherals/Peripherals.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "ServiceBroker.h"

using namespace KODI;
using namespace GAME;

#define ESC_KEY_CODE  27
#define SKIPPING_DETECTION_MS  200

// Duration to wait for axes to neutralize after mapping is finished
#define POST_MAPPING_WAIT_TIME_MS  (5 * 1000)

CGUIConfigurationWizard::CGUIConfigurationWizard(bool bEmulation, unsigned int controllerNumber /* = 0 */) :
  CThread("GUIConfigurationWizard"),
  m_bEmulation(bEmulation),
  m_controllerNumber(controllerNumber),
  m_actionMap(new KEYBOARD::CKeymapActionMap)
{
  InitializeState();
}

CGUIConfigurationWizard::~CGUIConfigurationWizard(void) = default;

void CGUIConfigurationWizard::InitializeState(void)
{
  m_currentButton = nullptr;
  m_currentDirection = JOYSTICK::ANALOG_STICK_DIRECTION::UNKNOWN;
  m_history.clear();
  m_lateAxisDetected = false;
  m_deviceName.clear();
}

void CGUIConfigurationWizard::Run(const std::string& strControllerId, const std::vector<IFeatureButton*>& buttons)
{
  Abort();

  {
    CSingleLock lock(m_stateMutex);

    // Set Run() parameters
    m_strControllerId = strControllerId;
    m_buttons = buttons;

    // Reset synchronization variables
    m_inputEvent.Reset();
    m_motionlessEvent.Reset();
    m_bInMotion.clear();

    // Initialize state variables
    InitializeState();
  }

  Create();
}

void CGUIConfigurationWizard::OnUnfocus(IFeatureButton* button)
{
  CSingleLock lock(m_stateMutex);

  if (button == m_currentButton)
    Abort(false);
}

bool CGUIConfigurationWizard::Abort(bool bWait /* = true */)
{
  if (!m_bStop)
  {
    StopThread(false);

    m_inputEvent.Set();
    m_motionlessEvent.Set();

    if (bWait)
      StopThread(true);

    return true;
  }
  return false;
}

void CGUIConfigurationWizard::Process(void)
{
  CLog::Log(LOGDEBUG, "Starting configuration wizard");

  InstallHooks();

  bool bLateAxisDetected = false;

  {
    CSingleLock lock(m_stateMutex);
    for (IFeatureButton* button : m_buttons)
    {
      // Allow other threads to access the button we're using
      m_currentButton = button;

      while (!button->IsFinished())
      {
        // Allow other threads to access which direction the analog stick is on
        m_currentDirection = button->GetDirection();

        // Wait for input
        {
          CSingleExit exit(m_stateMutex);

          CLog::Log(LOGDEBUG, "%s: Waiting for input for feature \"%s\"", m_strControllerId.c_str(), button->Feature().Name().c_str());

          if (!button->PromptForInput(m_inputEvent))
            Abort(false);
        }

        if (m_bStop)
          break;
      }

      button->Reset();

      if (m_bStop)
        break;
    }

    bLateAxisDetected = m_lateAxisDetected;

    // Finished mapping
    InitializeState();
  }

  for (auto callback : ButtonMapCallbacks())
    callback.second->SaveButtonMap();

  if (bLateAxisDetected)
  {
    CGUIDialogAxisDetection dialog;
    dialog.Show();
  }
  else
  {
    // Wait for motion to stop to avoid sending analog actions for the button
    // that is pressed immediately after button mapping finishes.
    bool bInMotion;

    {
      CSingleLock lock(m_motionMutex);
      bInMotion = !m_bInMotion.empty();
    }

    if (bInMotion)
    {
      CLog::Log(LOGDEBUG, "Configuration wizard: waiting %ums for axes to neutralize", POST_MAPPING_WAIT_TIME_MS);
      m_motionlessEvent.WaitMSec(POST_MAPPING_WAIT_TIME_MS);
    }
  }

  RemoveHooks();

  CLog::Log(LOGDEBUG, "Configuration wizard ended");
}

bool CGUIConfigurationWizard::MapPrimitive(JOYSTICK::IButtonMap* buttonMap,
                                           IKeymap* keymap,
                                           const JOYSTICK::CDriverPrimitive& primitive)
{
  using namespace JOYSTICK;

  bool bHandled = false;

  // Handle esc key separately
  if (!m_deviceName.empty() && m_deviceName != buttonMap->DeviceName())
  {
    bool bIsCancelAction = false;

    //! @todo This only succeeds for game.controller.default; no actions are
    //        currently defined for other controllers
    if (keymap)
    {
      std::string feature;
      if (buttonMap->GetFeature(primitive, feature))
      {
        const auto &actions = keymap->GetActions(CJoystickUtils::MakeKeyName(feature));
        if (!actions.empty())
        {
          //! @todo Handle multiple actions mapped to the same key
          switch (actions.begin()->actionId)
          {
          case ACTION_NAV_BACK:
          case ACTION_PREVIOUS_MENU:
            bIsCancelAction = true;
            break;
          default:
            break;
          }
        }
      }
    }

    if (bIsCancelAction)
    {
      CLog::Log(LOGDEBUG, "%s: device \"%s\" is cancelling prompt", buttonMap->ControllerID().c_str(), buttonMap->DeviceName().c_str());
      Abort(false);
    }
    else
      CLog::Log(LOGDEBUG, "%s: ignoring input for device \"%s\"", buttonMap->ControllerID().c_str(), buttonMap->DeviceName().c_str());

    // Discard input
    bHandled = true;
  }
  else if (primitive.Type() == PRIMITIVE_TYPE::BUTTON &&
           primitive.Index() == ESC_KEY_CODE)
  {
    // Handle esc key
    bHandled = Abort(false);
  }
  else if (m_history.find(primitive) != m_history.end())
  {
    // Primitive has already been mapped this round, ignore it
    bHandled = true;
  }
  else if (buttonMap->IsIgnored(primitive))
  {
    bHandled = true;
  }
  else
  {
    // Get the current state of the thread
    IFeatureButton* currentButton;
    ANALOG_STICK_DIRECTION currentDirection;
    {
      CSingleLock lock(m_stateMutex);
      currentButton = m_currentButton;
      currentDirection = m_currentDirection;
    }

    if (currentButton)
    {
      const CControllerFeature& feature = currentButton->Feature();

      CLog::Log(LOGDEBUG, "%s: mapping feature \"%s\" for device %s",
        m_strControllerId.c_str(), feature.Name().c_str(), buttonMap->DeviceName().c_str());

      switch (feature.Type())
      {
        case FEATURE_TYPE::SCALAR:
        {
          buttonMap->AddScalar(feature.Name(), primitive);
          bHandled = true;
          break;
        }
        case FEATURE_TYPE::ANALOG_STICK:
        {
          buttonMap->AddAnalogStick(feature.Name(), currentDirection, primitive);
          bHandled = true;
          break;
        }
        case FEATURE_TYPE::RELPOINTER:
        {
          buttonMap->AddRelativePointer(feature.Name(), currentDirection, primitive);
          bHandled = true;
          break;
        }
        default:
          break;
      }

      if (bHandled)
      {
        m_history.insert(primitive);

        OnMotion(buttonMap);
        m_inputEvent.Set();
        m_deviceName = buttonMap->DeviceName();
      }
    }
  }
  
  return bHandled;
}

void CGUIConfigurationWizard::OnEventFrame(const JOYSTICK::IButtonMap* buttonMap, bool bMotion)
{
  CSingleLock lock(m_motionMutex);

  if (m_bInMotion.find(buttonMap) != m_bInMotion.end() && !bMotion)
    OnMotionless(buttonMap);
}

void CGUIConfigurationWizard::OnLateAxis(const JOYSTICK::IButtonMap* buttonMap, unsigned int axisIndex)
{
  CSingleLock lock(m_stateMutex);

  m_lateAxisDetected = true;
  Abort(false);
}

void CGUIConfigurationWizard::OnMotion(const JOYSTICK::IButtonMap* buttonMap)
{
  CSingleLock lock(m_motionMutex);

  m_motionlessEvent.Reset();
  m_bInMotion.insert(buttonMap);
}

void CGUIConfigurationWizard::OnMotionless(const JOYSTICK::IButtonMap* buttonMap)
{
  m_bInMotion.erase(buttonMap);
  if (m_bInMotion.empty())
    m_motionlessEvent.Set();
}

bool CGUIConfigurationWizard::OnKeyPress(const CKey& key)
{
  using namespace KEYBOARD;

  bool bHandled = false;

  if (!m_bStop)
  {
    switch (m_actionMap->GetActionID(key))
    {
    case ACTION_MOVE_LEFT:
    case ACTION_MOVE_RIGHT:
    case ACTION_MOVE_UP:
    case ACTION_MOVE_DOWN:
    case ACTION_PAGE_UP:
    case ACTION_PAGE_DOWN:
      // Abort and allow motion
      Abort(false);
      bHandled = false;
      break;

    case ACTION_PARENT_DIR:
    case ACTION_PREVIOUS_MENU:
    case ACTION_STOP:
      // Abort and prevent action
      Abort(false);
      bHandled = true;
      break;

    default:
      // Absorb keypress
      bHandled = true;
      break;
    }
  }

  return bHandled;
}

bool CGUIConfigurationWizard::OnButtonPress(const std::string& button)
{
  return Abort(false);
}

void CGUIConfigurationWizard::InstallHooks(void)
{
  CServiceBroker::GetPeripherals().RegisterJoystickButtonMapper(this);
  CServiceBroker::GetPeripherals().RegisterObserver(this);

  // If we're not using emulation, allow keyboard input to abort prompt
  if (!m_bEmulation)
    CServiceBroker::GetInputManager().RegisterKeyboardHandler(this);

  CServiceBroker::GetInputManager().RegisterMouseHandler(this);
}

void CGUIConfigurationWizard::RemoveHooks(void)
{
  CServiceBroker::GetInputManager().UnregisterMouseHandler(this);

  if (!m_bEmulation)
    CServiceBroker::GetInputManager().UnregisterKeyboardHandler(this);

  CServiceBroker::GetPeripherals().UnregisterObserver(this);
  CServiceBroker::GetPeripherals().UnregisterJoystickButtonMapper(this);
}

void CGUIConfigurationWizard::Notify(const Observable& obs, const ObservableMessage msg)
{
  switch (msg)
  {
    case ObservableMessagePeripheralsChanged:
    {
      CServiceBroker::GetPeripherals().UnregisterJoystickButtonMapper(this);
      CServiceBroker::GetPeripherals().RegisterJoystickButtonMapper(this);
      break;
    }
    default:
      break;
  }
}
