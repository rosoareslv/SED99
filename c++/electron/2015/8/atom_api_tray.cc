// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/api/atom_api_tray.h"

#include <string>

#include "atom/browser/api/atom_api_menu.h"
#include "atom/browser/browser.h"
#include "atom/browser/ui/tray_icon.h"
#include "atom/common/native_mate_converters/gfx_converter.h"
#include "atom/common/native_mate_converters/image_converter.h"
#include "atom/common/native_mate_converters/string16_converter.h"
#include "native_mate/constructor.h"
#include "native_mate/dictionary.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/image/image.h"

#include "atom/common/node_includes.h"

namespace atom {

namespace api {

Tray::Tray(const gfx::Image& image)
    : tray_icon_(TrayIcon::Create()) {
  tray_icon_->SetImage(image);
  tray_icon_->AddObserver(this);
}

Tray::~Tray() {
}

// static
mate::Wrappable* Tray::New(v8::Isolate* isolate, const gfx::Image& image) {
  if (!Browser::Get()->is_ready()) {
    node::ThrowError(isolate, "Cannot create Tray before app is ready");
    return nullptr;
  }
  return new Tray(image);
}

void Tray::OnClicked(const gfx::Rect& bounds, int modifiers) {
  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  EmitCustomEvent("clicked",
                  ModifiersToObject(isolate(), modifiers), bounds);
}

void Tray::OnDoubleClicked(const gfx::Rect& bounds, int modifiers) {
  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  EmitCustomEvent("double-clicked",
                  ModifiersToObject(isolate(), modifiers), bounds);
}

void Tray::OnRightClicked(const gfx::Rect& bounds, int modifiers) {
  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  EmitCustomEvent("right-clicked",
                  ModifiersToObject(isolate(), modifiers), bounds);
}

void Tray::OnBalloonShow() {
  Emit("balloon-show");
}

void Tray::OnBalloonClicked() {
  Emit("balloon-clicked");
}

void Tray::OnBalloonClosed() {
  Emit("balloon-closed");
}

void Tray::OnDropFiles(const std::vector<std::string>& files) {
  Emit("drop-files", files);
}

bool Tray::IsDestroyed() const {
  return !tray_icon_;
}

void Tray::Destroy() {
  tray_icon_.reset();
}

void Tray::SetImage(mate::Arguments* args, const gfx::Image& image) {
  tray_icon_->SetImage(image);
}

void Tray::SetPressedImage(mate::Arguments* args, const gfx::Image& image) {
  tray_icon_->SetPressedImage(image);
}

void Tray::SetToolTip(mate::Arguments* args, const std::string& tool_tip) {
  tray_icon_->SetToolTip(tool_tip);
}

void Tray::SetTitle(mate::Arguments* args, const std::string& title) {
  tray_icon_->SetTitle(title);
}

void Tray::SetHighlightMode(mate::Arguments* args, bool highlight) {
  tray_icon_->SetHighlightMode(highlight);
}

void Tray::DisplayBalloon(mate::Arguments* args,
                          const mate::Dictionary& options) {
  gfx::Image icon;
  options.Get("icon", &icon);
  base::string16 title, content;
  if (!options.Get("title", &title) ||
      !options.Get("content", &content)) {
    args->ThrowError("'title' and 'content' must be defined");
    return;
  }

  tray_icon_->DisplayBalloon(icon, title, content);
}

void Tray::PopUpContextMenu(mate::Arguments* args) {
  gfx::Point pos;
  args->GetNext(&pos);
  tray_icon_->PopUpContextMenu(pos);
}

void Tray::SetContextMenu(mate::Arguments* args, Menu* menu) {
  tray_icon_->SetContextMenu(menu->model());
}

v8::Local<v8::Object> Tray::ModifiersToObject(v8::Isolate* isolate,
                                              int modifiers) {
  mate::Dictionary obj(isolate, v8::Object::New(isolate));
  obj.Set("shiftKey", static_cast<bool>(modifiers & ui::EF_SHIFT_DOWN));
  obj.Set("ctrlKey", static_cast<bool>(modifiers & ui::EF_CONTROL_DOWN));
  obj.Set("altKey", static_cast<bool>(modifiers & ui::EF_ALT_DOWN));
  obj.Set("metaKey", static_cast<bool>(modifiers & ui::EF_COMMAND_DOWN));
  return obj.GetHandle();
}

// static
void Tray::BuildPrototype(v8::Isolate* isolate,
                          v8::Local<v8::ObjectTemplate> prototype) {
  mate::ObjectTemplateBuilder(isolate, prototype)
      .SetMethod("destroy", &Tray::Destroy, true)
      .SetMethod("setImage", &Tray::SetImage)
      .SetMethod("setPressedImage", &Tray::SetPressedImage)
      .SetMethod("setToolTip", &Tray::SetToolTip)
      .SetMethod("setTitle", &Tray::SetTitle)
      .SetMethod("setHighlightMode", &Tray::SetHighlightMode)
      .SetMethod("displayBalloon", &Tray::DisplayBalloon)
      .SetMethod("popUpContextMenu", &Tray::PopUpContextMenu)
      .SetMethod("_setContextMenu", &Tray::SetContextMenu);
}

}  // namespace api

}  // namespace atom


namespace {

void Initialize(v8::Local<v8::Object> exports, v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context, void* priv) {
  using atom::api::Tray;
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::Function> constructor = mate::CreateConstructor<Tray>(
      isolate, "Tray", base::Bind(&Tray::New));
  mate::Dictionary dict(isolate, exports);
  dict.Set("Tray", static_cast<v8::Local<v8::Value>>(constructor));
}

}  // namespace

NODE_MODULE_CONTEXT_AWARE_BUILTIN(atom_browser_tray, Initialize)
