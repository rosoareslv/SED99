// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This interface is for managing the global services of the application. Each
// service is lazily created when requested the first time. The service getters
// will return NULL if the service is not available, so callers must check for
// this condition.

#ifndef ATOM_BROWSER_BROWSER_PROCESS_IMPL_H_
#define ATOM_BROWSER_BROWSER_PROCESS_IMPL_H_

#include <memory>
#include <string>

#include "atom/browser/io_thread.h"
#include "atom/browser/net/system_network_context_manager.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "chrome/browser/browser_process.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/value_map_pref_store.h"
#include "printing/buildflags/buildflags.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace net_log {
class ChromeNetLog;
}

namespace printing {
class PrintJobManager;
}

// Empty definition for std::unique_ptr
class BackgroundModeManager {};

// NOT THREAD SAFE, call only from the main thread.
// These functions shouldn't return NULL unless otherwise noted.
class BrowserProcessImpl : public BrowserProcess {
 public:
  BrowserProcessImpl();
  ~BrowserProcessImpl() override;

  static void ApplyProxyModeFromCommandLine(ValueMapPrefStore* pref_store);

  void PostEarlyInitialization();
  void PreCreateThreads(const base::CommandLine& command_line);
  void PostDestroyThreads();
  void PostMainMessageLoopRun();

  void ResourceDispatcherHostCreated() override {}
  void EndSession() override {}
  void FlushLocalStateAndReply(base::OnceClosure reply) override {}
  bool IsShuttingDown() override;

  metrics_services_manager::MetricsServicesManager* GetMetricsServicesManager()
      override;
  metrics::MetricsService* metrics_service() override;
  rappor::RapporServiceImpl* rappor_service() override;
  ProfileManager* profile_manager() override;
  PrefService* local_state() override;
  net::URLRequestContextGetter* system_request_context() override;
  scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory()
      override;
  variations::VariationsService* variations_service() override;
  BrowserProcessPlatformPart* platform_part() override;
  extensions::EventRouterForwarder* extension_event_router_forwarder() override;
  NotificationUIManager* notification_ui_manager() override;
  NotificationPlatformBridge* notification_platform_bridge() override;
  IOThread* io_thread() override;
  SystemNetworkContextManager* system_network_context_manager() override;
  network::NetworkQualityTracker* network_quality_tracker() override;
  WatchDogThread* watchdog_thread() override;
  policy::ChromeBrowserPolicyConnector* browser_policy_connector() override;
  policy::PolicyService* policy_service() override;
  IconManager* icon_manager() override;
  GpuModeManager* gpu_mode_manager() override;
  printing::PrintPreviewDialogController* print_preview_dialog_controller()
      override;
  printing::BackgroundPrintingManager* background_printing_manager() override;
  IntranetRedirectDetector* intranet_redirect_detector() override;
  DownloadStatusUpdater* download_status_updater() override;
  DownloadRequestLimiter* download_request_limiter() override;
  BackgroundModeManager* background_mode_manager() override;
  StatusTray* status_tray() override;
  safe_browsing::SafeBrowsingService* safe_browsing_service() override;
  safe_browsing::ClientSideDetectionService* safe_browsing_detection_service()
      override;
  subresource_filter::RulesetService* subresource_filter_ruleset_service()
      override;
  optimization_guide::OptimizationGuideService* optimization_guide_service()
      override;
  net_log::ChromeNetLog* net_log() override;
  component_updater::ComponentUpdateService* component_updater() override;
  component_updater::SupervisedUserWhitelistInstaller*
  supervised_user_whitelist_installer() override;
  MediaFileSystemRegistry* media_file_system_registry() override;
  WebRtcLogUploader* webrtc_log_uploader() override;
  network_time::NetworkTimeTracker* network_time_tracker() override;
  gcm::GCMDriver* gcm_driver() override;
  resource_coordinator::ResourceCoordinatorParts* resource_coordinator_parts()
      override;
  resource_coordinator::TabManager* GetTabManager() override;
  shell_integration::DefaultWebClientState CachedDefaultWebClientState()
      override;
  prefs::InProcessPrefServiceFactory* pref_service_factory() const override;
  void CreateDevToolsProtocolHandler() override {}
  void CreateDevToolsAutoOpener() override {}
  void set_background_mode_manager_for_test(
      std::unique_ptr<BackgroundModeManager> manager) override {}
#if (defined(OS_WIN) || defined(OS_LINUX))
  void StartAutoupdateTimer() override {}
#endif
  void SetApplicationLocale(const std::string& locale) override;
  const std::string& GetApplicationLocale() override;
  printing::PrintJobManager* print_job_manager() override;

 private:
#if BUILDFLAG(ENABLE_PRINTING)
  std::unique_ptr<printing::PrintJobManager> print_job_manager_;
#endif
  std::unique_ptr<PrefService> local_state_;
  std::unique_ptr<IOThread> io_thread_;
  std::unique_ptr<net_log::ChromeNetLog> net_log_;
  std::string locale_;

  DISALLOW_COPY_AND_ASSIGN(BrowserProcessImpl);
};

#endif  // ATOM_BROWSER_BROWSER_PROCESS_IMPL_H_
