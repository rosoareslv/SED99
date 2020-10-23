// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/net/url_request_fetch_job.h"

#include <algorithm>
#include <string>

#include "base/strings/string_util.h"
#include "base/thread_task_runner_handle.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_fetcher_response_writer.h"
#include "net/url_request/url_request_context_builder.h"
#include "net/url_request/url_request_status.h"

namespace atom {

namespace {

// Convert string to RequestType.
net::URLFetcher::RequestType GetRequestType(const std::string& raw) {
  std::string method = StringToUpperASCII(raw);
  if (method.empty() || method == "GET")
    return net::URLFetcher::GET;
  else if (method == "POST")
    return net::URLFetcher::POST;
  else if (method == "HEAD")
    return net::URLFetcher::HEAD;
  else if (method == "DELETE")
    return net::URLFetcher::DELETE_REQUEST;
  else if (method == "PUT")
    return net::URLFetcher::PUT;
  else if (method == "PATCH")
    return net::URLFetcher::PATCH;
  else  // Use "GET" as fallback.
    return net::URLFetcher::GET;
}

// Pipe the response writer back to URLRequestFetchJob.
class ResponsePiper : public net::URLFetcherResponseWriter {
 public:
  explicit ResponsePiper(URLRequestFetchJob* job)
      : first_write_(true), job_(job) {}

  // net::URLFetcherResponseWriter:
  int Initialize(const net::CompletionCallback& callback) override {
    return net::OK;
  }
  int Write(net::IOBuffer* buffer,
            int num_bytes,
            const net::CompletionCallback& callback) override {
    if (first_write_) {
      // The URLFetcherResponseWriter doesn't have an event when headers have
      // been read, so we have to emulate by hooking to first write event.
      job_->HeadersCompleted();
      first_write_ = false;
    }
    return job_->DataAvailable(buffer, num_bytes);
  }
  int Finish(const net::CompletionCallback& callback) override {
    return net::OK;
  }

 private:
  bool first_write_;
  URLRequestFetchJob* job_;

  DISALLOW_COPY_AND_ASSIGN(ResponsePiper);
};

}  // namespace

URLRequestFetchJob::URLRequestFetchJob(
    scoped_refptr<net::URLRequestContextGetter> request_context_getter,
    net::URLRequest* request,
    net::NetworkDelegate* network_delegate,
    const GURL& url,
    const std::string& method,
    const std::string& referrer)
    : net::URLRequestJob(request, network_delegate),
      pending_buffer_size_(0) {
  // Use |request|'s method if |method| is not specified.
  net::URLFetcher::RequestType request_type;
  if (method.empty())
    request_type = GetRequestType(request->method());
  else
    request_type = GetRequestType(method);

  fetcher_.reset(net::URLFetcher::Create(url, request_type, this));
  // Use request context if provided else create one.
  if (request_context_getter)
    fetcher_->SetRequestContext(request_context_getter.get());
  else
    fetcher_->SetRequestContext(GetRequestContext());

  fetcher_->SaveResponseWithWriter(make_scoped_ptr(new ResponsePiper(this)));

  // Use |request|'s referrer if |referrer| is not specified.
  if (referrer.empty()) {
    fetcher_->SetReferrer(request->referrer());
  } else {
    fetcher_->SetReferrer(referrer);
  }

  // Use |request|'s headers.
  fetcher_->SetExtraRequestHeaders(request->extra_request_headers().ToString());
}

net::URLRequestContextGetter* URLRequestFetchJob::GetRequestContext() {
  if (!url_request_context_getter_.get()) {
    auto task_runner = base::ThreadTaskRunnerHandle::Get();
    net::URLRequestContextBuilder builder;
    builder.set_proxy_service(net::ProxyService::CreateDirect());
    url_request_context_getter_ =
        new net::TrivialURLRequestContextGetter(builder.Build(), task_runner);
  }
  return url_request_context_getter_.get();
}

void URLRequestFetchJob::HeadersCompleted() {
  response_info_.reset(new net::HttpResponseInfo);
  response_info_->headers = fetcher_->GetResponseHeaders();
  NotifyHeadersComplete();
}

int URLRequestFetchJob::DataAvailable(net::IOBuffer* buffer, int num_bytes) {
  // Clear the IO_PENDING status.
  SetStatus(net::URLRequestStatus());
  // Do nothing if pending_buffer_ is empty, i.e. there's no ReadRawData()
  // operation waiting for IO completion.
  if (!pending_buffer_.get())
    return net::ERR_IO_PENDING;

  // pending_buffer_ is set to the IOBuffer instance provided to ReadRawData()
  // by URLRequestJob.

  int bytes_read = std::min(num_bytes, pending_buffer_size_);
  memcpy(pending_buffer_->data(), buffer->data(), bytes_read);

  // Clear the buffers before notifying the read is complete, so that it is
  // safe for the observer to read.
  pending_buffer_ = nullptr;
  pending_buffer_size_ = 0;

  NotifyReadComplete(bytes_read);
  return bytes_read;
}

void URLRequestFetchJob::Start() {
  fetcher_->Start();
}

void URLRequestFetchJob::Kill() {
  URLRequestJob::Kill();
  fetcher_.reset();
}

bool URLRequestFetchJob::ReadRawData(net::IOBuffer* dest,
                                     int dest_size,
                                     int* bytes_read) {
  pending_buffer_ = dest;
  pending_buffer_size_ = dest_size;
  SetStatus(net::URLRequestStatus(net::URLRequestStatus::IO_PENDING, 0));
  return false;
}

bool URLRequestFetchJob::GetMimeType(std::string* mime_type) const {
  if (!response_info_)
    return false;

  return response_info_->headers->GetMimeType(mime_type);
}

void URLRequestFetchJob::GetResponseInfo(net::HttpResponseInfo* info) {
  if (response_info_)
    *info = *response_info_;
}

int URLRequestFetchJob::GetResponseCode() const {
  if (!response_info_)
    return -1;

  return response_info_->headers->response_code();
}

void URLRequestFetchJob::OnURLFetchComplete(const net::URLFetcher* source) {
  pending_buffer_ = nullptr;
  pending_buffer_size_ = 0;
  NotifyDone(fetcher_->GetStatus());
  if (fetcher_->GetStatus().is_success())
    NotifyReadComplete(0);
}

}  // namespace atom
