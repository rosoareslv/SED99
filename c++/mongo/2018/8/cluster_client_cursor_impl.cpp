/**
 *    Copyright (C) 2015 MongoDB Inc.
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

#include "mongo/platform/basic.h"

#include "mongo/s/query/cluster_client_cursor_impl.h"

#include "mongo/s/query/router_stage_limit.h"
#include "mongo/s/query/router_stage_merge.h"
#include "mongo/s/query/router_stage_remove_metadata_fields.h"
#include "mongo/s/query/router_stage_skip.h"
#include "mongo/stdx/memory.h"

namespace mongo {

ClusterClientCursorGuard::ClusterClientCursorGuard(OperationContext* opCtx,
                                                   std::unique_ptr<ClusterClientCursor> ccc)
    : _opCtx(opCtx), _ccc(std::move(ccc)) {}

ClusterClientCursorGuard::~ClusterClientCursorGuard() {
    if (_ccc && !_ccc->remotesExhausted()) {
        _ccc->kill(_opCtx);
    }
}

ClusterClientCursor* ClusterClientCursorGuard::operator->() {
    return _ccc.get();
}

std::unique_ptr<ClusterClientCursor> ClusterClientCursorGuard::releaseCursor() {
    return std::move(_ccc);
}

ClusterClientCursorGuard ClusterClientCursorImpl::make(OperationContext* opCtx,
                                                       executor::TaskExecutor* executor,
                                                       ClusterClientCursorParams&& params) {
    std::unique_ptr<ClusterClientCursor> cursor(new ClusterClientCursorImpl(
        opCtx, executor, std::move(params), opCtx->getLogicalSessionId()));
    return ClusterClientCursorGuard(opCtx, std::move(cursor));
}

ClusterClientCursorGuard ClusterClientCursorImpl::make(OperationContext* opCtx,
                                                       std::unique_ptr<RouterExecStage> root,
                                                       ClusterClientCursorParams&& params) {
    std::unique_ptr<ClusterClientCursor> cursor(new ClusterClientCursorImpl(
        opCtx, std::move(root), std::move(params), opCtx->getLogicalSessionId()));
    return ClusterClientCursorGuard(opCtx, std::move(cursor));
}

ClusterClientCursorImpl::ClusterClientCursorImpl(OperationContext* opCtx,
                                                 executor::TaskExecutor* executor,
                                                 ClusterClientCursorParams&& params,
                                                 boost::optional<LogicalSessionId> lsid)
    : _params(std::move(params)),
      _root(buildMergerPlan(opCtx, executor, &_params)),
      _lsid(lsid),
      _opCtx(opCtx) {
    dassert(!_params.compareWholeSortKey ||
            SimpleBSONObjComparator::kInstance.evaluate(
                _params.sort == AsyncResultsMerger::kWholeSortKeySortPattern));
}

ClusterClientCursorImpl::ClusterClientCursorImpl(OperationContext* opCtx,
                                                 std::unique_ptr<RouterExecStage> root,
                                                 ClusterClientCursorParams&& params,
                                                 boost::optional<LogicalSessionId> lsid)
    : _params(std::move(params)), _root(std::move(root)), _lsid(lsid), _opCtx(opCtx) {
    dassert(!_params.compareWholeSortKey ||
            SimpleBSONObjComparator::kInstance.evaluate(
                _params.sort == AsyncResultsMerger::kWholeSortKeySortPattern));
}

StatusWith<ClusterQueryResult> ClusterClientCursorImpl::next(
    RouterExecStage::ExecContext execContext) {

    invariant(_opCtx);
    const auto interruptStatus = _opCtx->checkForInterruptNoAssert();
    if (!interruptStatus.isOK()) {
        return interruptStatus;
    }

    // First return stashed results, if there are any.
    if (!_stash.empty()) {
        auto front = std::move(_stash.front());
        _stash.pop();
        ++_numReturnedSoFar;
        return {front};
    }

    auto next = _root->next(execContext);
    if (next.isOK() && !next.getValue().isEOF()) {
        ++_numReturnedSoFar;
    }
    return next;
}

void ClusterClientCursorImpl::kill(OperationContext* opCtx) {
    _root->kill(opCtx);
}

void ClusterClientCursorImpl::reattachToOperationContext(OperationContext* opCtx) {
    _opCtx = opCtx;
    _root->reattachToOperationContext(opCtx);
}

void ClusterClientCursorImpl::detachFromOperationContext() {
    _opCtx = nullptr;
    _root->detachFromOperationContext();
}

OperationContext* ClusterClientCursorImpl::getCurrentOperationContext() const {
    return _opCtx;
}

bool ClusterClientCursorImpl::isTailable() const {
    return _params.tailableMode != TailableModeEnum::kNormal;
}

bool ClusterClientCursorImpl::isTailableAndAwaitData() const {
    return _params.tailableMode == TailableModeEnum::kTailableAndAwaitData;
}

BSONObj ClusterClientCursorImpl::getOriginatingCommand() const {
    return _params.originatingCommandObj;
}

std::size_t ClusterClientCursorImpl::getNumRemotes() const {
    return _root->getNumRemotes();
}

long long ClusterClientCursorImpl::getNumReturnedSoFar() const {
    return _numReturnedSoFar;
}

void ClusterClientCursorImpl::queueResult(const ClusterQueryResult& result) {
    auto resultObj = result.getResult();
    if (resultObj) {
        invariant(resultObj->isOwned());
    }
    _stash.push(result);
}

bool ClusterClientCursorImpl::remotesExhausted() {
    return _root->remotesExhausted();
}

Status ClusterClientCursorImpl::setAwaitDataTimeout(Milliseconds awaitDataTimeout) {
    return _root->setAwaitDataTimeout(awaitDataTimeout);
}

boost::optional<LogicalSessionId> ClusterClientCursorImpl::getLsid() const {
    return _lsid;
}

boost::optional<TxnNumber> ClusterClientCursorImpl::getTxnNumber() const {
    return _params.txnNumber;
}

boost::optional<ReadPreferenceSetting> ClusterClientCursorImpl::getReadPreference() const {
    return _params.readPreference;
}

std::unique_ptr<RouterExecStage> ClusterClientCursorImpl::buildMergerPlan(
    OperationContext* opCtx, executor::TaskExecutor* executor, ClusterClientCursorParams* params) {
    const auto skip = params->skip;
    const auto limit = params->limit;

    std::unique_ptr<RouterExecStage> root =
        std::make_unique<RouterStageMerge>(opCtx, executor, params->extractARMParams());

    if (skip) {
        root = stdx::make_unique<RouterStageSkip>(opCtx, std::move(root), *skip);
    }

    if (limit) {
        root = stdx::make_unique<RouterStageLimit>(opCtx, std::move(root), *limit);
    }

    const bool hasSort = !params->sort.isEmpty();
    if (hasSort) {
        // Strip out the sort key after sorting.
        root = stdx::make_unique<RouterStageRemoveMetadataFields>(
            opCtx, std::move(root), std::vector<StringData>{AsyncResultsMerger::kSortKeyField});
    }

    return root;
}

}  // namespace mongo
