// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#ifndef BRIGHTRAY_BROWSER_URL_REQUEST_CONTEXT_GETTER_H_
#define BRIGHTRAY_BROWSER_URL_REQUEST_CONTEXT_GETTER_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/content_browser_client.h"
#include "net/cookies/cookie_store.h"
#include "net/http/http_cache.h"
#include "net/http/transport_security_state.h"
#include "net/http/url_security_manager.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"

#if DCHECK_IS_ON()
#include "base/debug/leak_tracker.h"
#endif

namespace base {
class MessageLoop;
}

namespace net {
class HostMappingRules;
class HostResolver;
class HttpAuthPreferences;
class NetworkDelegate;
class ProxyConfigService;
class URLRequestContextStorage;
class URLRequestJobFactory;
}  // namespace net

namespace brightray {

class RequireCTDelegate;
class NetLog;

class URLRequestContextGetter : public net::URLRequestContextGetter {
 public:
  class Delegate {
   public:
    Delegate() {}
    virtual ~Delegate() {}

    virtual std::unique_ptr<net::NetworkDelegate> CreateNetworkDelegate();
    virtual std::string GetUserAgent();
    virtual std::unique_ptr<net::URLRequestJobFactory>
    CreateURLRequestJobFactory(content::ProtocolHandlerMap* protocol_handlers);
    virtual net::HttpCache::BackendFactory* CreateHttpCacheBackendFactory(
        const base::FilePath& base_path);
    virtual std::unique_ptr<net::CertVerifier> CreateCertVerifier(
        RequireCTDelegate* ct_delegate);
    virtual net::SSLConfigService* CreateSSLConfigService();
    virtual std::vector<std::string> GetCookieableSchemes();
    virtual void NotifyCookieChange(const net::CanonicalCookie& cookie,
                                    bool removed,
                                    net::CookieStore::ChangeCause cause) {}
  };

  URLRequestContextGetter(
      Delegate* delegate,
      NetLog* net_log,
      const base::FilePath& base_path,
      bool in_memory,
      scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
      content::ProtocolHandlerMap* protocol_handlers,
      content::URLRequestInterceptorScopedVector protocol_interceptors);

  // net::CookieStore::CookieChangedCallback implementation.
  void OnCookieChanged(const net::CanonicalCookie& cookie,
                       net::CookieStore::ChangeCause cause);

  // net::URLRequestContextGetter:
  net::URLRequestContext* GetURLRequestContext() override;
  scoped_refptr<base::SingleThreadTaskRunner> GetNetworkTaskRunner()
      const override;

  net::HostResolver* host_resolver();
  net::URLRequestJobFactory* job_factory() const { return job_factory_; }

  void NotifyContextShutdownOnIO();

 private:
  ~URLRequestContextGetter() override;

  Delegate* delegate_;

  NetLog* net_log_;
  base::FilePath base_path_;
  bool in_memory_;
  scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;

  std::string user_agent_;

#if DCHECK_IS_ON()
  base::debug::LeakTracker<URLRequestContextGetter> leak_tracker_;
#endif

  std::unique_ptr<RequireCTDelegate> ct_delegate_;
  std::unique_ptr<net::ProxyConfigService> proxy_config_service_;
  std::unique_ptr<net::URLRequestContextStorage> storage_;
  std::unique_ptr<net::URLRequestContext> url_request_context_;
  std::unique_ptr<net::HostMappingRules> host_mapping_rules_;
  std::unique_ptr<net::HttpAuthPreferences> http_auth_preferences_;
  std::unique_ptr<net::HttpNetworkSession> http_network_session_;
  std::unique_ptr<net::CookieStore::CookieChangedSubscription>
      cookie_change_sub_;
  content::ProtocolHandlerMap protocol_handlers_;
  content::URLRequestInterceptorScopedVector protocol_interceptors_;

  net::URLRequestJobFactory* job_factory_;  // weak ref

  bool context_shutting_down_;

  DISALLOW_COPY_AND_ASSIGN(URLRequestContextGetter);
};

}  // namespace brightray

#endif  // BRIGHTRAY_BROWSER_URL_REQUEST_CONTEXT_GETTER_H_
