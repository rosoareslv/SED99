// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef SHELL_BROWSER_API_ATOM_API_DEBUGGER_H_
#define SHELL_BROWSER_API_ATOM_API_DEBUGGER_H_

#include <map>
#include <string>

#include "base/callback.h"
#include "base/values.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/web_contents_observer.h"
#include "gin/handle.h"
#include "shell/common/gin_helper/promise.h"
#include "shell/common/gin_helper/trackable_object.h"

namespace content {
class DevToolsAgentHost;
class WebContents;
}  // namespace content

namespace electron {

namespace api {

class Debugger : public gin_helper::TrackableObject<Debugger>,
                 public content::DevToolsAgentHostClient,
                 public content::WebContentsObserver {
 public:
  static gin::Handle<Debugger> Create(v8::Isolate* isolate,
                                      content::WebContents* web_contents);

  // gin_helper::TrackableObject:
  static void BuildPrototype(v8::Isolate* isolate,
                             v8::Local<v8::FunctionTemplate> prototype);

 protected:
  Debugger(v8::Isolate* isolate, content::WebContents* web_contents);
  ~Debugger() override;

  // content::DevToolsAgentHostClient:
  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override;
  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               const std::string& message) override;

  // content::WebContentsObserver:
  void RenderFrameHostChanged(content::RenderFrameHost* old_rfh,
                              content::RenderFrameHost* new_rfh) override;

 private:
  using PendingRequestMap =
      std::map<int, gin_helper::Promise<base::DictionaryValue>>;

  void Attach(gin_helper::Arguments* args);
  bool IsAttached();
  void Detach();
  v8::Local<v8::Promise> SendCommand(gin_helper::Arguments* args);
  void ClearPendingRequests();

  content::WebContents* web_contents_;  // Weak Reference.
  scoped_refptr<content::DevToolsAgentHost> agent_host_;

  PendingRequestMap pending_requests_;
  int previous_request_id_ = 0;

  DISALLOW_COPY_AND_ASSIGN(Debugger);
};

}  // namespace api

}  // namespace electron

#endif  // SHELL_BROWSER_API_ATOM_API_DEBUGGER_H_
