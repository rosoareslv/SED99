/**
 *    Copyright (C) 2019-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/db/exec/sort_executor.h"
#include "mongo/db/pipeline/value_comparator.h"

namespace mongo {
namespace {
/**
 * Generates a new file name on each call using a static, atomic and monotonically increasing
 * number.
 *
 * Each user of the Sorter must implement this function to ensure that all temporary files that the
 * Sorter instances produce are uniquely identified using a unique file name extension with separate
 * atomic variable. This is necessary because the sorter.cpp code is separately included in multiple
 * places, rather than compiled in one place and linked, and so cannot provide a globally unique ID.
 */
std::string nextFileName() {
    static AtomicWord<unsigned> sortExecutorFileCounter;
    return "extsort-sort-executor." + std::to_string(sortExecutorFileCounter.fetchAndAdd(1));
}
}  // namespace

SortExecutor::SortExecutor(SortPattern sortPattern,
                           uint64_t limit,
                           uint64_t maxMemoryUsageBytes,
                           std::string tempDir,
                           bool allowDiskUse)
    : _sortPattern(std::move(sortPattern)),
      _limit(limit),
      _maxMemoryUsageBytes(maxMemoryUsageBytes),
      _tempDir(std::move(tempDir)),
      _diskUseAllowed(allowDiskUse) {}

int SortExecutor::Comparator::operator()(const DocumentSorter::Data& lhs,
                                         const DocumentSorter::Data& rhs) const {
    Value lhsKey = lhs.first;
    Value rhsKey = rhs.first;
    // DocumentSourceSort::populate() has already guaranteed that the sort key is non-empty.
    // However, the tricky part is deciding what to do if none of the sort keys are present. In that
    // case, consider the document "less".
    //
    // Note that 'comparator' must use binary comparisons here, as both 'lhs' and 'rhs' are
    // collation comparison keys.
    ValueComparator comparator;
    const size_t n = _sort.size();
    if (n == 1) {  // simple fast case
        if (_sort[0].isAscending)
            return comparator.compare(lhsKey, rhsKey);
        else
            return -comparator.compare(lhsKey, rhsKey);
    }

    // compound sort
    for (size_t i = 0; i < n; i++) {
        int cmp = comparator.compare(lhsKey[i], rhsKey[i]);
        if (cmp) {
            /* if necessary, adjust the return value by the key ordering */
            if (!_sort[i].isAscending)
                cmp = -cmp;

            return cmp;
        }
    }

    /*
      If we got here, everything matched (or didn't exist), so we'll
      consider the documents equal for purposes of this sort.
    */
    return 0;
}

boost::optional<Document> SortExecutor::getNext() {
    if (_isEOF) {
        return boost::none;
    }

    if (!_output->more()) {
        _output.reset();
        _isEOF = true;
        return boost::none;
    }

    return _output->next().second;
}

void SortExecutor::add(Value sortKey, Document data) {
    if (!_sorter) {
        _sorter.reset(DocumentSorter::make(makeSortOptions(), Comparator(_sortPattern)));
    }
    _sorter->add(std::move(sortKey), std::move(data));
}

void SortExecutor::loadingDone() {
    // This conditional should only pass if no documents were added to the sorter.
    if (!_sorter) {
        _sorter.reset(DocumentSorter::make(makeSortOptions(), Comparator(_sortPattern)));
    }
    _output.reset(_sorter->done());
    _wasDiskUsed = _wasDiskUsed || _sorter->usedDisk();
    _sorter.reset();
}

SortOptions SortExecutor::makeSortOptions() const {
    SortOptions opts;
    if (_limit) {
        opts.limit = _limit;
    }

    opts.maxMemoryUsageBytes = _maxMemoryUsageBytes;
    if (_diskUseAllowed) {
        opts.extSortAllowed = true;
        opts.tempDir = _tempDir;
    }

    return opts;
}
}  // namespace mongo

#include "mongo/db/sorter/sorter.cpp"
// Explicit instantiation unneeded since we aren't exposing Sorter outside of this file.
