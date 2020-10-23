// Copyright (c) 2017 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "brightray/browser/io_thread.h"

#include "content/public/browser/browser_thread.h"
#include "net/proxy_resolution/proxy_service.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "net/url_request/url_request_context_getter.h"

#if defined(USE_NSS_CERTS)
#include "net/cert_net/nss_ocsp.h"
#endif

using content::BrowserThread;

namespace brightray {

IOThread::IOThread() {
  BrowserThread::SetIOThreadDelegate(this);
}

IOThread::~IOThread() {
  BrowserThread::SetIOThreadDelegate(nullptr);
}

void IOThread::Init() {
  net::URLRequestContextBuilder builder;
  builder.set_proxy_resolution_service(
      net::ProxyResolutionService::CreateDirect());
  url_request_context_ = builder.Build();
  url_request_context_getter_ = new net::TrivialURLRequestContextGetter(
      url_request_context_.get(), base::ThreadTaskRunnerHandle::Get());
  url_request_context_getter_->AddRef();

#if defined(USE_NSS_CERTS)
  net::SetMessageLoopForNSSHttpIO();
  net::SetURLRequestContextForNSSHttpIO(url_request_context_.get());
#endif
}

void IOThread::CleanUp() {
#if defined(USE_NSS_CERTS)
  net::ShutdownNSSHttpIO();
  net::SetURLRequestContextForNSSHttpIO(nullptr);
#endif
  // Explicitly release before the IO thread gets destroyed.
  url_request_context_getter_->Release();
  url_request_context_.reset();
}

}  // namespace brightray
