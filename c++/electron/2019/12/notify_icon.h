// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef SHELL_BROWSER_UI_WIN_NOTIFY_ICON_H_
#define SHELL_BROWSER_UI_WIN_NOTIFY_ICON_H_

#include <windows.h>  // windows.h must be included first

#include <shellapi.h>

#include <memory>
#include <string>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/win/scoped_gdi_object.h"
#include "shell/browser/ui/tray_icon.h"

namespace gfx {
class Point;
}

namespace views {
class MenuRunner;
class Widget;
}  // namespace views

namespace electron {

class NotifyIconHost;

class NotifyIcon : public TrayIcon {
 public:
  // Constructor which provides this icon's unique ID and messaging window.
  NotifyIcon(NotifyIconHost* host, UINT id, HWND window, UINT message);
  ~NotifyIcon() override;

  // Handles a click event from the user - if |left_button_click| is true and
  // there is a registered observer, passes the click event to the observer,
  // otherwise displays the context menu if there is one.
  void HandleClickEvent(int modifiers,
                        bool left_button_click,
                        bool double_button_click);

  // Handles a mouse move event from the user.
  void HandleMouseMoveEvent(int modifiers);

  // Re-creates the status tray icon now after the taskbar has been created.
  void ResetIcon();

  UINT icon_id() const { return icon_id_; }
  HWND window() const { return window_; }
  UINT message_id() const { return message_id_; }

  // Overridden from TrayIcon:
  void SetImage(HICON image) override;
  void SetPressedImage(HICON image) override;
  void SetToolTip(const std::string& tool_tip) override;
  void DisplayBalloon(const BalloonOptions& options) override;
  void RemoveBalloon() override;
  void Focus() override;
  void PopUpContextMenu(const gfx::Point& pos,
                        AtomMenuModel* menu_model) override;
  void SetContextMenu(AtomMenuModel* menu_model) override;
  gfx::Rect GetBounds() override;

 private:
  void InitIconData(NOTIFYICONDATA* icon_data);
  void OnContextMenuClosed();

  // The tray that owns us.  Weak.
  NotifyIconHost* host_;

  // The unique ID corresponding to this icon.
  UINT icon_id_;

  // Window used for processing messages from this icon.
  HWND window_;

  // The message identifier used for status icon messages.
  UINT message_id_;

  // The currently-displayed icon for the window.
  base::win::ScopedHICON icon_;

  // The context menu.
  AtomMenuModel* menu_model_ = nullptr;

  // Context menu associated with this icon (if any).
  std::unique_ptr<views::MenuRunner> menu_runner_;

  // Temporary widget for the context menu, needed for keyboard event capture.
  std::unique_ptr<views::Widget> widget_;

  // WeakPtrFactory for CloseClosure safety.
  base::WeakPtrFactory<NotifyIcon> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(NotifyIcon);
};

}  // namespace electron

#endif  // SHELL_BROWSER_UI_WIN_NOTIFY_ICON_H_
