// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ATOM_BROWSER_NATIVE_WINDOW_OBSERVER_H_
#define ATOM_BROWSER_NATIVE_WINDOW_OBSERVER_H_

#include <string>

#include "base/strings/string16.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace atom {

class NativeWindowObserver {
 public:
  virtual ~NativeWindowObserver() {}

  // Called when the web page of the window has updated it's document title.
  virtual void OnPageTitleUpdated(bool* prevent_default,
                                  const std::string& title) {}

  // Called when the web page in window wants to create a popup window.
  virtual void WillCreatePopupWindow(const base::string16& frame_name,
                                     const GURL& target_url,
                                     const std::string& partition_id,
                                     WindowOpenDisposition disposition) {}

  virtual void OnFrameRendered(scoped_ptr<uint8[]> rgb, const int size) {}

  // Called when user is starting an navigation in web page.
  virtual void WillNavigate(bool* prevent_default, const GURL& url) {}

  // Called when the window is gonna closed.
  virtual void WillCloseWindow(bool* prevent_default) {}

  // Called when the window is closed.
  virtual void OnWindowClosed() {}

  // Called when window loses focus.
  virtual void OnWindowBlur() {}

  // Called when window gains focus.
  virtual void OnWindowFocus() {}

  // Called when window state changed.
  virtual void OnWindowMaximize() {}
  virtual void OnWindowUnmaximize() {}
  virtual void OnWindowMinimize() {}
  virtual void OnWindowRestore() {}
  virtual void OnWindowResize() {}
  virtual void OnWindowMove() {}
  virtual void OnWindowMoved() {}
  virtual void OnWindowEnterFullScreen() {}
  virtual void OnWindowLeaveFullScreen() {}
  virtual void OnWindowEnterHtmlFullScreen() {}
  virtual void OnWindowLeaveHtmlFullScreen() {}

  // Redirect devtools events.
  virtual void OnDevToolsFocus() {}
  virtual void OnDevToolsOpened() {}
  virtual void OnDevToolsClosed() {}

  // Called when renderer is hung.
  virtual void OnRendererUnresponsive() {}

  // Called when renderer recovers.
  virtual void OnRendererResponsive() {}

  // Called on Windows when App Commands arrive (WM_APPCOMMAND)
  virtual void OnExecuteWindowsCommand(const std::string& command_name) {}
};

}  // namespace atom

#endif  // ATOM_BROWSER_NATIVE_WINDOW_OBSERVER_H_
