// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/common/node_includes.h"
#include "native_mate/dictionary.h"

namespace {

bool IsPDFViewerEnabled() {
#if defined(ENABLE_PDF_VIEWER)
  return true;
#else
  return false;
#endif
}

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  mate::Dictionary dict(context->GetIsolate(), exports);
  dict.SetMethod("isPDFViewerEnabled", &IsPDFViewerEnabled);
}

}  // namespace

NODE_BUILTIN_MODULE_CONTEXT_AWARE(atom_common_features, Initialize)
