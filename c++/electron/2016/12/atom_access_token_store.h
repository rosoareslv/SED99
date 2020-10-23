// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ATOM_BROWSER_ATOM_ACCESS_TOKEN_STORE_H_
#define ATOM_BROWSER_ATOM_ACCESS_TOKEN_STORE_H_

#include "content/public/browser/access_token_store.h"

namespace atom {

class AtomBrowserContext;

namespace internal {
class TokenLoadingJob;
}

class AtomAccessTokenStore : public content::AccessTokenStore {
 public:
  AtomAccessTokenStore();
  ~AtomAccessTokenStore();

  // content::AccessTokenStore:
  void LoadAccessTokens(
      const LoadAccessTokensCallback& callback) override;
  void SaveAccessToken(const GURL& server_url,
                       const base::string16& access_token) override;

 private:
  void RunTokenLoadingJob(scoped_refptr<internal::TokenLoadingJob> job);

  scoped_refptr<AtomBrowserContext> browser_context_;
  DISALLOW_COPY_AND_ASSIGN(AtomAccessTokenStore);
};

}  // namespace atom

#endif  // ATOM_BROWSER_ATOM_ACCESS_TOKEN_STORE_H_
