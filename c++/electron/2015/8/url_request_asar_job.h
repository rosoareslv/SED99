// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ATOM_BROWSER_NET_ASAR_URL_REQUEST_ASAR_JOB_H_
#define ATOM_BROWSER_NET_ASAR_URL_REQUEST_ASAR_JOB_H_

#include <memory>
#include <string>

#include "atom/common/asar/archive.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "net/url_request/url_request_job.h"

namespace base {
class TaskRunner;
}

namespace net {
class FileStream;
}

namespace asar {

// Createa a request job according to the file path.
net::URLRequestJob* CreateJobFromPath(
    const base::FilePath& full_path,
    net::URLRequest* request,
    net::NetworkDelegate* network_delegate,
    const scoped_refptr<base::TaskRunner> file_task_runner);

class URLRequestAsarJob : public net::URLRequestJob {
 public:
  URLRequestAsarJob(net::URLRequest* request,
                    net::NetworkDelegate* network_delegate,
                    std::shared_ptr<Archive> archive,
                    const base::FilePath& file_path,
                    const Archive::FileInfo& file_info,
                    const scoped_refptr<base::TaskRunner>& file_task_runner);

  // net::URLRequestJob:
  void Start() override;
  void Kill() override;
  bool ReadRawData(net::IOBuffer* buf,
                   int buf_size,
                   int* bytes_read) override;
  bool GetMimeType(std::string* mime_type) const override;

 protected:
  virtual ~URLRequestAsarJob();

 private:
  // Callback after opening file on a background thread.
  void DidOpen(int result);

  // Callback after seeking to the beginning of |byte_range_| in the file
  // on a background thread.
  void DidSeek(int64 result);

  // Callback after data is asynchronously read from the file into |buf|.
  void DidRead(scoped_refptr<net::IOBuffer> buf, int result);

  std::shared_ptr<Archive> archive_;
  base::FilePath file_path_;
  Archive::FileInfo file_info_;

  scoped_ptr<net::FileStream> stream_;
  int64 remaining_bytes_;

  const scoped_refptr<base::TaskRunner> file_task_runner_;

  base::WeakPtrFactory<URLRequestAsarJob> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(URLRequestAsarJob);
};

}  // namespace asar

#endif  // ATOM_BROWSER_NET_ASAR_URL_REQUEST_ASAR_JOB_H_
