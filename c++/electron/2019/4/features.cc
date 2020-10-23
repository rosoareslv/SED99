// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/common/node_includes.h"
#include "electron/buildflags/buildflags.h"
#include "native_mate/dictionary.h"
#include "printing/buildflags/buildflags.h"

namespace {

bool IsDesktopCapturerEnabled() {
  return BUILDFLAG(ENABLE_DESKTOP_CAPTURER);
}

bool IsOffscreenRenderingEnabled() {
  return BUILDFLAG(ENABLE_OSR);
}

bool IsPDFViewerEnabled() {
  return BUILDFLAG(ENABLE_PDF_VIEWER);
}

bool IsRunAsNodeEnabled() {
  return BUILDFLAG(ENABLE_RUN_AS_NODE);
}

bool IsFakeLocationProviderEnabled() {
  return BUILDFLAG(OVERRIDE_LOCATION_PROVIDER);
}

bool IsViewApiEnabled() {
  return BUILDFLAG(ENABLE_VIEW_API);
}

bool IsTtsEnabled() {
  return BUILDFLAG(ENABLE_TTS);
}

bool IsPrintingEnabled() {
  return BUILDFLAG(ENABLE_PRINTING);
}

bool IsComponentBuild() {
#if defined(COMPONENT_BUILD)
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
  dict.SetMethod("isDesktopCapturerEnabled", &IsDesktopCapturerEnabled);
  dict.SetMethod("isOffscreenRenderingEnabled", &IsOffscreenRenderingEnabled);
  dict.SetMethod("isPDFViewerEnabled", &IsPDFViewerEnabled);
  dict.SetMethod("isRunAsNodeEnabled", &IsRunAsNodeEnabled);
  dict.SetMethod("isFakeLocationProviderEnabled",
                 &IsFakeLocationProviderEnabled);
  dict.SetMethod("isViewApiEnabled", &IsViewApiEnabled);
  dict.SetMethod("isTtsEnabled", &IsTtsEnabled);
  dict.SetMethod("isPrintingEnabled", &IsPrintingEnabled);
  dict.SetMethod("isComponentBuild", &IsComponentBuild);
}

}  // namespace

NODE_LINKED_MODULE_CONTEXT_AWARE(atom_common_features, Initialize)
