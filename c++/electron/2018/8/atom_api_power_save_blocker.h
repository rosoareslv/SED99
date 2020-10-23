// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ATOM_BROWSER_API_ATOM_API_POWER_SAVE_BLOCKER_H_
#define ATOM_BROWSER_API_ATOM_API_POWER_SAVE_BLOCKER_H_

#include <map>
#include <memory>

#include "atom/browser/api/trackable_object.h"
#include "native_mate/handle.h"
#include "services/device/wake_lock/power_save_blocker/power_save_blocker.h"

namespace mate {
class Dictionary;
}

namespace atom {

namespace api {

class PowerSaveBlocker : public mate::TrackableObject<PowerSaveBlocker> {
 public:
  static mate::Handle<PowerSaveBlocker> Create(v8::Isolate* isolate);

  static void BuildPrototype(v8::Isolate* isolate,
                             v8::Local<v8::FunctionTemplate> prototype);

 protected:
  explicit PowerSaveBlocker(v8::Isolate* isolate);
  ~PowerSaveBlocker() override;

 private:
  void UpdatePowerSaveBlocker();
  int Start(device::mojom::WakeLockType type);
  bool Stop(int id);
  bool IsStarted(int id);

  std::unique_ptr<device::PowerSaveBlocker> power_save_blocker_;

  // Current blocker type used by |power_save_blocker_|
  device::mojom::WakeLockType current_blocker_type_;

  // Map from id to the corresponding blocker type for each request.
  using WakeLockTypeMap = std::map<int, device::mojom::WakeLockType>;
  WakeLockTypeMap power_save_blocker_types_;

  DISALLOW_COPY_AND_ASSIGN(PowerSaveBlocker);
};

}  // namespace api

}  // namespace atom

#endif  // ATOM_BROWSER_API_ATOM_API_POWER_SAVE_BLOCKER_H_
