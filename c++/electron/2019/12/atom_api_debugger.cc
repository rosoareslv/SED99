// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/atom_api_debugger.h"

#include <memory>
#include <string>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/web_contents.h"
#include "shell/common/gin_converters/value_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/gin_helper/object_template_builder.h"
#include "shell/common/node_includes.h"

using content::DevToolsAgentHost;

namespace electron {

namespace api {

Debugger::Debugger(v8::Isolate* isolate, content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents), web_contents_(web_contents) {
  Init(isolate);
}

Debugger::~Debugger() = default;

void Debugger::AgentHostClosed(DevToolsAgentHost* agent_host) {
  DCHECK(agent_host == agent_host_);
  agent_host_ = nullptr;
  ClearPendingRequests();
  Emit("detach", "target closed");
}

void Debugger::DispatchProtocolMessage(DevToolsAgentHost* agent_host,
                                       const std::string& message) {
  DCHECK(agent_host == agent_host_);

  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());

  std::unique_ptr<base::Value> parsed_message =
      base::JSONReader::ReadDeprecated(message);
  if (!parsed_message || !parsed_message->is_dict())
    return;
  base::DictionaryValue* dict =
      static_cast<base::DictionaryValue*>(parsed_message.get());
  int id;
  if (!dict->GetInteger("id", &id)) {
    std::string method;
    if (!dict->GetString("method", &method))
      return;
    base::DictionaryValue* params_value = nullptr;
    base::DictionaryValue params;
    if (dict->GetDictionary("params", &params_value))
      params.Swap(params_value);
    Emit("message", method, params);
  } else {
    auto it = pending_requests_.find(id);
    if (it == pending_requests_.end())
      return;

    gin_helper::Promise<base::DictionaryValue> promise = std::move(it->second);
    pending_requests_.erase(it);

    base::DictionaryValue* error = nullptr;
    if (dict->GetDictionary("error", &error)) {
      std::string message;
      error->GetString("message", &message);
      promise.RejectWithErrorMessage(message);
    } else {
      base::DictionaryValue* result_body = nullptr;
      base::DictionaryValue result;
      if (dict->GetDictionary("result", &result_body)) {
        result.Swap(result_body);
      }
      promise.Resolve(result);
    }
  }
}

void Debugger::RenderFrameHostChanged(content::RenderFrameHost* old_rfh,
                                      content::RenderFrameHost* new_rfh) {
  if (agent_host_) {
    agent_host_->DisconnectWebContents();
    auto* web_contents = content::WebContents::FromRenderFrameHost(new_rfh);
    agent_host_->ConnectWebContents(web_contents);
  }
}

void Debugger::Attach(gin_helper::Arguments* args) {
  std::string protocol_version;
  args->GetNext(&protocol_version);

  if (agent_host_) {
    args->ThrowError("Debugger is already attached to the target");
    return;
  }

  if (!protocol_version.empty() &&
      !DevToolsAgentHost::IsSupportedProtocolVersion(protocol_version)) {
    args->ThrowError("Requested protocol version is not supported");
    return;
  }

  agent_host_ = DevToolsAgentHost::GetOrCreateFor(web_contents_);
  if (!agent_host_) {
    args->ThrowError("No target available");
    return;
  }

  agent_host_->AttachClient(this);
}

bool Debugger::IsAttached() {
  return agent_host_ && agent_host_->IsAttached();
}

void Debugger::Detach() {
  if (!agent_host_)
    return;
  agent_host_->DetachClient(this);
  AgentHostClosed(agent_host_.get());
}

v8::Local<v8::Promise> Debugger::SendCommand(gin_helper::Arguments* args) {
  gin_helper::Promise<base::DictionaryValue> promise(isolate());
  v8::Local<v8::Promise> handle = promise.GetHandle();

  if (!agent_host_) {
    promise.RejectWithErrorMessage("No target available");
    return handle;
  }

  std::string method;
  if (!args->GetNext(&method)) {
    promise.RejectWithErrorMessage("Invalid method");
    return handle;
  }

  base::DictionaryValue command_params;
  args->GetNext(&command_params);

  base::DictionaryValue request;
  int request_id = ++previous_request_id_;
  pending_requests_.emplace(request_id, std::move(promise));
  request.SetInteger("id", request_id);
  request.SetString("method", method);
  if (!command_params.empty())
    request.Set("params",
                base::Value::ToUniquePtrValue(command_params.Clone()));

  std::string json_args;
  base::JSONWriter::Write(request, &json_args);
  agent_host_->DispatchProtocolMessage(this, json_args);

  return handle;
}

void Debugger::ClearPendingRequests() {
  for (auto& it : pending_requests_)
    it.second.RejectWithErrorMessage("target closed while handling command");
}

// static
gin::Handle<Debugger> Debugger::Create(v8::Isolate* isolate,
                                       content::WebContents* web_contents) {
  return gin::CreateHandle(isolate, new Debugger(isolate, web_contents));
}

// static
void Debugger::BuildPrototype(v8::Isolate* isolate,
                              v8::Local<v8::FunctionTemplate> prototype) {
  prototype->SetClassName(gin::StringToV8(isolate, "Debugger"));
  gin_helper::ObjectTemplateBuilder(isolate, prototype->PrototypeTemplate())
      .SetMethod("attach", &Debugger::Attach)
      .SetMethod("isAttached", &Debugger::IsAttached)
      .SetMethod("detach", &Debugger::Detach)
      .SetMethod("sendCommand", &Debugger::SendCommand);
}

}  // namespace api

}  // namespace electron

namespace {

using electron::api::Debugger;

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  gin_helper::Dictionary(isolate, exports)
      .Set("Debugger", Debugger::GetConstructor(isolate)
                           ->GetFunction(context)
                           .ToLocalChecked());
}

}  // namespace

NODE_LINKED_MODULE_CONTEXT_AWARE(atom_browser_debugger, Initialize)
