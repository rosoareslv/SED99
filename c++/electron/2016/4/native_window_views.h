// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ATOM_BROWSER_NATIVE_WINDOW_VIEWS_H_
#define ATOM_BROWSER_NATIVE_WINDOW_VIEWS_H_

#include "atom/browser/native_window.h"

#include <string>
#include <vector>

#include "atom/browser/ui/accelerator_util.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"

#if defined(OS_WIN)
#include "atom/browser/ui/win/message_handler_delegate.h"
#include "atom/browser/ui/win/taskbar_host.h"
#endif

namespace views {
class UnhandledKeyboardEventHandler;
}

namespace atom {

class GlobalMenuBarX11;
class MenuBar;
class WindowStateWatcher;

#if defined(OS_WIN)
class AtomDesktopWindowTreeHostWin;
#endif

class NativeWindowViews : public NativeWindow,
#if defined(OS_WIN)
                          public MessageHandlerDelegate,
#endif
                          public views::WidgetDelegateView,
                          public views::WidgetObserver {
 public:
  NativeWindowViews(brightray::InspectableWebContents* inspectable_web_contents,
                    const mate::Dictionary& options);
  ~NativeWindowViews() override;

  // NativeWindow:
  void Close() override;
  void CloseImmediately() override;
  void Focus(bool focus) override;
  bool IsFocused() override;
  void Show() override;
  void ShowInactive() override;
  void Hide() override;
  bool IsVisible() override;
  void Maximize() override;
  void Unmaximize() override;
  bool IsMaximized() override;
  void Minimize() override;
  void Restore() override;
  bool IsMinimized() override;
  void SetFullScreen(bool fullscreen) override;
  bool IsFullscreen() const override;
  void SetBounds(const gfx::Rect& bounds, bool animate) override;
  gfx::Rect GetBounds() override;
  gfx::Size GetContentSize() override;
  void SetContentSizeConstraints(
      const extensions::SizeConstraints& size_constraints) override;
  void SetResizable(bool resizable) override;
  bool IsResizable() override;
  void SetMovable(bool movable) override;
  bool IsMovable() override;
  void SetMinimizable(bool minimizable) override;
  bool IsMinimizable() override;
  void SetMaximizable(bool maximizable) override;
  bool IsMaximizable() override;
  void SetFullScreenable(bool fullscreenable) override;
  bool IsFullScreenable() override;
  void SetClosable(bool closable) override;
  bool IsClosable() override;
  void SetAlwaysOnTop(bool top) override;
  bool IsAlwaysOnTop() override;
  void Center() override;
  void SetTitle(const std::string& title) override;
  std::string GetTitle() override;
  void FlashFrame(bool flash) override;
  void SetSkipTaskbar(bool skip) override;
  void SetKiosk(bool kiosk) override;
  bool IsKiosk() override;
  void SetBackgroundColor(const std::string& color_name) override;
  void SetHasShadow(bool has_shadow) override;
  bool HasShadow() override;
  void SetMenu(ui::MenuModel* menu_model) override;
  gfx::NativeWindow GetNativeWindow() override;
  void SetOverlayIcon(const gfx::Image& overlay,
                      const std::string& description) override;
  void SetProgressBar(double value) override;
  void SetAutoHideMenuBar(bool auto_hide) override;
  bool IsMenuBarAutoHide() override;
  void SetMenuBarVisibility(bool visible) override;
  bool IsMenuBarVisible() override;
  void SetVisibleOnAllWorkspaces(bool visible) override;
  bool IsVisibleOnAllWorkspaces() override;

  gfx::AcceleratedWidget GetAcceleratedWidget() override;

  views::Widget* widget() const { return window_.get(); }

#if defined(OS_WIN)
  TaskbarHost& taskbar_host() { return taskbar_host_; }
#endif

 private:
  // views::WidgetObserver:
  void OnWidgetActivationChanged(
      views::Widget* widget, bool active) override;
  void OnWidgetBoundsChanged(
      views::Widget* widget, const gfx::Rect& bounds) override;

  // views::WidgetDelegate:
  void DeleteDelegate() override;
  views::View* GetInitiallyFocusedView() override;
  bool CanResize() const override;
  bool CanMaximize() const override;
  bool CanMinimize() const override;
  base::string16 GetWindowTitle() const override;
  bool ShouldHandleSystemCommands() const override;
  gfx::ImageSkia GetWindowAppIcon() override;
  gfx::ImageSkia GetWindowIcon() override;
  views::Widget* GetWidget() override;
  const views::Widget* GetWidget() const override;
  views::View* GetContentsView() override;
  bool ShouldDescendIntoChildForEventHandling(
     gfx::NativeView child,
     const gfx::Point& location) override;
  views::ClientView* CreateClientView(views::Widget* widget) override;
  views::NonClientFrameView* CreateNonClientFrameView(
      views::Widget* widget) override;
  void OnWidgetMove() override;
#if defined(OS_WIN)
  bool ExecuteWindowsCommand(int command_id) override;
#endif

#if defined(OS_WIN)
  // MessageHandlerDelegate:
  bool PreHandleMSG(
      UINT message, WPARAM w_param, LPARAM l_param, LRESULT* result) override;

  void HandleSizeEvent(WPARAM w_param, LPARAM l_param);
#endif

  // NativeWindow:
  gfx::Size ContentSizeToWindowSize(const gfx::Size& size) override;
  gfx::Size WindowSizeToContentSize(const gfx::Size& size) override;
  void HandleKeyboardEvent(
      content::WebContents*,
      const content::NativeWebKeyboardEvent& event) override;

  // views::View:
  gfx::Size GetMinimumSize() override;
  gfx::Size GetMaximumSize() override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;

  // Register accelerators supported by the menu model.
  void RegisterAccelerators(ui::MenuModel* menu_model);

  // Returns the restore state for the window.
  ui::WindowShowState GetRestoredState();

  scoped_ptr<views::Widget> window_;
  views::View* web_view_;  // Managed by inspectable_web_contents_.

  scoped_ptr<MenuBar> menu_bar_;
  bool menu_bar_autohide_;
  bool menu_bar_visible_;
  bool menu_bar_alt_pressed_;

#if defined(USE_X11)
  scoped_ptr<GlobalMenuBarX11> global_menu_bar_;

  // Handles window state events.
  scoped_ptr<WindowStateWatcher> window_state_watcher_;

  // The "resizable" flag on Linux is implemented by setting size constraints,
  // we need to make sure size constraints are restored when window becomes
  // resizable again.
  extensions::SizeConstraints old_size_constraints_;
#elif defined(OS_WIN)
  // Weak ref.
  AtomDesktopWindowTreeHostWin* atom_desktop_window_tree_host_win_;

  ui::WindowShowState last_window_state_;

  // There's an issue with restore on Windows, that sometimes causes the Window
  // to receive the wrong size (#2498). To circumvent that, we keep tabs on the
  // size of the window while in the normal state (not maximized, minimized or
  // fullscreen), so we restore it correctly.
  gfx::Size last_normal_size_;

  // In charge of running taskbar related APIs.
  TaskbarHost taskbar_host_;

  // If true we have enabled a11y
  bool enabled_a11y_support_;

#endif

  // Handles unhandled keyboard messages coming back from the renderer process.
  scoped_ptr<views::UnhandledKeyboardEventHandler> keyboard_event_handler_;

  // Map from accelerator to menu item's command id.
  accelerator_util::AcceleratorTable accelerator_table_;

  bool use_content_size_;
  bool movable_;
  bool resizable_;
  bool maximizable_;
  bool minimizable_;
  bool fullscreenable_;
  std::string title_;
  gfx::Size widget_size_;

  DISALLOW_COPY_AND_ASSIGN(NativeWindowViews);
};

}  // namespace atom

#endif  // ATOM_BROWSER_NATIVE_WINDOW_VIEWS_H_
