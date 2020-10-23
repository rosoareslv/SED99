// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/renderer/atom_sandboxed_renderer_client.h"

#include "atom/common/api/api_messages.h"
#include "atom/common/api/atom_bindings.h"
#include "atom/common/application_info.h"
#include "atom/common/native_mate_converters/string16_converter.h"
#include "atom/common/native_mate_converters/value_converter.h"
#include "atom/common/node_bindings.h"
#include "atom/common/options_switches.h"
#include "atom/renderer/api/atom_api_renderer_ipc.h"
#include "atom/renderer/atom_render_frame_observer.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/process/process_handle.h"
#include "content/public/renderer/render_frame.h"
#include "gin/converter.h"
#include "native_mate/dictionary.h"
#include "third_party/blink/public/web/blink.h"
#include "third_party/blink/public/web/web_document.h"

#include "atom/common/node_includes.h"
#include "atom_natives.h"  // NOLINT: This file is generated with js2c

namespace atom {

namespace {

const char kIpcKey[] = "ipcNative";
const char kModuleCacheKey[] = "native-module-cache";

bool IsDevTools(content::RenderFrame* render_frame) {
  return render_frame->GetWebFrame()->GetDocument().Url().ProtocolIs(
      "chrome-devtools");
}

bool IsDevToolsExtension(content::RenderFrame* render_frame) {
  return render_frame->GetWebFrame()->GetDocument().Url().ProtocolIs(
      "chrome-extension");
}

v8::Local<v8::Object> GetModuleCache(v8::Isolate* isolate) {
  mate::Dictionary global(isolate, isolate->GetCurrentContext()->Global());
  v8::Local<v8::Value> cache;

  if (!global.GetHidden(kModuleCacheKey, &cache)) {
    cache = v8::Object::New(isolate);
    global.SetHidden(kModuleCacheKey, cache);
  }

  return cache->ToObject(isolate);
}

// adapted from node.cc
v8::Local<v8::Value> GetBinding(v8::Isolate* isolate,
                                v8::Local<v8::String> key,
                                mate::Arguments* margs) {
  v8::Local<v8::Object> exports;
  std::string module_key = gin::V8ToString(isolate, key);
  mate::Dictionary cache(isolate, GetModuleCache(isolate));

  if (cache.Get(module_key.c_str(), &exports)) {
    return exports;
  }

  auto* mod = node::get_builtin_module(module_key.c_str());

  if (!mod) {
    char errmsg[1024];
    snprintf(errmsg, sizeof(errmsg), "No such module: %s", module_key.c_str());
    margs->ThrowError(errmsg);
    return exports;
  }

  exports = v8::Object::New(isolate);
  DCHECK_EQ(mod->nm_register_func, nullptr);
  DCHECK_NE(mod->nm_context_register_func, nullptr);
  mod->nm_context_register_func(exports, v8::Null(isolate),
                                isolate->GetCurrentContext(), mod->nm_priv);
  cache.Set(module_key.c_str(), exports);
  return exports;
}

v8::Local<v8::Value> CreatePreloadScript(v8::Isolate* isolate,
                                         v8::Local<v8::String> preloadSrc) {
  return RendererClientBase::RunScript(isolate->GetCurrentContext(),
                                       preloadSrc);
}

class AtomSandboxedRenderFrameObserver : public AtomRenderFrameObserver {
 public:
  AtomSandboxedRenderFrameObserver(content::RenderFrame* render_frame,
                                   AtomSandboxedRendererClient* renderer_client)
      : AtomRenderFrameObserver(render_frame, renderer_client),
        renderer_client_(renderer_client) {}

 protected:
  void EmitIPCEvent(blink::WebLocalFrame* frame,
                    bool internal,
                    const std::string& channel,
                    const base::ListValue& args,
                    int32_t sender_id) override {
    if (!frame)
      return;

    auto* isolate = blink::MainThreadIsolate();
    v8::HandleScope handle_scope(isolate);
    auto context = frame->MainWorldScriptContext();
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Value> argv[] = {mate::ConvertToV8(isolate, channel),
                                   mate::ConvertToV8(isolate, args),
                                   mate::ConvertToV8(isolate, sender_id)};
    renderer_client_->InvokeIpcCallback(
        context, internal ? "onInternalMessage" : "onMessage",
        std::vector<v8::Local<v8::Value>>(argv, argv + node::arraysize(argv)));
  }

 private:
  AtomSandboxedRendererClient* renderer_client_;
  DISALLOW_COPY_AND_ASSIGN(AtomSandboxedRenderFrameObserver);
};

}  // namespace

AtomSandboxedRendererClient::AtomSandboxedRendererClient() {
  // Explicitly register electron's builtin modules.
  NodeBindings::RegisterBuiltinModules();
  metrics_ = base::ProcessMetrics::CreateCurrentProcessMetrics();
}

AtomSandboxedRendererClient::~AtomSandboxedRendererClient() {}

void AtomSandboxedRendererClient::InitializeBindings(
    v8::Local<v8::Object> binding,
    v8::Local<v8::Context> context) {
  auto* isolate = context->GetIsolate();
  mate::Dictionary b(isolate, binding);
  b.SetMethod("get", GetBinding);
  b.SetMethod("createPreloadScript", CreatePreloadScript);

  mate::Dictionary process = mate::Dictionary::CreateEmpty(isolate);
  b.Set("process", process);

  AtomBindings::BindProcess(isolate, &process, metrics_.get());

  process.Set("argv", base::CommandLine::ForCurrentProcess()->argv());
  process.SetReadOnly("pid", base::GetCurrentProcId());
  process.SetReadOnly("sandboxed", true);
  process.SetReadOnly("type", "renderer");

  // Pass in CLI flags needed to setup the renderer
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kGuestInstanceID))
    b.Set(options::kGuestInstanceID,
          command_line->GetSwitchValueASCII(switches::kGuestInstanceID));
}

void AtomSandboxedRendererClient::RenderFrameCreated(
    content::RenderFrame* render_frame) {
  new AtomSandboxedRenderFrameObserver(render_frame, this);
  RendererClientBase::RenderFrameCreated(render_frame);
}

void AtomSandboxedRendererClient::RenderViewCreated(
    content::RenderView* render_view) {
  RendererClientBase::RenderViewCreated(render_view);
}

void AtomSandboxedRendererClient::DidCreateScriptContext(
    v8::Handle<v8::Context> context,
    content::RenderFrame* render_frame) {
  RendererClientBase::DidCreateScriptContext(context, render_frame);

  // Only allow preload for the main frame or
  // For devtools we still want to run the preload_bundle script
  if (!render_frame->IsMainFrame() && !IsDevTools(render_frame) &&
      !IsDevToolsExtension(render_frame))
    return;

  auto* isolate = context->GetIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(context);
  // Wrap the bundle into a function that receives the binding object and the
  // preload script path as arguments.
  std::string left = "(function(binding, require) {\n";
  std::string right = "\n})";
  // Compile the wrapper and run it to get the function object
  auto source = v8::String::Concat(
      isolate, mate::ConvertToV8(isolate, left)->ToString(isolate),
      v8::String::Concat(isolate,
                         node::preload_bundle_value.ToStringChecked(isolate),
                         mate::ConvertToV8(isolate, right)->ToString(isolate)));
  auto result = RunScript(context, source);

  DCHECK(result->IsFunction());
  // Create and initialize the binding object
  auto binding = v8::Object::New(isolate);
  InitializeBindings(binding, context);
  AddRenderBindings(isolate, binding);
  v8::Local<v8::Value> args[] = {binding};
  // Execute the function with proper arguments
  ignore_result(result.As<v8::Function>()->Call(context, v8::Null(isolate),
                                                node::arraysize(args), args));
}

void AtomSandboxedRendererClient::WillReleaseScriptContext(
    v8::Handle<v8::Context> context,
    content::RenderFrame* render_frame) {
  // Only allow preload for the main frame
  if (!render_frame->IsMainFrame())
    return;

  auto* isolate = context->GetIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(context);
  InvokeIpcCallback(context, "onExit", std::vector<v8::Local<v8::Value>>());
}

void AtomSandboxedRendererClient::InvokeIpcCallback(
    v8::Handle<v8::Context> context,
    const std::string& callback_name,
    std::vector<v8::Handle<v8::Value>> args) {
  auto* isolate = context->GetIsolate();
  auto binding_key = mate::ConvertToV8(isolate, kIpcKey)->ToString(isolate);
  auto private_binding_key = v8::Private::ForApi(isolate, binding_key);
  auto global_object = context->Global();
  v8::Local<v8::Value> value;
  if (!global_object->GetPrivate(context, private_binding_key).ToLocal(&value))
    return;
  if (value.IsEmpty() || !value->IsObject())
    return;
  auto binding = value->ToObject(isolate);
  auto callback_key =
      mate::ConvertToV8(isolate, callback_name)->ToString(isolate);
  auto callback_value = binding->Get(callback_key);
  DCHECK(callback_value->IsFunction());  // set by sandboxed_renderer/init.js
  auto callback = v8::Handle<v8::Function>::Cast(callback_value);
  ignore_result(callback->Call(context, binding, args.size(), args.data()));
}

}  // namespace atom
