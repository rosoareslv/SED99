// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/osr/osr_render_widget_host_view.h"

#include <vector>

#include "base/callback_helpers.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/single_thread_task_runner.h"
#include "base/time/time.h"
#include "cc/output/copy_output_request.h"
#include "cc/scheduler/delay_based_time_source.h"
#include "components/display_compositor/gl_helper.h"
#include "content/browser/renderer_host/render_widget_host_delegate.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/common/view_messages.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/context_factory.h"
#include "content/public/browser/render_widget_host_view_frame_subscriber.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_type.h"
#include "ui/events/latency_info.h"
#include "ui/gfx/geometry/dip_util.h"
#include "ui/gfx/native_widget_types.h"

namespace atom {

namespace {

const float kDefaultScaleFactor = 1.0;
const int kFrameRetryLimit = 2;

}  // namespace

class AtomCopyFrameGenerator {
 public:
  AtomCopyFrameGenerator(int frame_rate_threshold_ms,
                        OffScreenRenderWidgetHostView* view)
    : frame_rate_threshold_ms_(frame_rate_threshold_ms),
      view_(view),
      frame_pending_(false),
      frame_in_progress_(false),
      frame_retry_count_(0),
      weak_ptr_factory_(this) {
    last_time_ = base::Time::Now();
  }

  void GenerateCopyFrame(
      bool force_frame,
      const gfx::Rect& damage_rect) {
    if (force_frame && !frame_pending_)
      frame_pending_ = true;

    if (!frame_pending_)
      return;

    if (!damage_rect.IsEmpty())
      pending_damage_rect_.Union(damage_rect);

    if (frame_in_progress_)
      return;

    frame_in_progress_ = true;

    const int64_t frame_rate_delta =
        (base::TimeTicks::Now() - frame_start_time_).InMilliseconds();
    if (frame_rate_delta < frame_rate_threshold_ms_) {
      content::BrowserThread::PostDelayedTask(content::BrowserThread::UI,
        FROM_HERE,
        base::Bind(&AtomCopyFrameGenerator::InternalGenerateCopyFrame,
                   weak_ptr_factory_.GetWeakPtr()),
        base::TimeDelta::FromMilliseconds(
            frame_rate_threshold_ms_ - frame_rate_delta));
      return;
    }

    InternalGenerateCopyFrame();
  }

  bool frame_pending() const { return frame_pending_; }

  void set_frame_rate_threshold_ms(int frame_rate_threshold_ms) {
    frame_rate_threshold_ms_ = frame_rate_threshold_ms;
  }

 private:
  void InternalGenerateCopyFrame() {
    frame_pending_ = false;
    frame_start_time_ = base::TimeTicks::Now();

    if (!view_->render_widget_host())
      return;

    const gfx::Rect damage_rect = pending_damage_rect_;
    pending_damage_rect_.SetRect(0, 0, 0, 0);

    std::unique_ptr<cc::CopyOutputRequest> request =
        cc::CopyOutputRequest::CreateRequest(base::Bind(
            &AtomCopyFrameGenerator::CopyFromCompositingSurfaceHasResult,
            weak_ptr_factory_.GetWeakPtr(),
            damage_rect));

    request->set_area(gfx::Rect(view_->GetPhysicalBackingSize()));
    view_->GetRootLayer()->RequestCopyOfOutput(std::move(request));
  }

  void CopyFromCompositingSurfaceHasResult(
      const gfx::Rect& damage_rect,
      std::unique_ptr<cc::CopyOutputResult> result) {
    if (result->IsEmpty() || result->size().IsEmpty() ||
        !view_->render_widget_host()) {
      OnCopyFrameCaptureFailure(damage_rect);
      return;
    }

    if (result->HasTexture()) {
      PrepareTextureCopyOutputResult(damage_rect, std::move(result));
      return;
    }

    DCHECK(result->HasBitmap());
    PrepareBitmapCopyOutputResult(damage_rect, std::move(result));
  }

  void PrepareTextureCopyOutputResult(
      const gfx::Rect& damage_rect,
      std::unique_ptr<cc::CopyOutputResult> result) {
    DCHECK(result->HasTexture());
    base::ScopedClosureRunner scoped_callback_runner(
        base::Bind(&AtomCopyFrameGenerator::OnCopyFrameCaptureFailure,
                   weak_ptr_factory_.GetWeakPtr(),
                   damage_rect));

    const gfx::Size& result_size = result->size();
    SkIRect bitmap_size;
    if (bitmap_)
      bitmap_->getBounds(&bitmap_size);

    if (!bitmap_ ||
        bitmap_size.width() != result_size.width() ||
        bitmap_size.height() != result_size.height()) {
      bitmap_.reset(new SkBitmap);
      bitmap_->allocN32Pixels(result_size.width(),
                              result_size.height(),
                              true);
      if (bitmap_->drawsNothing())
        return;
    }

    content::ImageTransportFactory* factory =
        content::ImageTransportFactory::GetInstance();
    display_compositor::GLHelper* gl_helper = factory->GetGLHelper();
    if (!gl_helper)
      return;

    std::unique_ptr<SkAutoLockPixels> bitmap_pixels_lock(
        new SkAutoLockPixels(*bitmap_));
    uint8_t* pixels = static_cast<uint8_t*>(bitmap_->getPixels());

    cc::TextureMailbox texture_mailbox;
    std::unique_ptr<cc::SingleReleaseCallback> release_callback;
    result->TakeTexture(&texture_mailbox, &release_callback);
    DCHECK(texture_mailbox.IsTexture());
    if (!texture_mailbox.IsTexture())
      return;

    ignore_result(scoped_callback_runner.Release());

    gl_helper->CropScaleReadbackAndCleanMailbox(
        texture_mailbox.mailbox(),
        texture_mailbox.sync_token(),
        result_size,
        gfx::Rect(result_size),
        result_size,
        pixels,
        kN32_SkColorType,
        base::Bind(
            &AtomCopyFrameGenerator::CopyFromCompositingSurfaceFinishedProxy,
            weak_ptr_factory_.GetWeakPtr(),
            base::Passed(&release_callback),
            damage_rect,
            base::Passed(&bitmap_),
            base::Passed(&bitmap_pixels_lock)),
        display_compositor::GLHelper::SCALER_QUALITY_FAST);
  }

  static void CopyFromCompositingSurfaceFinishedProxy(
      base::WeakPtr<AtomCopyFrameGenerator> generator,
      std::unique_ptr<cc::SingleReleaseCallback> release_callback,
      const gfx::Rect& damage_rect,
      std::unique_ptr<SkBitmap> bitmap,
      std::unique_ptr<SkAutoLockPixels> bitmap_pixels_lock,
      bool result) {
    gpu::SyncToken sync_token;
    if (result) {
      display_compositor::GLHelper* gl_helper =
          content::ImageTransportFactory::GetInstance()->GetGLHelper();
      if (gl_helper)
        gl_helper->GenerateSyncToken(&sync_token);
    }
    const bool lost_resource = !sync_token.HasData();
    release_callback->Run(sync_token, lost_resource);

    if (generator) {
      generator->CopyFromCompositingSurfaceFinished(
          damage_rect, std::move(bitmap), std::move(bitmap_pixels_lock),
          result);
    } else {
      bitmap_pixels_lock.reset();
      bitmap.reset();
    }
  }

  void CopyFromCompositingSurfaceFinished(
      const gfx::Rect& damage_rect,
      std::unique_ptr<SkBitmap> bitmap,
      std::unique_ptr<SkAutoLockPixels> bitmap_pixels_lock,
      bool result) {
    DCHECK(!bitmap_);
    bitmap_ = std::move(bitmap);

    if (result) {
      OnCopyFrameCaptureSuccess(damage_rect, *bitmap_,
                                std::move(bitmap_pixels_lock));
    } else {
      bitmap_pixels_lock.reset();
      OnCopyFrameCaptureFailure(damage_rect);
    }
  }

  void PrepareBitmapCopyOutputResult(
      const gfx::Rect& damage_rect,
      std::unique_ptr<cc::CopyOutputResult> result) {
    DCHECK(result->HasBitmap());
    std::unique_ptr<SkBitmap> source = result->TakeBitmap();
    DCHECK(source);
    if (source) {
      std::unique_ptr<SkAutoLockPixels> bitmap_pixels_lock(
          new SkAutoLockPixels(*source));
      OnCopyFrameCaptureSuccess(damage_rect, *source,
                                std::move(bitmap_pixels_lock));
    } else {
      OnCopyFrameCaptureFailure(damage_rect);
    }
  }

  void OnCopyFrameCaptureFailure(
      const gfx::Rect& damage_rect) {
    pending_damage_rect_.Union(damage_rect);

    const bool force_frame = (++frame_retry_count_ <= kFrameRetryLimit);
    OnCopyFrameCaptureCompletion(force_frame);
  }

  void OnCopyFrameCaptureSuccess(
      const gfx::Rect& damage_rect,
      const SkBitmap& bitmap,
      std::unique_ptr<SkAutoLockPixels> bitmap_pixels_lock) {
    view_->OnPaint(damage_rect, bitmap);

    if (frame_retry_count_ > 0)
      frame_retry_count_ = 0;

    OnCopyFrameCaptureCompletion(false);
  }

  void OnCopyFrameCaptureCompletion(bool force_frame) {
    frame_in_progress_ = false;

    if (frame_pending_) {
      content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
        base::Bind(&AtomCopyFrameGenerator::GenerateCopyFrame,
                   weak_ptr_factory_.GetWeakPtr(),
                   force_frame,
                   gfx::Rect()));
    }
  }

  int frame_rate_threshold_ms_;
  OffScreenRenderWidgetHostView* view_;

  base::Time last_time_;

  base::TimeTicks frame_start_time_;
  bool frame_pending_;
  bool frame_in_progress_;
  int frame_retry_count_;
  std::unique_ptr<SkBitmap> bitmap_;
  gfx::Rect pending_damage_rect_;

  base::WeakPtrFactory<AtomCopyFrameGenerator> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(AtomCopyFrameGenerator);
};

class AtomBeginFrameTimer : public cc::DelayBasedTimeSourceClient {
 public:
  AtomBeginFrameTimer(int frame_rate_threshold_ms,
                      const base::Closure& callback)
      : callback_(callback) {
    time_source_.reset(new cc::DelayBasedTimeSource(
        content::BrowserThread::GetTaskRunnerForThread(
            content::BrowserThread::UI).get()));
    time_source_->SetClient(this);
  }

  void SetActive(bool active) {
    time_source_->SetActive(active);
  }

  bool IsActive() const {
    return time_source_->Active();
  }

  void SetFrameRateThresholdMs(int frame_rate_threshold_ms) {
    time_source_->SetTimebaseAndInterval(
        base::TimeTicks::Now(),
        base::TimeDelta::FromMilliseconds(frame_rate_threshold_ms));
  }

 private:
  void OnTimerTick() override {
    callback_.Run();
  }

  const base::Closure callback_;
  std::unique_ptr<cc::DelayBasedTimeSource> time_source_;

  DISALLOW_COPY_AND_ASSIGN(AtomBeginFrameTimer);
};

OffScreenRenderWidgetHostView::OffScreenRenderWidgetHostView(
    bool transparent,
    const OnPaintCallback& callback,
    content::RenderWidgetHost* host,
    NativeWindow* native_window)
    : render_widget_host_(content::RenderWidgetHostImpl::From(host)),
      native_window_(native_window),
      software_output_device_(nullptr),
      transparent_(transparent),
      callback_(callback),
      frame_rate_(60),
      frame_rate_threshold_ms_(0),
      last_time_(base::Time::Now()),
      scale_factor_(kDefaultScaleFactor),
      is_showing_(!render_widget_host_->is_hidden()),
      size_(native_window->GetSize()),
      painting_(true),
      weak_ptr_factory_(this) {
  DCHECK(render_widget_host_);
  render_widget_host_->SetView(this);

#if !defined(OS_MACOSX)
  content::ImageTransportFactory* factory =
      content::ImageTransportFactory::GetInstance();
  delegated_frame_host_ = base::MakeUnique<content::DelegatedFrameHost>(
      factory->GetContextFactory()->AllocateFrameSinkId(), this);

  root_layer_.reset(new ui::Layer(ui::LAYER_SOLID_COLOR));
#endif

#if defined(OS_MACOSX)
  CreatePlatformWidget();
#else
  compositor_.reset(
      new ui::Compositor(content::GetContextFactory(),
                         base::ThreadTaskRunnerHandle::Get()));
  compositor_->SetAcceleratedWidget(native_window_->GetAcceleratedWidget());
  compositor_->SetRootLayer(root_layer_.get());
#endif
  GetCompositor()->SetDelegate(this);

  native_window_->AddObserver(this);

  ResizeRootLayer();
}

OffScreenRenderWidgetHostView::~OffScreenRenderWidgetHostView() {
  if (native_window_)
    native_window_->RemoveObserver(this);

#if defined(OS_MACOSX)
  if (is_showing_)
    browser_compositor_->SetRenderWidgetHostIsHidden(true);
#else
  // Marking the DelegatedFrameHost as removed from the window hierarchy is
  // necessary to remove all connections to its old ui::Compositor.
  if (is_showing_)
    delegated_frame_host_->WasHidden();
  delegated_frame_host_->ResetCompositor();
#endif

#if defined(OS_MACOSX)
  DestroyPlatformWidget();
#endif
}

void OffScreenRenderWidgetHostView::OnBeginFrameTimerTick() {
  const base::TimeTicks frame_time = base::TimeTicks::Now();
  const base::TimeDelta vsync_period =
      base::TimeDelta::FromMilliseconds(frame_rate_threshold_ms_);
  SendBeginFrame(frame_time, vsync_period);
}

void OffScreenRenderWidgetHostView::SendBeginFrame(
    base::TimeTicks frame_time, base::TimeDelta vsync_period) {
  base::TimeTicks display_time = frame_time + vsync_period;

  base::TimeDelta estimated_browser_composite_time =
      base::TimeDelta::FromMicroseconds(
          (1.0f * base::Time::kMicrosecondsPerSecond) / (3.0f * 60));

  base::TimeTicks deadline = display_time - estimated_browser_composite_time;

  render_widget_host_->Send(new ViewMsg_BeginFrame(
      render_widget_host_->GetRoutingID(),
      cc::BeginFrameArgs::Create(BEGINFRAME_FROM_HERE, frame_time, deadline,
                                 vsync_period, cc::BeginFrameArgs::NORMAL)));
}

bool OffScreenRenderWidgetHostView::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(OffScreenRenderWidgetHostView, message)
    IPC_MESSAGE_HANDLER(ViewHostMsg_SetNeedsBeginFrames,
                        SetNeedsBeginFrames)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  if (!handled)
    return content::RenderWidgetHostViewBase::OnMessageReceived(message);
  return handled;
}

void OffScreenRenderWidgetHostView::InitAsChild(gfx::NativeView) {
}

content::RenderWidgetHost* OffScreenRenderWidgetHostView::GetRenderWidgetHost()
    const {
  return render_widget_host_;
}

void OffScreenRenderWidgetHostView::SetSize(const gfx::Size& size) {
  size_ = size;

  ResizeRootLayer();
  if (render_widget_host_)
    render_widget_host_->WasResized();
  GetDelegatedFrameHost()->WasResized();
}

void OffScreenRenderWidgetHostView::SetBounds(const gfx::Rect& new_bounds) {
}

gfx::Vector2dF OffScreenRenderWidgetHostView::GetLastScrollOffset() const {
  return last_scroll_offset_;
}

gfx::NativeView OffScreenRenderWidgetHostView::GetNativeView() const {
  return gfx::NativeView();
}

gfx::NativeViewAccessible
OffScreenRenderWidgetHostView::GetNativeViewAccessible() {
  return gfx::NativeViewAccessible();
}

ui::TextInputClient* OffScreenRenderWidgetHostView::GetTextInputClient() {
  return nullptr;
}

void OffScreenRenderWidgetHostView::Focus() {
}

bool OffScreenRenderWidgetHostView::HasFocus() const {
  return false;
}

bool OffScreenRenderWidgetHostView::IsSurfaceAvailableForCopy() const {
  return GetDelegatedFrameHost()->CanCopyToBitmap();
}

void OffScreenRenderWidgetHostView::Show() {
  if (is_showing_)
    return;

  is_showing_ = true;

#if defined(OS_MACOSX)
  browser_compositor_->SetRenderWidgetHostIsHidden(false);
#else
  delegated_frame_host_->SetCompositor(compositor_.get());
  delegated_frame_host_->WasShown(ui::LatencyInfo());
#endif

  if (render_widget_host_)
    render_widget_host_->WasShown(ui::LatencyInfo());
}

void OffScreenRenderWidgetHostView::Hide() {
  if (!is_showing_)
    return;

  if (render_widget_host_)
    render_widget_host_->WasHidden();

#if defined(OS_MACOSX)
  browser_compositor_->SetRenderWidgetHostIsHidden(true);
#else
  GetDelegatedFrameHost()->WasHidden();
  GetDelegatedFrameHost()->ResetCompositor();
#endif

  is_showing_ = false;
}

bool OffScreenRenderWidgetHostView::IsShowing() {
  return is_showing_;
}

gfx::Rect OffScreenRenderWidgetHostView::GetViewBounds() const {
  return gfx::Rect(size_);
}

void OffScreenRenderWidgetHostView::SetBackgroundColor(SkColor color) {
  if (transparent_)
    color = SkColorSetARGB(SK_AlphaTRANSPARENT, 0, 0, 0);

  content::RenderWidgetHostViewBase::SetBackgroundColor(color);

  const bool opaque = !transparent_ && GetBackgroundOpaque();
  if (render_widget_host_)
    render_widget_host_->SetBackgroundOpaque(opaque);
}

gfx::Size OffScreenRenderWidgetHostView::GetVisibleViewportSize() const {
  return size_;
}

void OffScreenRenderWidgetHostView::SetInsets(const gfx::Insets& insets) {
}

bool OffScreenRenderWidgetHostView::LockMouse() {
  return false;
}

void OffScreenRenderWidgetHostView::UnlockMouse() {
}

void OffScreenRenderWidgetHostView::OnSwapCompositorFrame(
  uint32_t output_surface_id,
  cc::CompositorFrame frame) {
  TRACE_EVENT0("electron",
    "OffScreenRenderWidgetHostView::OnSwapCompositorFrame");

  if (frame.metadata.root_scroll_offset != last_scroll_offset_) {
    last_scroll_offset_ = frame.metadata.root_scroll_offset;
  }

  if (frame.delegated_frame_data) {
    if (software_output_device_) {
      if (!begin_frame_timer_.get()) {
        software_output_device_->SetActive(painting_);
      }

      // The compositor will draw directly to the SoftwareOutputDevice which
      // then calls OnPaint.
#if defined(OS_MACOSX)
      browser_compositor_->SwapCompositorFrame(output_surface_id,
                                               std::move(frame));
#else
      delegated_frame_host_->SwapDelegatedFrame(output_surface_id,
                                                std::move(frame));
#endif
    } else {
      if (!copy_frame_generator_.get()) {
        copy_frame_generator_.reset(
            new AtomCopyFrameGenerator(frame_rate_threshold_ms_, this));
      }

      // Determine the damage rectangle for the current frame. This is the same
      // calculation that SwapDelegatedFrame uses.
      cc::RenderPass* root_pass =
          frame.delegated_frame_data->render_pass_list.back().get();
      gfx::Size frame_size = root_pass->output_rect.size();
      gfx::Rect damage_rect =
          gfx::ToEnclosingRect(gfx::RectF(root_pass->damage_rect));
      damage_rect.Intersect(gfx::Rect(frame_size));

#if defined(OS_MACOSX)
      browser_compositor_->SwapCompositorFrame(output_surface_id,
                                               std::move(frame));
#else
      delegated_frame_host_->SwapDelegatedFrame(output_surface_id,
                                                std::move(frame));
#endif

      // Request a copy of the last compositor frame which will eventually call
      // OnPaint asynchronously.
      copy_frame_generator_->GenerateCopyFrame(true, damage_rect);
    }
  }
}

void OffScreenRenderWidgetHostView::ClearCompositorFrame() {
  GetDelegatedFrameHost()->ClearDelegatedFrame();
}

void OffScreenRenderWidgetHostView::InitAsPopup(
    content::RenderWidgetHostView* parent_host_view, const gfx::Rect& pos) {
}

void OffScreenRenderWidgetHostView::InitAsFullscreen(
  content::RenderWidgetHostView *) {
}

void OffScreenRenderWidgetHostView::UpdateCursor(const content::WebCursor &) {
}

void OffScreenRenderWidgetHostView::SetIsLoading(bool loading) {
}

void OffScreenRenderWidgetHostView::TextInputStateChanged(
  const content::TextInputState& params) {
}

void OffScreenRenderWidgetHostView::ImeCancelComposition() {
}

void OffScreenRenderWidgetHostView::RenderProcessGone(base::TerminationStatus,
    int) {
  Destroy();
}

void OffScreenRenderWidgetHostView::Destroy() {
  delete this;
}

void OffScreenRenderWidgetHostView::SetTooltipText(const base::string16 &) {
}

void OffScreenRenderWidgetHostView::SelectionBoundsChanged(
  const ViewHostMsg_SelectionBounds_Params &) {
}

void OffScreenRenderWidgetHostView::CopyFromCompositingSurface(
    const gfx::Rect& src_subrect,
    const gfx::Size& dst_size,
    const content::ReadbackRequestCallback& callback,
    const SkColorType preferred_color_type) {
  GetDelegatedFrameHost()->CopyFromCompositingSurface(
    src_subrect, dst_size, callback, preferred_color_type);
}

void OffScreenRenderWidgetHostView::CopyFromCompositingSurfaceToVideoFrame(
    const gfx::Rect& src_subrect,
    const scoped_refptr<media::VideoFrame>& target,
    const base::Callback<void(const gfx::Rect&, bool)>& callback) {
  GetDelegatedFrameHost()->CopyFromCompositingSurfaceToVideoFrame(
    src_subrect, target, callback);
}

bool OffScreenRenderWidgetHostView::CanCopyToVideoFrame() const {
  return GetDelegatedFrameHost()->CanCopyToVideoFrame();
}

void OffScreenRenderWidgetHostView::BeginFrameSubscription(
  std::unique_ptr<content::RenderWidgetHostViewFrameSubscriber> subscriber) {
  GetDelegatedFrameHost()->BeginFrameSubscription(std::move(subscriber));
}

void OffScreenRenderWidgetHostView::EndFrameSubscription() {
  GetDelegatedFrameHost()->EndFrameSubscription();
}

bool OffScreenRenderWidgetHostView::HasAcceleratedSurface(const gfx::Size &) {
  return false;
}

gfx::Rect OffScreenRenderWidgetHostView::GetBoundsInRootWindow() {
  return gfx::Rect(size_);
}

void OffScreenRenderWidgetHostView::LockCompositingSurface() {
}

void OffScreenRenderWidgetHostView::UnlockCompositingSurface() {
}

void OffScreenRenderWidgetHostView::ImeCompositionRangeChanged(
  const gfx::Range &, const std::vector<gfx::Rect>&) {
}

gfx::Size OffScreenRenderWidgetHostView::GetPhysicalBackingSize() const {
  return size_;
}

gfx::Size OffScreenRenderWidgetHostView::GetRequestedRendererSize() const {
  return size_;
}

#if !defined(OS_MACOSX)
ui::Layer* OffScreenRenderWidgetHostView::DelegatedFrameHostGetLayer() const {
  return const_cast<ui::Layer*>(root_layer_.get());
}

bool OffScreenRenderWidgetHostView::DelegatedFrameHostIsVisible() const {
  return !render_widget_host_->is_hidden();
}

SkColor OffScreenRenderWidgetHostView::DelegatedFrameHostGetGutterColor(
    SkColor color) const {
  return color;
}

gfx::Size OffScreenRenderWidgetHostView::DelegatedFrameHostDesiredSizeInDIP()
  const {
  return size_;
}

bool OffScreenRenderWidgetHostView::DelegatedFrameCanCreateResizeLock() const {
  return false;
}

std::unique_ptr<content::ResizeLock>
  OffScreenRenderWidgetHostView::DelegatedFrameHostCreateResizeLock(
      bool defer_compositor_lock) {
  return nullptr;
}

void OffScreenRenderWidgetHostView::DelegatedFrameHostResizeLockWasReleased() {
  return render_widget_host_->WasResized();
}

void
OffScreenRenderWidgetHostView::DelegatedFrameHostSendReclaimCompositorResources(
    int output_surface_id,
    bool is_swap_ack,
    const cc::ReturnedResourceArray& resources) {
  render_widget_host_->Send(new ViewMsg_ReclaimCompositorResources(
      render_widget_host_->GetRoutingID(), output_surface_id, is_swap_ack,
      resources));
}

void OffScreenRenderWidgetHostView::SetBeginFrameSource(
    cc::BeginFrameSource* source) {
}

#endif  // !defined(OS_MACOSX)

std::unique_ptr<cc::SoftwareOutputDevice>
  OffScreenRenderWidgetHostView::CreateSoftwareOutputDevice(
    ui::Compositor* compositor) {
  DCHECK_EQ(GetCompositor(), compositor);
  DCHECK(!copy_frame_generator_);
  DCHECK(!software_output_device_);

  software_output_device_ = new OffScreenOutputDevice(
      transparent_,
      base::Bind(&OffScreenRenderWidgetHostView::OnPaint,
                 weak_ptr_factory_.GetWeakPtr()));
  return base::WrapUnique(software_output_device_);
}

bool OffScreenRenderWidgetHostView::InstallTransparency() {
  if (transparent_) {
    SetBackgroundColor(SkColor());
#if defined(OS_MACOSX)
    browser_compositor_->SetHasTransparentBackground(true);
#else
    compositor_->SetHostHasTransparentBackground(true);
#endif
    return true;
  }
  return false;
}

bool OffScreenRenderWidgetHostView::IsAutoResizeEnabled() const {
  return false;
}

void OffScreenRenderWidgetHostView::SetNeedsBeginFrames(
    bool needs_begin_frames) {
  SetupFrameRate(false);

  begin_frame_timer_->SetActive(needs_begin_frames);

  if (software_output_device_) {
    software_output_device_->SetActive(needs_begin_frames && painting_);
  }
}

void OffScreenRenderWidgetHostView::OnPaint(
    const gfx::Rect& damage_rect, const SkBitmap& bitmap) {
  TRACE_EVENT0("electron", "OffScreenRenderWidgetHostView::OnPaint");
  callback_.Run(damage_rect, bitmap);
}

void OffScreenRenderWidgetHostView::SetPainting(bool painting) {
  painting_ = painting;

  if (software_output_device_) {
    software_output_device_->SetActive(painting_);
  }
}

bool OffScreenRenderWidgetHostView::IsPainting() const {
  return painting_;
}

void OffScreenRenderWidgetHostView::SetFrameRate(int frame_rate) {
  if (frame_rate <= 0)
    frame_rate = 1;
  if (frame_rate > 60)
    frame_rate = 60;

  frame_rate_ = frame_rate;

  SetupFrameRate(true);
}

int OffScreenRenderWidgetHostView::GetFrameRate() const {
  return frame_rate_;
}

#if !defined(OS_MACOSX)
ui::Compositor* OffScreenRenderWidgetHostView::GetCompositor() const {
  return compositor_.get();
}

ui::Layer* OffScreenRenderWidgetHostView::GetRootLayer() const {
  return root_layer_.get();
}

content::DelegatedFrameHost*
OffScreenRenderWidgetHostView::GetDelegatedFrameHost() const {
  return delegated_frame_host_.get();
}
#endif

void OffScreenRenderWidgetHostView::SetupFrameRate(bool force) {
  if (!force && frame_rate_threshold_ms_ != 0)
    return;

  frame_rate_threshold_ms_ = 1000 / frame_rate_;

  GetCompositor()->vsync_manager()->SetAuthoritativeVSyncInterval(
      base::TimeDelta::FromMilliseconds(frame_rate_threshold_ms_));

  if (copy_frame_generator_) {
    copy_frame_generator_->set_frame_rate_threshold_ms(
        frame_rate_threshold_ms_);
  }

  if (begin_frame_timer_) {
    begin_frame_timer_->SetFrameRateThresholdMs(frame_rate_threshold_ms_);
  } else {
    begin_frame_timer_.reset(new AtomBeginFrameTimer(
        frame_rate_threshold_ms_,
        base::Bind(&OffScreenRenderWidgetHostView::OnBeginFrameTimerTick,
                   weak_ptr_factory_.GetWeakPtr())));
  }
}

void OffScreenRenderWidgetHostView::Invalidate() {
  const gfx::Rect& bounds_in_pixels = GetViewBounds();

  if (software_output_device_) {
    software_output_device_->OnPaint(bounds_in_pixels);
  } else if (copy_frame_generator_) {
    copy_frame_generator_->GenerateCopyFrame(true, bounds_in_pixels);
  }
}

void OffScreenRenderWidgetHostView::ResizeRootLayer() {
  SetupFrameRate(false);

  const float orgScaleFactor = scale_factor_;
  const bool scaleFactorDidChange = (orgScaleFactor != scale_factor_);

  gfx::Size size = GetViewBounds().size();

  if (!scaleFactorDidChange && size == GetRootLayer()->bounds().size())
    return;

  const gfx::Size& size_in_pixels =
      gfx::ConvertSizeToPixel(scale_factor_, size);

  GetRootLayer()->SetBounds(gfx::Rect(size));
  GetCompositor()->SetScaleAndSize(scale_factor_, size_in_pixels);
}

void OffScreenRenderWidgetHostView::OnWindowResize() {
  // In offscreen mode call RenderWidgetHostView's SetSize explicitly
  auto size = native_window_->GetSize();
  SetSize(size);
}

void OffScreenRenderWidgetHostView::OnWindowClosed() {
  native_window_->RemoveObserver(this);
  native_window_ = nullptr;
}

}  // namespace atom
