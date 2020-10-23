// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/gin_converters/content_converter.h"

#include <string>
#include <vector>

#include "content/public/browser/native_web_keyboard_event.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/context_menu_params.h"
#include "shell/browser/api/atom_api_web_contents.h"
#include "shell/browser/web_contents_permission_helper.h"
#include "shell/common/gin_converters/blink_converter.h"
#include "shell/common/gin_converters/callback_converter.h"
#include "shell/common/gin_converters/gurl_converter.h"
#include "shell/common/gin_helper/dictionary.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"

namespace {

void ExecuteCommand(content::WebContents* web_contents,
                    int action,
                    const content::CustomContextMenuContext& context) {
  web_contents->ExecuteCustomContextMenuCommand(action, context);
}

// Forward declaration for nested recursive call.
v8::Local<v8::Value> MenuToV8(v8::Isolate* isolate,
                              content::WebContents* web_contents,
                              const content::CustomContextMenuContext& context,
                              const std::vector<content::MenuItem>& menu);

v8::Local<v8::Value> MenuItemToV8(
    v8::Isolate* isolate,
    content::WebContents* web_contents,
    const content::CustomContextMenuContext& context,
    const content::MenuItem& item) {
  gin_helper::Dictionary v8_item = gin::Dictionary::CreateEmpty(isolate);
  switch (item.type) {
    case content::MenuItem::CHECKABLE_OPTION:
    case content::MenuItem::GROUP:
      v8_item.Set("checked", item.checked);
      FALLTHROUGH;
    case content::MenuItem::OPTION:
    case content::MenuItem::SUBMENU:
      v8_item.Set("label", item.label);
      v8_item.Set("enabled", item.enabled);
      FALLTHROUGH;
    default:
      v8_item.Set("type", item.type);
  }
  if (item.type == content::MenuItem::SUBMENU)
    v8_item.Set("submenu",
                MenuToV8(isolate, web_contents, context, item.submenu));
  else if (item.action > 0)
    v8_item.Set("click", base::BindRepeating(ExecuteCommand, web_contents,
                                             item.action, context));
  return v8_item.GetHandle();
}

v8::Local<v8::Value> MenuToV8(v8::Isolate* isolate,
                              content::WebContents* web_contents,
                              const content::CustomContextMenuContext& context,
                              const std::vector<content::MenuItem>& menu) {
  std::vector<v8::Local<v8::Value>> v8_menu;
  v8_menu.reserve(menu.size());
  for (const auto& menu_item : menu)
    v8_menu.push_back(MenuItemToV8(isolate, web_contents, context, menu_item));
  return gin::ConvertToV8(isolate, v8_menu);
}

}  // namespace

namespace gin {

template <>
struct Converter<ui::MenuSourceType> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   const ui::MenuSourceType& in) {
    switch (in) {
      case ui::MENU_SOURCE_MOUSE:
        return StringToV8(isolate, "mouse");
      case ui::MENU_SOURCE_KEYBOARD:
        return StringToV8(isolate, "keyboard");
      case ui::MENU_SOURCE_TOUCH:
        return StringToV8(isolate, "touch");
      case ui::MENU_SOURCE_TOUCH_EDIT_MENU:
        return StringToV8(isolate, "touchMenu");
      default:
        return StringToV8(isolate, "none");
    }
  }
};

// static
v8::Local<v8::Value> Converter<content::MenuItem::Type>::ToV8(
    v8::Isolate* isolate,
    const content::MenuItem::Type& val) {
  switch (val) {
    case content::MenuItem::CHECKABLE_OPTION:
      return StringToV8(isolate, "checkbox");
    case content::MenuItem::GROUP:
      return StringToV8(isolate, "radio");
    case content::MenuItem::SEPARATOR:
      return StringToV8(isolate, "separator");
    case content::MenuItem::SUBMENU:
      return StringToV8(isolate, "submenu");
    case content::MenuItem::OPTION:
    default:
      return StringToV8(isolate, "normal");
  }
}

// static
v8::Local<v8::Value> Converter<ContextMenuParamsWithWebContents>::ToV8(
    v8::Isolate* isolate,
    const ContextMenuParamsWithWebContents& val) {
  const auto& params = val.first;
  gin_helper::Dictionary dict = gin::Dictionary::CreateEmpty(isolate);
  dict.Set("x", params.x);
  dict.Set("y", params.y);
  dict.Set("linkURL", params.link_url);
  dict.Set("linkText", params.link_text);
  dict.Set("pageURL", params.page_url);
  dict.Set("frameURL", params.frame_url);
  dict.Set("srcURL", params.src_url);
  dict.Set("mediaType", params.media_type);
  dict.Set("mediaFlags", MediaFlagsToV8(isolate, params.media_flags));
  bool has_image_contents =
      (params.media_type == blink::ContextMenuDataMediaType::kImage) &&
      params.has_image_contents;
  dict.Set("hasImageContents", has_image_contents);
  dict.Set("isEditable", params.is_editable);
  dict.Set("editFlags", EditFlagsToV8(isolate, params.edit_flags));
  dict.Set("selectionText", params.selection_text);
  dict.Set("titleText", params.title_text);
  dict.Set("misspelledWord", params.misspelled_word);
#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
  dict.Set("dictionarySuggestions", params.dictionary_suggestions);
#endif
  dict.Set("frameCharset", params.frame_charset);
  dict.Set("inputFieldType", params.input_field_type);
  dict.Set("menuSourceType", params.source_type);

  if (params.custom_context.is_pepper_menu)
    dict.Set("menu", MenuToV8(isolate, val.second, params.custom_context,
                              params.custom_items));
  return gin::ConvertToV8(isolate, dict);
}

// static
bool Converter<blink::mojom::PermissionStatus>::FromV8(
    v8::Isolate* isolate,
    v8::Local<v8::Value> val,
    blink::mojom::PermissionStatus* out) {
  bool result;
  if (!ConvertFromV8(isolate, val, &result))
    return false;

  if (result)
    *out = blink::mojom::PermissionStatus::GRANTED;
  else
    *out = blink::mojom::PermissionStatus::DENIED;

  return true;
}

// static
v8::Local<v8::Value> Converter<content::PermissionType>::ToV8(
    v8::Isolate* isolate,
    const content::PermissionType& val) {
  using PermissionType = electron::WebContentsPermissionHelper::PermissionType;
  switch (val) {
    case content::PermissionType::MIDI_SYSEX:
      return StringToV8(isolate, "midiSysex");
    case content::PermissionType::NOTIFICATIONS:
      return StringToV8(isolate, "notifications");
    case content::PermissionType::GEOLOCATION:
      return StringToV8(isolate, "geolocation");
    case content::PermissionType::AUDIO_CAPTURE:
    case content::PermissionType::VIDEO_CAPTURE:
      return StringToV8(isolate, "media");
    case content::PermissionType::PROTECTED_MEDIA_IDENTIFIER:
      return StringToV8(isolate, "mediaKeySystem");
    case content::PermissionType::MIDI:
      return StringToV8(isolate, "midi");
    default:
      break;
  }

  switch (static_cast<PermissionType>(val)) {
    case PermissionType::POINTER_LOCK:
      return StringToV8(isolate, "pointerLock");
    case PermissionType::FULLSCREEN:
      return StringToV8(isolate, "fullscreen");
    case PermissionType::OPEN_EXTERNAL:
      return StringToV8(isolate, "openExternal");
    default:
      return StringToV8(isolate, "unknown");
  }
}

// static
bool Converter<content::StopFindAction>::FromV8(v8::Isolate* isolate,
                                                v8::Local<v8::Value> val,
                                                content::StopFindAction* out) {
  std::string action;
  if (!ConvertFromV8(isolate, val, &action))
    return false;

  if (action == "clearSelection")
    *out = content::STOP_FIND_ACTION_CLEAR_SELECTION;
  else if (action == "keepSelection")
    *out = content::STOP_FIND_ACTION_KEEP_SELECTION;
  else if (action == "activateSelection")
    *out = content::STOP_FIND_ACTION_ACTIVATE_SELECTION;
  else
    return false;

  return true;
}

// static
v8::Local<v8::Value> Converter<content::WebContents*>::ToV8(
    v8::Isolate* isolate,
    content::WebContents* val) {
  if (!val)
    return v8::Null(isolate);
  return electron::api::WebContents::FromOrCreate(isolate, val).ToV8();
}

// static
bool Converter<content::WebContents*>::FromV8(v8::Isolate* isolate,
                                              v8::Local<v8::Value> val,
                                              content::WebContents** out) {
  electron::api::WebContents* web_contents = nullptr;
  if (!gin::ConvertFromV8(isolate, val, &web_contents) || !web_contents)
    return false;

  *out = web_contents->web_contents();
  return true;
}

// static
v8::Local<v8::Value> Converter<content::Referrer>::ToV8(
    v8::Isolate* isolate,
    const content::Referrer& val) {
  gin_helper::Dictionary dict = gin::Dictionary::CreateEmpty(isolate);
  dict.Set("url", ConvertToV8(isolate, val.url));
  dict.Set("policy", ConvertToV8(isolate, val.policy));
  return gin::ConvertToV8(isolate, dict);
}

// static
bool Converter<content::Referrer>::FromV8(v8::Isolate* isolate,
                                          v8::Local<v8::Value> val,
                                          content::Referrer* out) {
  gin_helper::Dictionary dict;
  if (!ConvertFromV8(isolate, val, &dict))
    return false;

  if (!dict.Get("url", &out->url))
    return false;

  if (!dict.Get("policy", &out->policy))
    return false;

  return true;
}

// static
bool Converter<content::NativeWebKeyboardEvent>::FromV8(
    v8::Isolate* isolate,
    v8::Local<v8::Value> val,
    content::NativeWebKeyboardEvent* out) {
  gin_helper::Dictionary dict;
  if (!ConvertFromV8(isolate, val, &dict))
    return false;
  if (!ConvertFromV8(isolate, val, static_cast<blink::WebKeyboardEvent*>(out)))
    return false;
  dict.Get("skipInBrowser", &out->skip_in_browser);
  return true;
}

// static
v8::Local<v8::Value> Converter<content::NativeWebKeyboardEvent>::ToV8(
    v8::Isolate* isolate,
    const content::NativeWebKeyboardEvent& in) {
  gin_helper::Dictionary dict = gin::Dictionary::CreateEmpty(isolate);

  if (in.GetType() == blink::WebInputEvent::Type::kRawKeyDown)
    dict.Set("type", "keyDown");
  else if (in.GetType() == blink::WebInputEvent::Type::kKeyUp)
    dict.Set("type", "keyUp");
  dict.Set("key", ui::KeycodeConverter::DomKeyToKeyString(in.dom_key));
  dict.Set("code", ui::KeycodeConverter::DomCodeToCodeString(
                       static_cast<ui::DomCode>(in.dom_code)));

  using Modifiers = blink::WebInputEvent::Modifiers;
  dict.Set("isAutoRepeat", (in.GetModifiers() & Modifiers::kIsAutoRepeat) != 0);
  dict.Set("shift", (in.GetModifiers() & Modifiers::kShiftKey) != 0);
  dict.Set("control", (in.GetModifiers() & Modifiers::kControlKey) != 0);
  dict.Set("alt", (in.GetModifiers() & Modifiers::kAltKey) != 0);
  dict.Set("meta", (in.GetModifiers() & Modifiers::kMetaKey) != 0);

  return dict.GetHandle();
}

}  // namespace gin
