// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/browser.h"

#include <fcntl.h>
#include <stdlib.h>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/process/launch.h"
#include "electron/electron_version.h"
#include "shell/browser/native_window.h"
#include "shell/browser/window_list.h"
#include "shell/common/application_info.h"

#if defined(USE_X11)
#include "chrome/browser/ui/libgtkui/gtk_util.h"
#include "chrome/browser/ui/libgtkui/unity_service.h"
#endif

namespace electron {

const char kXdgSettings[] = "xdg-settings";
const char kXdgSettingsDefaultSchemeHandler[] = "default-url-scheme-handler";

bool LaunchXdgUtility(const std::vector<std::string>& argv, int* exit_code) {
  *exit_code = EXIT_FAILURE;
  int devnull = open("/dev/null", O_RDONLY);
  if (devnull < 0)
    return false;

  base::LaunchOptions options;
  options.fds_to_remap.push_back(std::make_pair(devnull, STDIN_FILENO));

  base::Process process = base::LaunchProcess(argv, options);
  close(devnull);

  if (!process.IsValid())
    return false;
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
  for (auto* const window : WindowList::GetWindows()) {
    if (window->IsVisible()) {
      window->Focus(true);
      break;
    }
  }
}

void Browser::AddRecentDocument(const base::FilePath& path) {}

void Browser::ClearRecentDocuments() {}

void Browser::SetAppUserModelID(const base::string16& name) {}

bool Browser::SetAsDefaultProtocolClient(const std::string& protocol,
                                         mate::Arguments* args) {
  return SetDefaultWebClient(protocol);
}

bool Browser::IsDefaultProtocolClient(const std::string& protocol,
                                      mate::Arguments* args) {
  std::unique_ptr<base::Environment> env(base::Environment::Create());

  if (protocol.empty())
    return false;

  std::vector<std::string> argv;
  argv.push_back(kXdgSettings);
  argv.push_back("check");
  argv.push_back(kXdgSettingsDefaultSchemeHandler);
  argv.push_back(protocol);
  argv.push_back(libgtkui::GetDesktopName(env.get()));

  std::string reply;
  int success_code;
  bool ran_ok = base::GetAppOutputWithExitCode(base::CommandLine(argv), &reply,
                                               &success_code);

  if (!ran_ok || success_code != EXIT_SUCCESS)
    return false;

  // Allow any reply that starts with "yes".
  return base::StartsWith(reply, "yes", base::CompareCase::SENSITIVE) ? true
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

void Browser::SetLoginItemSettings(LoginItemSettings settings) {}

Browser::LoginItemSettings Browser::GetLoginItemSettings(
    const LoginItemSettings& options) {
  return LoginItemSettings();
}

std::string Browser::GetExecutableFileVersion() const {
  return GetApplicationVersion();
}

std::string Browser::GetExecutableFileProductName() const {
  return GetApplicationName();
}

bool Browser::IsUnityRunning() {
  return unity::IsRunning();
}

bool Browser::IsEmojiPanelSupported() {
  return false;
}

void Browser::ShowAboutPanel() {
  const auto& opts = about_panel_options_;

  if (!opts.is_dict()) {
    LOG(WARNING) << "Called showAboutPanel(), but didn't use "
                    "setAboutPanelSettings() first";
    return;
  }

  GtkWidget* dialogWidget = gtk_about_dialog_new();
  GtkAboutDialog* dialog = GTK_ABOUT_DIALOG(dialogWidget);

  const std::string* str;
  const base::Value* val;

  if ((str = opts.FindStringKey("applicationName"))) {
    gtk_about_dialog_set_program_name(dialog, str->c_str());
  }
  if ((str = opts.FindStringKey("applicationVersion"))) {
    gtk_about_dialog_set_version(dialog, str->c_str());
  }
  if ((str = opts.FindStringKey("copyright"))) {
    gtk_about_dialog_set_copyright(dialog, str->c_str());
  }
  if ((str = opts.FindStringKey("website"))) {
    gtk_about_dialog_set_website(dialog, str->c_str());
  }
  if ((str = opts.FindStringKey("iconPath"))) {
    GError* error = nullptr;
    constexpr int width = 64;   // width of about panel icon in pixels
    constexpr int height = 64;  // height of about panel icon in pixels

    // set preserve_aspect_ratio to true
    GdkPixbuf* icon =
        gdk_pixbuf_new_from_file_at_size(str->c_str(), width, height, &error);
    if (error != nullptr) {
      g_warning("%s", error->message);
      g_clear_error(&error);
    } else {
      gtk_about_dialog_set_logo(dialog, icon);
      g_clear_object(&icon);
    }
  }

  if ((val = opts.FindListKey("authors"))) {
    std::vector<const char*> cstrs;
    for (const auto& authorVal : val->GetList()) {
      if (authorVal.is_string()) {
        cstrs.push_back(authorVal.GetString().c_str());
      }
    }
    if (cstrs.empty()) {
      LOG(WARNING) << "No author strings found in 'authors' array";
    } else {
      cstrs.push_back(nullptr);  // null-terminated char* array
      gtk_about_dialog_set_authors(dialog, cstrs.data());
    }
  }

  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialogWidget);
}

void Browser::SetAboutPanelOptions(const base::DictionaryValue& options) {
  about_panel_options_ = options.Clone();
}

}  // namespace electron
