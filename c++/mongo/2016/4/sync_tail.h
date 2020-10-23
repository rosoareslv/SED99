/**
 *    Copyright (C) 2008 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <deque>

#include "mongo/base/status.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/db/repl/multiapplier.h"
#include "mongo/db/repl/oplog_entry.h"
#include "mongo/db/storage/mmap_v1/dur.h"
#include "mongo/stdx/functional.h"
#include "mongo/util/concurrency/old_thread_pool.h"

namespace mongo {

class Database;
class OperationContext;

namespace repl {
class BackgroundSyncInterface;
class ReplicationCoordinator;
class OpTime;

/**
 * "Normal" replica set syncing
 */
class SyncTail {
public:
    using MultiSyncApplyFunc = stdx::function<void(const std::vector<OplogEntry>& ops)>;

    /**
     * Type of function to increment "repl.apply.ops" server status metric.
     */
    using IncrementOpsAppliedStatsFn = stdx::function<void()>;

    /**
     * Type of function that takes a non-command op and applies it locally.
     * Used for applying from an oplog.
     * 'db' is the database where the op will be applied.
     * 'opObj' is a BSONObj describing the op to be applied.
     * 'convertUpdateToUpsert' indicates to convert some updates to upserts for idempotency reasons.
     * 'opCounter' is used to update server status metrics.
     * Returns failure status if the op was an update that could not be applied.
     */
    using ApplyOperationInLockFn = stdx::function<Status(OperationContext* txn,
                                                         Database* db,
                                                         const BSONObj& opObj,
                                                         bool convertUpdateToUpsert,
                                                         IncrementOpsAppliedStatsFn opCounter)>;

    /**
     * Type of function that takes a command op and applies it locally.
     * Used for applying from an oplog.
     * Returns failure status if the op that could not be applied.
     */
    using ApplyCommandInLockFn = stdx::function<Status(OperationContext*, const BSONObj&)>;

    SyncTail(BackgroundSyncInterface* q, MultiSyncApplyFunc func);
    virtual ~SyncTail();

    /**
     * Applies the operation that is in param o.
     * Functions for applying operations/commands and increment server status counters may
     * be overridden for testing.
     */
    static Status syncApply(OperationContext* txn,
                            const BSONObj& o,
                            bool convertUpdateToUpsert,
                            ApplyOperationInLockFn applyOperationInLock,
                            ApplyCommandInLockFn applyCommandInLock,
                            IncrementOpsAppliedStatsFn incrementOpsAppliedStats);

    static Status syncApply(OperationContext* txn, const BSONObj& o, bool convertUpdateToUpsert);

    void oplogApplication();
    bool peek(BSONObj* obj);

    class OpQueue {
    public:
        OpQueue() : _size(0) {}
        size_t getSize() const {
            return _size;
        }
        const std::deque<OplogEntry>& getDeque() const {
            return _deque;
        }
        void push_back(OplogEntry&& op) {
            _size += op.raw.objsize();
            _deque.push_back(std::move(op));
        }
        bool empty() const {
            return _deque.empty();
        }

        const OplogEntry& back() const {
            invariant(!_deque.empty());
            return _deque.back();
        }

    private:
        std::deque<OplogEntry> _deque;
        size_t _size;
    };

    // returns true if we should continue waiting for BSONObjs, false if we should
    // stop waiting and apply the queue we have.  Only returns false if !ops.empty().
    bool tryPopAndWaitForMore(OperationContext* txn, OpQueue* ops);

    /**
     * Fetch a single document referenced in the operation from the sync source.
     */
    virtual BSONObj getMissingDoc(OperationContext* txn, Database* db, const BSONObj& o);

    /**
     * If applyOperation_inlock should be called again after an update fails.
     */
    virtual bool shouldRetry(OperationContext* txn, const BSONObj& o);
    void setHostname(const std::string& hostname);

    /**
     * This variable determines the number of writer threads SyncTail will have. It has a default
     * value, which varies based on architecture and can be overridden using the
     * "replWriterThreadCount" server parameter.
     */
    static int replWriterThreadCount;

protected:
    // Cap the batches using the limit on journal commits.
    // This works out to be 100 MB (64 bit) or 50 MB (32 bit)
    static const unsigned int replBatchLimitBytes = dur::UncommittedBytesLimit;
    static const int replBatchLimitSeconds = 1;
    static const unsigned int replBatchLimitOperations = 5000;

    // Apply a batch of operations, using multiple threads.
    // Returns the last OpTime applied during the apply batch, ops.end["ts"] basically.
    OpTime multiApply(OperationContext* txn, const OpQueue& ops);

private:
    class OpQueueBatcher;

    std::string _hostname;

    BackgroundSyncInterface* _networkQueue;

    // Function to use during applyOps
    MultiSyncApplyFunc _applyFunc;

    // persistent pool of worker threads for writing ops to the databases
    OldThreadPool _writerPool;
    // persistent pool of worker threads for prefetching
    OldThreadPool _prefetcherPool;
};

/**
 * Applies the opeartions described in the oplog entries contained in "ops" using the
 * "applyOperation" function.
 *
 * Returns ErrorCode::InterruptedAtShutdown if the node enters shutdown while applying ops,
 * ErrorCodes::CannotApplyOplogWhilePrimary if the node has become primary, and the OpTime of the
 * final operation applied otherwise.
 *
 * Shared between here and MultiApplier.
 */
StatusWith<OpTime> multiApply(OperationContext* txn,
                              const MultiApplier::Operations& ops,
                              MultiApplier::ApplyOperationFn applyOperation);

// These free functions are used by the thread pool workers to write ops to the db.
void multiSyncApply(const std::vector<OplogEntry>& ops);
void multiInitialSyncApply(const std::vector<OplogEntry>& ops);

}  // namespace repl
}  // namespace mongo
