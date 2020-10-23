// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/atom_api_cookies.h"

#include <memory>
#include <utility>

#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "gin/dictionary.h"
#include "gin/object_template_builder.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_store.h"
#include "net/cookies/cookie_util.h"
#include "shell/browser/atom_browser_context.h"
#include "shell/browser/cookie_change_notifier.h"
#include "shell/common/native_mate_converters/callback.h"
#include "shell/common/native_mate_converters/gurl_converter.h"
#include "shell/common/native_mate_converters/value_converter.h"

using content::BrowserThread;

namespace gin {

template <>
struct Converter<net::CanonicalCookie> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   const net::CanonicalCookie& val) {
    gin::Dictionary dict(isolate, v8::Object::New(isolate));
    dict.Set("name", val.Name());
    dict.Set("value", val.Value());
    dict.Set("domain", val.Domain());
    dict.Set("hostOnly", net::cookie_util::DomainIsHostOnly(val.Domain()));
    dict.Set("path", val.Path());
    dict.Set("secure", val.IsSecure());
    dict.Set("httpOnly", val.IsHttpOnly());
    dict.Set("session", !val.IsPersistent());
    if (val.IsPersistent())
      dict.Set("expirationDate", val.ExpiryDate().ToDoubleT());
    return ConvertToV8(isolate, dict).As<v8::Object>();
  }
};

template <>
struct Converter<network::mojom::CookieChangeCause> {
  static v8::Local<v8::Value> ToV8(
      v8::Isolate* isolate,
      const network::mojom::CookieChangeCause& val) {
    switch (val) {
      case network::mojom::CookieChangeCause::INSERTED:
      case network::mojom::CookieChangeCause::EXPLICIT:
        return gin::StringToV8(isolate, "explicit");
      case network::mojom::CookieChangeCause::OVERWRITE:
        return gin::StringToV8(isolate, "overwrite");
      case network::mojom::CookieChangeCause::EXPIRED:
        return gin::StringToV8(isolate, "expired");
      case network::mojom::CookieChangeCause::EVICTED:
        return gin::StringToV8(isolate, "evicted");
      case network::mojom::CookieChangeCause::EXPIRED_OVERWRITE:
        return gin::StringToV8(isolate, "expired-overwrite");
      default:
        return gin::StringToV8(isolate, "unknown");
    }
  }
};

}  // namespace gin

namespace electron {

namespace api {

namespace {

// Returns whether |domain| matches |filter|.
bool MatchesDomain(std::string filter, const std::string& domain) {
  // Add a leading '.' character to the filter domain if it doesn't exist.
  if (net::cookie_util::DomainIsHostOnly(filter))
    filter.insert(0, ".");

  std::string sub_domain(domain);
  // Strip any leading '.' character from the input cookie domain.
  if (!net::cookie_util::DomainIsHostOnly(sub_domain))
    sub_domain = sub_domain.substr(1);

  // Now check whether the domain argument is a subdomain of the filter domain.
  for (sub_domain.insert(0, "."); sub_domain.length() >= filter.length();) {
    if (sub_domain == filter)
      return true;
    const size_t next_dot = sub_domain.find('.', 1);  // Skip over leading dot.
    sub_domain.erase(0, next_dot);
  }
  return false;
}

// Returns whether |cookie| matches |filter|.
bool MatchesCookie(const base::Value& filter,
                   const net::CanonicalCookie& cookie) {
  const std::string* str;
  if ((str = filter.FindStringKey("name")) && *str != cookie.Name())
    return false;
  if ((str = filter.FindStringKey("path")) && *str != cookie.Path())
    return false;
  if ((str = filter.FindStringKey("domain")) &&
      !MatchesDomain(*str, cookie.Domain()))
    return false;
  base::Optional<bool> secure_filter = filter.FindBoolKey("secure");
  if (secure_filter && *secure_filter == cookie.IsSecure())
    return false;
  base::Optional<bool> session_filter = filter.FindBoolKey("session");
  if (session_filter && *session_filter != !cookie.IsPersistent())
    return false;
  return true;
}

// Remove cookies from |list| not matching |filter|, and pass it to |callback|.
void FilterCookies(const base::Value& filter,
                   util::Promise<net::CookieList> promise,
                   const net::CookieStatusList& list,
                   const net::CookieStatusList& excluded_list) {
  net::CookieList result;
  net::CookieList stripped_cookies = net::cookie_util::StripStatuses(list);
  for (const auto& cookie : stripped_cookies) {
    if (MatchesCookie(filter, cookie))
      result.push_back(cookie);
  }

  promise.ResolveWithGin(result);
}

std::string InclusionStatusToString(
    net::CanonicalCookie::CookieInclusionStatus status) {
  switch (status) {
    case net::CanonicalCookie::CookieInclusionStatus::EXCLUDE_HTTP_ONLY:
      return "Failed to create httponly cookie";
    case net::CanonicalCookie::CookieInclusionStatus::EXCLUDE_SECURE_ONLY:
      return "Cannot create a secure cookie from an insecure URL";
    case net::CanonicalCookie::CookieInclusionStatus::EXCLUDE_FAILURE_TO_STORE:
      return "Failed to parse cookie";
    case net::CanonicalCookie::CookieInclusionStatus::EXCLUDE_INVALID_DOMAIN:
      return "Failed to get cookie domain";
    case net::CanonicalCookie::CookieInclusionStatus::EXCLUDE_INVALID_PREFIX:
      return "Failed because the cookie violated prefix rules.";
    case net::CanonicalCookie::CookieInclusionStatus::
        EXCLUDE_NONCOOKIEABLE_SCHEME:
      return "Cannot set cookie for current scheme";
    case net::CanonicalCookie::CookieInclusionStatus::INCLUDE:
      return "";
    default:
      return "Setting cookie failed";
  }
}

}  // namespace

Cookies::Cookies(v8::Isolate* isolate, AtomBrowserContext* browser_context)
    : browser_context_(browser_context) {
  Init(isolate);
  cookie_change_subscription_ =
      browser_context_->cookie_change_notifier()->RegisterCookieChangeCallback(
          base::BindRepeating(&Cookies::OnCookieChanged,
                              base::Unretained(this)));
}

Cookies::~Cookies() {}

v8::Local<v8::Promise> Cookies::Get(const base::DictionaryValue& filter) {
  util::Promise<net::CookieList> promise(isolate());
  v8::Local<v8::Promise> handle = promise.GetHandle();

  std::string url_string;
  filter.GetString("url", &url_string);
  GURL url(url_string);

  auto callback =
      base::BindOnce(FilterCookies, filter.Clone(), std::move(promise));

  auto* storage_partition = content::BrowserContext::GetDefaultStoragePartition(
      browser_context_.get());
  auto* manager = storage_partition->GetCookieManagerForBrowserProcess();

  net::CookieOptions options;
  options.set_include_httponly();
  options.set_same_site_cookie_context(
      net::CookieOptions::SameSiteCookieContext::SAME_SITE_STRICT);
  options.set_do_not_update_access_time();

  manager->GetCookieList(url, options, std::move(callback));

  return handle;
}

v8::Local<v8::Promise> Cookies::Remove(const GURL& url,
                                       const std::string& name) {
  util::Promise<void*> promise(isolate());
  v8::Local<v8::Promise> handle = promise.GetHandle();

  auto cookie_deletion_filter = network::mojom::CookieDeletionFilter::New();
  cookie_deletion_filter->url = url;
  cookie_deletion_filter->cookie_name = name;

  auto* storage_partition = content::BrowserContext::GetDefaultStoragePartition(
      browser_context_.get());
  auto* manager = storage_partition->GetCookieManagerForBrowserProcess();

  manager->DeleteCookies(
      std::move(cookie_deletion_filter),
      base::BindOnce(
          [](util::Promise<void*> promise, uint32_t num_deleted) {
            util::Promise<void*>::ResolveEmptyPromise(std::move(promise));
          },
          std::move(promise)));

  return handle;
}

v8::Local<v8::Promise> Cookies::Set(const base::DictionaryValue& details) {
  util::Promise<void*> promise(isolate());
  v8::Local<v8::Promise> handle = promise.GetHandle();

  const std::string* url_string = details.FindStringKey("url");
  const std::string* name = details.FindStringKey("name");
  const std::string* value = details.FindStringKey("value");
  const std::string* domain = details.FindStringKey("domain");
  const std::string* path = details.FindStringKey("path");
  bool secure = details.FindBoolKey("secure").value_or(false);
  bool http_only = details.FindBoolKey("httpOnly").value_or(false);
  base::Optional<double> creation_date = details.FindDoubleKey("creationDate");
  base::Optional<double> expiration_date =
      details.FindDoubleKey("expirationDate");
  base::Optional<double> last_access_date =
      details.FindDoubleKey("lastAccessDate");

  base::Time creation_time = creation_date
                                 ? base::Time::FromDoubleT(*creation_date)
                                 : base::Time::UnixEpoch();
  base::Time expiration_time = expiration_date
                                   ? base::Time::FromDoubleT(*expiration_date)
                                   : base::Time::UnixEpoch();
  base::Time last_access_time = last_access_date
                                    ? base::Time::FromDoubleT(*last_access_date)
                                    : base::Time::UnixEpoch();

  GURL url(url_string ? *url_string : "");
  if (!url.is_valid()) {
    promise.RejectWithErrorMessage(InclusionStatusToString(
        net::CanonicalCookie::CookieInclusionStatus::EXCLUDE_INVALID_DOMAIN));
    return handle;
  }

  if (!name || name->empty()) {
    promise.RejectWithErrorMessage(InclusionStatusToString(
        net::CanonicalCookie::CookieInclusionStatus::EXCLUDE_FAILURE_TO_STORE));
    return handle;
  }

  auto canonical_cookie = net::CanonicalCookie::CreateSanitizedCookie(
      url, *name, value ? *value : "", domain ? *domain : "", path ? *path : "",
      creation_time, expiration_time, last_access_time, secure, http_only,
      net::CookieSameSite::NO_RESTRICTION, net::COOKIE_PRIORITY_DEFAULT);
  if (!canonical_cookie || !canonical_cookie->IsCanonical()) {
    promise.RejectWithErrorMessage(InclusionStatusToString(
        net::CanonicalCookie::CookieInclusionStatus::EXCLUDE_FAILURE_TO_STORE));
    return handle;
  }
  net::CookieOptions options;
  if (http_only) {
    options.set_include_httponly();
  }

  auto* storage_partition = content::BrowserContext::GetDefaultStoragePartition(
      browser_context_.get());
  auto* manager = storage_partition->GetCookieManagerForBrowserProcess();
  manager->SetCanonicalCookie(
      *canonical_cookie, url.scheme(), options,
      base::BindOnce(
          [](util::Promise<void*> promise,
             net::CanonicalCookie::CookieInclusionStatus status) {
            auto errmsg = InclusionStatusToString(status);
            if (errmsg.empty()) {
              promise.Resolve();
            } else {
              promise.RejectWithErrorMessage(errmsg);
            }
          },
          std::move(promise)));

  return handle;
}

v8::Local<v8::Promise> Cookies::FlushStore() {
  util::Promise<void*> promise(isolate());
  v8::Local<v8::Promise> handle = promise.GetHandle();

  auto* storage_partition = content::BrowserContext::GetDefaultStoragePartition(
      browser_context_.get());
  auto* manager = storage_partition->GetCookieManagerForBrowserProcess();

  manager->FlushCookieStore(base::BindOnce(
      util::Promise<void*>::ResolveEmptyPromise, std::move(promise)));

  return handle;
}

void Cookies::OnCookieChanged(const CookieDetails* details) {
  Emit("changed", gin::ConvertToV8(isolate(), *(details->cookie)),
       gin::ConvertToV8(isolate(), details->cause),
       gin::ConvertToV8(isolate(), details->removed));
}

// static
gin::Handle<Cookies> Cookies::Create(v8::Isolate* isolate,
                                     AtomBrowserContext* browser_context) {
  return gin::CreateHandle(isolate, new Cookies(isolate, browser_context));
}

// static
void Cookies::BuildPrototype(v8::Isolate* isolate,
                             v8::Local<v8::FunctionTemplate> prototype) {
  prototype->SetClassName(gin::StringToV8(isolate, "Cookies"));
  mate::ObjectTemplateBuilder(isolate, prototype->PrototypeTemplate())
      .SetMethod("get", &Cookies::Get)
      .SetMethod("remove", &Cookies::Remove)
      .SetMethod("set", &Cookies::Set)
      .SetMethod("flushStore", &Cookies::FlushStore);
}

}  // namespace api

}  // namespace electron
