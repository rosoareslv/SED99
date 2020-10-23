// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <set>
#include <string>

#include "atom/common/native_mate_converters/callback.h"
#include "atom/common/native_mate_converters/file_path_converter.h"
#include "atom/common/native_mate_converters/value_converter.h"
#include "atom/common/node_includes.h"
#include "atom/common/promise_util.h"
#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/threading/thread_restrictions.h"
#include "content/public/browser/tracing_controller.h"
#include "native_mate/dictionary.h"

using content::TracingController;

namespace mate {

template <>
struct Converter<base::trace_event::TraceConfig> {
  static bool FromV8(v8::Isolate* isolate,
                     v8::Local<v8::Value> val,
                     base::trace_event::TraceConfig* out) {
    // (alexeykuzmin): A combination of "categoryFilter" and "traceOptions"
    // has to be checked first because none of the fields
    // in the `memory_dump_config` dict below are mandatory
    // and we cannot check the config format.
    Dictionary options;
    if (ConvertFromV8(isolate, val, &options)) {
      std::string category_filter, trace_options;
      if (options.Get("categoryFilter", &category_filter) &&
          options.Get("traceOptions", &trace_options)) {
        *out = base::trace_event::TraceConfig(category_filter, trace_options);
        return true;
      }
    }

    base::DictionaryValue memory_dump_config;
    if (ConvertFromV8(isolate, val, &memory_dump_config)) {
      *out = base::trace_event::TraceConfig(memory_dump_config);
      return true;
    }

    return false;
  }
};

}  // namespace mate

namespace {

using CompletionCallback = base::RepeatingCallback<void(const base::FilePath&)>;

scoped_refptr<TracingController::TraceDataEndpoint> GetTraceDataEndpoint(
    const base::FilePath& path,
    const CompletionCallback& callback) {
  base::FilePath result_file_path = path;

  // base::CreateTemporaryFile prevents blocking so we need to allow it
  // for now since offloading this to a different sequence would require
  // changing the api shape
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  if (result_file_path.empty() && !base::CreateTemporaryFile(&result_file_path))
    LOG(ERROR) << "Creating temporary file failed";

  return TracingController::CreateFileEndpoint(
      result_file_path, base::BindRepeating(callback, result_file_path));
}

v8::Local<v8::Promise> StopRecording(v8::Isolate* isolate,
                                     const base::FilePath& path) {
  atom::util::Promise promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  // TODO(zcbenz): Remove the use of CopyablePromise when the
  // CreateFileEndpoint API accepts OnceCallback.
  TracingController::GetInstance()->StopTracing(GetTraceDataEndpoint(
      path,
      base::BindRepeating(atom::util::CopyablePromise::ResolveCopyablePromise<
                              const base::FilePath&>,
                          atom::util::CopyablePromise(promise))));
  return handle;
}

v8::Local<v8::Promise> GetCategories(v8::Isolate* isolate) {
  atom::util::Promise promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  // Note: This method always succeeds.
  TracingController::GetInstance()->GetCategories(base::BindOnce(
      atom::util::Promise::ResolvePromise<const std::set<std::string>&>,
      std::move(promise)));

  return handle;
}

v8::Local<v8::Promise> StartTracing(
    v8::Isolate* isolate,
    const base::trace_event::TraceConfig& trace_config) {
  atom::util::Promise promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  // Note: This method always succeeds.
  TracingController::GetInstance()->StartTracing(
      trace_config, base::BindOnce(atom::util::Promise::ResolveEmptyPromise,
                                   std::move(promise)));
  return handle;
}

void OnTraceBufferUsageAvailable(atom::util::Promise promise,
                                 float percent_full,
                                 size_t approximate_count) {
  mate::Dictionary dict = mate::Dictionary::CreateEmpty(promise.isolate());
  dict.Set("percentage", percent_full);
  dict.Set("value", approximate_count);

  promise.Resolve(dict.GetHandle());
}

v8::Local<v8::Promise> GetTraceBufferUsage(v8::Isolate* isolate) {
  atom::util::Promise promise(isolate);
  v8::Local<v8::Promise> handle = promise.GetHandle();

  // Note: This method always succeeds.
  TracingController::GetInstance()->GetTraceBufferUsage(
      base::BindOnce(&OnTraceBufferUsageAvailable, std::move(promise)));
  return handle;
}

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  mate::Dictionary dict(context->GetIsolate(), exports);
  dict.SetMethod("getCategories", &GetCategories);
  dict.SetMethod("startRecording", &StartTracing);
  dict.SetMethod("stopRecording", &StopRecording);
  dict.SetMethod("getTraceBufferUsage", &GetTraceBufferUsage);
}

}  // namespace

NODE_LINKED_MODULE_CONTEXT_AWARE(atom_browser_content_tracing, Initialize)
