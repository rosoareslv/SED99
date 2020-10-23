// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ATOM_BROWSER_NATIVE_WINDOW_MAC_H_
#define ATOM_BROWSER_NATIVE_WINDOW_MAC_H_

#import <Cocoa/Cocoa.h>

#include <string>
#include <vector>

#include "base/mac/scoped_nsobject.h"
#include "atom/browser/native_window.h"

@class AtomNSWindow;
@class AtomNSWindowDelegate;
@class FullSizeContentView;

namespace atom {

class NativeWindowMac : public NativeWindow {
 public:
  NativeWindowMac(brightray::InspectableWebContents* inspectable_web_contents,
                  const mate::Dictionary& options);
  ~NativeWindowMac() override;

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
  void SetBounds(const gfx::Rect& bounds) override;
  gfx::Rect GetBounds() override;
  void SetContentSizeConstraints(
      const extensions::SizeConstraints& size_constraints) override;
  void SetResizable(bool resizable) override;
  bool IsResizable() override;
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
  void SetRepresentedFilename(const std::string& filename) override;
  std::string GetRepresentedFilename() override;
  void SetDocumentEdited(bool edited) override;
  bool IsDocumentEdited() override;
  void SetIgnoreMouseEvents(bool ignore) override;
  bool HasModalDialog() override;
  gfx::NativeWindow GetNativeWindow() override;
  void SetProgressBar(double progress) override;
  void SetOverlayIcon(const gfx::Image& overlay,
                      const std::string& description) override;
  void ShowDefinitionForSelection() override;

  void SetVisibleOnAllWorkspaces(bool visible) override;
  bool IsVisibleOnAllWorkspaces() override;

  // Refresh the DraggableRegion views.
  void UpdateDraggableRegionViews() {
    UpdateDraggableRegionViews(draggable_regions_);
  }

  bool should_hide_native_toolbar_in_fullscreen() const {
    return should_hide_native_toolbar_in_fullscreen_;
  }

 protected:
  // NativeWindow:
  void HandleKeyboardEvent(
      content::WebContents*,
      const content::NativeWebKeyboardEvent&) override;

  // Return a vector of non-draggable regions that fill a window of size
  // |width| by |height|, but leave gaps where the window should be draggable.
  std::vector<gfx::Rect> CalculateNonDraggableRegions(
      const std::vector<DraggableRegion>& regions, int width, int height);

 private:
  // NativeWindow:
  gfx::Size ContentSizeToWindowSize(const gfx::Size& size) override;
  gfx::Size WindowSizeToContentSize(const gfx::Size& size) override;
  void UpdateDraggableRegions(
      const std::vector<DraggableRegion>& regions) override;

  void InstallView();
  void UninstallView();

  // Install the drag view, which will cover the whole window and decides
  // whehter we can drag.
  void UpdateDraggableRegionViews(const std::vector<DraggableRegion>& regions);

  base::scoped_nsobject<AtomNSWindow> window_;
  base::scoped_nsobject<AtomNSWindowDelegate> window_delegate_;

  // The view that will fill the whole frameless window.
  base::scoped_nsobject<FullSizeContentView> content_view_;

  std::vector<DraggableRegion> draggable_regions_;

  bool is_kiosk_;

  NSInteger attention_request_id_;  // identifier from requestUserAttention

  // The presentation options before entering kiosk mode.
  NSApplicationPresentationOptions kiosk_options_;

  bool should_hide_native_toolbar_in_fullscreen_;

  DISALLOW_COPY_AND_ASSIGN(NativeWindowMac);
};

}  // namespace atom

#endif  // ATOM_BROWSER_NATIVE_WINDOW_MAC_H_
