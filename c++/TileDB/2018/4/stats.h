/**
 * @file   stats.h
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2018 TileDB, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * @section DESCRIPTION
 *
 * This file contains declarations of statistics-related code.
 */

#ifndef TILEDB_STATS_H
#define TILEDB_STATS_H

#include <inttypes.h>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace tiledb {
namespace sm {
namespace stats {

/**
 * Class that defines stats counters and methods to manipulate them.
 */
class Statistics {
 public:
  /* Define the counters */
#define STATS_DEFINE_FUNC_STAT(function_name)     \
  std::atomic<uint64_t> function_name##_total_ns; \
  std::atomic<uint64_t> function_name##_call_count;
#include "tiledb/sm/misc/stats_counters.h"
#undef STATS_DEFINE_FUNC_STAT

#define STATS_DEFINE_COUNTER_STAT(counter_name) \
  std::atomic<uint64_t> counter_##counter_name;
#include "tiledb/sm/misc/stats_counters.h"
#undef STATS_DEFINE_COUNTER_STAT

  /** Constructor. */
  Statistics();

  /** Returns true if statistics are currently enabled. */
  bool enabled() const;

  /** Reset all counters to zero. */
  void reset() {
#define STATS_INIT_FUNC_STAT(function_name) \
  function_name##_total_ns = 0;             \
  function_name##_call_count = 0;
#include "tiledb/sm/misc/stats_counters.h"
#undef STATS_INIT_FUNC_STAT

#define STATS_INIT_COUNTER_STAT(counter_name) counter_##counter_name = 0;
#include "tiledb/sm/misc/stats_counters.h"
#undef STATS_INIT_COUNTER_STAT
  }

  /** Dump the current counter values to the given file. */
  void dump(FILE* out) const;

  /** Enable or disable statistics gathering. */
  void set_enabled(bool enabled);

 private:
  /** True if stats are being gathered. */
  bool enabled_;

  /** Dump all function stats to the output. */
  void dump_all_func_stats(FILE* out) const {
#define STATS_REPORT_FUNC_STAT(function_name) \
  fprintf(                                    \
      out,                                    \
      "%-30s%20" PRIu64 ",%20" PRIu64 "\n",   \
      "  " #function_name ",",                \
      (uint64_t)function_name##_call_count,   \
      (uint64_t)function_name##_total_ns);
#include "tiledb/sm/misc/stats_counters.h"
#undef STATS_REPORT_FUNC_STAT
  }

  /** Dump all counter stats to the output. */
  void dump_all_counter_stats(FILE* out) const {
#define STATS_REPORT_COUNTER_STAT(counter_name) \
  fprintf(                                      \
      out,                                      \
      "%-30s%20" PRIu64 "\n",                   \
      "  " #counter_name ",",                   \
      (uint64_t)counter_##counter_name);
#include "tiledb/sm/misc/stats_counters.h"
#undef STATS_REPORT_COUNTER_STAT
  }
};

/**
 * The singleton instance holding all global stats counters. The report will
 * be automatically made when this object is destroyed (at program termination).
 */
extern Statistics all_stats;

/** Marks the beginning of a stats-enabled function. This should come before the
 * first statement where you want the function timer to start. */
#define STATS_FUNC_IN(f)                                 \
  auto __stats_start = std::chrono::steady_clock::now(); \
  auto __stats_##f##_retval = [&]() {
/** Marks the end of a stats-enabled function. This should come after the last
 * statement in the function. Note that a function can have multiple exit paths
 * (i.e. multiple returns), but you should still put this macro after the very
 * last statement in the function. */
#define STATS_FUNC_OUT(f)                                     \
  }                                                           \
  ();                                                         \
  if (stats::all_stats.enabled()) {                           \
    auto __stats_end = std::chrono::steady_clock::now();      \
    uint64_t __stats_dur_ns =                                 \
        std::chrono::duration_cast<std::chrono::nanoseconds>( \
            __stats_end - __stats_start)                      \
            .count();                                         \
    stats::all_stats.f##_total_ns += __stats_dur_ns;          \
    stats::all_stats.f##_call_count++;                        \
  }                                                           \
  return __stats_##f##_retval;

/** Marks the beginning of a stats-enabled void function. This should come
 * before the first statement where you want the function timer to start. */
#define STATS_FUNC_VOID_IN(f)                                  \
  auto __stats_##f##_start = std::chrono::steady_clock::now(); \
  [&]() {
/** Marks the end of a stats-enabled void function. This should come after the
 * last statement in the function. */
#define STATS_FUNC_VOID_OUT(f)                                 \
  }                                                            \
  ();                                                          \
  if (stats::all_stats.enabled()) {                            \
    auto __stats_##f##_end = std::chrono::steady_clock::now(); \
    uint64_t __stats_dur_ns =                                  \
        std::chrono::duration_cast<std::chrono::nanoseconds>(  \
            __stats_##f##_end - __stats_##f##_start)           \
            .count();                                          \
    stats::all_stats.f##_total_ns += __stats_dur_ns;           \
    stats::all_stats.f##_call_count++;                         \
  }
/** Adds a value to a counter stat. */
#define STATS_COUNTER_ADD(counter_name, value)          \
  if (stats::all_stats.enabled()) {                     \
    stats::all_stats.counter_##counter_name += (value); \
  }

}  // namespace stats
}  // namespace sm
}  // namespace tiledb

#endif  // TILEDB_STATS_H
