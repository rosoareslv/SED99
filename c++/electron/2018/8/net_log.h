// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef BRIGHTRAY_BROWSER_NET_LOG_H_
#define BRIGHTRAY_BROWSER_NET_LOG_H_

#include "base/callback.h"
#include "base/files/file_path.h"
#include "net/log/net_log.h"

namespace net {
class FileNetLogObserver;
}

namespace brightray {

class NetLog : public net::NetLog {
 public:
  NetLog();
  ~NetLog() override;

  void StartLogging();
  void StopLogging();

  void StartDynamicLogging(const base::FilePath& path);
  bool IsDynamicLogging();
  base::FilePath GetDynamicLoggingPath();
  void StopDynamicLogging(base::OnceClosure callback = base::OnceClosure());

 private:
  // This observer handles writing NetLogs.
  std::unique_ptr<net::FileNetLogObserver> file_net_log_observer_;
  std::unique_ptr<net::FileNetLogObserver> dynamic_file_net_log_observer_;
  base::FilePath dynamic_file_net_log_path_;

  DISALLOW_COPY_AND_ASSIGN(NetLog);
};

}  // namespace brightray

#endif  // BRIGHTRAY_BROWSER_NET_LOG_H_
