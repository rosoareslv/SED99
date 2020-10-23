// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/atom_browser_context.h"

#include <utility>

#include "atom/browser/atom_blob_reader.h"
#include "atom/browser/atom_browser_main_parts.h"
#include "atom/browser/atom_download_manager_delegate.h"
#include "atom/browser/atom_paths.h"
#include "atom/browser/atom_permission_manager.h"
#include "atom/browser/cookie_change_notifier.h"
#include "atom/browser/net/resolve_proxy_helper.h"
#include "atom/browser/pref_store_delegate.h"
#include "atom/browser/special_storage_policy.h"
#include "atom/browser/ui/inspectable_web_contents_impl.h"
#include "atom/browser/web_view_manager.h"
#include "atom/browser/zoom_level_delegate.h"
#include "atom/common/application_info.h"
#include "atom/common/options_switches.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/prefs/json_pref_store.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_service_factory.h"
#include "components/prefs/value_map_pref_store.h"
#include "components/proxy_config/pref_proxy_config_tracker_impl.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "content/browser/blob_storage/chrome_blob_storage_context.h"  // nogncheck
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/escape.h"
#include "services/network/public/cpp/features.h"

using content::BrowserThread;

namespace atom {

namespace {

// Convert string to lower case and escape it.
std::string MakePartitionName(const std::string& input) {
  return net::EscapePath(base::ToLowerASCII(input));
}

}  // namespace

// static
AtomBrowserContext::BrowserContextMap AtomBrowserContext::browser_context_map_;

AtomBrowserContext::AtomBrowserContext(const std::string& partition,
                                       bool in_memory,
                                       const base::DictionaryValue& options)
    : base::RefCountedDeleteOnSequence<AtomBrowserContext>(
          base::ThreadTaskRunnerHandle::Get()),
      in_memory_pref_store_(nullptr),
      storage_policy_(new SpecialStoragePolicy),
      in_memory_(in_memory),
      weak_factory_(this) {
  user_agent_ = GetApplicationUserAgent();

  // Read options.
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  use_cache_ = !command_line->HasSwitch(switches::kDisableHttpCache);
  options.GetBoolean("cache", &use_cache_);

  base::StringToInt(command_line->GetSwitchValueASCII(switches::kDiskCacheSize),
                    &max_cache_size_);

  if (!base::PathService::Get(DIR_USER_DATA, &path_)) {
    base::PathService::Get(DIR_APP_DATA, &path_);
    path_ = path_.Append(base::FilePath::FromUTF8Unsafe(GetApplicationName()));
    base::PathService::Override(DIR_USER_DATA, path_);
  }

  if (!in_memory && !partition.empty())
    path_ = path_.Append(FILE_PATH_LITERAL("Partitions"))
                .Append(base::FilePath::FromUTF8Unsafe(
                    MakePartitionName(partition)));

  content::BrowserContext::Initialize(this, path_);

  // Initialize Pref Registry.
  InitPrefs();

  if (!base::FeatureList::IsEnabled(network::features::kNetworkService)) {
    proxy_config_monitor_ = std::make_unique<ProxyConfigMonitor>(prefs_.get());
    io_handle_ =
        new URLRequestContextGetter::Handle(weak_factory_.GetWeakPtr());
  }

  cookie_change_notifier_ = std::make_unique<CookieChangeNotifier>(this);

  BrowserContextDependencyManager::GetInstance()->MarkBrowserContextLive(this);
}

AtomBrowserContext::~AtomBrowserContext() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  NotifyWillBeDestroyed(this);
  ShutdownStoragePartitions();

  BrowserThread::DeleteSoon(BrowserThread::IO, FROM_HERE,
                            std::move(resource_context_));

  if (!base::FeatureList::IsEnabled(network::features::kNetworkService))
    io_handle_->ShutdownOnUIThread();

  // Notify any keyed services of browser context destruction.
  BrowserContextDependencyManager::GetInstance()->DestroyBrowserContextServices(
      this);
}

void AtomBrowserContext::InitPrefs() {
  auto prefs_path = GetPath().Append(FILE_PATH_LITERAL("Preferences"));
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  PrefServiceFactory prefs_factory;
  scoped_refptr<JsonPrefStore> pref_store =
      base::MakeRefCounted<JsonPrefStore>(prefs_path);
  pref_store->ReadPrefs();  // Synchronous.
  prefs_factory.set_user_prefs(pref_store);

  auto registry = WrapRefCounted(new PrefRegistrySimple);

  registry->RegisterFilePathPref(prefs::kSelectFileLastDirectory,
                                 base::FilePath());
  base::FilePath download_dir;
  base::PathService::Get(chrome::DIR_DEFAULT_DOWNLOADS, &download_dir);
  registry->RegisterFilePathPref(prefs::kDownloadDefaultDirectory,
                                 download_dir);
  registry->RegisterDictionaryPref(prefs::kDevToolsFileSystemPaths);
  InspectableWebContentsImpl::RegisterPrefs(registry.get());
  MediaDeviceIDSalt::RegisterPrefs(registry.get());
  ZoomLevelDelegate::RegisterPrefs(registry.get());
  PrefProxyConfigTrackerImpl::RegisterPrefs(registry.get());

  prefs_ = prefs_factory.Create(
      registry.get(),
      std::make_unique<PrefStoreDelegate>(weak_factory_.GetWeakPtr()));
  prefs_->UpdateCommandLinePrefStore(new ValueMapPrefStore);
}

void AtomBrowserContext::SetUserAgent(const std::string& user_agent) {
  user_agent_ = user_agent;
}

net::URLRequestContextGetter* AtomBrowserContext::CreateRequestContext(
    content::ProtocolHandlerMap* protocol_handlers,
    content::URLRequestInterceptorScopedVector protocol_interceptors) {
  if (!base::FeatureList::IsEnabled(network::features::kNetworkService)) {
    return io_handle_
        ->CreateMainRequestContextGetter(protocol_handlers,
                                         std::move(protocol_interceptors))
        .get();
  } else {
    NOTREACHED();
    return nullptr;
  }
}

net::URLRequestContextGetter* AtomBrowserContext::CreateMediaRequestContext() {
  if (!base::FeatureList::IsEnabled(network::features::kNetworkService)) {
    return io_handle_->GetMainRequestContextGetter().get();
  } else {
    NOTREACHED();
    return nullptr;
  }
}

net::URLRequestContextGetter* AtomBrowserContext::GetRequestContext() {
  if (!base::FeatureList::IsEnabled(network::features::kNetworkService)) {
    return GetDefaultStoragePartition(this)->GetURLRequestContext();
  } else {
    NOTREACHED();
    return nullptr;
  }
}

network::mojom::NetworkContextPtr AtomBrowserContext::GetNetworkContext() {
  if (!base::FeatureList::IsEnabled(network::features::kNetworkService)) {
    return io_handle_->GetNetworkContext();
  } else {
    NOTREACHED();
    return nullptr;
  }
}

base::FilePath AtomBrowserContext::GetPath() const {
  return path_;
}

bool AtomBrowserContext::IsOffTheRecord() const {
  return in_memory_;
}

bool AtomBrowserContext::CanUseHttpCache() const {
  return use_cache_;
}

int AtomBrowserContext::GetMaxCacheSize() const {
  return max_cache_size_;
}

content::ResourceContext* AtomBrowserContext::GetResourceContext() {
  if (!base::FeatureList::IsEnabled(network::features::kNetworkService)) {
    return io_handle_->GetResourceContext();
  } else {
    if (!resource_context_)
      resource_context_.reset(new content::ResourceContext);
    return resource_context_.get();
  }
}

std::string AtomBrowserContext::GetMediaDeviceIDSalt() {
  if (!media_device_id_salt_.get())
    media_device_id_salt_.reset(new MediaDeviceIDSalt(prefs_.get()));
  return media_device_id_salt_->GetSalt();
}

std::unique_ptr<content::ZoomLevelDelegate>
AtomBrowserContext::CreateZoomLevelDelegate(
    const base::FilePath& partition_path) {
  if (!IsOffTheRecord()) {
    return std::make_unique<ZoomLevelDelegate>(prefs(), partition_path);
  }
  return std::unique_ptr<content::ZoomLevelDelegate>();
}

content::DownloadManagerDelegate*
AtomBrowserContext::GetDownloadManagerDelegate() {
  if (!download_manager_delegate_.get()) {
    auto* download_manager = content::BrowserContext::GetDownloadManager(this);
    download_manager_delegate_.reset(
        new AtomDownloadManagerDelegate(download_manager));
  }
  return download_manager_delegate_.get();
}

content::BrowserPluginGuestManager* AtomBrowserContext::GetGuestManager() {
  if (!guest_manager_)
    guest_manager_.reset(new WebViewManager);
  return guest_manager_.get();
}

content::PermissionControllerDelegate*
AtomBrowserContext::GetPermissionControllerDelegate() {
  if (!permission_manager_.get())
    permission_manager_.reset(new AtomPermissionManager);
  return permission_manager_.get();
}

storage::SpecialStoragePolicy* AtomBrowserContext::GetSpecialStoragePolicy() {
  return storage_policy_.get();
}

std::string AtomBrowserContext::GetUserAgent() const {
  return user_agent_;
}

AtomBlobReader* AtomBrowserContext::GetBlobReader() {
  if (!blob_reader_.get()) {
    content::ChromeBlobStorageContext* blob_context =
        content::ChromeBlobStorageContext::GetFor(this);
    blob_reader_.reset(new AtomBlobReader(blob_context));
  }
  return blob_reader_.get();
}

content::PushMessagingService* AtomBrowserContext::GetPushMessagingService() {
  return nullptr;
}

content::SSLHostStateDelegate* AtomBrowserContext::GetSSLHostStateDelegate() {
  return nullptr;
}

content::BackgroundFetchDelegate*
AtomBrowserContext::GetBackgroundFetchDelegate() {
  return nullptr;
}

content::BackgroundSyncController*
AtomBrowserContext::GetBackgroundSyncController() {
  return nullptr;
}

content::BrowsingDataRemoverDelegate*
AtomBrowserContext::GetBrowsingDataRemoverDelegate() {
  return nullptr;
}

content::ClientHintsControllerDelegate*
AtomBrowserContext::GetClientHintsControllerDelegate() {
  return nullptr;
}

net::URLRequestContextGetter*
AtomBrowserContext::CreateRequestContextForStoragePartition(
    const base::FilePath& partition_path,
    bool in_memory,
    content::ProtocolHandlerMap* protocol_handlers,
    content::URLRequestInterceptorScopedVector request_interceptors) {
  NOTREACHED();
  return nullptr;
}

net::URLRequestContextGetter*
AtomBrowserContext::CreateMediaRequestContextForStoragePartition(
    const base::FilePath& partition_path,
    bool in_memory) {
  NOTREACHED();
  return nullptr;
}

ResolveProxyHelper* AtomBrowserContext::GetResolveProxyHelper() {
  if (!resolve_proxy_helper_) {
    resolve_proxy_helper_ = base::MakeRefCounted<ResolveProxyHelper>(this);
  }
  return resolve_proxy_helper_.get();
}

// static
scoped_refptr<AtomBrowserContext> AtomBrowserContext::From(
    const std::string& partition,
    bool in_memory,
    const base::DictionaryValue& options) {
  PartitionKey key(partition, in_memory);
  auto* browser_context = browser_context_map_[key].get();
  if (browser_context)
    return scoped_refptr<AtomBrowserContext>(browser_context);

  auto* new_context = new AtomBrowserContext(partition, in_memory, options);
  browser_context_map_[key] = new_context->GetWeakPtr();
  return scoped_refptr<AtomBrowserContext>(new_context);
}

}  // namespace atom
