// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/web_view_guest_delegate.h"

#include <memory>

#include "atom/browser/api/atom_api_web_contents.h"
#include "atom/common/native_mate_converters/gurl_converter.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"

namespace atom {

WebViewGuestDelegate::WebViewGuestDelegate(content::WebContents* embedder,
                                           api::WebContents* api_web_contents)
    : embedder_web_contents_(embedder), api_web_contents_(api_web_contents) {}

WebViewGuestDelegate::~WebViewGuestDelegate() {
  ResetZoomController();
}

void WebViewGuestDelegate::AttachToIframe(
    content::WebContents* embedder_web_contents,
    int embedder_frame_id) {
  embedder_web_contents_ = embedder_web_contents;

  int embedder_process_id =
      embedder_web_contents_->GetMainFrame()->GetProcess()->GetID();
  auto* embedder_frame =
      content::RenderFrameHost::FromID(embedder_process_id, embedder_frame_id);
  DCHECK_EQ(embedder_web_contents_,
            content::WebContents::FromRenderFrameHost(embedder_frame));

  // Attach this inner WebContents |guest_web_contents| to the outer
  // WebContents |embedder_web_contents|. The outer WebContents's
  // frame |embedder_frame| hosts the inner WebContents.
  api_web_contents_->web_contents()->AttachToOuterWebContentsFrame(
      embedder_web_contents_, embedder_frame);

  ResetZoomController();

  embedder_zoom_controller_ =
      WebContentsZoomController::FromWebContents(embedder_web_contents_);
  embedder_zoom_controller_->AddObserver(this);
  auto* zoom_controller = api_web_contents_->GetZoomController();
  zoom_controller->SetEmbedderZoomController(embedder_zoom_controller_);

  api_web_contents_->Emit("did-attach");
}

void WebViewGuestDelegate::DidDetach() {
  ResetZoomController();
}

content::WebContents* WebViewGuestDelegate::GetOwnerWebContents() const {
  return embedder_web_contents_;
}

void WebViewGuestDelegate::OnZoomLevelChanged(
    content::WebContents* web_contents,
    double level,
    bool is_temporary) {
  if (web_contents == GetOwnerWebContents()) {
    if (is_temporary) {
      api_web_contents_->GetZoomController()->SetTemporaryZoomLevel(level);
    } else {
      api_web_contents_->GetZoomController()->SetZoomLevel(level);
    }
    // Change the default zoom factor to match the embedders' new zoom level.
    double zoom_factor = content::ZoomLevelToZoomFactor(level);
    api_web_contents_->GetZoomController()->SetDefaultZoomFactor(zoom_factor);
  }
}

void WebViewGuestDelegate::OnZoomControllerWebContentsDestroyed() {
  ResetZoomController();
}

void WebViewGuestDelegate::ResetZoomController() {
  if (embedder_zoom_controller_) {
    embedder_zoom_controller_->RemoveObserver(this);
    embedder_zoom_controller_ = nullptr;
  }
}

content::RenderWidgetHost* WebViewGuestDelegate::GetOwnerRenderWidgetHost() {
  return embedder_web_contents_->GetRenderViewHost()->GetWidget();
}

content::SiteInstance* WebViewGuestDelegate::GetOwnerSiteInstance() {
  return embedder_web_contents_->GetSiteInstance();
}

content::WebContents* WebViewGuestDelegate::CreateNewGuestWindow(
    const content::WebContents::CreateParams& create_params) {
  // Code below mirrors what content::WebContentsImpl::CreateNewWindow
  // does for non-guest sources
  content::WebContents::CreateParams guest_params(create_params);
  guest_params.initial_size =
      embedder_web_contents_->GetContainerBounds().size();
  guest_params.context = embedder_web_contents_->GetNativeView();
  std::unique_ptr<content::WebContents> guest_contents =
      content::WebContents::Create(guest_params);
  content::RenderWidgetHost* render_widget_host =
      guest_contents->GetRenderViewHost()->GetWidget();
  auto* guest_contents_impl =
      static_cast<content::WebContentsImpl*>(guest_contents.release());
  guest_contents_impl->GetView()->CreateViewForWidget(render_widget_host,
                                                      false);

  return guest_contents_impl;
}

}  // namespace atom
