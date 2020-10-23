// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/api/atom_api_cookies.h"

#include "atom/browser/atom_browser_context.h"
#include "atom/browser/request_context_delegate.h"
#include "atom/common/native_mate_converters/callback.h"
#include "atom/common/native_mate_converters/gurl_converter.h"
#include "atom/common/native_mate_converters/value_converter.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "native_mate/dictionary.h"
#include "native_mate/object_template_builder.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_store.h"
#include "net/cookies/cookie_util.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"

using content::BrowserThread;

namespace mate {

template <>
struct Converter<atom::api::Cookies::Error> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   atom::api::Cookies::Error val) {
    if (val == atom::api::Cookies::SUCCESS)
      return v8::Null(isolate);
    else
      return v8::Exception::Error(StringToV8(isolate, "Setting cookie failed"));
  }
};

template <>
struct Converter<net::CanonicalCookie> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   const net::CanonicalCookie& val) {
    mate::Dictionary dict(isolate, v8::Object::New(isolate));
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
    return dict.GetHandle();
  }
};

template <>
struct Converter<net::CookieChangeCause> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   const net::CookieChangeCause& val) {
    switch (val) {
      case net::CookieChangeCause::INSERTED:
      case net::CookieChangeCause::EXPLICIT:
        return mate::StringToV8(isolate, "explicit");
      case net::CookieChangeCause::OVERWRITE:
        return mate::StringToV8(isolate, "overwrite");
      case net::CookieChangeCause::EXPIRED:
        return mate::StringToV8(isolate, "expired");
      case net::CookieChangeCause::EVICTED:
        return mate::StringToV8(isolate, "evicted");
      case net::CookieChangeCause::EXPIRED_OVERWRITE:
        return mate::StringToV8(isolate, "expired-overwrite");
      default:
        return mate::StringToV8(isolate, "unknown");
    }
  }
};

}  // namespace mate

namespace atom {

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
bool MatchesCookie(const base::DictionaryValue* filter,
                   const net::CanonicalCookie& cookie) {
  std::string str;
  bool b;
  if (filter->GetString("name", &str) && str != cookie.Name())
    return false;
  if (filter->GetString("path", &str) && str != cookie.Path())
    return false;
  if (filter->GetString("domain", &str) && !MatchesDomain(str, cookie.Domain()))
    return false;
  if (filter->GetBoolean("secure", &b) && b != cookie.IsSecure())
    return false;
  if (filter->GetBoolean("session", &b) && b != !cookie.IsPersistent())
    return false;
  return true;
}

// Helper to returns the CookieStore.
inline net::CookieStore* GetCookieStore(
    scoped_refptr<net::URLRequestContextGetter> getter) {
  return getter->GetURLRequestContext()->cookie_store();
}

// Run |callback| on UI thread.
void RunCallbackInUI(const base::Closure& callback) {
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE, callback);
}

// Remove cookies from |list| not matching |filter|, and pass it to |callback|.
void FilterCookies(std::unique_ptr<base::DictionaryValue> filter,
                   const Cookies::GetCallback& callback,
                   const net::CookieList& list) {
  net::CookieList result;
  for (const auto& cookie : list) {
    if (MatchesCookie(filter.get(), cookie))
      result.push_back(cookie);
  }
  RunCallbackInUI(base::Bind(callback, Cookies::SUCCESS, result));
}

// Receives cookies matching |filter| in IO thread.
void GetCookiesOnIO(scoped_refptr<net::URLRequestContextGetter> getter,
                    std::unique_ptr<base::DictionaryValue> filter,
                    const Cookies::GetCallback& callback) {
  std::string url;
  filter->GetString("url", &url);

  auto filtered_callback =
      base::Bind(FilterCookies, base::Passed(&filter), callback);

  // Empty url will match all url cookies.
  if (url.empty())
    GetCookieStore(getter)->GetAllCookiesAsync(filtered_callback);
  else
    GetCookieStore(getter)->GetAllCookiesForURLAsync(GURL(url),
                                                     filtered_callback);
}

// Removes cookie with |url| and |name| in IO thread.
void RemoveCookieOnIOThread(scoped_refptr<net::URLRequestContextGetter> getter,
                            const GURL& url,
                            const std::string& name,
                            const base::Closure& callback) {
  GetCookieStore(getter)->DeleteCookieAsync(
      url, name, base::BindOnce(RunCallbackInUI, callback));
}

// Callback of SetCookie.
void OnSetCookie(const Cookies::SetCallback& callback, bool success) {
  RunCallbackInUI(
      base::Bind(callback, success ? Cookies::SUCCESS : Cookies::FAILED));
}

// Flushes cookie store in IO thread.
void FlushCookieStoreOnIOThread(
    scoped_refptr<net::URLRequestContextGetter> getter,
    const base::Closure& callback) {
  GetCookieStore(getter)->FlushStore(base::BindOnce(RunCallbackInUI, callback));
}

// Sets cookie with |details| in IO thread.
void SetCookieOnIO(scoped_refptr<net::URLRequestContextGetter> getter,
                   std::unique_ptr<base::DictionaryValue> details,
                   const Cookies::SetCallback& callback) {
  std::string url, name, value, domain, path;
  bool secure = false;
  bool http_only = false;
  double creation_date;
  double expiration_date;
  double last_access_date;
  details->GetString("url", &url);
  details->GetString("name", &name);
  details->GetString("value", &value);
  details->GetString("domain", &domain);
  details->GetString("path", &path);
  details->GetBoolean("secure", &secure);
  details->GetBoolean("httpOnly", &http_only);

  base::Time creation_time;
  if (details->GetDouble("creationDate", &creation_date)) {
    creation_time = (creation_date == 0)
                        ? base::Time::UnixEpoch()
                        : base::Time::FromDoubleT(creation_date);
  }

  base::Time expiration_time;
  if (details->GetDouble("expirationDate", &expiration_date)) {
    expiration_time = (expiration_date == 0)
                          ? base::Time::UnixEpoch()
                          : base::Time::FromDoubleT(expiration_date);
  }

  base::Time last_access_time;
  if (details->GetDouble("lastAccessDate", &last_access_date)) {
    last_access_time = (last_access_date == 0)
                           ? base::Time::UnixEpoch()
                           : base::Time::FromDoubleT(last_access_date);
  }

  std::unique_ptr<net::CanonicalCookie> canonical_cookie(
      net::CanonicalCookie::CreateSanitizedCookie(
          GURL(url), name, value, domain, path, creation_time, expiration_time,
          last_access_time, secure, http_only,
          net::CookieSameSite::DEFAULT_MODE, net::COOKIE_PRIORITY_DEFAULT));
  auto completion_callback = base::BindOnce(OnSetCookie, callback);
  if (!canonical_cookie || !canonical_cookie->IsCanonical()) {
    std::move(completion_callback).Run(false);
    return;
  }
  if (url.empty()) {
    std::move(completion_callback).Run(false);
    return;
  }
  if (name.empty()) {
    std::move(completion_callback).Run(false);
    return;
  }
  GetCookieStore(getter)->SetCanonicalCookieAsync(
      std::move(canonical_cookie), secure, http_only,
      std::move(completion_callback));
}

}  // namespace

Cookies::Cookies(v8::Isolate* isolate, AtomBrowserContext* browser_context)
    : browser_context_(browser_context) {
  Init(isolate);
  cookie_change_subscription_ =
      browser_context->GetRequestContextDelegate()
          ->RegisterCookieChangeCallback(
              base::Bind(&Cookies::OnCookieChanged, base::Unretained(this)));
}

Cookies::~Cookies() {}

void Cookies::Get(const base::DictionaryValue& filter,
                  const GetCallback& callback) {
  auto copy = base::DictionaryValue::From(
      base::Value::ToUniquePtrValue(filter.Clone()));
  auto* getter = browser_context_->GetRequestContext();
  content::BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::BindOnce(GetCookiesOnIO, base::RetainedRef(getter), std::move(copy),
                     callback));
}

void Cookies::Remove(const GURL& url,
                     const std::string& name,
                     const base::Closure& callback) {
  auto* getter = browser_context_->GetRequestContext();
  content::BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::BindOnce(RemoveCookieOnIOThread, base::RetainedRef(getter), url,
                     name, callback));
}

void Cookies::Set(const base::DictionaryValue& details,
                  const SetCallback& callback) {
  auto copy = base::DictionaryValue::From(
      base::Value::ToUniquePtrValue(details.Clone()));
  auto* getter = browser_context_->GetRequestContext();
  content::BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::BindOnce(SetCookieOnIO, base::RetainedRef(getter), std::move(copy),
                     callback));
}

void Cookies::FlushStore(const base::Closure& callback) {
  auto* getter = browser_context_->GetRequestContext();
  content::BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::BindOnce(FlushCookieStoreOnIOThread, base::RetainedRef(getter),
                     callback));
}

void Cookies::OnCookieChanged(const CookieDetails* details) {
  Emit("changed", *(details->cookie), details->cause, details->removed);
}

// static
mate::Handle<Cookies> Cookies::Create(v8::Isolate* isolate,
                                      AtomBrowserContext* browser_context) {
  return mate::CreateHandle(isolate, new Cookies(isolate, browser_context));
}

// static
void Cookies::BuildPrototype(v8::Isolate* isolate,
                             v8::Local<v8::FunctionTemplate> prototype) {
  prototype->SetClassName(mate::StringToV8(isolate, "Cookies"));
  mate::ObjectTemplateBuilder(isolate, prototype->PrototypeTemplate())
      .SetMethod("get", &Cookies::Get)
      .SetMethod("remove", &Cookies::Remove)
      .SetMethod("set", &Cookies::Set)
      .SetMethod("flushStore", &Cookies::FlushStore);
}

}  // namespace api

}  // namespace atom
