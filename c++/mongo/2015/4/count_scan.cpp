/**
 *    Copyright (C) 2014 MongoDB Inc.
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

#include "mongo/db/exec/count_scan.h"

#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/exec/scoped_timer.h"
#include "mongo/db/index/index_descriptor.h"

namespace mongo {

    using std::auto_ptr;
    using std::vector;

    // static
    const char* CountScan::kStageType = "COUNT_SCAN";

    CountScan::CountScan(OperationContext* txn,
                         const CountScanParams& params,
                         WorkingSet* workingSet)
        : _txn(txn),
          _workingSet(workingSet),
          _descriptor(params.descriptor),
          _iam(params.descriptor->getIndexCatalog()->getIndex(params.descriptor)),
          _shouldDedup(params.descriptor->isMultikey(txn)),
          _params(params),
          _commonStats(kStageType) {
        _specificStats.keyPattern = _params.descriptor->keyPattern();
        _specificStats.indexName = _params.descriptor->indexName();
        _specificStats.isMultiKey = _params.descriptor->isMultikey(txn);
        _specificStats.indexVersion = _params.descriptor->version();

        // endKey must be after startKey in index order since we only do forward scans.
        dassert(_params.startKey.woCompare(_params.endKey,
                                           Ordering::make(params.descriptor->keyPattern()),
                                           /*compareFieldNames*/false) <= 0);
    }


    PlanStage::StageState CountScan::work(WorkingSetID* out) {
        ++_commonStats.works;
        if (_commonStats.isEOF) return PlanStage::IS_EOF;

        // Adds the amount of time taken by work() to executionTimeMillis.
        ScopedTimer timer(&_commonStats.executionTimeMillis);

        boost::optional<IndexKeyEntry> entry;
        const bool needInit = !_cursor;
        try {
            // We don't care about the keys.
            const auto kWantLoc = SortedDataInterface::Cursor::kWantLoc;

            if (needInit) {
                // First call to work().  Perform cursor init.
                _cursor = _iam->newCursor(_txn);
                _cursor->setEndPosition(_params.endKey, _params.endKeyInclusive);

                entry = _cursor->seek(_params.startKey, _params.startKeyInclusive, kWantLoc);
            }
            else {
                entry = _cursor->next(kWantLoc);
            }
        }
        catch (const WriteConflictException& wce) {
            if (needInit) {
                // Release our cursor and try again next time.
                _cursor.reset();
            }
            *out = WorkingSet::INVALID_ID;
            return PlanStage::NEED_YIELD;
        }

        ++_specificStats.keysExamined;

        if (!entry) {
            _commonStats.isEOF = true;
            _cursor.reset();
            return PlanStage::IS_EOF;
        }

        if (_shouldDedup && !_returned.insert(entry->loc).second) {
            // *loc was already in _returned.
            ++_commonStats.needTime;
            return PlanStage::NEED_TIME;
        }

        *out = WorkingSet::INVALID_ID;
        ++_commonStats.advanced;
        return PlanStage::ADVANCED;
    }

    bool CountScan::isEOF() {
        return _commonStats.isEOF;
    }

    void CountScan::saveState() {
        _txn = NULL;
        ++_commonStats.yields;
        if (_cursor) _cursor->savePositioned();
    }

    void CountScan::restoreState(OperationContext* opCtx) {
        invariant(_txn == NULL);
        _txn = opCtx;
        ++_commonStats.unyields;
        
        if (_cursor) _cursor->restore(opCtx);

        // This can change during yielding.
        // TODO this isn't sufficient. See SERVER-17678.
        _shouldDedup = _descriptor->isMultikey(_txn);
    }

    void CountScan::invalidate(OperationContext* txn, const RecordId& dl, InvalidationType type) {
        ++_commonStats.invalidates;

        // The only state we're responsible for holding is what RecordIds to drop.  If a document
        // mutates the underlying index cursor will deal with it.
        if (INVALIDATION_MUTATION == type) {
            return;
        }

        // If we see this RecordId again, it may not be the same document it was before, so we want
        // to return it if we see it again.
        unordered_set<RecordId, RecordId::Hasher>::iterator it = _returned.find(dl);
        if (it != _returned.end()) {
            _returned.erase(it);
        }
    }

    vector<PlanStage*> CountScan::getChildren() const {
        vector<PlanStage*> empty;
        return empty;
    }

    PlanStageStats* CountScan::getStats() {
        auto_ptr<PlanStageStats> ret(new PlanStageStats(_commonStats, STAGE_COUNT_SCAN));

        CountScanStats* countStats = new CountScanStats(_specificStats);
        countStats->keyPattern = _specificStats.keyPattern.getOwned();
        ret->specific.reset(countStats);

        return ret.release();
    }

    const CommonStats* CountScan::getCommonStats() const {
        return &_commonStats;
    }

    const SpecificStats* CountScan::getSpecificStats() const {
        return &_specificStats;
    }

}  // namespace mongo
