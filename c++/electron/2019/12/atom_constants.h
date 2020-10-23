// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef SHELL_COMMON_ATOM_CONSTANTS_H_
#define SHELL_COMMON_ATOM_CONSTANTS_H_

#include "build/build_config.h"
#include "electron/buildflags/buildflags.h"

namespace electron {

// The app-command in NativeWindow.
extern const char kBrowserForward[];
extern const char kBrowserBackward[];

// Header to ignore CORS.
extern const char kCORSHeader[];

// Strings describing Chrome security policy for DevTools security panel.
extern const char kSHA1Certificate[];
extern const char kSHA1MajorDescription[];
extern const char kSHA1MinorDescription[];
extern const char kCertificateError[];
extern const char kValidCertificate[];
extern const char kValidCertificateDescription[];
extern const char kSecureProtocol[];
extern const char kSecureProtocolDescription[];

#if defined(OS_WIN)
// Crashpad pipe name.
extern const char kCrashpadPipeName[];
#endif

#if BUILDFLAG(ENABLE_RUN_AS_NODE)
extern const char kRunAsNode[];
#endif

#if BUILDFLAG(ENABLE_PDF_VIEWER)
// The MIME type used for the PDF plugin.
extern const char kPdfPluginMimeType[];
extern const char kPdfPluginPath[];
extern const char kPdfPluginSrc[];

// Constants for PDF viewer webui.
extern const char kPdfViewerUIOrigin[];
extern const char kPdfViewerUIHost[];
#endif  // BUILDFLAG(ENABLE_PDF_VIEWER)

}  // namespace electron

#endif  // SHELL_COMMON_ATOM_CONSTANTS_H_
