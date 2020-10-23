// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHELL_BROWSER_EXTENSIONS_ATOM_EXTENSION_LOADER_H_
#define SHELL_BROWSER_EXTENSIONS_ATOM_EXTENSION_LOADER_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/common/extension_id.h"

namespace base {
class FilePath;
}  // namespace base

namespace content {
class BrowserContext;
}  // namespace content

namespace extensions {

class Extension;

// Handles extension loading and reloading using ExtensionRegistrar.
class AtomExtensionLoader : public ExtensionRegistrar::Delegate {
 public:
  explicit AtomExtensionLoader(content::BrowserContext* browser_context);
  ~AtomExtensionLoader() override;

  // Loads an unpacked extension from a directory synchronously. Returns the
  // extension on success, or nullptr otherwise.
  const Extension* LoadExtension(const base::FilePath& extension_dir);

  // Starts reloading the extension. A keep-alive is maintained until the
  // reload succeeds/fails. If the extension is an app, it will be launched upon
  // reloading.
  // This may invalidate references to the old Extension object, so it takes the
  // ID by value.
  void ReloadExtension(ExtensionId extension_id);

 private:
  // If the extension loaded successfully, enables it. If it's an app, launches
  // it. If the load failed, updates ShellKeepAliveRequester.
  void FinishExtensionReload(const ExtensionId old_extension_id,
                             scoped_refptr<const Extension> extension);

  // ExtensionRegistrar::Delegate:
  void PreAddExtension(const Extension* extension,
                       const Extension* old_extension) override;
  void PostActivateExtension(scoped_refptr<const Extension> extension) override;
  void PostDeactivateExtension(
      scoped_refptr<const Extension> extension) override;
  void LoadExtensionForReload(
      const ExtensionId& extension_id,
      const base::FilePath& path,
      ExtensionRegistrar::LoadErrorBehavior load_error_behavior) override;
  bool CanEnableExtension(const Extension* extension) override;
  bool CanDisableExtension(const Extension* extension) override;
  bool ShouldBlockExtension(const Extension* extension) override;

  content::BrowserContext* browser_context_;  // Not owned.

  // Registers and unregisters extensions.
  ExtensionRegistrar extension_registrar_;

  // Holds keep-alives for relaunching apps.
  //   ShellKeepAliveRequester keep_alive_requester_;

  // Indicates that we posted the (asynchronous) task to start reloading.
  // Used by ReloadExtension() to check whether ExtensionRegistrar calls
  // LoadExtensionForReload().
  bool did_schedule_reload_ = false;

  base::WeakPtrFactory<AtomExtensionLoader> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(AtomExtensionLoader);
};

}  // namespace extensions

#endif  // SHELL_BROWSER_EXTENSIONS_ATOM_EXTENSION_LOADER_H_
