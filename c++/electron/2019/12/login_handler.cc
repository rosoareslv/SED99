// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/login_handler.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/callback.h"
#include "base/strings/string16.h"
#include "gin/arguments.h"
#include "gin/dictionary.h"
#include "shell/browser/api/atom_api_web_contents.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/gurl_converter.h"
#include "shell/common/gin_converters/net_converter.h"
#include "shell/common/gin_converters/value_converter.h"

using content::BrowserThread;

namespace electron {

LoginHandler::LoginHandler(
    const net::AuthChallengeInfo& auth_info,
    content::WebContents* web_contents,
    bool is_main_frame,
    const GURL& url,
    scoped_refptr<net::HttpResponseHeaders> response_headers,
    bool first_auth_attempt,
    LoginAuthRequiredCallback auth_required_callback)

    : WebContentsObserver(web_contents),
      auth_required_callback_(std::move(auth_required_callback)) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  base::PostTask(
      FROM_HERE, {base::CurrentThread()},
      base::BindOnce(&LoginHandler::EmitEvent, weak_factory_.GetWeakPtr(),
                     auth_info, is_main_frame, url, response_headers,
                     first_auth_attempt));
}

void LoginHandler::EmitEvent(
    net::AuthChallengeInfo auth_info,
    bool is_main_frame,
    const GURL& url,
    scoped_refptr<net::HttpResponseHeaders> response_headers,
    bool first_auth_attempt) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();

  auto api_web_contents = api::WebContents::From(isolate, web_contents());
  if (api_web_contents.IsEmpty()) {
    std::move(auth_required_callback_).Run(base::nullopt);
    return;
  }

  v8::HandleScope scope(isolate);

  auto details = gin::Dictionary::CreateEmpty(isolate);
  details.Set("url", url);

  // These parameters aren't documented, and I'm not sure that they're useful,
  // but we might as well stick 'em on the details object. If it turns out they
  // are useful, we can add them to the docs :)
  details.Set("isMainFrame", is_main_frame);
  details.Set("firstAuthAttempt", first_auth_attempt);
  details.Set("responseHeaders", response_headers.get());

  bool default_prevented =
      api_web_contents->Emit("login", std::move(details), auth_info,
                             base::BindOnce(&LoginHandler::CallbackFromJS,
                                            weak_factory_.GetWeakPtr()));
  if (!default_prevented && auth_required_callback_) {
    std::move(auth_required_callback_).Run(base::nullopt);
  }
}

LoginHandler::~LoginHandler() = default;

void LoginHandler::CallbackFromJS(gin::Arguments* args) {
  if (auth_required_callback_) {
    base::string16 username, password;
    if (!args->GetNext(&username) || !args->GetNext(&password)) {
      std::move(auth_required_callback_).Run(base::nullopt);
      return;
    }
    std::move(auth_required_callback_)
        .Run(net::AuthCredentials(username, password));
  }
}

}  // namespace electron
