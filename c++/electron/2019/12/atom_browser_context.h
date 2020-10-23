// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef SHELL_BROWSER_ATOM_BROWSER_CONTEXT_H_
#define SHELL_BROWSER_ATOM_BROWSER_CONTEXT_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/ref_counted_delete_on_sequence.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/net/proxy_config_monitor.h"
#include "chrome/browser/predictors/preconnect_manager.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/resource_context.h"
#include "electron/buildflags/buildflags.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "shell/browser/media/media_device_id_salt.h"

class PrefRegistrySimple;
class PrefService;
class ValueMapPrefStore;

namespace network {
class SharedURLLoaderFactory;
}

namespace storage {
class SpecialStoragePolicy;
}

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
namespace extensions {
class AtomExtensionSystem;
}
#endif

namespace electron {

class AtomBrowserContext;
class AtomDownloadManagerDelegate;
class AtomPermissionManager;
class CookieChangeNotifier;
class ResolveProxyHelper;
class SpecialStoragePolicy;
class WebViewManager;

class AtomBrowserContext
    : public base::RefCountedDeleteOnSequence<AtomBrowserContext>,
      public content::BrowserContext,
      public network::mojom::TrustedURLLoaderAuthClient {
 public:
  // partition_id => browser_context
  struct PartitionKey {
    std::string partition;
    bool in_memory;

    PartitionKey(const std::string& partition, bool in_memory)
        : partition(partition), in_memory(in_memory) {}

    bool operator<(const PartitionKey& other) const {
      if (partition == other.partition)
        return in_memory < other.in_memory;
      return partition < other.partition;
    }

    bool operator==(const PartitionKey& other) const {
      return (partition == other.partition) && (in_memory == other.in_memory);
    }
  };
  using BrowserContextMap =
      std::map<PartitionKey, base::WeakPtr<AtomBrowserContext>>;

  // Get or create the BrowserContext according to its |partition| and
  // |in_memory|. The |options| will be passed to constructor when there is no
  // existing BrowserContext.
  static scoped_refptr<AtomBrowserContext> From(
      const std::string& partition,
      bool in_memory,
      base::DictionaryValue options = base::DictionaryValue());

  static BrowserContextMap browser_context_map() {
    return browser_context_map_;
  }

  void SetUserAgent(const std::string& user_agent);
  std::string GetUserAgent() const;
  bool CanUseHttpCache() const;
  int GetMaxCacheSize() const;
  ResolveProxyHelper* GetResolveProxyHelper();
  predictors::PreconnectManager* GetPreconnectManager();
  scoped_refptr<network::SharedURLLoaderFactory> GetURLLoaderFactory();

  // content::BrowserContext:
  base::FilePath GetPath() override;
  bool IsOffTheRecord() override;
  content::ResourceContext* GetResourceContext() override;
  std::unique_ptr<content::ZoomLevelDelegate> CreateZoomLevelDelegate(
      const base::FilePath& partition_path) override;
  content::PushMessagingService* GetPushMessagingService() override;
  content::SSLHostStateDelegate* GetSSLHostStateDelegate() override;
  content::BackgroundFetchDelegate* GetBackgroundFetchDelegate() override;
  content::BackgroundSyncController* GetBackgroundSyncController() override;
  content::BrowsingDataRemoverDelegate* GetBrowsingDataRemoverDelegate()
      override;
  std::string GetMediaDeviceIDSalt() override;
  content::DownloadManagerDelegate* GetDownloadManagerDelegate() override;
  content::BrowserPluginGuestManager* GetGuestManager() override;
  content::PermissionControllerDelegate* GetPermissionControllerDelegate()
      override;
  storage::SpecialStoragePolicy* GetSpecialStoragePolicy() override;
  content::ClientHintsControllerDelegate* GetClientHintsControllerDelegate()
      override;
  content::StorageNotificationService* GetStorageNotificationService() override;

  // extensions deps
  void SetCorsOriginAccessListForOrigin(
      const url::Origin& source_origin,
      std::vector<network::mojom::CorsOriginPatternPtr> allow_patterns,
      std::vector<network::mojom::CorsOriginPatternPtr> block_patterns,
      base::OnceClosure closure) override;

  CookieChangeNotifier* cookie_change_notifier() const {
    return cookie_change_notifier_.get();
  }
  ProxyConfigMonitor* proxy_config_monitor() {
    return proxy_config_monitor_.get();
  }
  PrefService* prefs() const { return prefs_.get(); }
  void set_in_memory_pref_store(ValueMapPrefStore* pref_store) {
    in_memory_pref_store_ = pref_store;
  }
  ValueMapPrefStore* in_memory_pref_store() const {
    return in_memory_pref_store_;
  }
  base::WeakPtr<AtomBrowserContext> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 protected:
  AtomBrowserContext(const std::string& partition,
                     bool in_memory,
                     base::DictionaryValue options);
  ~AtomBrowserContext() override;

 private:
  friend class base::RefCountedDeleteOnSequence<AtomBrowserContext>;
  friend class base::DeleteHelper<AtomBrowserContext>;

  void OnLoaderCreated(int32_t request_id,
                       mojo::PendingReceiver<network::mojom::TrustedAuthClient>
                           header_client) override;

  // Initialize pref registry.
  void InitPrefs();

  static BrowserContextMap browser_context_map_;

  ValueMapPrefStore* in_memory_pref_store_;

  std::unique_ptr<content::ResourceContext> resource_context_;
  std::unique_ptr<CookieChangeNotifier> cookie_change_notifier_;
  std::unique_ptr<PrefService> prefs_;
  std::unique_ptr<AtomDownloadManagerDelegate> download_manager_delegate_;
  std::unique_ptr<WebViewManager> guest_manager_;
  std::unique_ptr<AtomPermissionManager> permission_manager_;
  std::unique_ptr<MediaDeviceIDSalt> media_device_id_salt_;
  scoped_refptr<ResolveProxyHelper> resolve_proxy_helper_;
  scoped_refptr<storage::SpecialStoragePolicy> storage_policy_;

  // Tracks the ProxyConfig to use, and passes any updates to a NetworkContext
  // ProxyConfigClient.
  std::unique_ptr<ProxyConfigMonitor> proxy_config_monitor_;

  std::unique_ptr<predictors::PreconnectManager> preconnect_manager_;

  std::string user_agent_;
  base::FilePath path_;
  bool in_memory_ = false;
  bool use_cache_ = true;
  int max_cache_size_ = 0;

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
  // Owned by the KeyedService system.
  extensions::AtomExtensionSystem* extension_system_;
#endif

  // Shared URLLoaderFactory.
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  mojo::Receiver<network::mojom::TrustedURLLoaderAuthClient> auth_client_{this};

  base::WeakPtrFactory<AtomBrowserContext> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(AtomBrowserContext);
};

}  // namespace electron

#endif  // SHELL_BROWSER_ATOM_BROWSER_CONTEXT_H_
