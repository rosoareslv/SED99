// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/api/atom_api_menu.h"

#include "atom/browser/native_window.h"
#include "atom/common/native_mate_converters/accelerator_converter.h"
#include "atom/common/native_mate_converters/callback.h"
#include "atom/common/native_mate_converters/image_converter.h"
#include "atom/common/native_mate_converters/string16_converter.h"
#include "atom/common/node_includes.h"
#include "native_mate/constructor.h"
#include "native_mate/dictionary.h"
#include "native_mate/object_template_builder.h"

namespace atom {

namespace api {

Menu::Menu(v8::Isolate* isolate, v8::Local<v8::Object> wrapper)
    : model_(new AtomMenuModel(this)) {
  InitWith(isolate, wrapper);
  model_->AddObserver(this);
}

Menu::~Menu() {
  if (model_) {
    model_->RemoveObserver(this);
  }
}

void Menu::AfterInit(v8::Isolate* isolate) {
  mate::Dictionary wrappable(isolate, GetWrapper());
  mate::Dictionary delegate;
  if (!wrappable.Get("delegate", &delegate))
    return;

  delegate.Get("isCommandIdChecked", &is_checked_);
  delegate.Get("isCommandIdEnabled", &is_enabled_);
  delegate.Get("isCommandIdVisible", &is_visible_);
  delegate.Get("shouldCommandIdWorkWhenHidden", &works_when_hidden_);
  delegate.Get("getAcceleratorForCommandId", &get_accelerator_);
  delegate.Get("shouldRegisterAcceleratorForCommandId",
               &should_register_accelerator_);
  delegate.Get("executeCommand", &execute_command_);
  delegate.Get("menuWillShow", &menu_will_show_);
}

bool Menu::IsCommandIdChecked(int command_id) const {
  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  return is_checked_.Run(GetWrapper(), command_id);
}

bool Menu::IsCommandIdEnabled(int command_id) const {
  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  return is_enabled_.Run(GetWrapper(), command_id);
}

bool Menu::IsCommandIdVisible(int command_id) const {
  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  return is_visible_.Run(GetWrapper(), command_id);
}

bool Menu::ShouldCommandIdWorkWhenHidden(int command_id) const {
  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  return works_when_hidden_.Run(GetWrapper(), command_id);
}

bool Menu::GetAcceleratorForCommandIdWithParams(
    int command_id,
    bool use_default_accelerator,
    ui::Accelerator* accelerator) const {
  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  v8::Local<v8::Value> val =
      get_accelerator_.Run(GetWrapper(), command_id, use_default_accelerator);
  return mate::ConvertFromV8(isolate(), val, accelerator);
}

bool Menu::ShouldRegisterAcceleratorForCommandId(int command_id) const {
  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  return should_register_accelerator_.Run(GetWrapper(), command_id);
}

void Menu::ExecuteCommand(int command_id, int flags) {
  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  execute_command_.Run(GetWrapper(),
                       mate::internal::CreateEventFromFlags(isolate(), flags),
                       command_id);
}

void Menu::OnMenuWillShow(ui::SimpleMenuModel* source) {
  v8::Locker locker(isolate());
  v8::HandleScope handle_scope(isolate());
  menu_will_show_.Run(GetWrapper());
}

void Menu::InsertItemAt(int index,
                        int command_id,
                        const base::string16& label) {
  model_->InsertItemAt(index, command_id, label);
}

void Menu::InsertSeparatorAt(int index) {
  model_->InsertSeparatorAt(index, ui::NORMAL_SEPARATOR);
}

void Menu::InsertCheckItemAt(int index,
                             int command_id,
                             const base::string16& label) {
  model_->InsertCheckItemAt(index, command_id, label);
}

void Menu::InsertRadioItemAt(int index,
                             int command_id,
                             const base::string16& label,
                             int group_id) {
  model_->InsertRadioItemAt(index, command_id, label, group_id);
}

void Menu::InsertSubMenuAt(int index,
                           int command_id,
                           const base::string16& label,
                           Menu* menu) {
  menu->parent_ = this;
  model_->InsertSubMenuAt(index, command_id, label, menu->model_.get());
}

void Menu::SetIcon(int index, const gfx::Image& image) {
  model_->SetIcon(index, image);
}

void Menu::SetSublabel(int index, const base::string16& sublabel) {
  model_->SetSublabel(index, sublabel);
}

void Menu::SetRole(int index, const base::string16& role) {
  model_->SetRole(index, role);
}

void Menu::Clear() {
  model_->Clear();
}

int Menu::GetIndexOfCommandId(int command_id) {
  return model_->GetIndexOfCommandId(command_id);
}

int Menu::GetItemCount() const {
  return model_->GetItemCount();
}

int Menu::GetCommandIdAt(int index) const {
  return model_->GetCommandIdAt(index);
}

base::string16 Menu::GetLabelAt(int index) const {
  return model_->GetLabelAt(index);
}

base::string16 Menu::GetSublabelAt(int index) const {
  return model_->GetSublabelAt(index);
}

base::string16 Menu::GetAcceleratorTextAt(int index) const {
  ui::Accelerator accelerator;
  model_->GetAcceleratorAtWithParams(index, true, &accelerator);
  return accelerator.GetShortcutText();
}

bool Menu::IsItemCheckedAt(int index) const {
  return model_->IsItemCheckedAt(index);
}

bool Menu::IsEnabledAt(int index) const {
  return model_->IsEnabledAt(index);
}

bool Menu::IsVisibleAt(int index) const {
  return model_->IsVisibleAt(index);
}

bool Menu::WorksWhenHiddenAt(int index) const {
  return model_->WorksWhenHiddenAt(index);
}

void Menu::OnMenuWillClose() {
  Emit("menu-will-close");
}

void Menu::OnMenuWillShow() {
  Emit("menu-will-show");
}

// static
void Menu::BuildPrototype(v8::Isolate* isolate,
                          v8::Local<v8::FunctionTemplate> prototype) {
  prototype->SetClassName(mate::StringToV8(isolate, "Menu"));
  mate::ObjectTemplateBuilder(isolate, prototype->PrototypeTemplate())
      .MakeDestroyable()
      .SetMethod("insertItem", &Menu::InsertItemAt)
      .SetMethod("insertCheckItem", &Menu::InsertCheckItemAt)
      .SetMethod("insertRadioItem", &Menu::InsertRadioItemAt)
      .SetMethod("insertSeparator", &Menu::InsertSeparatorAt)
      .SetMethod("insertSubMenu", &Menu::InsertSubMenuAt)
      .SetMethod("setIcon", &Menu::SetIcon)
      .SetMethod("setSublabel", &Menu::SetSublabel)
      .SetMethod("setRole", &Menu::SetRole)
      .SetMethod("clear", &Menu::Clear)
      .SetMethod("getIndexOfCommandId", &Menu::GetIndexOfCommandId)
      .SetMethod("getItemCount", &Menu::GetItemCount)
      .SetMethod("getCommandIdAt", &Menu::GetCommandIdAt)
      .SetMethod("getLabelAt", &Menu::GetLabelAt)
      .SetMethod("getSublabelAt", &Menu::GetSublabelAt)
      .SetMethod("getAcceleratorTextAt", &Menu::GetAcceleratorTextAt)
      .SetMethod("isItemCheckedAt", &Menu::IsItemCheckedAt)
      .SetMethod("isEnabledAt", &Menu::IsEnabledAt)
      .SetMethod("worksWhenHiddenAt", &Menu::WorksWhenHiddenAt)
      .SetMethod("isVisibleAt", &Menu::IsVisibleAt)
      .SetMethod("popupAt", &Menu::PopupAt)
      .SetMethod("closePopupAt", &Menu::ClosePopupAt);
}

}  // namespace api

}  // namespace atom

namespace {

using atom::api::Menu;

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  Menu::SetConstructor(isolate, base::BindRepeating(&Menu::New));

  mate::Dictionary dict(isolate, exports);
  dict.Set(
      "Menu",
      Menu::GetConstructor(isolate)->GetFunction(context).ToLocalChecked());
#if defined(OS_MACOSX)
  dict.SetMethod("setApplicationMenu", &Menu::SetApplicationMenu);
  dict.SetMethod("sendActionToFirstResponder",
                 &Menu::SendActionToFirstResponder);
#endif
}

}  // namespace

NODE_LINKED_MODULE_CONTEXT_AWARE(atom_browser_menu, Initialize)
