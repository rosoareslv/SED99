/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

#include <boost/intrusive/list.hpp>

#include <folly/Hash.h>
#include <folly/Indestructible.h>
#include <folly/Optional.h>
#include <folly/Portability.h>
#include <folly/Unit.h>

namespace folly {

namespace parking_lot_detail {

struct WaitNodeBase : public boost::intrusive::list_base_hook<> {
  const uint64_t key_;
  const uint64_t lotid_;

  // tricky: hold both bucket and node mutex to write, either to read
  bool signaled_;
  std::mutex mutex_;
  std::condition_variable cond_;

  WaitNodeBase(uint64_t key, uint64_t lotid)
      : key_(key), lotid_(lotid), signaled_(false) {}

  template <typename Clock, typename Duration>
  std::cv_status wait(std::chrono::time_point<Clock, Duration> deadline) {
    std::cv_status status = std::cv_status::no_timeout;
    std::unique_lock<std::mutex> nodeLock(mutex_);
    while (!signaled_ && status != std::cv_status::timeout) {
      if (deadline != std::chrono::time_point<Clock, Duration>::max()) {
        status = cond_.wait_until(nodeLock, deadline);
      } else {
        cond_.wait(nodeLock);
      }
    }
    return status;
  }

  void wake() {
    std::unique_lock<std::mutex> nodeLock(mutex_);
    signaled_ = true;
    cond_.notify_one();
  }

  bool signaled() {
    return signaled_;
  }
};

extern std::atomic<uint64_t> idallocator;

// Our emulated futex uses 4096 lists of wait nodes.  There are two levels
// of locking: a per-list mutex that controls access to the list and a
// per-node mutex, condvar, and bool that are used for the actual wakeups.
// The per-node mutex allows us to do precise wakeups without thundering
// herds.
struct Bucket {
  std::mutex mutex_;
  boost::intrusive::list<WaitNodeBase> waiters_;

  static Bucket& bucketFor(uint64_t key);
};

} // namespace parking_lot_detail

enum class UnparkControl {
  RetainContinue,
  RemoveContinue,
  RetainBreak,
  RemoveBreak,
};

enum class ParkResult {
  Skip,
  Unpark,
  Timeout,
};

/*
 * ParkingLot provides an interface that is similar to Linux's futex
 * system call, but with additional functionality.  It is implemented
 * in a portable way on top of std::mutex and std::condition_variable.
 *
 * Additional reading:
 * https://webkit.org/blog/6161/locking-in-webkit/
 * https://github.com/WebKit/webkit/blob/master/Source/WTF/wtf/ParkingLot.h
 * https://locklessinc.com/articles/futex_cheat_sheet/
 *
 * The main difference from futex is that park/unpark take lambdas,
 * such that nearly anything can be done while holding the bucket
 * lock.  Unpark() lambda can also be used to wake up any number of
 * waiters.
 *
 * ParkingLot is templated on the data type, however, all ParkingLot
 * implementations are backed by a single static array of buckets to
 * avoid large memory overhead.  Lambdas will only ever be called on
 * the specific ParkingLot's nodes.
 */
template <typename Data = Unit>
class ParkingLot {
  const uint64_t lotid_;
  ParkingLot(const ParkingLot&) = delete;

  struct WaitNode : public parking_lot_detail::WaitNodeBase {
    const Data data_;

    template <typename D>
    WaitNode(uint64_t key, uint64_t lotid, D&& data)
        : WaitNodeBase(key, lotid), data_(std::forward<Data>(data)) {}
  };

 public:
  ParkingLot() : lotid_(parking_lot_detail::idallocator++) {}

  /* Park API
   *
   * Key is almost always the address of a variable.
   *
   * ToPark runs while holding the bucket lock: usually this
   * is a check to see if we can sleep, by checking waiter bits.
   *
   * PreWait is usually used to implement condition variable like
   * things, such that you can unlock the condition variable's lock at
   * the appropriate time.
   */
  template <typename Key, typename D, typename ToPark, typename PreWait>
  ParkResult park(const Key key, D&& data, ToPark&& toPark, PreWait&& preWait) {
    return park_until(
        key,
        std::forward<D>(data),
        std::forward<ToPark>(toPark),
        std::forward<PreWait>(preWait),
        std::chrono::steady_clock::time_point::max());
  }

  template <
      typename Key,
      typename D,
      typename ToPark,
      typename PreWait,
      typename Clock,
      typename Duration>
  ParkResult park_until(
      const Key key,
      D&& data,
      ToPark&& toPark,
      PreWait&& preWait,
      std::chrono::time_point<Clock, Duration> deadline);

  template <
      typename Key,
      typename D,
      typename ToPark,
      typename PreWait,
      typename Rep,
      typename Period>
  ParkResult park_for(
      const Key key,
      D&& data,
      ToPark&& toPark,
      PreWait&& preWait,
      std::chrono::duration<Rep, Period>& timeout) {
    return park_until(
        key,
        std::forward<D>(data),
        std::forward<ToPark>(toPark),
        std::forward<PreWait>(preWait),
        timeout + std::chrono::steady_clock::now());
  }

  /*
   * Unpark API
   *
   * Key is the same uniqueaddress used in park(), and is used as a
   * hash key for lookup of waiters.
   *
   * Unparker is a function that is given the Data parameter, and
   * returns an UnparkControl.  The Remove* results will remove and
   * wake the waiter, the Ignore/Stop results will not, while stopping
   * or continuing iteration of the waiter list.
   */
  template <typename Key, typename Unparker>
  void unpark(const Key key, Unparker&& func);
};

template <typename Data>
template <
    typename Key,
    typename D,
    typename ToPark,
    typename PreWait,
    typename Clock,
    typename Duration>
ParkResult ParkingLot<Data>::park_until(
    const Key bits,
    D&& data,
    ToPark&& toPark,
    PreWait&& preWait,
    std::chrono::time_point<Clock, Duration> deadline) {
  auto key = hash::twang_mix64(uint64_t(bits));
  auto& bucket = parking_lot_detail::Bucket::bucketFor(key);
  WaitNode node(key, lotid_, std::forward<D>(data));

  {
    std::unique_lock<std::mutex> bucketLock(bucket.mutex_);

    if (!std::forward<ToPark>(toPark)()) {
      return ParkResult::Skip;
    }

    bucket.waiters_.push_back(node);
  } // bucketLock scope

  std::forward<PreWait>(preWait)();

  auto status = node.wait(deadline);

  if (status == std::cv_status::timeout) {
    // it's not really a timeout until we unlink the unsignaled node
    std::unique_lock<std::mutex> bucketLock(bucket.mutex_);
    if (!node.signaled()) {
      bucket.waiters_.erase(bucket.waiters_.iterator_to(node));
      return ParkResult::Timeout;
    }
  }

  return ParkResult::Unpark;
}

template <typename Data>
template <typename Key, typename Func>
void ParkingLot<Data>::unpark(const Key bits, Func&& func) {
  auto key = hash::twang_mix64(uint64_t(bits));
  auto& bucket = parking_lot_detail::Bucket::bucketFor(key);
  std::unique_lock<std::mutex> bucketLock(bucket.mutex_);

  for (auto iter = bucket.waiters_.begin(); iter != bucket.waiters_.end();) {
    auto current = iter;
    auto& node = *static_cast<WaitNode*>(&*iter++);
    if (node.key_ == key && node.lotid_ == lotid_) {
      auto result = std::forward<Func>(func)(node.data_);
      if (result == UnparkControl::RemoveBreak ||
          result == UnparkControl::RemoveContinue) {
        // we unlink, but waiter destroys the node
        bucket.waiters_.erase(current);

        node.wake();
      }
      if (result == UnparkControl::RemoveBreak ||
          result == UnparkControl::RetainBreak) {
        return;
      }
    }
  }
}

} // namespace folly
