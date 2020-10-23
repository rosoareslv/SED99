/**
 * Copyright (C) 2017 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/s/query/router_stage_pipeline.h"

#include "mongo/db/pipeline/document_source.h"
#include "mongo/db/pipeline/document_source_change_stream.h"
#include "mongo/db/pipeline/document_source_list_local_sessions.h"
#include "mongo/db/pipeline/document_source_merge_cursors.h"
#include "mongo/db/pipeline/expression_context.h"

namespace mongo {

RouterStagePipeline::RouterStagePipeline(std::unique_ptr<Pipeline, PipelineDeleter> mergePipeline)
    : RouterExecStage(mergePipeline->getContext()->opCtx),
      _mergePipeline(std::move(mergePipeline)) {
    invariant(!_mergePipeline->getSources().empty());
    _mergeCursorsStage =
        dynamic_cast<DocumentSourceMergeCursors*>(_mergePipeline->getSources().front().get());
}

StatusWith<ClusterQueryResult> RouterStagePipeline::next(RouterExecStage::ExecContext execContext) {
    if (_mergeCursorsStage) {
        _mergeCursorsStage->setExecContext(execContext);
    }

    // Pipeline::getNext will return a boost::optional<Document> or boost::none if EOF.
    if (auto result = _mergePipeline->getNext()) {
        return {result->toBson()};
    }

    // If we reach this point, we have hit EOF.
    if (!_mergePipeline->getContext()->isTailableAwaitData()) {
        _mergePipeline.get_deleter().dismissDisposal();
        _mergePipeline->dispose(getOpCtx());
    }

    return {ClusterQueryResult()};
}

void RouterStagePipeline::doReattachToOperationContext() {
    _mergePipeline->reattachToOperationContext(getOpCtx());
}

void RouterStagePipeline::doDetachFromOperationContext() {
    _mergePipeline->detachFromOperationContext();
}

void RouterStagePipeline::kill(OperationContext* opCtx) {
    _mergePipeline.get_deleter().dismissDisposal();
    _mergePipeline->dispose(opCtx);
}

std::size_t RouterStagePipeline::getNumRemotes() const {
    if (_mergeCursorsStage) {
        return _mergeCursorsStage->getNumRemotes();
    }
    return 0;
}

bool RouterStagePipeline::remotesExhausted() {
    return !_mergeCursorsStage || _mergeCursorsStage->remotesExhausted();
}

Status RouterStagePipeline::doSetAwaitDataTimeout(Milliseconds awaitDataTimeout) {
    invariant(_mergeCursorsStage,
              "The only cursors which should be tailable are those with remote cursors.");
    return _mergeCursorsStage->setAwaitDataTimeout(awaitDataTimeout);
}

}  // namespace mongo
