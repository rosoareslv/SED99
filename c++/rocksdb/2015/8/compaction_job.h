//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#pragma once

#include <atomic>
#include <deque>
#include <limits>
#include <set>
#include <utility>
#include <vector>
#include <string>
#include <functional>

#include "db/dbformat.h"
#include "db/log_writer.h"
#include "db/column_family.h"
#include "db/version_edit.h"
#include "db/memtable_list.h"
#include "port/port.h"
#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include "rocksdb/memtablerep.h"
#include "rocksdb/compaction_filter.h"
#include "rocksdb/compaction_job_stats.h"
#include "rocksdb/transaction_log.h"
#include "util/autovector.h"
#include "util/event_logger.h"
#include "util/stop_watch.h"
#include "util/thread_local.h"
#include "util/scoped_arena_iterator.h"
#include "db/internal_stats.h"
#include "db/write_controller.h"
#include "db/flush_scheduler.h"
#include "db/write_thread.h"
#include "db/job_context.h"

namespace rocksdb {

class MemTable;
class TableCache;
class Version;
class VersionEdit;
class VersionSet;
class Arena;

class CompactionJob {
 public:
  CompactionJob(int job_id, Compaction* compaction, const DBOptions& db_options,
                const EnvOptions& env_options, VersionSet* versions,
                std::atomic<bool>* shutting_down, LogBuffer* log_buffer,
                Directory* db_directory, Directory* output_directory,
                Statistics* stats,
                std::vector<SequenceNumber> existing_snapshots,
                std::shared_ptr<Cache> table_cache, EventLogger* event_logger,
                bool paranoid_file_checks, bool measure_io_stats,
                const std::string& dbname,
                CompactionJobStats* compaction_job_stats);

  ~CompactionJob();

  // no copy/move
  CompactionJob(CompactionJob&& job) = delete;
  CompactionJob(const CompactionJob& job) = delete;
  CompactionJob& operator=(const CompactionJob& job) = delete;

  // REQUIRED: mutex held
  void Prepare();
  // REQUIRED mutex not held
  Status Run();

  // REQUIRED: mutex held
  Status Install(const MutableCFOptions& mutable_cf_options,
                 InstrumentedMutex* db_mutex);

 private:
  struct SubCompactionState;

  void AggregateStatistics();
  // Set up the individual states used by each subcompaction
  void InitializeSubCompactions();

  // update the thread status for starting a compaction.
  void ReportStartedCompaction(Compaction* compaction);
  void AllocateCompactionOutputFileNumbers();
  // Call compaction filter. Then iterate through input and compact the
  // kv-pairs
  void ProcessKeyValueCompaction(SubCompactionState* sub_compact);

  Status WriteKeyValue(const Slice& key, const Slice& value,
                       const ParsedInternalKey& ikey,
                       const Status& input_status,
                       SubCompactionState* sub_compact);

  Status FinishCompactionOutputFile(const Status& input_status,
                                    SubCompactionState* sub_compact);
  Status InstallCompactionResults(const MutableCFOptions& mutable_cf_options,
                                  InstrumentedMutex* db_mutex);
  SequenceNumber findEarliestVisibleSnapshot(SequenceNumber in,
                                             SequenceNumber* prev_snapshot);
  void RecordCompactionIOStats();
  Status OpenCompactionOutputFile(SubCompactionState* sub_compact);
  void CleanupCompaction();
  void UpdateCompactionJobStats(
    const InternalStats::CompactionStats& stats) const;
  void RecordDroppedKeys(int64_t* key_drop_user,
                         int64_t* key_drop_newer_entry,
                         int64_t* key_drop_obsolete,
                         CompactionJobStats* compaction_job_stats = nullptr);

  void UpdateCompactionStats();
  void UpdateCompactionInputStatsHelper(
      int* num_files, uint64_t* bytes_read, int input_level);

  void LogCompaction();

  int job_id_;

  // CompactionJob state
  struct CompactionState;
  CompactionState* compact_;
  CompactionJobStats* compaction_job_stats_;

  bool bottommost_level_;

  InternalStats::CompactionStats compaction_stats_;

  SequenceNumber earliest_snapshot_;
  SequenceNumber latest_snapshot_;
  SequenceNumber visible_at_tip_;

  // DBImpl state
  const std::string& dbname_;
  const DBOptions& db_options_;
  const EnvOptions& env_options_;
  Env* env_;
  VersionSet* versions_;
  std::atomic<bool>* shutting_down_;
  LogBuffer* log_buffer_;
  Directory* db_directory_;
  Directory* output_directory_;
  Statistics* stats_;
  // If there were two snapshots with seq numbers s1 and
  // s2 and s1 < s2, and if we find two instances of a key k1 then lies
  // entirely within s1 and s2, then the earlier version of k1 can be safely
  // deleted because that version is not visible in any snapshot.
  std::vector<SequenceNumber> existing_snapshots_;
  std::shared_ptr<Cache> table_cache_;

  EventLogger* event_logger_;

  bool paranoid_file_checks_;
  bool measure_io_stats_;
  std::vector<Slice> sub_compaction_boundaries_;
};

}  // namespace rocksdb
