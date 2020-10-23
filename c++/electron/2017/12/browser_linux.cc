// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/browser.h"

#include <fcntl.h>
#include <stdlib.h>

#include "atom/browser/native_window.h"
#include "atom/browser/window_list.h"
#include "atom/common/atom_version.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/process/launch.h"
#include "brightray/common/application_info.h"

#if defined(USE_X11)
#include "chrome/browser/ui/libgtkui/gtk_util.h"
#include "chrome/browser/ui/libgtkui/unity_service.h"
#endif

namespace atom {

const char kXdgSettings[] = "xdg-settings";
const char kXdgSettingsDefaultSchemeHandler[] = "default-url-scheme-handler";

bool LaunchXdgUtility(const std::vector<std::string>& argv, int* exit_code) {
  *exit_code = EXIT_FAILURE;
  int devnull = open("/dev/null", O_RDONLY);
  if (devnull < 0) return false;

  base::LaunchOptions options;

  base::FileHandleMappingVector remap;
  remap.push_back(std::make_pair(devnull, STDIN_FILENO));
  options.fds_to_remap = &remap;

  base::Process process = base::LaunchProcess(argv, options);
  close(devnull);

  if (!process.IsValid()) return false;
  return process.WaitForExit(exit_code);
}

bool SetDefaultWebClient(const std::string& protocol) {
  std::unique_ptr<base::Environment> env(base::Environment::Create());

  std::vector<std::string> argv;
  argv.push_back(kXdgSettings);
  argv.push_back("set");
  if (!protocol.empty()) {
    argv.push_back(kXdgSettingsDefaultSchemeHandler);
    argv.push_back(protocol);
  }
  argv.push_back(libgtkui::GetDesktopName(env.get()));

  int exit_code;
  bool ran_ok = LaunchXdgUtility(argv, &exit_code);
  return ran_ok && exit_code == EXIT_SUCCESS;
}

void Browser::Focus() {
  // Focus on the first visible window.
  for (const auto& window : WindowList::GetWindows()) {
    if (window->IsVisible()) {
      window->Focus(true);
      break;
    }
  }
}

void Browser::AddRecentDocument(const base::FilePath& path) {
}

void Browser::ClearRecentDocuments() {
}

void Browser::SetAppUserModelID(const base::string16& name) {
}

bool Browser::SetAsDefaultProtocolClient(const std::string& protocol,
                                         mate::Arguments* args) {
  return SetDefaultWebClient(protocol);
}

bool Browser::IsDefaultProtocolClient(const std::string& protocol,
                                      mate::Arguments* args) {
  std::unique_ptr<base::Environment> env(base::Environment::Create());

  if (protocol.empty()) return false;

  std::vector<std::string> argv;
  argv.push_back(kXdgSettings);
  argv.push_back("check");
  argv.push_back(kXdgSettingsDefaultSchemeHandler);
  argv.push_back(protocol);
  argv.push_back(libgtkui::GetDesktopName(env.get()));

  std::string reply;
  int success_code;
  bool ran_ok = base::GetAppOutputWithExitCode(base::CommandLine(argv),
  &reply, &success_code);

  if (!ran_ok || success_code != EXIT_SUCCESS) return false;

  // Allow any reply that starts with "yes".
  return base::StartsWith(reply, "yes", base::CompareCase::SENSITIVE)
             ? true
             : false;
}

// Todo implement
bool Browser::RemoveAsDefaultProtocolClient(const std::string& protocol,
                                            mate::Arguments* args) {
  return false;
}

bool Browser::SetBadgeCount(int count) {
  if (IsUnityRunning()) {
    unity::SetDownloadCount(count);
    badge_count_ = count;
    return true;
  } else {
    return false;
  }
}

void Browser::SetLoginItemSettings(LoginItemSettings settings) {
}

Browser::LoginItemSettings Browser::GetLoginItemSettings(
    const LoginItemSettings& options) {
  return LoginItemSettings();
}

std::string Browser::GetExecutableFileVersion() const {
  return brightray::GetApplicationVersion();
}

std::string Browser::GetExecutableFileProductName() const {
  return brightray::GetApplicationName();
}

bool Browser::IsUnityRunning() {
  return unity::IsRunning();
}

}  // namespace atom
