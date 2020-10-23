// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/common/api/remote_callback_freer.h"

#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "electron/atom/common/api/api.mojom.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"

namespace atom {

// static
void RemoteCallbackFreer::BindTo(v8::Isolate* isolate,
                                 v8::Local<v8::Object> target,
                                 const std::string& context_id,
                                 int object_id,
                                 content::WebContents* web_contents) {
  new RemoteCallbackFreer(isolate, target, context_id, object_id, web_contents);
}

RemoteCallbackFreer::RemoteCallbackFreer(v8::Isolate* isolate,
                                         v8::Local<v8::Object> target,
                                         const std::string& context_id,
                                         int object_id,
                                         content::WebContents* web_contents)
    : ObjectLifeMonitor(isolate, target),
      content::WebContentsObserver(web_contents),
      context_id_(context_id),
      object_id_(object_id) {}

RemoteCallbackFreer::~RemoteCallbackFreer() {}

void RemoteCallbackFreer::RunDestructor() {
  auto* channel = "ELECTRON_RENDERER_RELEASE_CALLBACK";
  base::ListValue args;
  int32_t sender_id = 0;
  args.AppendString(context_id_);
  args.AppendInteger(object_id_);
  auto* frame_host = web_contents()->GetMainFrame();
  if (frame_host) {
    mojom::ElectronRendererAssociatedPtr electron_ptr;
    frame_host->GetRemoteAssociatedInterfaces()->GetInterface(
        mojo::MakeRequest(&electron_ptr));
    electron_ptr->Message(true /* internal */, false /* send_to_all */, channel,
                          args.Clone(), sender_id);
  }

  Observe(nullptr);
}

void RemoteCallbackFreer::RenderViewDeleted(content::RenderViewHost*) {
  delete this;
}

}  // namespace atom
