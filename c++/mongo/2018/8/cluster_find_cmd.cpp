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

#include "mongo/platform/basic.h"

#include <boost/optional.hpp>

#include "mongo/client/read_preference.h"
#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/commands.h"
#include "mongo/db/matcher/extensions_callback_noop.h"
#include "mongo/db/query/cursor_response.h"
#include "mongo/db/stats/counters.h"
#include "mongo/db/views/resolved_view.h"
#include "mongo/rpc/get_status_from_command_result.h"
#include "mongo/s/catalog_cache.h"
#include "mongo/s/commands/cluster_commands_helpers.h"
#include "mongo/s/commands/cluster_explain.h"
#include "mongo/s/grid.h"
#include "mongo/s/query/cluster_aggregate.h"
#include "mongo/s/query/cluster_find.h"

namespace mongo {
namespace {

using std::unique_ptr;
using std::string;
using std::vector;

const char kTermField[] = "term";

/**
 * Implements the find command on mongos.
 */
class ClusterFindCmd final : public Command {
public:
    ClusterFindCmd() : Command("find") {}

    std::unique_ptr<CommandInvocation> parse(OperationContext* opCtx,
                                             const OpMsgRequest& opMsgRequest) override {
        // TODO: Parse into a QueryRequest here.
        return std::make_unique<Invocation>(this, opMsgRequest, opMsgRequest.getDatabase());
    }

    AllowedOnSecondary secondaryAllowed(ServiceContext* context) const override {
        return AllowedOnSecondary::kOptIn;
    }

    bool maintenanceOk() const final {
        return false;
    }

    bool adminOnly() const final {
        return false;
    }

    bool shouldAffectCommandCounter() const final {
        return false;
    }

    std::string help() const override {
        return "query for documents";
    }

    class Invocation final : public CommandInvocation {
    public:
        Invocation(const ClusterFindCmd* definition, const OpMsgRequest& request, StringData dbName)
            : CommandInvocation(definition), _request(request), _dbName(dbName) {}

    private:
        bool supportsWriteConcern() const override {
            return false;
        }

        bool supportsReadConcern(repl::ReadConcernLevel level) const final {
            return true;
        }

        NamespaceString ns() const override {
            // TODO get the ns from the parsed QueryRequest.
            return NamespaceString(
                CommandHelpers::parseNsCollectionRequired(_dbName, _request.body));
        }

        /**
         * In order to run the find command, you must be authorized for the "find" action
         * type on the collection.
         */
        void doCheckAuthorization(OperationContext* opCtx) const final {
            auto hasTerm = _request.body.hasField(kTermField);
            uassertStatusOK(
                AuthorizationSession::get(opCtx->getClient())->checkAuthForFind(ns(), hasTerm));
        }

        void explain(OperationContext* opCtx,
                     ExplainOptions::Verbosity verbosity,
                     rpc::ReplyBuilderInterface* result) override {
            // Parse the command BSON to a QueryRequest.
            bool isExplain = true;
            auto qr =
                uassertStatusOK(QueryRequest::makeFromFindCommand(ns(), _request.body, isExplain));

            try {
                const auto explainCmd = ClusterExplain::wrapAsExplain(_request.body, verbosity);

                long long millisElapsed;
                std::vector<AsyncRequestsSender::Response> shardResponses;

                // We will time how long it takes to run the commands on the shards.
                Timer timer;
                const auto routingInfo = uassertStatusOK(
                    Grid::get(opCtx)->catalogCache()->getCollectionRoutingInfo(opCtx, qr->nss()));
                shardResponses =
                    scatterGatherVersionedTargetByRoutingTable(opCtx,
                                                               qr->nss().db(),
                                                               qr->nss(),
                                                               routingInfo,
                                                               explainCmd,
                                                               ReadPreferenceSetting::get(opCtx),
                                                               Shard::RetryPolicy::kIdempotent,
                                                               qr->getFilter(),
                                                               qr->getCollation());
                millisElapsed = timer.millis();

                const char* mongosStageName =
                    ClusterExplain::getStageNameForReadOp(shardResponses.size(), _request.body);

                auto bodyBuilder = result->getBodyBuilder();
                uassertStatusOK(ClusterExplain::buildExplainResult(
                    opCtx,
                    ClusterExplain::downconvert(opCtx, shardResponses),
                    mongosStageName,
                    millisElapsed,
                    &bodyBuilder));

            } catch (const ExceptionFor<ErrorCodes::CommandOnShardedViewNotSupportedOnMongod>& ex) {
                auto bodyBuilder = result->getBodyBuilder();
                bodyBuilder.resetToEmpty();

                auto aggCmdOnView = uassertStatusOK(qr->asAggregationCommand());

                auto aggRequestOnView = uassertStatusOK(
                    AggregationRequest::parseFromBSON(ns(), aggCmdOnView, verbosity));

                auto resolvedAggRequest = ex->asExpandedViewAggregation(aggRequestOnView);
                auto resolvedAggCmd = resolvedAggRequest.serializeToCommandObj().toBson();

                ClusterAggregate::Namespaces nsStruct;
                nsStruct.requestedNss = ns();
                nsStruct.executionNss = std::move(ex->getNamespace());

                uassertStatusOK(ClusterAggregate::runAggregate(
                    opCtx, nsStruct, resolvedAggRequest, resolvedAggCmd, &bodyBuilder));
            }
        }

        void run(OperationContext* opCtx, rpc::ReplyBuilderInterface* result) {
            // We count find command as a query op.
            globalOpCounters.gotQuery();

            const bool isExplain = false;
            auto qr =
                uassertStatusOK(QueryRequest::makeFromFindCommand(ns(), _request.body, isExplain));

            const boost::intrusive_ptr<ExpressionContext> expCtx;
            auto cq = uassertStatusOK(
                CanonicalQuery::canonicalize(opCtx,
                                             std::move(qr),
                                             expCtx,
                                             ExtensionsCallbackNoop(),
                                             MatchExpressionParser::kAllowAllSpecialFeatures));

            try {
                // Do the work to generate the first batch of results. This blocks waiting to get
                // responses from the shard(s).
                std::vector<BSONObj> batch;
                auto cursorId =
                    ClusterFind::runQuery(opCtx, *cq, ReadPreferenceSetting::get(opCtx), &batch);

                // Build the response document.
                CursorResponseBuilder::Options options;
                options.isInitialResponse = true;
                CursorResponseBuilder firstBatch(result, options);
                for (const auto& obj : batch) {
                    firstBatch.append(obj);
                }
                firstBatch.done(cursorId, cq->ns());
            } catch (const ExceptionFor<ErrorCodes::CommandOnShardedViewNotSupportedOnMongod>& ex) {
                result->reset();

                auto aggCmdOnView = uassertStatusOK(cq->getQueryRequest().asAggregationCommand());

                auto aggRequestOnView =
                    uassertStatusOK(AggregationRequest::parseFromBSON(ns(), aggCmdOnView));

                auto resolvedAggRequest = ex->asExpandedViewAggregation(aggRequestOnView);
                auto resolvedAggCmd = resolvedAggRequest.serializeToCommandObj().toBson();

                // We pass both the underlying collection namespace and the view namespace here. The
                // underlying collection namespace is used to execute the aggregation on mongoD. Any
                // cursor returned will be registered under the view namespace so that subsequent
                // getMore and killCursors calls against the view have access.
                ClusterAggregate::Namespaces nsStruct;
                nsStruct.requestedNss = ns();
                nsStruct.executionNss = std::move(ex->getNamespace());

                auto bodyBuilder = result->getBodyBuilder();
                uassertStatusOK(ClusterAggregate::runAggregate(
                    opCtx, nsStruct, resolvedAggRequest, resolvedAggCmd, &bodyBuilder));
            }
        }

    private:
        const OpMsgRequest& _request;
        const StringData _dbName;
    };

} cmdFindCluster;

}  // namespace
}  // namespace mongo
