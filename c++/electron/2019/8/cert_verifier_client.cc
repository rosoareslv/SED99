// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <utility>

#include "shell/browser/net/cert_verifier_client.h"
#include "shell/common/native_mate_converters/net_converter.h"

namespace electron {

VerifyRequestParams::VerifyRequestParams() = default;

VerifyRequestParams::~VerifyRequestParams() = default;

VerifyRequestParams::VerifyRequestParams(const VerifyRequestParams&) = default;

CertVerifierClient::CertVerifierClient(CertVerifyProc proc)
    : cert_verify_proc_(proc) {}

CertVerifierClient::~CertVerifierClient() = default;

void CertVerifierClient::Verify(
    int default_error,
    const net::CertVerifyResult& default_result,
    const scoped_refptr<net::X509Certificate>& certificate,
    const std::string& hostname,
    int flags,
    const base::Optional<std::string>& ocsp_response,
    VerifyCallback callback) {
  VerifyRequestParams params;
  params.hostname = hostname;
  params.default_result = net::ErrorToString(default_error);
  params.error_code = default_error;
  params.certificate = certificate;
  cert_verify_proc_.Run(
      params,
      base::AdaptCallbackForRepeating(base::BindOnce(
          [](VerifyCallback callback, const net::CertVerifyResult& result,
             int err) { std::move(callback).Run(err, result); },
          std::move(callback), default_result)));
}

}  // namespace electron
