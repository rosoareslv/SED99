// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/common/options_switches.h"

namespace atom {

namespace switches {

const char kTitle[]      = "title";
const char kIcon[]       = "icon";
const char kFrame[]      = "frame";
const char kShow[]       = "show";
const char kCenter[]     = "center";
const char kX[]          = "x";
const char kY[]          = "y";
const char kWidth[]      = "width";
const char kHeight[]     = "height";
const char kMinWidth[]   = "min-width";
const char kMinHeight[]  = "min-height";
const char kMaxWidth[]   = "max-width";
const char kMaxHeight[]  = "max-height";
const char kResizable[]  = "resizable";
const char kFullscreen[] = "fullscreen";

// Whether the window should show in taskbar.
const char kSkipTaskbar[] = "skip-taskbar";

// Start with the kiosk mode, see Opera's page for description:
// http://www.opera.com/support/mastering/kiosk/
const char kKiosk[] = "kiosk";

// Make windows stays on the top of all other windows.
const char kAlwaysOnTop[] = "always-on-top";

const char kNodeIntegration[] = "node-integration";

// Enable the NSView to accept first mouse event.
const char kAcceptFirstMouse[] = "accept-first-mouse";

// Whether window size should include window frame.
const char kUseContentSize[] = "use-content-size";

// The WebPreferences.
const char kWebPreferences[] = "web-preferences";

// The factor of which page should be zoomed.
const char kZoomFactor[] = "zoom-factor";

// The menu bar is hidden unless "Alt" is pressed.
const char kAutoHideMenuBar[] = "auto-hide-menu-bar";

// Enable window to be resized larger than screen.
const char kEnableLargerThanScreen[] = "enable-larger-than-screen";

// Forces to use dark theme on Linux.
const char kDarkTheme[] = "dark-theme";

// Enable DirectWrite on Windows.
const char kDirectWrite[] = "direct-write";

// Enable plugins.
const char kEnablePlugins[] = "enable-plugins";

// Ppapi Flash path.
const char kPpapiFlashPath[] = "ppapi-flash-path";

// Ppapi Flash version.
const char kPpapiFlashVersion[] = "ppapi-flash-version";

// Instancd ID of guest WebContents.
const char kGuestInstanceID[] = "guest-instance-id";

// Script that will be loaded by guest WebContents before other scripts.
const char kPreloadScript[] = "preload";

// Whether the window should be transparent.
const char kTransparent[] = "transparent";

// Window type hint.
const char kType[] = "type";

// Disable auto-hiding cursor.
const char kDisableAutoHideCursor[] = "disable-auto-hide-cursor";

// Use the OS X's standard window instead of the textured window.
const char kStandardWindow[] = "standard-window";

// Path to client certificate.
const char kClientCertificate[] = "client-certificate";

// Web runtime features.
const char kExperimentalFeatures[]       = "experimental-features";
const char kExperimentalCanvasFeatures[] = "experimental-canvas-features";
const char kSubpixelFontScaling[]        = "subpixel-font-scaling";
const char kOverlayScrollbars[]          = "overlay-scrollbars";
const char kOverlayFullscreenVideo[]     = "overlay-fullscreen-video";
const char kSharedWorker[]               = "shared-worker";

// Set page visiblity to always visible.
const char kPageVisibility[] = "page-visibility";

// Disable HTTP cache.
const char kDisableHttpCache[] = "disable-http-cache";

// Register schemes to standard.
const char kRegisterStandardSchemes[] = "register-standard-schemes";

// The browser process app model ID
const char kAppUserModelId[] = "app-user-model-id";

const char kOffScreenRender[] = "offscreen-render";

const char kModifiers[] = "modifiers";
const char kKeyCode[] = "keycode";

const char kMovementX[] = "movement-x";
const char kMovementY[] = "movement-y";
const char kClickCount[] = "click-count";
const char kMouseEventType[] = "type";
const char kMouseEventButton[] = "button";
const char kMouseWheelPrecise[] = "precise";

}  // namespace switches

}  // namespace atom
