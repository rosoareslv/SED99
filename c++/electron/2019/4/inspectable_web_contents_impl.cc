// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Copyright (c) 2013 Adam Roben <adam@roben.org>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#include <memory>
#include <utility>

#include "atom/browser/ui/inspectable_web_contents_impl.h"

#include "atom/browser/ui/inspectable_web_contents_delegate.h"
#include "atom/browser/ui/inspectable_web_contents_view.h"
#include "atom/browser/ui/inspectable_web_contents_view_delegate.h"
#include "atom/common/platform_util.h"
#include "base/base64.h"
#include "base/guid.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/metrics/histogram.h"
#include "base/stl_util.h"
#include "base/strings/pattern.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/post_task.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/user_agent.h"
#include "ipc/ipc_channel.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"

namespace atom {

namespace {

const double kPresetZoomFactors[] = {0.25, 0.333, 0.5,  0.666, 0.75, 0.9,
                                     1.0,  1.1,   1.25, 1.5,   1.75, 2.0,
                                     2.5,  3.0,   4.0,  5.0};

const char kChromeUIDevToolsURL[] =
    "chrome-devtools://devtools/bundled/devtools_app.html?"
    "remoteBase=%s&"
    "can_dock=%s&"
    "toolbarColor=rgba(223,223,223,1)&"
    "textColor=rgba(0,0,0,1)&"
    "experiments=true";
const char kChromeUIDevToolsRemoteFrontendBase[] =
    "https://chrome-devtools-frontend.appspot.com/";
const char kChromeUIDevToolsRemoteFrontendPath[] = "serve_file";

const char kDevToolsBoundsPref[] = "electron.devtools.bounds";
const char kDevToolsZoomPref[] = "electron.devtools.zoom";
const char kDevToolsPreferences[] = "electron.devtools.preferences";

const char kFrontendHostId[] = "id";
const char kFrontendHostMethod[] = "method";
const char kFrontendHostParams[] = "params";
const char kTitleFormat[] = "Developer Tools - %s";

const size_t kMaxMessageChunkSize = IPC::Channel::kMaximumMessageSize / 4;

base::Value RectToDictionary(const gfx::Rect& bounds) {
  base::Value dict(base::Value::Type::DICTIONARY);
  dict.SetKey("x", base::Value(bounds.x()));
  dict.SetKey("y", base::Value(bounds.y()));
  dict.SetKey("width", base::Value(bounds.width()));
  dict.SetKey("height", base::Value(bounds.height()));
  return dict;
}

gfx::Rect DictionaryToRect(const base::Value* dict) {
  const base::Value* found = dict->FindKey("x");
  int x = found ? found->GetInt() : 0;

  found = dict->FindKey("y");
  int y = found ? found->GetInt() : 0;

  found = dict->FindKey("width");
  int width = found ? found->GetInt() : 800;

  found = dict->FindKey("height");
  int height = found ? found->GetInt() : 600;

  return gfx::Rect(x, y, width, height);
}

bool IsPointInRect(const gfx::Point& point, const gfx::Rect& rect) {
  return point.x() > rect.x() && point.x() < (rect.width() + rect.x()) &&
         point.y() > rect.y() && point.y() < (rect.height() + rect.y());
}

bool IsPointInScreen(const gfx::Point& point) {
  for (const auto& display : display::Screen::GetScreen()->GetAllDisplays()) {
    if (IsPointInRect(point, display.bounds()))
      return true;
  }
  return false;
}

void SetZoomLevelForWebContents(content::WebContents* web_contents,
                                double level) {
  content::HostZoomMap::SetZoomLevel(web_contents, level);
}

double GetNextZoomLevel(double level, bool out) {
  double factor = content::ZoomLevelToZoomFactor(level);
  size_t size = base::size(kPresetZoomFactors);
  for (size_t i = 0; i < size; ++i) {
    if (!content::ZoomValuesEqual(kPresetZoomFactors[i], factor))
      continue;
    if (out && i > 0)
      return content::ZoomFactorToZoomLevel(kPresetZoomFactors[i - 1]);
    if (!out && i != size - 1)
      return content::ZoomFactorToZoomLevel(kPresetZoomFactors[i + 1]);
  }
  return level;
}

GURL GetRemoteBaseURL() {
  return GURL(base::StringPrintf("%s%s/%s/",
                                 kChromeUIDevToolsRemoteFrontendBase,
                                 kChromeUIDevToolsRemoteFrontendPath,
                                 content::GetWebKitRevision().c_str()));
}

GURL GetDevToolsURL(bool can_dock) {
  auto url_string = base::StringPrintf(kChromeUIDevToolsURL,
                                       GetRemoteBaseURL().spec().c_str(),
                                       can_dock ? "true" : "");
  return GURL(url_string);
}

}  // namespace

class InspectableWebContentsImpl::NetworkResourceLoader
    : public network::SimpleURLLoaderStreamConsumer {
 public:
  NetworkResourceLoader(int stream_id,
                        InspectableWebContentsImpl* bindings,
                        std::unique_ptr<network::SimpleURLLoader> loader,
                        network::mojom::URLLoaderFactory* url_loader_factory,
                        const DispatchCallback& callback)
      : stream_id_(stream_id),
        bindings_(bindings),
        loader_(std::move(loader)),
        callback_(callback) {
    loader_->SetOnResponseStartedCallback(base::BindOnce(
        &NetworkResourceLoader::OnResponseStarted, base::Unretained(this)));
    loader_->DownloadAsStream(url_loader_factory, this);
  }

  NetworkResourceLoader(const NetworkResourceLoader&) = delete;
  NetworkResourceLoader& operator=(const NetworkResourceLoader&) = delete;

 private:
  void OnResponseStarted(const GURL& final_url,
                         const network::ResourceResponseHead& response_head) {
    response_headers_ = response_head.headers;
  }

  void OnDataReceived(base::StringPiece chunk,
                      base::OnceClosure resume) override {
    base::Value chunkValue;

    bool encoded = !base::IsStringUTF8(chunk);
    if (encoded) {
      std::string encoded_string;
      base::Base64Encode(chunk, &encoded_string);
      chunkValue = base::Value(std::move(encoded_string));
    } else {
      chunkValue = base::Value(chunk);
    }
    base::Value id(stream_id_);
    base::Value encodedValue(encoded);

    bindings_->CallClientFunction("DevToolsAPI.streamWrite", &id, &chunkValue,
                                  &encodedValue);
    std::move(resume).Run();
  }

  void OnComplete(bool success) override {
    base::DictionaryValue response;
    response.SetInteger("statusCode", response_headers_
                                          ? response_headers_->response_code()
                                          : 200);

    auto headers = std::make_unique<base::DictionaryValue>();
    size_t iterator = 0;
    std::string name;
    std::string value;
    while (response_headers_ &&
           response_headers_->EnumerateHeaderLines(&iterator, &name, &value))
      headers->SetString(name, value);

    response.Set("headers", std::move(headers));
    callback_.Run(&response);

    bindings_->loaders_.erase(bindings_->loaders_.find(this));
  }

  void OnRetry(base::OnceClosure start_retry) override {}

  const int stream_id_;
  InspectableWebContentsImpl* const bindings_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  DispatchCallback callback_;
  scoped_refptr<net::HttpResponseHeaders> response_headers_;
};

// Implemented separately on each platform.
InspectableWebContentsView* CreateInspectableContentsView(
    InspectableWebContentsImpl* inspectable_web_contents_impl);

void InspectableWebContentsImpl::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kDevToolsBoundsPref,
                                   RectToDictionary(gfx::Rect(0, 0, 800, 600)));
  registry->RegisterDoublePref(kDevToolsZoomPref, 0.);
  registry->RegisterDictionaryPref(kDevToolsPreferences);
}

InspectableWebContentsImpl::InspectableWebContentsImpl(
    content::WebContents* web_contents,
    PrefService* pref_service,
    bool is_guest)
    : frontend_loaded_(false),
      can_dock_(true),
      delegate_(nullptr),
      pref_service_(pref_service),
      web_contents_(web_contents),
      is_guest_(is_guest),
      view_(CreateInspectableContentsView(this)),
      weak_factory_(this) {
  const base::Value* bounds_dict = pref_service_->Get(kDevToolsBoundsPref);
  if (bounds_dict->is_dict()) {
    devtools_bounds_ = DictionaryToRect(bounds_dict);
    // Sometimes the devtools window is out of screen or has too small size.
    if (devtools_bounds_.height() < 100 || devtools_bounds_.width() < 100) {
      devtools_bounds_.set_height(600);
      devtools_bounds_.set_width(800);
    }
    if (!IsPointInScreen(devtools_bounds_.origin())) {
      gfx::Rect display;
      if (!is_guest && web_contents->GetNativeView()) {
        display = display::Screen::GetScreen()
                      ->GetDisplayNearestView(web_contents->GetNativeView())
                      .bounds();
      } else {
        display = display::Screen::GetScreen()->GetPrimaryDisplay().bounds();
      }

      devtools_bounds_.set_x(display.x() +
                             (display.width() - devtools_bounds_.width()) / 2);
      devtools_bounds_.set_y(
          display.y() + (display.height() - devtools_bounds_.height()) / 2);
    }
  }
}

InspectableWebContentsImpl::~InspectableWebContentsImpl() {
  // Unsubscribe from devtools and Clean up resources.
  if (GetDevToolsWebContents()) {
    if (managed_devtools_web_contents_)
      managed_devtools_web_contents_->SetDelegate(nullptr);
    // Calling this also unsubscribes the observer, so WebContentsDestroyed
    // won't be called again.
    WebContentsDestroyed();
  }
  // Let destructor destroy managed_devtools_web_contents_.
}

InspectableWebContentsView* InspectableWebContentsImpl::GetView() const {
  return view_.get();
}

content::WebContents* InspectableWebContentsImpl::GetWebContents() const {
  return web_contents_.get();
}

content::WebContents* InspectableWebContentsImpl::GetDevToolsWebContents()
    const {
  if (external_devtools_web_contents_)
    return external_devtools_web_contents_;
  else
    return managed_devtools_web_contents_.get();
}

void InspectableWebContentsImpl::InspectElement(int x, int y) {
  if (agent_host_)
    agent_host_->InspectElement(web_contents_->GetMainFrame(), x, y);
}

void InspectableWebContentsImpl::SetDelegate(
    InspectableWebContentsDelegate* delegate) {
  delegate_ = delegate;
}

InspectableWebContentsDelegate* InspectableWebContentsImpl::GetDelegate()
    const {
  return delegate_;
}

bool InspectableWebContentsImpl::IsGuest() const {
  return is_guest_;
}

void InspectableWebContentsImpl::ReleaseWebContents() {
  web_contents_.release();
}

void InspectableWebContentsImpl::SetDockState(const std::string& state) {
  if (state == "detach") {
    can_dock_ = false;
  } else {
    can_dock_ = true;
    dock_state_ = state;
  }
}

void InspectableWebContentsImpl::SetDevToolsWebContents(
    content::WebContents* devtools) {
  if (!managed_devtools_web_contents_)
    external_devtools_web_contents_ = devtools;
}

void InspectableWebContentsImpl::ShowDevTools(bool activate) {
  if (embedder_message_dispatcher_) {
    if (managed_devtools_web_contents_)
      view_->ShowDevTools(activate);
    return;
  }

  activate_ = activate;

  // Show devtools only after it has done loading, this is to make sure the
  // SetIsDocked is called *BEFORE* ShowDevTools.
  embedder_message_dispatcher_ =
      DevToolsEmbedderMessageDispatcher::CreateForDevToolsFrontend(this);

  if (!external_devtools_web_contents_) {  // no external devtools
    managed_devtools_web_contents_ = content::WebContents::Create(
        content::WebContents::CreateParams(web_contents_->GetBrowserContext()));
    managed_devtools_web_contents_->SetDelegate(this);
  }

  Observe(GetDevToolsWebContents());
  AttachTo(content::DevToolsAgentHost::GetOrCreateFor(web_contents_.get()));

  GetDevToolsWebContents()->GetController().LoadURL(
      GetDevToolsURL(can_dock_), content::Referrer(),
      ui::PAGE_TRANSITION_AUTO_TOPLEVEL, std::string());
}

void InspectableWebContentsImpl::CloseDevTools() {
  if (GetDevToolsWebContents()) {
    frontend_loaded_ = false;
    if (managed_devtools_web_contents_) {
      view_->CloseDevTools();
      managed_devtools_web_contents_.reset();
    }
    embedder_message_dispatcher_.reset();
    if (!IsGuest())
      web_contents_->Focus();
  }
}

bool InspectableWebContentsImpl::IsDevToolsViewShowing() {
  return managed_devtools_web_contents_ && view_->IsDevToolsViewShowing();
}

void InspectableWebContentsImpl::AttachTo(
    scoped_refptr<content::DevToolsAgentHost> host) {
  Detach();
  agent_host_ = std::move(host);
  // We could use ForceAttachClient here if problem arises with
  // devtools multiple session support.
  agent_host_->AttachClient(this);
}

void InspectableWebContentsImpl::Detach() {
  if (agent_host_)
    agent_host_->DetachClient(this);
  agent_host_ = nullptr;
}

void InspectableWebContentsImpl::Reattach(const DispatchCallback& callback) {
  if (agent_host_) {
    agent_host_->DetachClient(this);
    agent_host_->AttachClient(this);
  }
  callback.Run(nullptr);
}

void InspectableWebContentsImpl::CallClientFunction(
    const std::string& function_name,
    const base::Value* arg1,
    const base::Value* arg2,
    const base::Value* arg3) {
  if (!GetDevToolsWebContents())
    return;

  std::string javascript = function_name + "(";
  if (arg1) {
    std::string json;
    base::JSONWriter::Write(*arg1, &json);
    javascript.append(json);
    if (arg2) {
      base::JSONWriter::Write(*arg2, &json);
      javascript.append(", ").append(json);
      if (arg3) {
        base::JSONWriter::Write(*arg3, &json);
        javascript.append(", ").append(json);
      }
    }
  }
  javascript.append(");");
  GetDevToolsWebContents()->GetMainFrame()->ExecuteJavaScript(
      base::UTF8ToUTF16(javascript), base::NullCallback());
}

gfx::Rect InspectableWebContentsImpl::GetDevToolsBounds() const {
  return devtools_bounds_;
}

void InspectableWebContentsImpl::SaveDevToolsBounds(const gfx::Rect& bounds) {
  pref_service_->Set(kDevToolsBoundsPref, RectToDictionary(bounds));
  devtools_bounds_ = bounds;
}

double InspectableWebContentsImpl::GetDevToolsZoomLevel() const {
  return pref_service_->GetDouble(kDevToolsZoomPref);
}

void InspectableWebContentsImpl::UpdateDevToolsZoomLevel(double level) {
  pref_service_->SetDouble(kDevToolsZoomPref, level);
}

void InspectableWebContentsImpl::ActivateWindow() {
  // Set the zoom level.
  SetZoomLevelForWebContents(GetDevToolsWebContents(), GetDevToolsZoomLevel());
}

void InspectableWebContentsImpl::CloseWindow() {
  GetDevToolsWebContents()->DispatchBeforeUnload(false /* auto_cancel */);
}

void InspectableWebContentsImpl::LoadCompleted() {
  frontend_loaded_ = true;
  if (managed_devtools_web_contents_)
    view_->ShowDevTools(activate_);

  // If the devtools can dock, "SetIsDocked" will be called by devtools itself.
  if (!can_dock_) {
    SetIsDocked(DispatchCallback(), false);
  } else {
    if (dock_state_.empty()) {
      const base::DictionaryValue* prefs =
          pref_service_->GetDictionary(kDevToolsPreferences);
      std::string current_dock_state;
      prefs->GetString("currentDockState", &current_dock_state);
      base::RemoveChars(current_dock_state, "\"", &dock_state_);
    }
    base::string16 javascript = base::UTF8ToUTF16(
        "Components.dockController.setDockSide(\"" + dock_state_ + "\");");
    GetDevToolsWebContents()->GetMainFrame()->ExecuteJavaScript(
        javascript, base::NullCallback());
  }

  if (view_->GetDelegate())
    view_->GetDelegate()->DevToolsOpened();
}

void InspectableWebContentsImpl::SetInspectedPageBounds(const gfx::Rect& rect) {
  DevToolsContentsResizingStrategy strategy(rect);
  if (contents_resizing_strategy_.Equals(strategy))
    return;

  contents_resizing_strategy_.CopyFrom(strategy);
  if (managed_devtools_web_contents_)
    view_->SetContentsResizingStrategy(contents_resizing_strategy_);
}

void InspectableWebContentsImpl::InspectElementCompleted() {}

void InspectableWebContentsImpl::InspectedURLChanged(const std::string& url) {
  if (managed_devtools_web_contents_)
    view_->SetTitle(
        base::UTF8ToUTF16(base::StringPrintf(kTitleFormat, url.c_str())));
}

void InspectableWebContentsImpl::LoadNetworkResource(
    const DispatchCallback& callback,
    const std::string& url,
    const std::string& headers,
    int stream_id) {
  GURL gurl(url);
  if (!gurl.is_valid()) {
    base::DictionaryValue response;
    response.SetInteger("statusCode", 404);
    callback.Run(&response);
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = gurl;
  resource_request->headers.AddHeadersFromString(headers);

  auto* partition = content::BrowserContext::GetDefaultStoragePartition(
      GetDevToolsWebContents()->GetBrowserContext());
  auto factory = partition->GetURLLoaderFactoryForBrowserProcess();

  auto simple_url_loader = network::SimpleURLLoader::Create(
      std::move(resource_request), NO_TRAFFIC_ANNOTATION_YET);
  auto resource_loader = std::make_unique<NetworkResourceLoader>(
      stream_id, this, std::move(simple_url_loader), factory.get(), callback);
  loaders_.insert(std::move(resource_loader));
}

void InspectableWebContentsImpl::SetIsDocked(const DispatchCallback& callback,
                                             bool docked) {
  if (managed_devtools_web_contents_)
    view_->SetIsDocked(docked, activate_);
  if (!callback.is_null())
    callback.Run(nullptr);
}

void InspectableWebContentsImpl::OpenInNewTab(const std::string& url) {}

void InspectableWebContentsImpl::ShowItemInFolder(
    const std::string& file_system_path) {
  if (file_system_path.empty())
    return;

  base::FilePath path = base::FilePath::FromUTF8Unsafe(file_system_path);
  platform_util::OpenItem(path);
}

void InspectableWebContentsImpl::SaveToFile(const std::string& url,
                                            const std::string& content,
                                            bool save_as) {
  if (delegate_)
    delegate_->DevToolsSaveToFile(url, content, save_as);
}

void InspectableWebContentsImpl::AppendToFile(const std::string& url,
                                              const std::string& content) {
  if (delegate_)
    delegate_->DevToolsAppendToFile(url, content);
}

void InspectableWebContentsImpl::RequestFileSystems() {
  if (delegate_)
    delegate_->DevToolsRequestFileSystems();
}

void InspectableWebContentsImpl::AddFileSystem(const std::string& type) {
  if (delegate_)
    delegate_->DevToolsAddFileSystem(type, base::FilePath());
}

void InspectableWebContentsImpl::RemoveFileSystem(
    const std::string& file_system_path) {
  if (delegate_)
    delegate_->DevToolsRemoveFileSystem(
        base::FilePath::FromUTF8Unsafe(file_system_path));
}

void InspectableWebContentsImpl::UpgradeDraggedFileSystemPermissions(
    const std::string& file_system_url) {}

void InspectableWebContentsImpl::IndexPath(
    int request_id,
    const std::string& file_system_path,
    const std::string& excluded_folders) {
  if (delegate_)
    delegate_->DevToolsIndexPath(request_id, file_system_path,
                                 excluded_folders);
}

void InspectableWebContentsImpl::StopIndexing(int request_id) {
  if (delegate_)
    delegate_->DevToolsStopIndexing(request_id);
}

void InspectableWebContentsImpl::SearchInPath(
    int request_id,
    const std::string& file_system_path,
    const std::string& query) {
  if (delegate_)
    delegate_->DevToolsSearchInPath(request_id, file_system_path, query);
}

void InspectableWebContentsImpl::SetWhitelistedShortcuts(
    const std::string& message) {}

void InspectableWebContentsImpl::SetEyeDropperActive(bool active) {}
void InspectableWebContentsImpl::ShowCertificateViewer(
    const std::string& cert_chain) {}

void InspectableWebContentsImpl::ZoomIn() {
  double new_level = GetNextZoomLevel(GetDevToolsZoomLevel(), false);
  SetZoomLevelForWebContents(GetDevToolsWebContents(), new_level);
  UpdateDevToolsZoomLevel(new_level);
}

void InspectableWebContentsImpl::ZoomOut() {
  double new_level = GetNextZoomLevel(GetDevToolsZoomLevel(), true);
  SetZoomLevelForWebContents(GetDevToolsWebContents(), new_level);
  UpdateDevToolsZoomLevel(new_level);
}

void InspectableWebContentsImpl::ResetZoom() {
  SetZoomLevelForWebContents(GetDevToolsWebContents(), 0.);
  UpdateDevToolsZoomLevel(0.);
}

void InspectableWebContentsImpl::SetDevicesDiscoveryConfig(
    bool discover_usb_devices,
    bool port_forwarding_enabled,
    const std::string& port_forwarding_config,
    bool network_discovery_enabled,
    const std::string& network_discovery_config) {}

void InspectableWebContentsImpl::SetDevicesUpdatesEnabled(bool enabled) {}

void InspectableWebContentsImpl::PerformActionOnRemotePage(
    const std::string& page_id,
    const std::string& action) {}

void InspectableWebContentsImpl::OpenRemotePage(const std::string& browser_id,
                                                const std::string& url) {}

void InspectableWebContentsImpl::OpenNodeFrontend() {}

void InspectableWebContentsImpl::DispatchProtocolMessageFromDevToolsFrontend(
    const std::string& message) {
  // If the devtools wants to reload the page, hijack the message and handle it
  // to the delegate.
  if (base::MatchPattern(message,
                         "{\"id\":*,"
                         "\"method\":\"Page.reload\","
                         "\"params\":*}")) {
    if (delegate_)
      delegate_->DevToolsReloadPage();
    return;
  }

  if (agent_host_)
    agent_host_->DispatchProtocolMessage(this, message);
}

void InspectableWebContentsImpl::SendJsonRequest(
    const DispatchCallback& callback,
    const std::string& browser_id,
    const std::string& url) {
  callback.Run(nullptr);
}

void InspectableWebContentsImpl::GetPreferences(
    const DispatchCallback& callback) {
  const base::DictionaryValue* prefs =
      pref_service_->GetDictionary(kDevToolsPreferences);
  callback.Run(prefs);
}

void InspectableWebContentsImpl::SetPreference(const std::string& name,
                                               const std::string& value) {
  DictionaryPrefUpdate update(pref_service_, kDevToolsPreferences);
  update.Get()->SetKey(name, base::Value(value));
}

void InspectableWebContentsImpl::RemovePreference(const std::string& name) {
  DictionaryPrefUpdate update(pref_service_, kDevToolsPreferences);
  update.Get()->RemoveWithoutPathExpansion(name, nullptr);
}

void InspectableWebContentsImpl::ClearPreferences() {
  DictionaryPrefUpdate update(pref_service_, kDevToolsPreferences);
  update.Get()->Clear();
}

void InspectableWebContentsImpl::ConnectionReady() {}

void InspectableWebContentsImpl::RegisterExtensionsAPI(
    const std::string& origin,
    const std::string& script) {
  extensions_api_[origin + "/"] = script;
}

void InspectableWebContentsImpl::HandleMessageFromDevToolsFrontend(
    const std::string& message) {
  // TODO(alexeykuzmin): Should we expect it to exist?
  if (!embedder_message_dispatcher_) {
    return;
  }

  std::string method;
  base::ListValue empty_params;
  base::ListValue* params = &empty_params;

  base::DictionaryValue* dict = nullptr;
  std::unique_ptr<base::Value> parsed_message(
      base::JSONReader::ReadDeprecated(message));
  if (!parsed_message || !parsed_message->GetAsDictionary(&dict) ||
      !dict->GetString(kFrontendHostMethod, &method) ||
      (dict->HasKey(kFrontendHostParams) &&
       !dict->GetList(kFrontendHostParams, &params))) {
    LOG(ERROR) << "Invalid message was sent to embedder: " << message;
    return;
  }
  int id = 0;
  dict->GetInteger(kFrontendHostId, &id);
  embedder_message_dispatcher_->Dispatch(
      base::Bind(&InspectableWebContentsImpl::SendMessageAck,
                 weak_factory_.GetWeakPtr(), id),
      method, params);
}

void InspectableWebContentsImpl::DispatchProtocolMessage(
    content::DevToolsAgentHost* agent_host,
    const std::string& message) {
  if (!frontend_loaded_)
    return;

  if (message.length() < kMaxMessageChunkSize) {
    base::string16 javascript =
        base::UTF8ToUTF16("DevToolsAPI.dispatchMessage(" + message + ");");
    GetDevToolsWebContents()->GetMainFrame()->ExecuteJavaScript(
        javascript, base::NullCallback());
    return;
  }

  base::Value total_size(static_cast<int>(message.length()));
  for (size_t pos = 0; pos < message.length(); pos += kMaxMessageChunkSize) {
    base::Value message_value(message.substr(pos, kMaxMessageChunkSize));
    CallClientFunction("DevToolsAPI.dispatchMessageChunk", &message_value,
                       pos ? nullptr : &total_size, nullptr);
  }
}

void InspectableWebContentsImpl::AgentHostClosed(
    content::DevToolsAgentHost* agent_host) {}

void InspectableWebContentsImpl::RenderFrameHostChanged(
    content::RenderFrameHost* old_host,
    content::RenderFrameHost* new_host) {
  if (new_host->GetParent())
    return;
  frontend_host_ = content::DevToolsFrontendHost::Create(
      new_host,
      base::Bind(&InspectableWebContentsImpl::HandleMessageFromDevToolsFrontend,
                 weak_factory_.GetWeakPtr()));
}

void InspectableWebContentsImpl::WebContentsDestroyed() {
  frontend_loaded_ = false;
  external_devtools_web_contents_ = nullptr;
  Observe(nullptr);
  Detach();
  embedder_message_dispatcher_.reset();

  if (view_ && view_->GetDelegate())
    view_->GetDelegate()->DevToolsClosed();
}

bool InspectableWebContentsImpl::DidAddMessageToConsole(
    content::WebContents* source,
    int32_t level,
    const base::string16& message,
    int32_t line_no,
    const base::string16& source_id) {
  logging::LogMessage("CONSOLE", line_no, level).stream()
      << "\"" << message << "\", source: " << source_id << " (" << line_no
      << ")";
  return true;
}

bool InspectableWebContentsImpl::ShouldCreateWebContents(
    content::WebContents* web_contents,
    content::RenderFrameHost* opener,
    content::SiteInstance* source_site_instance,
    int32_t route_id,
    int32_t main_frame_route_id,
    int32_t main_frame_widget_route_id,
    content::mojom::WindowContainerType window_container_type,
    const GURL& opener_url,
    const std::string& frame_name,
    const GURL& target_url,
    const std::string& partition_id,
    content::SessionStorageNamespace* session_storage_namespace) {
  return false;
}

bool InspectableWebContentsImpl::HandleKeyboardEvent(
    content::WebContents* source,
    const content::NativeWebKeyboardEvent& event) {
  auto* delegate = web_contents_->GetDelegate();
  return !delegate || delegate->HandleKeyboardEvent(source, event);
}

void InspectableWebContentsImpl::CloseContents(content::WebContents* source) {
  // This is where the devtools closes itself (by clicking the x button).
  CloseDevTools();
}

content::ColorChooser* InspectableWebContentsImpl::OpenColorChooser(
    content::WebContents* source,
    SkColor color,
    const std::vector<blink::mojom::ColorSuggestionPtr>& suggestions) {
  auto* delegate = web_contents_->GetDelegate();
  if (delegate)
    return delegate->OpenColorChooser(source, color, suggestions);
  return nullptr;
}

void InspectableWebContentsImpl::RunFileChooser(
    content::RenderFrameHost* render_frame_host,
    std::unique_ptr<content::FileSelectListener> listener,
    const blink::mojom::FileChooserParams& params) {
  auto* delegate = web_contents_->GetDelegate();
  if (delegate)
    delegate->RunFileChooser(render_frame_host, std::move(listener), params);
}

void InspectableWebContentsImpl::EnumerateDirectory(
    content::WebContents* source,
    std::unique_ptr<content::FileSelectListener> listener,
    const base::FilePath& path) {
  auto* delegate = web_contents_->GetDelegate();
  if (delegate)
    delegate->EnumerateDirectory(source, std::move(listener), path);
}

void InspectableWebContentsImpl::OnWebContentsFocused(
    content::RenderWidgetHost* render_widget_host) {
#if defined(TOOLKIT_VIEWS)
  if (view_->GetDelegate())
    view_->GetDelegate()->DevToolsFocused();
#endif
}

void InspectableWebContentsImpl::ReadyToCommitNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInMainFrame()) {
    if (navigation_handle->GetRenderFrameHost() ==
            GetDevToolsWebContents()->GetMainFrame() &&
        frontend_host_) {
      return;
    }
    frontend_host_ = content::DevToolsFrontendHost::Create(
        web_contents()->GetMainFrame(),
        base::Bind(
            &InspectableWebContentsImpl::HandleMessageFromDevToolsFrontend,
            base::Unretained(this)));
    return;
  }
}

void InspectableWebContentsImpl::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInMainFrame() ||
      !navigation_handle->GetURL().SchemeIs("chrome-extension") ||
      !navigation_handle->HasCommitted())
    return;
  content::RenderFrameHost* frame = navigation_handle->GetRenderFrameHost();
  auto origin = navigation_handle->GetURL().GetOrigin().spec();
  auto it = extensions_api_.find(origin);
  if (it == extensions_api_.end())
    return;
  // Injected Script from devtools frontend doesn't expose chrome,
  // most likely bug in chromium.
  base::ReplaceFirstSubstringAfterOffset(&it->second, 0, "var chrome",
                                         "var chrome = window.chrome ");
  auto script = base::StringPrintf("%s(\"%s\")", it->second.c_str(),
                                   base::GenerateGUID().c_str());
  // Invoking content::DevToolsFrontendHost::SetupExtensionsAPI(frame, script);
  // should be enough, but it seems to be a noop currently.
  frame->ExecuteJavaScriptForTests(base::UTF8ToUTF16(script),
                                   base::NullCallback());
}

void InspectableWebContentsImpl::SendMessageAck(int request_id,
                                                const base::Value* arg) {
  base::Value id_value(request_id);
  CallClientFunction("DevToolsAPI.embedderMessageAck", &id_value, arg, nullptr);
}

}  // namespace atom
