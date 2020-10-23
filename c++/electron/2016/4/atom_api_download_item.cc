// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/api/atom_api_download_item.h"

#include <map>

#include "atom/browser/atom_browser_main_parts.h"
#include "atom/common/native_mate_converters/callback.h"
#include "atom/common/native_mate_converters/file_path_converter.h"
#include "atom/common/native_mate_converters/gurl_converter.h"
#include "atom/common/node_includes.h"
#include "base/memory/linked_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "native_mate/dictionary.h"
#include "net/base/filename_util.h"

namespace mate {

template<>
struct Converter<content::DownloadItem::DownloadState> {
  static v8::Local<v8::Value> ToV8(v8::Isolate* isolate,
                                   content::DownloadItem::DownloadState state) {
    std::string download_state;
    switch (state) {
      case content::DownloadItem::COMPLETE:
        download_state = "completed";
        break;
      case content::DownloadItem::CANCELLED:
        download_state = "cancelled";
        break;
      case content::DownloadItem::INTERRUPTED:
        download_state = "interrupted";
        break;
      default:
        break;
    }
    return ConvertToV8(isolate, download_state);
  }
};

}  // namespace mate

namespace atom {

namespace api {

namespace {

// The wrapDownloadItem funtion which is implemented in JavaScript
using WrapDownloadItemCallback = base::Callback<void(v8::Local<v8::Value>)>;
WrapDownloadItemCallback g_wrap_download_item;

std::map<uint32_t, linked_ptr<v8::Global<v8::Value>>> g_download_item_objects;

}  // namespace

DownloadItem::DownloadItem(v8::Isolate* isolate,
                           content::DownloadItem* download_item)
    : download_item_(download_item) {
  download_item_->AddObserver(this);
  Init(isolate);
  AttachAsUserData(download_item);
}

DownloadItem::~DownloadItem() {
  if (download_item_) {
    // Destroyed by either garbage collection or destroy().
    download_item_->RemoveObserver(this);
    download_item_->Remove();
  }

  // Remove from the global map.
  auto iter = g_download_item_objects.find(weak_map_id());
  if (iter != g_download_item_objects.end())
    g_download_item_objects.erase(iter);
}

void DownloadItem::OnDownloadUpdated(content::DownloadItem* item) {
  if (download_item_->IsDone()) {
    Emit("done", item->GetState());

    // Destroy the item once item is downloaded.
    base::MessageLoop::current()->PostTask(FROM_HERE, GetDestroyClosure());
  } else {
    Emit("updated");
  }
}

void DownloadItem::OnDownloadDestroyed(content::DownloadItem* download_item) {
  download_item_ = nullptr;
  // Destroy the native class immediately when downloadItem is destroyed.
  delete this;
}

void DownloadItem::Pause() {
  download_item_->Pause();
}

void DownloadItem::Resume() {
  download_item_->Resume();
}

void DownloadItem::Cancel() {
  download_item_->Cancel(true);
  download_item_->Remove();
}

int64_t DownloadItem::GetReceivedBytes() const {
  return download_item_->GetReceivedBytes();
}

int64_t DownloadItem::GetTotalBytes() const {
  return download_item_->GetTotalBytes();
}

std::string DownloadItem::GetMimeType() const {
  return download_item_->GetMimeType();
}

bool DownloadItem::HasUserGesture() const {
  return download_item_->HasUserGesture();
}

std::string DownloadItem::GetFilename() const {
  return base::UTF16ToUTF8(net::GenerateFileName(GetURL(),
                           GetContentDisposition(),
                           std::string(),
                           download_item_->GetSuggestedFilename(),
                           GetMimeType(),
                           std::string()).LossyDisplayName());
}

std::string DownloadItem::GetContentDisposition() const {
  return download_item_->GetContentDisposition();
}

const GURL& DownloadItem::GetURL() const {
  return download_item_->GetURL();
}

void DownloadItem::SetSavePath(const base::FilePath& path) {
  save_path_ = path;
}

base::FilePath DownloadItem::GetSavePath() const {
  return save_path_;
}

// static
void DownloadItem::BuildPrototype(v8::Isolate* isolate,
                                  v8::Local<v8::ObjectTemplate> prototype) {
  mate::ObjectTemplateBuilder(isolate, prototype)
      .MakeDestroyable()
      .SetMethod("pause", &DownloadItem::Pause)
      .SetMethod("resume", &DownloadItem::Resume)
      .SetMethod("cancel", &DownloadItem::Cancel)
      .SetMethod("getReceivedBytes", &DownloadItem::GetReceivedBytes)
      .SetMethod("getTotalBytes", &DownloadItem::GetTotalBytes)
      .SetMethod("getMimeType", &DownloadItem::GetMimeType)
      .SetMethod("hasUserGesture", &DownloadItem::HasUserGesture)
      .SetMethod("getFilename", &DownloadItem::GetFilename)
      .SetMethod("getContentDisposition", &DownloadItem::GetContentDisposition)
      .SetMethod("getURL", &DownloadItem::GetURL)
      .SetMethod("setSavePath", &DownloadItem::SetSavePath)
      .SetMethod("getSavePath", &DownloadItem::GetSavePath);
}

// static
mate::Handle<DownloadItem> DownloadItem::Create(
    v8::Isolate* isolate, content::DownloadItem* item) {
  auto existing = TrackableObject::FromWrappedClass(isolate, item);
  if (existing)
    return mate::CreateHandle(isolate, static_cast<DownloadItem*>(existing));

  auto handle = mate::CreateHandle(isolate, new DownloadItem(isolate, item));
  g_wrap_download_item.Run(handle.ToV8());

  // Reference this object in case it got garbage collected.
  g_download_item_objects[handle->weak_map_id()] = make_linked_ptr(
      new v8::Global<v8::Value>(isolate, handle.ToV8()));
  return handle;
}

void SetWrapDownloadItem(const WrapDownloadItemCallback& callback) {
  g_wrap_download_item = callback;
}

}  // namespace api

}  // namespace atom

namespace {

void Initialize(v8::Local<v8::Object> exports, v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context, void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  mate::Dictionary dict(isolate, exports);
  dict.SetMethod("_setWrapDownloadItem", &atom::api::SetWrapDownloadItem);
}

}  // namespace

NODE_MODULE_CONTEXT_AWARE_BUILTIN(atom_browser_download_item, Initialize);
