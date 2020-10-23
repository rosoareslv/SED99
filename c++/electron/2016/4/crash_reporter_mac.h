// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ATOM_COMMON_CRASH_REPORTER_CRASH_REPORTER_MAC_H_
#define ATOM_COMMON_CRASH_REPORTER_CRASH_REPORTER_MAC_H_

#include <string>
#include <vector>

#include "atom/common/crash_reporter/crash_reporter.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string_piece.h"
#include "vendor/crashpad/client/simple_string_dictionary.h"

namespace base {
template <typename T> struct DefaultSingletonTraits;
}

namespace crash_reporter {

class CrashReporterMac : public CrashReporter {
 public:
  static CrashReporterMac* GetInstance();

  void InitBreakpad(const std::string& product_name,
                    const std::string& version,
                    const std::string& company_name,
                    const std::string& submit_url,
                    bool auto_submit,
                    bool skip_system_crash_handler) override;
  void SetUploadParameters() override;

 private:
  friend struct base::DefaultSingletonTraits<CrashReporterMac>;

  CrashReporterMac();
  virtual ~CrashReporterMac();

  void SetUploadsEnabled(bool enable_uploads);
  void SetCrashKeyValue(const base::StringPiece& key,
                        const base::StringPiece& value);

  std::vector<UploadReportResult> GetUploadedReports(
      const std::string& path) override;

  scoped_ptr<crashpad::SimpleStringDictionary> simple_string_dictionary_;

  DISALLOW_COPY_AND_ASSIGN(CrashReporterMac);
};

}  // namespace crash_reporter

#endif  // ATOM_COMMON_CRASH_REPORTER_CRASH_REPORTER_MAC_H_
