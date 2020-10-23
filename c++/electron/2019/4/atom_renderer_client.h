// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ATOM_RENDERER_ATOM_RENDERER_CLIENT_H_
#define ATOM_RENDERER_ATOM_RENDERER_CLIENT_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "atom/renderer/renderer_client_base.h"

namespace node {
class Environment;
}  // namespace node

namespace atom {

class ElectronBindings;
class NodeBindings;

class AtomRendererClient : public RendererClientBase {
 public:
  AtomRendererClient();
  ~AtomRendererClient() override;

  // atom::RendererClientBase:
  void DidCreateScriptContext(v8::Handle<v8::Context> context,
                              content::RenderFrame* render_frame) override;
  void WillReleaseScriptContext(v8::Handle<v8::Context> context,
                                content::RenderFrame* render_frame) override;
  void SetupMainWorldOverrides(v8::Handle<v8::Context> context,
                               content::RenderFrame* render_frame) override;
  void SetupExtensionWorldOverrides(v8::Handle<v8::Context> context,
                                    content::RenderFrame* render_frame,
                                    int world_id) override;

 private:
  // content::ContentRendererClient:
  void RenderFrameCreated(content::RenderFrame*) override;
  void RunScriptsAtDocumentStart(content::RenderFrame* render_frame) override;
  void RunScriptsAtDocumentEnd(content::RenderFrame* render_frame) override;
  bool ShouldFork(blink::WebLocalFrame* frame,
                  const GURL& url,
                  const std::string& http_method,
                  bool is_initial_navigation,
                  bool is_server_redirect) override;
  void DidInitializeWorkerContextOnWorkerThread(
      v8::Local<v8::Context> context) override;
  void WillDestroyWorkerContextOnWorkerThread(
      v8::Local<v8::Context> context) override;

  node::Environment* GetEnvironment(content::RenderFrame* frame) const;

  // Whether the node integration has been initialized.
  bool node_integration_initialized_ = false;

  std::unique_ptr<NodeBindings> node_bindings_;
  std::unique_ptr<ElectronBindings> electron_bindings_;

  // The node::Environment::GetCurrent API does not return nullptr when it
  // is called for a context without node::Environment, so we have to keep
  // a book of the environments created.
  std::set<node::Environment*> environments_;

  // Getting main script context from web frame would lazily initializes
  // its script context. Doing so in a web page without scripts would trigger
  // assertion, so we have to keep a book of injected web frames.
  std::set<content::RenderFrame*> injected_frames_;

  DISALLOW_COPY_AND_ASSIGN(AtomRendererClient);
};

}  // namespace atom

#endif  // ATOM_RENDERER_ATOM_RENDERER_CLIENT_H_
