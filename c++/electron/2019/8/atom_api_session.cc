// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/atom_api_session.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/guid.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/post_task.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/pref_names.h"
#include "components/download/public/common/download_danger_type.h"
#include "components/download/public/common/download_url_parameters.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/value_map_pref_store.h"
#include "components/proxy_config/proxy_config_dictionary.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/download_manager_delegate.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/browser/storage_partition.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "native_mate/dictionary.h"
#include "native_mate/object_template_builder.h"
#include "net/base/completion_repeating_callback.h"
#include "net/base/load_flags.h"
#include "net/http/http_auth_handler_factory.h"
#include "net/http/http_auth_preferences.h"
#include "net/http/http_cache.h"
#include "services/network/network_service.h"
#include "services/network/public/cpp/features.h"
#include "shell/browser/api/atom_api_cookies.h"
#include "shell/browser/api/atom_api_download_item.h"
#include "shell/browser/api/atom_api_net_log.h"
#include "shell/browser/api/atom_api_protocol_ns.h"
#include "shell/browser/api/atom_api_web_request_ns.h"
#include "shell/browser/atom_browser_context.h"
#include "shell/browser/atom_browser_main_parts.h"
#include "shell/browser/atom_permission_manager.h"
#include "shell/browser/browser.h"
#include "shell/browser/media/media_device_id_salt.h"
#include "shell/browser/net/cert_verifier_client.h"
#include "shell/browser/session_preferences.h"
#include "shell/common/native_mate_converters/callback.h"
#include "shell/common/native_mate_converters/content_converter.h"
#include "shell/common/native_mate_converters/file_path_converter.h"
#include "shell/common/native_mate_converters/gurl_converter.h"
#include "shell/common/native_mate_converters/net_converter.h"
#include "shell/common/native_mate_converters/value_converter.h"
#include "shell/common/node_includes.h"
#include "shell/common/options_switches.h"
#include "ui/base/l10n/l10n_util.h"

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
#include "shell/browser/extensions/atom_extension_system.h"
#endif

using content::BrowserThread;
using content::StoragePartition;

namespace {

struct ClearStorageDataOptions {
  GURL origin;
  uint32_t storage_types = StoragePartition::REMOVE_DATA_MASK_ALL;
  uint32_t quota_types = StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL;
};

uint32_t GetStorageMask(const std::vector<std::string>& storage_types) {
  uint32_t storage_mask = 0;
  for (const auto& it : storage_types) {
    auto type = base::ToLowerASCII(it);
    if (type == "appcache")
      storage_mask |= StoragePartition::REMOVE_DATA_MASK_APPCACHE;
    else if (type == "cookies")
      storage_mask |= StoragePartition::REMOVE_DATA_MASK_COOKIES;
    else if (type == "filesystem")
      storage_mask |= StoragePartition::REMOVE_DATA_MASK_FILE_SYSTEMS;
    else if (type == "indexdb")
      storage_mask |= StoragePartition::REMOVE_DATA_MASK_INDEXEDDB;
    else if (type == "localstorage")
      storage_mask |= StoragePartition::REMOVE_DATA_MASK_LOCAL_STORAGE;
    else if (type == "shadercache")
      storage_mask |= StoragePartition::REMOVE_DATA_MASK_SHADER_CACHE;
    else if (type == "websql")
      storage_mask |= StoragePartition::REMOVE_DATA_MASK_WEBSQL;
    else if (type == "serviceworkers")
      storage_mask |= StoragePartition::REMOVE_DATA_MASK_SERVICE_WORKERS;
    else if (type == "cachestorage")
      storage_mask |= StoragePartition::REMOVE_DATA_MASK_CACHE_STORAGE;
  }
  return storage_mask;
}

uint32_t GetQuotaMask(const std::vector<std::string>& quota_types) {
  uint32_t quota_mask = 0;
  for (const auto& it : quota_types) {
    auto type = base::ToLowerASCII(it);
    if (type == "temporary")
      quota_mask |= StoragePartition::QUOTA_MANAGED_STORAGE_MASK_TEMPORARY;
    else if (type == "persistent")
      quota_mask |= StoragePartition::QUOTA_MANAGED_STORAGE_MASK_PERSISTENT;
    else if (type == "syncable")
      quota_mask |= StoragePartition::QUOTA_MANAGED_STORAGE_MASK_SYNCABLE;
  }
  return quota_mask;
}

}  // namespace

namespace mate {

template <>
struct Converter<ClearStorageDataOptions> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     ClearStorageDataOptions* out) {
    mate::Dictionary options;
    if (!ConvertFromV8(isolate, val, &options))
      return false;
    options.Get("origin", &out->origin);
    std::vector<std::string> types;
    if (options.Get("storages", &types))
      out->storage_types = GetStorageMask(types);
    if (options.Get("quotas", &types))
      out->quota_types = GetQuotaMask(types);
    return true;
  }
};

}  // namespace mate

namespace electron {

namespace api {

namespace {

const char kPersistPrefix[] = "persist:";

// Referenced session objects.
std::map<uint32_t, v8::Global<v8::Object>> g_sessions;

void DownloadIdCallback(content::DownloadManager* download_manager,
                        const base::FilePath& path,
                        const std::vector<GURL>& url_chain,
                        const std::string& mime_type,
                        int64_t offset,
                        int64_t length,
                        const std::string& last_modified,
                        const std::string& etag,
                        const base::Time& start_time,
                        uint32_t id) {
  download_manager->CreateDownloadItem(
      base::GenerateGUID(), id, path, path, url_chain, GURL(), GURL(), GURL(),
      GURL(), base::nullopt, mime_type, mime_type, start_time, base::Time(),
      etag, last_modified, offset, length, std::string(),
      download::DownloadItem::INTERRUPTED,
      download::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS,
      download::DOWNLOAD_INTERRUPT_REASON_NETWORK_TIMEOUT, false, base::Time(),
      false, std::vector<download::DownloadItem::ReceivedSlice>());
}

void DestroyGlobalHandle(v8::Isolate* isolate,
                         const v8::Global<v8::Value>& global_handle) {
  v8::Locker locker(isolate);
  v8::HandleScope handle_scope(isolate);
  if (!global_handle.IsEmpty()) {
    v8::Local<v8::Value> local_handle = global_handle.Get(isolate);
    v8::Local<v8::Object> object;
    if (local_handle->IsObject() &&
        local_handle->ToObject(isolate->GetCurrentContext()).ToLocal(&object)) {
      void* ptr = object->GetAlignedPointerFromInternalField(0);
      if (!ptr)
        return;
      delete static_cast<mate::WrappableBase*>(ptr);
      object->SetAlignedPointerInInternalField(0, nullptr);
    }
  }
}

}  // namespace

Session::Session(v8::Isolate* isolate, AtomBrowserContext* browser_context)
    : network_emulation_token_(base::UnguessableToken::Create()),
      browser_context_(browser_context) {
  // Observe DownloadManager to get download notifications.
  content::BrowserContext::GetDownloadManager(browser_context)
      ->AddObserver(this);

  new SessionPreferences(browser_context);

  Init(isolate);
  AttachAsUserData(browser_context);
}

Session::~Session() {
  content::BrowserContext::GetDownloadManager(browser_context())
      ->RemoveObserver(this);
  // TODO(zcbenz): Now since URLRequestContextGetter is gone, is this still
  // needed?
  // Refs https://github.com/electron/electron/pull/12305.
  DestroyGlobalHandle(isolate(), cookies_);
  DestroyGlobalHandle(isolate(), protocol_);
  DestroyGlobalHandle(isolate(), net_log_);
  g_sessions.erase(weak_map_id());
}

void Session::OnDownloadCreated(content::DownloadManager* manager,
                                download::DownloadItem* item) {
  if (item->IsSavePackageDownload())
    return;

  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  auto handle = DownloadItem::Create(isolate(), item);
  if (item->GetState() == download::DownloadItem::INTERRUPTED)
    handle->SetSavePath(item->GetTargetFilePath());
  content::WebContents* web_contents =
      content::DownloadItemUtils::GetWebContents(item);
  bool prevent_default = Emit("will-download", handle, web_contents);
  if (prevent_default) {
    item->Cancel(true);
    item->Remove();
  }
}

v8::Local<v8::Promise> Session::ResolveProxy(mate::Arguments* args) {
  v8::Isolate* isolate = args->isolate();
  util::Promise<std::string> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  GURL url;
  args->GetNext(&url);

  browser_context_->GetResolveProxyHelper()->ResolveProxy(
      url, base::BindOnce(util::Promise<std::string>::ResolvePromise,
                          std::move(promise)));

  return handle;
}

v8::Local<v8::Promise> Session::GetCacheSize() {
  auto* isolate = v8::Isolate::GetCurrent();
  util::Promise<int64_t> promise(isolate);
  auto handle = promise.GetHandle();

  content::BrowserContext::GetDefaultStoragePartition(browser_context_.get())
      ->GetNetworkContext()
      ->ComputeHttpCacheSize(
          base::Time(), base::Time::Max(),
          base::BindOnce(
              [](util::Promise<int64_t> promise, bool is_upper_bound,
                 int64_t size_or_error) {
                if (size_or_error < 0) {
                  promise.RejectWithErrorMessage(
                      net::ErrorToString(size_or_error));
                } else {
                  promise.Resolve(size_or_error);
                }
              },
              std::move(promise)));

  return handle;
}

v8::Local<v8::Promise> Session::ClearCache() {
  auto* isolate = v8::Isolate::GetCurrent();
  util::Promise<void*> promise(isolate);
  auto handle = promise.GetHandle();

  content::BrowserContext::GetDefaultStoragePartition(browser_context_.get())
      ->GetNetworkContext()
      ->ClearHttpCache(base::Time(), base::Time::Max(), nullptr,
                       base::BindOnce(util::Promise<void*>::ResolveEmptyPromise,
                                      std::move(promise)));

  return handle;
}

v8::Local<v8::Promise> Session::ClearStorageData(mate::Arguments* args) {
  v8::Isolate* isolate = args->isolate();
  util::Promise<void*> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  ClearStorageDataOptions options;
  args->GetNext(&options);

  auto* storage_partition =
      content::BrowserContext::GetStoragePartition(browser_context(), nullptr);
  if (options.storage_types & StoragePartition::REMOVE_DATA_MASK_COOKIES) {
    // Reset media device id salt when cookies are cleared.
    // https://w3c.github.io/mediacapture-main/#dom-mediadeviceinfo-deviceid
    MediaDeviceIDSalt::Reset(browser_context()->prefs());
  }

  storage_partition->ClearData(
      options.storage_types, options.quota_types, options.origin, base::Time(),
      base::Time::Max(),
      base::BindOnce(util::Promise<void*>::ResolveEmptyPromise,
                     std::move(promise)));
  return handle;
}

void Session::FlushStorageData() {
  auto* storage_partition =
      content::BrowserContext::GetStoragePartition(browser_context(), nullptr);
  storage_partition->Flush();
}

v8::Local<v8::Promise> Session::SetProxy(mate::Arguments* args) {
  v8::Isolate* isolate = args->isolate();
  util::Promise<void*> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  mate::Dictionary options;
  args->GetNext(&options);

  if (!browser_context_->in_memory_pref_store()) {
    promise.Resolve();
    return handle;
  }

  std::string proxy_rules, bypass_list, pac_url;

  options.Get("pacScript", &pac_url);
  options.Get("proxyRules", &proxy_rules);
  options.Get("proxyBypassRules", &bypass_list);

  // pacScript takes precedence over proxyRules.
  if (!pac_url.empty()) {
    browser_context_->in_memory_pref_store()->SetValue(
        proxy_config::prefs::kProxy,
        std::make_unique<base::Value>(ProxyConfigDictionary::CreatePacScript(
            pac_url, true /* pac_mandatory */)),
        WriteablePrefStore::DEFAULT_PREF_WRITE_FLAGS);
  } else {
    browser_context_->in_memory_pref_store()->SetValue(
        proxy_config::prefs::kProxy,
        std::make_unique<base::Value>(ProxyConfigDictionary::CreateFixedServers(
            proxy_rules, bypass_list)),
        WriteablePrefStore::DEFAULT_PREF_WRITE_FLAGS);
  }

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(util::Promise<void*>::ResolveEmptyPromise,
                                std::move(promise)));

  return handle;
}

void Session::SetDownloadPath(const base::FilePath& path) {
  browser_context_->prefs()->SetFilePath(prefs::kDownloadDefaultDirectory,
                                         path);
}

void Session::EnableNetworkEmulation(const mate::Dictionary& options) {
  auto conditions = network::mojom::NetworkConditions::New();

  options.Get("offline", &conditions->offline);
  options.Get("downloadThroughput", &conditions->download_throughput);
  options.Get("uploadThroughput", &conditions->upload_throughput);
  double latency = 0.0;
  if (options.Get("latency", &latency) && latency) {
    conditions->latency = base::TimeDelta::FromMillisecondsD(latency);
  }

  auto* network_context = content::BrowserContext::GetDefaultStoragePartition(
                              browser_context_.get())
                              ->GetNetworkContext();
  network_context->SetNetworkConditions(network_emulation_token_,
                                        std::move(conditions));
}

void Session::DisableNetworkEmulation() {
  auto* network_context = content::BrowserContext::GetDefaultStoragePartition(
                              browser_context_.get())
                              ->GetNetworkContext();
  network_context->SetNetworkConditions(
      network_emulation_token_, network::mojom::NetworkConditions::New());
}

void Session::SetCertVerifyProc(v8::Local<v8::Value> val,
                                mate::Arguments* args) {
  CertVerifierClient::CertVerifyProc proc;
  if (!(val->IsNull() || mate::ConvertFromV8(args->isolate(), val, &proc))) {
    args->ThrowError("Must pass null or function");
    return;
  }

  network::mojom::CertVerifierClientPtr cert_verifier_client;
  if (proc) {
    mojo::MakeStrongBinding(std::make_unique<CertVerifierClient>(proc),
                            mojo::MakeRequest(&cert_verifier_client));
  }
  content::BrowserContext::GetDefaultStoragePartition(browser_context_.get())
      ->GetNetworkContext()
      ->SetCertVerifierClient(std::move(cert_verifier_client));

  // This causes the cert verifier cache to be cleared.
  content::GetNetworkService()->OnCertDBChanged();
}

void Session::SetPermissionRequestHandler(v8::Local<v8::Value> val,
                                          mate::Arguments* args) {
  auto* permission_manager = static_cast<AtomPermissionManager*>(
      browser_context()->GetPermissionControllerDelegate());
  if (val->IsNull()) {
    permission_manager->SetPermissionRequestHandler(
        AtomPermissionManager::RequestHandler());
    return;
  }
  auto handler = std::make_unique<AtomPermissionManager::RequestHandler>();
  if (!mate::ConvertFromV8(args->isolate(), val, handler.get())) {
    args->ThrowError("Must pass null or function");
    return;
  }
  permission_manager->SetPermissionRequestHandler(base::BindRepeating(
      [](AtomPermissionManager::RequestHandler* handler,
         content::WebContents* web_contents,
         content::PermissionType permission_type,
         AtomPermissionManager::StatusCallback callback,
         const base::Value& details) {
        handler->Run(web_contents, permission_type,
                     base::AdaptCallbackForRepeating(std::move(callback)),
                     details);
      },
      base::Owned(std::move(handler))));
}

void Session::SetPermissionCheckHandler(v8::Local<v8::Value> val,
                                        mate::Arguments* args) {
  AtomPermissionManager::CheckHandler handler;
  if (!(val->IsNull() || mate::ConvertFromV8(args->isolate(), val, &handler))) {
    args->ThrowError("Must pass null or function");
    return;
  }
  auto* permission_manager = static_cast<AtomPermissionManager*>(
      browser_context()->GetPermissionControllerDelegate());
  permission_manager->SetPermissionCheckHandler(handler);
}

v8::Local<v8::Promise> Session::ClearHostResolverCache(mate::Arguments* args) {
  v8::Isolate* isolate = args->isolate();
  util::Promise<void*> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  content::BrowserContext::GetDefaultStoragePartition(browser_context_.get())
      ->GetNetworkContext()
      ->ClearHostCache(nullptr,
                       base::BindOnce(util::Promise<void*>::ResolveEmptyPromise,
                                      std::move(promise)));

  return handle;
}

v8::Local<v8::Promise> Session::ClearAuthCache() {
  auto* isolate = v8::Isolate::GetCurrent();
  util::Promise<void*> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  content::BrowserContext::GetDefaultStoragePartition(browser_context_.get())
      ->GetNetworkContext()
      ->ClearHttpAuthCache(
          base::Time(),
          base::BindOnce(util::Promise<void*>::ResolveEmptyPromise,
                         std::move(promise)));

  return handle;
}

void Session::AllowNTLMCredentialsForDomains(const std::string& domains) {
  network::mojom::HttpAuthDynamicParamsPtr auth_dynamic_params =
      network::mojom::HttpAuthDynamicParams::New();
  auth_dynamic_params->server_allowlist = domains;
  content::GetNetworkService()->ConfigureHttpAuthPrefs(
      std::move(auth_dynamic_params));
}

void Session::SetUserAgent(const std::string& user_agent,
                           mate::Arguments* args) {
  browser_context_->SetUserAgent(user_agent);
  content::BrowserContext::GetDefaultStoragePartition(browser_context_.get())
      ->GetNetworkContext()
      ->SetUserAgent(user_agent);
}

std::string Session::GetUserAgent() {
  return browser_context_->GetUserAgent();
}

v8::Local<v8::Promise> Session::GetBlobData(v8::Isolate* isolate,
                                            const std::string& uuid) {
  util::Promise<v8::Local<v8::Value>> promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  AtomBlobReader* blob_reader = browser_context()->GetBlobReader();
  base::PostTaskWithTraits(
      FROM_HERE, {BrowserThread::IO},
      base::BindOnce(&AtomBlobReader::StartReading,
                     base::Unretained(blob_reader), uuid, std::move(promise)));
  return handle;
}

void Session::DownloadURL(const GURL& url) {
  auto* download_manager =
      content::BrowserContext::GetDownloadManager(browser_context());
  auto download_params = std::make_unique<download::DownloadUrlParameters>(
      url, MISSING_TRAFFIC_ANNOTATION);
  download_manager->DownloadUrl(std::move(download_params));
}

void Session::CreateInterruptedDownload(const mate::Dictionary& options) {
  int64_t offset = 0, length = 0;
  double start_time = 0.0;
  std::string mime_type, last_modified, etag;
  base::FilePath path;
  std::vector<GURL> url_chain;
  options.Get("path", &path);
  options.Get("urlChain", &url_chain);
  options.Get("mimeType", &mime_type);
  options.Get("offset", &offset);
  options.Get("length", &length);
  options.Get("lastModified", &last_modified);
  options.Get("eTag", &etag);
  options.Get("startTime", &start_time);
  if (path.empty() || url_chain.empty() || length == 0) {
    isolate()->ThrowException(v8::Exception::Error(mate::StringToV8(
        isolate(), "Must pass non-empty path, urlChain and length.")));
    return;
  }
  if (offset >= length) {
    isolate()->ThrowException(v8::Exception::Error(mate::StringToV8(
        isolate(), "Must pass an offset value less than length.")));
    return;
  }
  auto* download_manager =
      content::BrowserContext::GetDownloadManager(browser_context());
  download_manager->GetDelegate()->GetNextId(base::BindRepeating(
      &DownloadIdCallback, download_manager, path, url_chain, mime_type, offset,
      length, last_modified, etag, base::Time::FromDoubleT(start_time)));
}

void Session::SetPreloads(
    const std::vector<base::FilePath::StringType>& preloads) {
  auto* prefs = SessionPreferences::FromBrowserContext(browser_context());
  DCHECK(prefs);
  prefs->set_preloads(preloads);
}

std::vector<base::FilePath::StringType> Session::GetPreloads() const {
  auto* prefs = SessionPreferences::FromBrowserContext(browser_context());
  DCHECK(prefs);
  return prefs->preloads();
}

#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
void Session::LoadChromeExtension(const base::FilePath extension_path) {
  auto* extension_system = static_cast<extensions::AtomExtensionSystem*>(
      extensions::ExtensionSystem::Get(browser_context()));
  extension_system->LoadExtension(extension_path);
}
#endif

v8::Local<v8::Value> Session::Cookies(v8::Isolate* isolate) {
  if (cookies_.IsEmpty()) {
    auto handle = Cookies::Create(isolate, browser_context());
    cookies_.Reset(isolate, handle.ToV8());
  }
  return v8::Local<v8::Value>::New(isolate, cookies_);
}

v8::Local<v8::Value> Session::Protocol(v8::Isolate* isolate) {
  if (protocol_.IsEmpty()) {
    v8::Local<v8::Value> handle;
    handle = ProtocolNS::Create(isolate, browser_context()).ToV8();
    protocol_.Reset(isolate, handle);
  }
  return v8::Local<v8::Value>::New(isolate, protocol_);
}

v8::Local<v8::Value> Session::WebRequest(v8::Isolate* isolate) {
  if (web_request_.IsEmpty()) {
    auto handle = WebRequestNS::Create(isolate, browser_context());
    web_request_.Reset(isolate, handle.ToV8());
  }
  return v8::Local<v8::Value>::New(isolate, web_request_);
}

v8::Local<v8::Value> Session::NetLog(v8::Isolate* isolate) {
  if (net_log_.IsEmpty()) {
    auto handle = electron::api::NetLog::Create(isolate, browser_context());
    net_log_.Reset(isolate, handle.ToV8());
  }
  return v8::Local<v8::Value>::New(isolate, net_log_);
}

static void StartPreconnectOnUI(
    scoped_refptr<AtomBrowserContext> browser_context,
    const GURL& url,
    int num_sockets_to_preconnect) {
  std::vector<predictors::PreconnectRequest> requests = {
      {url.GetOrigin(), num_sockets_to_preconnect, net::NetworkIsolationKey()}};
  browser_context->GetPreconnectManager()->Start(url, requests);
}

void Session::Preconnect(const mate::Dictionary& options,
                         mate::Arguments* args) {
  GURL url;
  if (!options.Get("url", &url) || !url.is_valid()) {
    args->ThrowError("Must pass non-empty valid url to session.preconnect.");
    return;
  }
  int num_sockets_to_preconnect = 1;
  if (options.Get("numSockets", &num_sockets_to_preconnect)) {
    const int kMinSocketsToPreconnect = 1;
    const int kMaxSocketsToPreconnect = 6;
    if (num_sockets_to_preconnect < kMinSocketsToPreconnect ||
        num_sockets_to_preconnect > kMaxSocketsToPreconnect) {
      args->ThrowError(
          base::StringPrintf("numSocketsToPreconnect is outside range [%d,%d]",
                             kMinSocketsToPreconnect, kMaxSocketsToPreconnect));
      return;
    }
  }

  DCHECK_GT(num_sockets_to_preconnect, 0);
  base::PostTaskWithTraits(
      FROM_HERE, {content::BrowserThread::UI},
      base::BindOnce(&StartPreconnectOnUI, base::RetainedRef(browser_context_),
                     url, num_sockets_to_preconnect));
}

// static
mate::Handle<Session> Session::CreateFrom(v8::Isolate* isolate,
                                          AtomBrowserContext* browser_context) {
  auto* existing = TrackableObject::FromWrappedClass(isolate, browser_context);
  if (existing)
    return mate::CreateHandle(isolate, static_cast<Session*>(existing));

  auto handle =
      mate::CreateHandle(isolate, new Session(isolate, browser_context));

  // The Sessions should never be garbage collected, since the common pattern is
  // to use partition strings, instead of using the Session object directly.
  g_sessions[handle->weak_map_id()] =
      v8::Global<v8::Object>(isolate, handle.ToV8());

  return handle;
}

// static
mate::Handle<Session> Session::FromPartition(
    v8::Isolate* isolate,
    const std::string& partition,
    const base::DictionaryValue& options) {
  scoped_refptr<AtomBrowserContext> browser_context;
  if (partition.empty()) {
    browser_context = AtomBrowserContext::From("", false, options);
  } else if (base::StartsWith(partition, kPersistPrefix,
                              base::CompareCase::SENSITIVE)) {
    std::string name = partition.substr(8);
    browser_context = AtomBrowserContext::From(name, false, options);
  } else {
    browser_context = AtomBrowserContext::From(partition, true, options);
  }
  return CreateFrom(isolate, browser_context.get());
}

// static
void Session::BuildPrototype(v8::Isolate* isolate,
                             v8::Local<v8::FunctionTemplate> prototype) {
  prototype->SetClassName(mate::StringToV8(isolate, "Session"));
  mate::ObjectTemplateBuilder(isolate, prototype->PrototypeTemplate())
      .MakeDestroyable()
      .SetMethod("resolveProxy", &Session::ResolveProxy)
      .SetMethod("getCacheSize", &Session::GetCacheSize)
      .SetMethod("clearCache", &Session::ClearCache)
      .SetMethod("clearStorageData", &Session::ClearStorageData)
      .SetMethod("flushStorageData", &Session::FlushStorageData)
      .SetMethod("setProxy", &Session::SetProxy)
      .SetMethod("setDownloadPath", &Session::SetDownloadPath)
      .SetMethod("enableNetworkEmulation", &Session::EnableNetworkEmulation)
      .SetMethod("disableNetworkEmulation", &Session::DisableNetworkEmulation)
      .SetMethod("setCertificateVerifyProc", &Session::SetCertVerifyProc)
      .SetMethod("setPermissionRequestHandler",
                 &Session::SetPermissionRequestHandler)
      .SetMethod("setPermissionCheckHandler",
                 &Session::SetPermissionCheckHandler)
      .SetMethod("clearHostResolverCache", &Session::ClearHostResolverCache)
      .SetMethod("clearAuthCache", &Session::ClearAuthCache)
      .SetMethod("allowNTLMCredentialsForDomains",
                 &Session::AllowNTLMCredentialsForDomains)
      .SetMethod("setUserAgent", &Session::SetUserAgent)
      .SetMethod("getUserAgent", &Session::GetUserAgent)
      .SetMethod("getBlobData", &Session::GetBlobData)
      .SetMethod("downloadURL", &Session::DownloadURL)
      .SetMethod("createInterruptedDownload",
                 &Session::CreateInterruptedDownload)
      .SetMethod("setPreloads", &Session::SetPreloads)
      .SetMethod("getPreloads", &Session::GetPreloads)
#if BUILDFLAG(ENABLE_ELECTRON_EXTENSIONS)
      .SetMethod("loadChromeExtension", &Session::LoadChromeExtension)
#endif
      .SetMethod("preconnect", &Session::Preconnect)
      .SetProperty("cookies", &Session::Cookies)
      .SetProperty("netLog", &Session::NetLog)
      .SetProperty("protocol", &Session::Protocol)
      .SetProperty("webRequest", &Session::WebRequest);
}

}  // namespace api

}  // namespace electron

namespace {

using electron::api::Cookies;
using electron::api::NetLog;
using electron::api::ProtocolNS;
using electron::api::Session;

v8::Local<v8::Value> FromPartition(const std::string& partition,
                                   mate::Arguments* args) {
  if (!electron::Browser::Get()->is_ready()) {
    args->ThrowError("Session can only be received when app is ready");
    return v8::Null(args->isolate());
  }
  base::DictionaryValue options;
  args->GetNext(&options);
  return Session::FromPartition(args->isolate(), partition, options).ToV8();
}

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  mate::Dictionary dict(isolate, exports);
  dict.Set(
      "Session",
      Session::GetConstructor(isolate)->GetFunction(context).ToLocalChecked());
  dict.Set(
      "Cookies",
      Cookies::GetConstructor(isolate)->GetFunction(context).ToLocalChecked());
  dict.Set(
      "NetLog",
      NetLog::GetConstructor(isolate)->GetFunction(context).ToLocalChecked());
  dict.Set("Protocol", ProtocolNS::GetConstructor(isolate)
                           ->GetFunction(context)
                           .ToLocalChecked());
  dict.SetMethod("fromPartition", &FromPartition);
}

}  // namespace

NODE_LINKED_MODULE_CONTEXT_AWARE(atom_browser_session, Initialize)
