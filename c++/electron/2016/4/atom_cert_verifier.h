// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ATOM_BROWSER_NET_ATOM_CERT_VERIFIER_H_
#define ATOM_BROWSER_NET_ATOM_CERT_VERIFIER_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "net/cert/cert_verifier.h"

namespace atom {

class AtomCertVerifier : public net::CertVerifier {
 public:
  AtomCertVerifier();
  virtual ~AtomCertVerifier();

  using VerifyProc =
      base::Callback<void(const std::string& hostname,
                          scoped_refptr<net::X509Certificate>,
                          const base::Callback<void(bool)>&)>;

  void SetVerifyProc(const VerifyProc& proc);

 protected:
  // net::CertVerifier:
  int Verify(net::X509Certificate* cert,
             const std::string& hostname,
             const std::string& ocsp_response,
             int flags,
             net::CRLSet* crl_set,
             net::CertVerifyResult* verify_result,
             const net::CompletionCallback& callback,
             scoped_ptr<Request>* out_req,
             const net::BoundNetLog& net_log) override;
  bool SupportsOCSPStapling() override;

 private:
  base::Lock lock_;
  VerifyProc verify_proc_;
  scoped_ptr<net::CertVerifier> default_cert_verifier_;

  DISALLOW_COPY_AND_ASSIGN(AtomCertVerifier);
};

}   // namespace atom

#endif  // ATOM_BROWSER_NET_ATOM_CERT_VERIFIER_H_
