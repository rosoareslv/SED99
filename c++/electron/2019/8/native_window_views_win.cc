// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <dwmapi.h>
#include <shellapi.h>

#include "content/public/browser/browser_accessibility_state.h"
#include "shell/browser/browser.h"
#include "shell/browser/native_window_views.h"
#include "shell/browser/ui/views/root_view.h"
#include "shell/common/atom_constants.h"
#include "ui/base/win/accessibility_misc_utils.h"
#include "ui/display/display.h"
#include "ui/display/win/screen_win.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/widget/native_widget_private.h"

// Must be included after other Windows headers.
#include <UIAutomationCoreApi.h>

namespace electron {

namespace {

// Convert Win32 WM_APPCOMMANDS to strings.
const char* AppCommandToString(int command_id) {
  switch (command_id) {
    case APPCOMMAND_BROWSER_BACKWARD:
      return kBrowserBackward;
    case APPCOMMAND_BROWSER_FORWARD:
      return kBrowserForward;
    case APPCOMMAND_BROWSER_REFRESH:
      return "browser-refresh";
    case APPCOMMAND_BROWSER_STOP:
      return "browser-stop";
    case APPCOMMAND_BROWSER_SEARCH:
      return "browser-search";
    case APPCOMMAND_BROWSER_FAVORITES:
      return "browser-favorites";
    case APPCOMMAND_BROWSER_HOME:
      return "browser-home";
    case APPCOMMAND_VOLUME_MUTE:
      return "volume-mute";
    case APPCOMMAND_VOLUME_DOWN:
      return "volume-down";
    case APPCOMMAND_VOLUME_UP:
      return "volume-up";
    case APPCOMMAND_MEDIA_NEXTTRACK:
      return "media-nexttrack";
    case APPCOMMAND_MEDIA_PREVIOUSTRACK:
      return "media-previoustrack";
    case APPCOMMAND_MEDIA_STOP:
      return "media-stop";
    case APPCOMMAND_MEDIA_PLAY_PAUSE:
      return "media-play-pause";
    case APPCOMMAND_LAUNCH_MAIL:
      return "launch-mail";
    case APPCOMMAND_LAUNCH_MEDIA_SELECT:
      return "launch-media-select";
    case APPCOMMAND_LAUNCH_APP1:
      return "launch-app1";
    case APPCOMMAND_LAUNCH_APP2:
      return "launch-app2";
    case APPCOMMAND_BASS_DOWN:
      return "bass-down";
    case APPCOMMAND_BASS_BOOST:
      return "bass-boost";
    case APPCOMMAND_BASS_UP:
      return "bass-up";
    case APPCOMMAND_TREBLE_DOWN:
      return "treble-down";
    case APPCOMMAND_TREBLE_UP:
      return "treble-up";
    case APPCOMMAND_MICROPHONE_VOLUME_MUTE:
      return "microphone-volume-mute";
    case APPCOMMAND_MICROPHONE_VOLUME_DOWN:
      return "microphone-volume-down";
    case APPCOMMAND_MICROPHONE_VOLUME_UP:
      return "microphone-volume-up";
    case APPCOMMAND_HELP:
      return "help";
    case APPCOMMAND_FIND:
      return "find";
    case APPCOMMAND_NEW:
      return "new";
    case APPCOMMAND_OPEN:
      return "open";
    case APPCOMMAND_CLOSE:
      return "close";
    case APPCOMMAND_SAVE:
      return "save";
    case APPCOMMAND_PRINT:
      return "print";
    case APPCOMMAND_UNDO:
      return "undo";
    case APPCOMMAND_REDO:
      return "redo";
    case APPCOMMAND_COPY:
      return "copy";
    case APPCOMMAND_CUT:
      return "cut";
    case APPCOMMAND_PASTE:
      return "paste";
    case APPCOMMAND_REPLY_TO_MAIL:
      return "reply-to-mail";
    case APPCOMMAND_FORWARD_MAIL:
      return "forward-mail";
    case APPCOMMAND_SEND_MAIL:
      return "send-mail";
    case APPCOMMAND_SPELL_CHECK:
      return "spell-check";
    case APPCOMMAND_MIC_ON_OFF_TOGGLE:
      return "mic-on-off-toggle";
    case APPCOMMAND_CORRECTION_LIST:
      return "correction-list";
    case APPCOMMAND_MEDIA_PLAY:
      return "media-play";
    case APPCOMMAND_MEDIA_PAUSE:
      return "media-pause";
    case APPCOMMAND_MEDIA_RECORD:
      return "media-record";
    case APPCOMMAND_MEDIA_FAST_FORWARD:
      return "media-fast-forward";
    case APPCOMMAND_MEDIA_REWIND:
      return "media-rewind";
    case APPCOMMAND_MEDIA_CHANNEL_UP:
      return "media-channel-up";
    case APPCOMMAND_MEDIA_CHANNEL_DOWN:
      return "media-channel-down";
    case APPCOMMAND_DELETE:
      return "delete";
    case APPCOMMAND_DICTATE_OR_COMMAND_CONTROL_TOGGLE:
      return "dictate-or-command-control-toggle";
    default:
      return "unknown";
  }
}

bool IsScreenReaderActive() {
  UINT screenReader = 0;
  SystemParametersInfo(SPI_GETSCREENREADER, 0, &screenReader, 0);
  return screenReader && UiaClientsAreListening();
}

// We use "enum" instead of "enum class" because we need to do bitwise compare.
enum AppbarAutohideEdge {
  TOP = 1 << 0,
  LEFT = 1 << 1,
  BOTTOM = 1 << 2,
  RIGHT = 1 << 3,
};

// The thickness of an auto-hide taskbar in pixel.
constexpr int kAutoHideTaskbarThicknessPx = 2;

// Code is copied from chrome_views_delegate_win.cc.
bool MonitorHasAutohideTaskbarForEdge(UINT edge, HMONITOR monitor) {
  APPBARDATA taskbar_data = {sizeof(APPBARDATA), NULL, 0, edge};
  taskbar_data.hWnd = ::GetForegroundWindow();

  // MSDN documents an ABM_GETAUTOHIDEBAREX, which supposedly takes a monitor
  // rect and returns autohide bars on that monitor.  This sounds like a good
  // idea for multi-monitor systems.  Unfortunately, it appears to not work at
  // least some of the time (erroneously returning NULL) and there's almost no
  // online documentation or other sample code using it that suggests ways to
  // address this problem. We do the following:-
  // 1. Use the ABM_GETAUTOHIDEBAR message. If it works, i.e. returns a valid
  //    window we are done.
  // 2. If the ABM_GETAUTOHIDEBAR message does not work we query the auto hide
  //    state of the taskbar and then retrieve its position. That call returns
  //    the edge on which the taskbar is present. If it matches the edge we
  //    are looking for, we are done.
  // NOTE: This call spins a nested run loop.
  HWND taskbar = reinterpret_cast<HWND>(
      SHAppBarMessage(ABM_GETAUTOHIDEBAR, &taskbar_data));
  if (!::IsWindow(taskbar)) {
    APPBARDATA taskbar_data = {sizeof(APPBARDATA), 0, 0, 0};
    unsigned int taskbar_state = SHAppBarMessage(ABM_GETSTATE, &taskbar_data);
    if (!(taskbar_state & ABS_AUTOHIDE))
      return false;

    taskbar_data.hWnd = ::FindWindow(L"Shell_TrayWnd", NULL);
    if (!::IsWindow(taskbar_data.hWnd))
      return false;

    SHAppBarMessage(ABM_GETTASKBARPOS, &taskbar_data);
    if (taskbar_data.uEdge == edge)
      taskbar = taskbar_data.hWnd;
  }

  // There is a potential race condition here:
  // 1. A maximized chrome window is fullscreened.
  // 2. It is switched back to maximized.
  // 3. In the process the window gets a WM_NCCACLSIZE message which calls us to
  //    get the autohide state.
  // 4. The worker thread is invoked. It calls the API to get the autohide
  //    state. On Windows versions  earlier than Windows 7, taskbars could
  //    easily be always on top or not.
  //    This meant that we only want to look for taskbars which have the topmost
  //    bit set.  However this causes problems in cases where the window on the
  //    main thread is still in the process of switching away from fullscreen.
  //    In this case the taskbar might not yet have the topmost bit set.
  // 5. The main thread resumes and does not leave space for the taskbar and
  //    hence it does not pop when hovered.
  //
  // To address point 4 above, it is best to not check for the WS_EX_TOPMOST
  // window style on the taskbar, as starting from Windows 7, the topmost
  // style is always set. We don't support XP and Vista anymore.
  if (::IsWindow(taskbar)) {
    if (MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST) == monitor)
      return true;
    // In some cases like when the autohide taskbar is on the left of the
    // secondary monitor, the MonitorFromWindow call above fails to return the
    // correct monitor the taskbar is on. We fallback to MonitorFromPoint for
    // the cursor position in that case, which seems to work well.
    POINT cursor_pos = {0};
    GetCursorPos(&cursor_pos);
    if (MonitorFromPoint(cursor_pos, MONITOR_DEFAULTTONEAREST) == monitor)
      return true;
  }
  return false;
}

int GetAppbarAutohideEdges(HWND hwnd) {
  HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
  if (!monitor)
    return 0;

  int edges = 0;
  if (MonitorHasAutohideTaskbarForEdge(ABE_LEFT, monitor))
    edges |= AppbarAutohideEdge::LEFT;
  if (MonitorHasAutohideTaskbarForEdge(ABE_TOP, monitor))
    edges |= AppbarAutohideEdge::TOP;
  if (MonitorHasAutohideTaskbarForEdge(ABE_RIGHT, monitor))
    edges |= AppbarAutohideEdge::RIGHT;
  if (MonitorHasAutohideTaskbarForEdge(ABE_BOTTOM, monitor))
    edges |= AppbarAutohideEdge::BOTTOM;
  return edges;
}

void TriggerNCCalcSize(HWND hwnd) {
  RECT rcClient;
  ::GetWindowRect(hwnd, &rcClient);

  ::SetWindowPos(hwnd, NULL, rcClient.left, rcClient.top,
                 rcClient.right - rcClient.left, rcClient.bottom - rcClient.top,
                 SWP_FRAMECHANGED);
}

}  // namespace

std::set<NativeWindowViews*> NativeWindowViews::forwarding_windows_;
HHOOK NativeWindowViews::mouse_hook_ = NULL;

void NativeWindowViews::Maximize() {
  int autohide_edges = 0;
  if (!has_frame())
    autohide_edges = GetAppbarAutohideEdges(GetAcceleratedWidget());

  // Only use Maximize() when:
  // 1. window has WS_THICKFRAME style;
  // 2. and window is not frameless when there is autohide taskbar.
  if ((::GetWindowLong(GetAcceleratedWidget(), GWL_STYLE) & WS_THICKFRAME) &&
      (has_frame() || autohide_edges == 0)) {
    if (IsVisible())
      widget()->Maximize();
    else
      widget()->native_widget_private()->Show(ui::SHOW_STATE_MAXIMIZED,
                                              gfx::Rect());
    return;
  }

  gfx::Insets insets;
  if (!has_frame()) {
    // When taskbar is autohide, we need to leave some space so the window
    // isn't treated as a "fullscreen app", which would cause the taskbars
    // to disappear.
    //
    // This trick comes from hwnd_message_handler.cc. While Chromium already
    // does this for normal window, somehow it is not applying the trick when
    // using frameless window, and we have to do it ourselves.
    float scale_factor =
        display::win::ScreenWin::GetScaleFactorForHWND(GetAcceleratedWidget());
    int thickness = std::ceil(kAutoHideTaskbarThicknessPx / scale_factor);
    if (autohide_edges & AppbarAutohideEdge::LEFT)
      insets.set_left(-thickness);
    if (autohide_edges & AppbarAutohideEdge::TOP)
      insets.set_top(-thickness);
    if (autohide_edges & AppbarAutohideEdge::RIGHT)
      insets.set_right(thickness);
    if (autohide_edges & AppbarAutohideEdge::BOTTOM)
      insets.set_bottom(thickness);
  }

  restore_bounds_ = GetBounds();
  auto display =
      display::Screen::GetScreen()->GetDisplayNearestPoint(GetPosition());
  gfx::Rect bounds = display.work_area();
  bounds.Inset(insets);
  SetBounds(bounds, false);
}

bool NativeWindowViews::ExecuteWindowsCommand(int command_id) {
  std::string command = AppCommandToString(command_id);
  NotifyWindowExecuteAppCommand(command);

  return false;
}

bool NativeWindowViews::PreHandleMSG(UINT message,
                                     WPARAM w_param,
                                     LPARAM l_param,
                                     LRESULT* result) {
  NotifyWindowMessage(message, w_param, l_param);

  switch (message) {
    // Screen readers send WM_GETOBJECT in order to get the accessibility
    // object, so take this opportunity to push Chromium into accessible
    // mode if it isn't already, always say we didn't handle the message
    // because we still want Chromium to handle returning the actual
    // accessibility object.
    case WM_GETOBJECT: {
      if (checked_for_a11y_support_)
        return false;

      const DWORD obj_id = static_cast<DWORD>(l_param);

      if (obj_id != static_cast<DWORD>(OBJID_CLIENT)) {
        return false;
      }

      if (!IsScreenReaderActive()) {
        return false;
      }

      checked_for_a11y_support_ = true;

      auto* const axState = content::BrowserAccessibilityState::GetInstance();
      if (axState && !axState->IsAccessibleBrowser()) {
        axState->OnScreenReaderDetected();
        Browser::Get()->OnAccessibilitySupportChanged();
      }

      return false;
    }
    case WM_GETMINMAXINFO: {
      // We need to handle GETMINMAXINFO ourselves because chromium tries to
      // get the scale factor of the window during it's version of this handler
      // based on the window position, which is invalid at this point. The
      // previous method of calling SetWindowPlacement fixed the window
      // position for the scale factor calculation but broke other things.
      MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(l_param);

      display::Display display =
          display::Screen::GetScreen()->GetDisplayNearestPoint(
              last_normal_placement_bounds_.origin());

      gfx::Size min_size = gfx::ScaleToCeiledSize(
          widget()->GetMinimumSize(), display.device_scale_factor());
      gfx::Size max_size = gfx::ScaleToCeiledSize(
          widget()->GetMaximumSize(), display.device_scale_factor());

      info->ptMinTrackSize.x = min_size.width();
      info->ptMinTrackSize.y = min_size.height();
      if (max_size.width() || max_size.height()) {
        if (!max_size.width())
          max_size.set_width(GetSystemMetrics(SM_CXMAXTRACK));
        if (!max_size.height())
          max_size.set_height(GetSystemMetrics(SM_CYMAXTRACK));
        info->ptMaxTrackSize.x = max_size.width();
        info->ptMaxTrackSize.y = max_size.height();
      }

      *result = 1;
      return true;
    }
    case WM_NCCALCSIZE: {
      if (!has_frame() && w_param == TRUE) {
        NCCALCSIZE_PARAMS* params =
            reinterpret_cast<NCCALCSIZE_PARAMS*>(l_param);
        RECT PROPOSED = params->rgrc[0];
        RECT BEFORE = params->rgrc[1];

        // We need to call the default to have cascade and tile windows
        // working
        // (https://github.com/rossy/borderless-window/blob/master/borderless-window.c#L239),
        // but we need to provide the proposed original value as suggested in
        // https://blogs.msdn.microsoft.com/wpfsdk/2008/09/08/custom-window-chrome-in-wpf/
        DefWindowProcW(GetAcceleratedWidget(), WM_NCCALCSIZE, w_param, l_param);

        // When fullscreen the window has no border
        int border = 0;
        if (!IsFullscreen()) {
          // When not fullscreen calculate the border size
          border = GetSystemMetrics(SM_CXFRAME) +
                   GetSystemMetrics(SM_CXPADDEDBORDER);
          if (!thick_frame_) {
            border -= GetSystemMetrics(SM_CXBORDER);
          }
        }

        if (last_window_state_ == ui::SHOW_STATE_MAXIMIZED) {
          // Position the top of the frame offset from where windows thinks by
          // exactly the border amount.  When fullscreen this is 0.
          params->rgrc[0].top = PROPOSED.top + border;
        } else {
          params->rgrc[0] = PROPOSED;
          params->rgrc[1] = BEFORE;
        }

        return true;
      } else {
        return false;
      }
    }
    case WM_COMMAND:
      // Handle thumbar button click message.
      if (HIWORD(w_param) == THBN_CLICKED)
        return taskbar_host_.HandleThumbarButtonEvent(LOWORD(w_param));
      return false;
    case WM_SIZING: {
      bool prevent_default = false;
      NotifyWindowWillResize(gfx::Rect(*reinterpret_cast<RECT*>(l_param)),
                             &prevent_default);
      if (prevent_default) {
        ::GetWindowRect(GetAcceleratedWidget(),
                        reinterpret_cast<RECT*>(l_param));
        return true;  // Tells Windows that the Sizing is handled.
      }
      return false;
    }
    case WM_SIZE: {
      // Handle window state change.
      HandleSizeEvent(w_param, l_param);
      return false;
    }
    case WM_MOVING: {
      bool prevent_default = false;
      NotifyWindowWillMove(gfx::Rect(*reinterpret_cast<RECT*>(l_param)),
                           &prevent_default);
      if (!movable_ || prevent_default) {
        ::GetWindowRect(GetAcceleratedWidget(),
                        reinterpret_cast<RECT*>(l_param));
        return true;  // Tells Windows that the Move is handled. If not true,
                      // frameless windows can be moved using
                      // -webkit-app-region: drag elements.
      }
      return false;
    }
    case WM_ENDSESSION: {
      if (w_param) {
        NotifyWindowEndSession();
      }
      return false;
    }
    case WM_PARENTNOTIFY: {
      if (LOWORD(w_param) == WM_CREATE) {
        // Because of reasons regarding legacy drivers and stuff, a window that
        // matches the client area is created and used internally by Chromium.
        // This is used when forwarding mouse messages. We only cache the first
        // occurrence (the webview window) because dev tools also cause this
        // message to be sent.
        if (!legacy_window_) {
          legacy_window_ = reinterpret_cast<HWND>(l_param);
        }
      }
      return false;
    }
    default:
      return false;
  }
}

void NativeWindowViews::HandleSizeEvent(WPARAM w_param, LPARAM l_param) {
  // Here we handle the WM_SIZE event in order to figure out what is the current
  // window state and notify the user accordingly.
  switch (w_param) {
    case SIZE_MAXIMIZED: {
      last_window_state_ = ui::SHOW_STATE_MAXIMIZED;

      if (!has_frame()) {
        TriggerNCCalcSize(GetAcceleratedWidget());
      }

      NotifyWindowMaximize();
      break;
    }
    case SIZE_MINIMIZED:
      last_window_state_ = ui::SHOW_STATE_MINIMIZED;

      WINDOWPLACEMENT wp;
      wp.length = sizeof(WINDOWPLACEMENT);

      if (GetWindowPlacement(GetAcceleratedWidget(), &wp)) {
        last_normal_placement_bounds_ = gfx::Rect(wp.rcNormalPosition);
      }

      NotifyWindowMinimize();
      break;
    case SIZE_RESTORED:
      switch (last_window_state_) {
        case ui::SHOW_STATE_MAXIMIZED:
          last_window_state_ = ui::SHOW_STATE_NORMAL;
          NotifyWindowUnmaximize();

          if (!has_frame()) {
            TriggerNCCalcSize(GetAcceleratedWidget());
          }

          break;
        case ui::SHOW_STATE_MINIMIZED:
          if (IsFullscreen()) {
            last_window_state_ = ui::SHOW_STATE_FULLSCREEN;
            NotifyWindowEnterFullScreen();
          } else {
            last_window_state_ = ui::SHOW_STATE_NORMAL;
            NotifyWindowRestore();
          }
          break;
        default:
          break;
      }
      break;
  }
}

void NativeWindowViews::SetForwardMouseMessages(bool forward) {
  if (forward && !forwarding_mouse_messages_) {
    forwarding_mouse_messages_ = true;
    forwarding_windows_.insert(this);

    // Subclassing is used to fix some issues when forwarding mouse messages;
    // see comments in |SubclassProc|.
    SetWindowSubclass(legacy_window_, SubclassProc, 1,
                      reinterpret_cast<DWORD_PTR>(this));

    if (!mouse_hook_) {
      mouse_hook_ = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, NULL, 0);
    }
  } else if (!forward && forwarding_mouse_messages_) {
    forwarding_mouse_messages_ = false;
    forwarding_windows_.erase(this);

    RemoveWindowSubclass(legacy_window_, SubclassProc, 1);

    if (forwarding_windows_.size() == 0) {
      UnhookWindowsHookEx(mouse_hook_);
      mouse_hook_ = NULL;
    }
  }
}

LRESULT CALLBACK NativeWindowViews::SubclassProc(HWND hwnd,
                                                 UINT msg,
                                                 WPARAM w_param,
                                                 LPARAM l_param,
                                                 UINT_PTR subclass_id,
                                                 DWORD_PTR ref_data) {
  NativeWindowViews* window = reinterpret_cast<NativeWindowViews*>(ref_data);
  switch (msg) {
    case WM_MOUSELEAVE: {
      // When input is forwarded to underlying windows, this message is posted.
      // If not handled, it interferes with Chromium logic, causing for example
      // mouseleave events to fire. If those events are used to exit forward
      // mode, excessive flickering on for example hover items in underlying
      // windows can occur due to rapidly entering and leaving forwarding mode.
      // By consuming and ignoring the message, we're essentially telling
      // Chromium that we have not left the window despite somebody else getting
      // the messages. As to why this is catched for the legacy window and not
      // the actual browser window is simply that the legacy window somehow
      // makes use of these events; posting to the main window didn't work.
      if (window->forwarding_mouse_messages_) {
        return 0;
      }
      break;
    }
  }

  return DefSubclassProc(hwnd, msg, w_param, l_param);
}

LRESULT CALLBACK NativeWindowViews::MouseHookProc(int n_code,
                                                  WPARAM w_param,
                                                  LPARAM l_param) {
  if (n_code < 0) {
    return CallNextHookEx(NULL, n_code, w_param, l_param);
  }

  // Post a WM_MOUSEMOVE message for those windows whose client area contains
  // the cursor since they are in a state where they would otherwise ignore all
  // mouse input.
  if (w_param == WM_MOUSEMOVE) {
    for (auto* window : forwarding_windows_) {
      // At first I considered enumerating windows to check whether the cursor
      // was directly above the window, but since nothing bad seems to happen
      // if we post the message even if some other window occludes it I have
      // just left it as is.
      RECT client_rect;
      GetClientRect(window->legacy_window_, &client_rect);
      POINT p = reinterpret_cast<MSLLHOOKSTRUCT*>(l_param)->pt;
      ScreenToClient(window->legacy_window_, &p);
      if (PtInRect(&client_rect, p)) {
        WPARAM w = 0;  // No virtual keys pressed for our purposes
        LPARAM l = MAKELPARAM(p.x, p.y);
        PostMessage(window->legacy_window_, WM_MOUSEMOVE, w, l);
      }
    }
  }

  return CallNextHookEx(NULL, n_code, w_param, l_param);
}

}  // namespace electron
