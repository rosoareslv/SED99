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
#include <atomic>
#include <memory>
#include <thread>

#include <folly/Hash.h>
#include <folly/concurrency/ConcurrentHashMap.h>
#include <folly/portability/GTest.h>
#include <folly/test/DeterministicSchedule.h>

using namespace folly::test;
using namespace folly;
using namespace std;

DEFINE_int64(seed, 0, "Seed for random number generators");

TEST(ConcurrentHashMap, MapTest) {
  ConcurrentHashMap<uint64_t, uint64_t> foomap(3);
  foomap.max_load_factor(1.05);
  EXPECT_TRUE(foomap.empty());
  EXPECT_EQ(foomap.find(1), foomap.cend());
  auto r = foomap.insert(1, 0);
  EXPECT_TRUE(r.second);
  auto r2 = foomap.insert(1, 0);
  EXPECT_EQ(r.first->second, 0);
  EXPECT_EQ(r.first->first, 1);
  EXPECT_EQ(r2.first->second, 0);
  EXPECT_EQ(r2.first->first, 1);
  EXPECT_EQ(r.first, r2.first);
  EXPECT_TRUE(r.second);
  EXPECT_FALSE(r2.second);
  EXPECT_FALSE(foomap.empty());
  EXPECT_TRUE(foomap.insert(std::make_pair(2, 0)).second);
  EXPECT_TRUE(foomap.insert_or_assign(2, 0).second);
  EXPECT_TRUE(foomap.assign_if_equal(2, 0, 3));
  EXPECT_TRUE(foomap.insert(3, 0).second);
  EXPECT_NE(foomap.find(1), foomap.cend());
  EXPECT_NE(foomap.find(2), foomap.cend());
  EXPECT_EQ(foomap.find(2)->second, 3);
  EXPECT_EQ(foomap[2], 3);
  EXPECT_EQ(foomap[20], 0);
  EXPECT_EQ(foomap.at(20), 0);
  EXPECT_FALSE(foomap.insert(1, 0).second);
  auto l = foomap.find(1);
  foomap.erase(l);
  EXPECT_FALSE(foomap.erase(1));
  EXPECT_EQ(foomap.find(1), foomap.cend());
  auto res = foomap.find(2);
  EXPECT_NE(res, foomap.cend());
  EXPECT_EQ(3, res->second);
  EXPECT_FALSE(foomap.empty());
  foomap.clear();
  EXPECT_TRUE(foomap.empty());
}

TEST(ConcurrentHashMap, MaxSizeTest) {
  ConcurrentHashMap<uint64_t, uint64_t> foomap(2, 16);
  bool insert_failed = false;
  for (int i = 0; i < 32; i++) {
    auto res = foomap.insert(0, 0);
    if (!res.second) {
      insert_failed = true;
    }
  }
  EXPECT_TRUE(insert_failed);
}

TEST(ConcurrentHashMap, MoveTest) {
  ConcurrentHashMap<uint64_t, uint64_t> foomap(2, 16);
  auto other = std::move(foomap);
  auto other2 = std::move(other);
  other = std::move(other2);
}

struct foo {
  static int moved;
  static int copied;
  foo(foo&& o) noexcept {
    (void*)&o;
    moved++;
  }
  foo& operator=(foo&& o) {
    (void*)&o;
    moved++;
    return *this;
  }
  foo& operator=(const foo& o) {
    (void*)&o;
    copied++;
    return *this;
  }
  foo(const foo& o) {
    (void*)&o;
    copied++;
  }
  foo() {}
};
int foo::moved{0};
int foo::copied{0};

TEST(ConcurrentHashMap, EmplaceTest) {
  ConcurrentHashMap<uint64_t, foo> foomap(200);
  foomap.insert(1, foo());
  EXPECT_EQ(foo::moved, 0);
  EXPECT_EQ(foo::copied, 1);
  foo::copied = 0;
  // The difference between emplace and try_emplace:
  // If insertion fails, try_emplace does not move its argument
  foomap.try_emplace(1, foo());
  EXPECT_EQ(foo::moved, 0);
  EXPECT_EQ(foo::copied, 0);
  foomap.emplace(1, foo());
  EXPECT_EQ(foo::moved, 1);
  EXPECT_EQ(foo::copied, 0);
}

TEST(ConcurrentHashMap, MapResizeTest) {
  ConcurrentHashMap<uint64_t, uint64_t> foomap(2);
  EXPECT_EQ(foomap.find(1), foomap.cend());
  EXPECT_TRUE(foomap.insert(1, 0).second);
  EXPECT_TRUE(foomap.insert(2, 0).second);
  EXPECT_TRUE(foomap.insert(3, 0).second);
  EXPECT_TRUE(foomap.insert(4, 0).second);
  foomap.reserve(512);
  EXPECT_NE(foomap.find(1), foomap.cend());
  EXPECT_NE(foomap.find(2), foomap.cend());
  EXPECT_FALSE(foomap.insert(1, 0).second);
  EXPECT_TRUE(foomap.erase(1));
  EXPECT_EQ(foomap.find(1), foomap.cend());
  auto res = foomap.find(2);
  EXPECT_NE(res, foomap.cend());
  if (res != foomap.cend()) {
    EXPECT_EQ(0, res->second);
  }
}

// Ensure we can insert objects without copy constructors.
TEST(ConcurrentHashMap, MapNoCopiesTest) {
  struct Uncopyable {
    Uncopyable(int i) {
      (void*)&i;
    }
    Uncopyable(const Uncopyable& that) = delete;
  };
  ConcurrentHashMap<uint64_t, Uncopyable> foomap(2);
  EXPECT_TRUE(foomap.try_emplace(1, 1).second);
  EXPECT_TRUE(foomap.try_emplace(2, 2).second);
  auto res = foomap.find(2);
  EXPECT_NE(res, foomap.cend());

  EXPECT_TRUE(foomap.try_emplace(3, 3).second);

  auto res2 = foomap.find(2);
  EXPECT_NE(res2, foomap.cend());
  EXPECT_EQ(&(res->second), &(res2->second));
}

TEST(ConcurrentHashMap, MapUpdateTest) {
  ConcurrentHashMap<uint64_t, uint64_t> foomap(2);
  EXPECT_TRUE(foomap.insert(1, 10).second);
  EXPECT_TRUE(bool(foomap.assign(1, 11)));
  auto res = foomap.find(1);
  EXPECT_NE(res, foomap.cend());
  EXPECT_EQ(11, res->second);
}

TEST(ConcurrentHashMap, MapIterateTest2) {
  ConcurrentHashMap<uint64_t, uint64_t> foomap(2);
  auto begin = foomap.cbegin();
  auto end = foomap.cend();
  EXPECT_EQ(begin, end);
}

TEST(ConcurrentHashMap, MapIterateTest) {
  ConcurrentHashMap<uint64_t, uint64_t> foomap(2);
  EXPECT_EQ(foomap.cbegin(), foomap.cend());
  EXPECT_TRUE(foomap.insert(1, 1).second);
  EXPECT_TRUE(foomap.insert(2, 2).second);
  auto iter = foomap.cbegin();
  EXPECT_NE(iter, foomap.cend());
  EXPECT_EQ(iter->first, 1);
  EXPECT_EQ(iter->second, 1);
  iter++;
  EXPECT_NE(iter, foomap.cend());
  EXPECT_EQ(iter->first, 2);
  EXPECT_EQ(iter->second, 2);
  iter++;
  EXPECT_EQ(iter, foomap.cend());

  int count = 0;
  for (auto it = foomap.cbegin(); it != foomap.cend(); it++) {
    count++;
  }
  EXPECT_EQ(count, 2);
}

// TODO: hazptrs must support DeterministicSchedule

#define Atom std::atomic // DeterministicAtomic
#define Mutex std::mutex // DeterministicMutex
#define lib std // DeterministicSchedule
#define join t.join() // DeterministicSchedule::join(t)
// #define Atom DeterministicAtomic
// #define Mutex DeterministicMutex
// #define lib DeterministicSchedule
// #define join DeterministicSchedule::join(t)

TEST(ConcurrentHashMap, UpdateStressTest) {
  DeterministicSchedule sched(DeterministicSchedule::uniform(FLAGS_seed));

  // size must match iters for this test.
  unsigned size = 128 * 128;
  unsigned iters = size;
  ConcurrentHashMap<
      unsigned long,
      unsigned long,
      std::hash<unsigned long>,
      std::equal_to<unsigned long>,
      std::allocator<uint8_t>,
      8,
      Atom,
      Mutex>
      m(2);

  for (uint i = 0; i < size; i++) {
    m.insert(i, i);
  }
  std::vector<std::thread> threads;
  unsigned int num_threads = 32;
  for (uint t = 0; t < num_threads; t++) {
    threads.push_back(lib::thread([&, t]() {
      int offset = (iters * t / num_threads);
      for (uint i = 0; i < iters / num_threads; i++) {
        unsigned long k = folly::hash::jenkins_rev_mix32((i + offset));
        k = k % (iters / num_threads) + offset;
        unsigned long val = 3;
        auto res = m.find(k);
        EXPECT_NE(res, m.cend());
        EXPECT_EQ(k, res->second);
        auto r = m.assign(k, res->second);
        EXPECT_TRUE(r);
        res = m.find(k);
        EXPECT_NE(res, m.cend());
        EXPECT_EQ(k, res->second);
        // Another random insertion to force table resizes
        val = size + i + offset;
        EXPECT_TRUE(m.insert(val, val).second);
      }
    }));
  }
  for (auto& t : threads) {
    join;
  }
}

TEST(ConcurrentHashMap, EraseStressTest) {
  DeterministicSchedule sched(DeterministicSchedule::uniform(FLAGS_seed));

  unsigned size = 2;
  unsigned iters = size * 128 * 2;
  ConcurrentHashMap<
      unsigned long,
      unsigned long,
      std::hash<unsigned long>,
      std::equal_to<unsigned long>,
      std::allocator<uint8_t>,
      8,
      Atom,
      Mutex>
      m(2);

  for (uint i = 0; i < size; i++) {
    unsigned long k = folly::hash::jenkins_rev_mix32(i);
    m.insert(k, k);
  }
  std::vector<std::thread> threads;
  unsigned int num_threads = 32;
  for (uint t = 0; t < num_threads; t++) {
    threads.push_back(lib::thread([&, t]() {
      int offset = (iters * t / num_threads);
      for (uint i = 0; i < iters / num_threads; i++) {
        unsigned long k = folly::hash::jenkins_rev_mix32((i + offset));
        unsigned long val;
        auto res = m.insert(k, k).second;
        if (res) {
          res = m.erase(k);
          if (!res) {
            printf("Faulre to erase thread %i val %li\n", t, k);
            exit(0);
          }
          EXPECT_TRUE(res);
        }
        res = m.insert(k, k).second;
        if (res) {
          res = bool(m.assign(k, k));
          if (!res) {
            printf("Thread %i update fail %li res%i\n", t, k, res);
            exit(0);
          }
          EXPECT_TRUE(res);
          auto res = m.find(k);
          if (res == m.cend()) {
            printf("Thread %i lookup fail %li\n", t, k);
            exit(0);
          }
          EXPECT_EQ(k, res->second);
        }
      }
    }));
  }
  for (auto& t : threads) {
    join;
  }
}

TEST(ConcurrentHashMap, IterateStressTest) {
  DeterministicSchedule sched(DeterministicSchedule::uniform(FLAGS_seed));

  unsigned size = 2;
  unsigned iters = size * 128 * 2;
  ConcurrentHashMap<
      unsigned long,
      unsigned long,
      std::hash<unsigned long>,
      std::equal_to<unsigned long>,
      std::allocator<uint8_t>,
      8,
      Atom,
      Mutex>
      m(2);

  for (uint i = 0; i < size; i++) {
    unsigned long k = folly::hash::jenkins_rev_mix32(i);
    m.insert(k, k);
  }
  for (uint i = 0; i < 10; i++) {
    m.insert(i, i);
  }
  std::vector<std::thread> threads;
  unsigned int num_threads = 32;
  for (uint t = 0; t < num_threads; t++) {
    threads.push_back(lib::thread([&, t]() {
      int offset = (iters * t / num_threads);
      for (uint i = 0; i < iters / num_threads; i++) {
        unsigned long k = folly::hash::jenkins_rev_mix32((i + offset));
        unsigned long val;
        auto res = m.insert(k, k).second;
        if (res) {
          res = m.erase(k);
          if (!res) {
            printf("Faulre to erase thread %i val %li\n", t, k);
            exit(0);
          }
          EXPECT_TRUE(res);
        }
        int count = 0;
        for (auto it = m.cbegin(); it != m.cend(); it++) {
          printf("Item is %li\n", it->first);
          if (it->first < 10) {
            count++;
          }
        }
        EXPECT_EQ(count, 10);
      }
    }));
  }
  for (auto& t : threads) {
    join;
  }
}

TEST(ConcurrentHashMap, insertStressTest) {
  DeterministicSchedule sched(DeterministicSchedule::uniform(FLAGS_seed));

  unsigned size = 2;
  unsigned iters = size * 64 * 4;
  ConcurrentHashMap<
      unsigned long,
      unsigned long,
      std::hash<unsigned long>,
      std::equal_to<unsigned long>,
      std::allocator<uint8_t>,
      8,
      Atom,
      Mutex>
      m(2);

  EXPECT_TRUE(m.insert(0, 0).second);
  EXPECT_FALSE(m.insert(0, 0).second);
  std::vector<std::thread> threads;
  unsigned int num_threads = 32;
  for (uint t = 0; t < num_threads; t++) {
    threads.push_back(lib::thread([&, t]() {
      int offset = (iters * t / num_threads);
      for (uint i = 0; i < iters / num_threads; i++) {
        auto var = offset + i + 1;
        EXPECT_TRUE(m.insert(var, var).second);
        EXPECT_FALSE(m.insert(0, 0).second);
      }
    }));
  }
  for (auto& t : threads) {
    join;
  }
}

TEST(ConcurrentHashMap, assignStressTest) {
  DeterministicSchedule sched(DeterministicSchedule::uniform(FLAGS_seed));

  unsigned size = 2;
  unsigned iters = size * 64 * 4;
  struct big_value {
    uint64_t v1;
    uint64_t v2;
    uint64_t v3;
    uint64_t v4;
    uint64_t v5;
    uint64_t v6;
    uint64_t v7;
    uint64_t v8;
    void set(uint64_t v) {
      v1 = v2 = v3 = v4 = v5 = v6 = v7 = v8 = v;
    }
    void check() const {
      auto v = v1;
      EXPECT_EQ(v, v8);
      EXPECT_EQ(v, v7);
      EXPECT_EQ(v, v6);
      EXPECT_EQ(v, v5);
      EXPECT_EQ(v, v4);
      EXPECT_EQ(v, v3);
      EXPECT_EQ(v, v2);
    }
  };
  ConcurrentHashMap<
      unsigned long,
      big_value,
      std::hash<unsigned long>,
      std::equal_to<unsigned long>,
      std::allocator<uint8_t>,
      8,
      Atom,
      Mutex>
      m(2);

  for (uint i = 0; i < iters; i++) {
    big_value a;
    a.set(i);
    m.insert(i, a);
  }

  std::vector<std::thread> threads;
  unsigned int num_threads = 32;
  for (uint t = 0; t < num_threads; t++) {
    threads.push_back(lib::thread([&]() {
      for (uint i = 0; i < iters; i++) {
        auto res = m.find(i);
        EXPECT_NE(res, m.cend());
        res->second.check();
        big_value b;
        b.set(res->second.v1 + 1);
        m.assign(i, b);
      }
    }));
  }
  for (auto& t : threads) {
    join;
  }
}
