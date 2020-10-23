// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/renderer/atom_renderer_client.h"

#include <string>
#include <vector>

#include "atom/common/api/atom_bindings.h"
#include "atom/common/api/event_emitter_caller.h"
#include "atom/common/asar/asar_util.h"
#include "atom/common/node_bindings.h"
#include "atom/common/options_switches.h"
#include "atom/renderer/atom_render_frame_observer.h"
#include "atom/renderer/web_worker_observer.h"
#include "base/command_line.h"
#include "content/public/renderer/render_frame.h"
#include "native_mate/dictionary.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"

#include "atom/common/node_includes.h"
#include "atom_natives.h"  // NOLINT: This file is generated with js2c
#include "tracing/trace_event.h"

namespace atom {

namespace {

bool IsDevToolsExtension(content::RenderFrame* render_frame) {
  return static_cast<GURL>(render_frame->GetWebFrame()->GetDocument().Url())
      .SchemeIs("chrome-extension");
}

}  // namespace

AtomRendererClient::AtomRendererClient()
    : node_bindings_(NodeBindings::Create(NodeBindings::RENDERER)),
      atom_bindings_(new AtomBindings(uv_default_loop())) {}

AtomRendererClient::~AtomRendererClient() {
  asar::ClearArchives();
}

void AtomRendererClient::RenderThreadStarted() {
  RendererClientBase::RenderThreadStarted();
}

void AtomRendererClient::RenderFrameCreated(
    content::RenderFrame* render_frame) {
  new AtomRenderFrameObserver(render_frame, this);
  RendererClientBase::RenderFrameCreated(render_frame);
}

void AtomRendererClient::RenderViewCreated(content::RenderView* render_view) {
  RendererClientBase::RenderViewCreated(render_view);
}

void AtomRendererClient::RunScriptsAtDocumentStart(
    content::RenderFrame* render_frame) {
  // Inform the document start pharse.
  v8::HandleScope handle_scope(v8::Isolate::GetCurrent());
  node::Environment* env = GetEnvironment(render_frame);
  if (env)
    mate::EmitEvent(env->isolate(), env->process_object(), "document-start");
}

void AtomRendererClient::RunScriptsAtDocumentEnd(
    content::RenderFrame* render_frame) {
  // Inform the document end pharse.
  v8::HandleScope handle_scope(v8::Isolate::GetCurrent());
  node::Environment* env = GetEnvironment(render_frame);
  if (env)
    mate::EmitEvent(env->isolate(), env->process_object(), "document-end");
}

void AtomRendererClient::DidCreateScriptContext(
    v8::Handle<v8::Context> context,
    content::RenderFrame* render_frame) {
  RendererClientBase::DidCreateScriptContext(context, render_frame);

  // Only allow node integration for the main frame of the top window, unless it
  // is a devtools extension page. Allowing child frames or child windows to
  // have node integration would result in memory leak, since we don't destroy
  // node environment when script context is destroyed.
  //
  // DevTools extensions do not follow this rule because our implementation
  // requires node integration in iframes to work. And usually DevTools
  // extensions do not dynamically add/remove iframes.
  //
  // TODO(zcbenz): Do not create Node environment if node integration is not
  // enabled.
  if (!(render_frame->IsMainFrame() &&
        !render_frame->GetWebFrame()->Opener()) &&
      !IsDevToolsExtension(render_frame))
    return;

  injected_frames_.insert(render_frame);

  // Prepare the node bindings.
  if (!node_integration_initialized_) {
    node_integration_initialized_ = true;
    node_bindings_->Initialize();
    node_bindings_->PrepareMessageLoop();
  }

  // Setup node tracing controller.
  if (!node::tracing::TraceEventHelper::GetAgent())
    node::tracing::TraceEventHelper::SetAgent(node::CreateAgent());

  // Setup node environment for each window.
  node::Environment* env = node_bindings_->CreateEnvironment(context);
  environments_.insert(env);

  // Add Electron extended APIs.
  atom_bindings_->BindTo(env->isolate(), env->process_object());
  AddRenderBindings(env->isolate(), env->process_object());

  // Load everything.
  node_bindings_->LoadEnvironment(env);

  if (node_bindings_->uv_env() == nullptr) {
    // Make uv loop being wrapped by window context.
    node_bindings_->set_uv_env(env);

    // Give the node loop a run to make sure everything is ready.
    node_bindings_->RunMessageLoop();
  }
}

void AtomRendererClient::WillReleaseScriptContext(
    v8::Handle<v8::Context> context,
    content::RenderFrame* render_frame) {
  if (injected_frames_.find(render_frame) == injected_frames_.end())
    return;
  injected_frames_.erase(render_frame);

  node::Environment* env = node::Environment::GetCurrent(context);
  if (environments_.find(env) == environments_.end())
    return;
  environments_.erase(env);

  mate::EmitEvent(env->isolate(), env->process_object(), "exit");

  // The main frame may be replaced.
  if (env == node_bindings_->uv_env())
    node_bindings_->set_uv_env(nullptr);

  // Destroy the node environment.
  // This is disabled because pending async tasks may still use the environment
  // and would cause crashes later. Node does not seem to clear all async tasks
  // when the environment is destroyed.
  // node::FreeEnvironment(env);

  // AtomBindings is tracking node environments.
  atom_bindings_->EnvironmentDestroyed(env);
}

bool AtomRendererClient::ShouldFork(blink::WebLocalFrame* frame,
                                    const GURL& url,
                                    const std::string& http_method,
                                    bool is_initial_navigation,
                                    bool is_server_redirect) {
  // Handle all the navigations and reloads in browser.
  // FIXME We only support GET here because http method will be ignored when
  // the OpenURLFromTab is triggered, which means form posting would not work,
  // we should solve this by patching Chromium in future.
  return http_method == "GET";
}

void AtomRendererClient::DidInitializeWorkerContextOnWorkerThread(
    v8::Local<v8::Context> context) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kNodeIntegrationInWorker)) {
    WebWorkerObserver::GetCurrent()->ContextCreated(context);
  }
}

void AtomRendererClient::WillDestroyWorkerContextOnWorkerThread(
    v8::Local<v8::Context> context) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kNodeIntegrationInWorker)) {
    WebWorkerObserver::GetCurrent()->ContextWillDestroy(context);
  }
}

void AtomRendererClient::SetupMainWorldOverrides(
    v8::Handle<v8::Context> context,
    content::RenderFrame* render_frame) {
  // Setup window overrides in the main world context
  v8::Isolate* isolate = context->GetIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(context);

  // Wrap the bundle into a function that receives the isolatedWorld as
  // an argument.
  std::string left = "(function (nodeProcess, isolatedWorld) {\n";
  std::string right = "\n})";
  auto source = v8::String::Concat(
      isolate, mate::ConvertToV8(isolate, left)->ToString(isolate),
      v8::String::Concat(isolate,
                         node::isolated_bundle_value.ToStringChecked(isolate),
                         mate::ConvertToV8(isolate, right)->ToString(isolate)));
  auto result = RunScript(context, source);
  DCHECK(result->IsFunction());

  v8::Local<v8::Value> args[] = {
      GetEnvironment(render_frame)->process_object(),
      GetContext(render_frame->GetWebFrame(), isolate)->Global(),
  };
  ignore_result(result.As<v8::Function>()->Call(context, v8::Null(isolate),
                                                node::arraysize(args), args));
}

node::Environment* AtomRendererClient::GetEnvironment(
    content::RenderFrame* render_frame) const {
  if (injected_frames_.find(render_frame) == injected_frames_.end())
    return nullptr;
  v8::HandleScope handle_scope(v8::Isolate::GetCurrent());
  auto context =
      GetContext(render_frame->GetWebFrame(), v8::Isolate::GetCurrent());
  node::Environment* env = node::Environment::GetCurrent(context);
  if (environments_.find(env) == environments_.end())
    return nullptr;
  return env;
}

}  // namespace atom
