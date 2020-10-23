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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kQuery

#include "mongo/platform/basic.h"

#include <memory>

#include "mongo/base/disallow_copying.h"
#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/catalog/collection.h"
#include "mongo/db/catalog/database.h"
#include "mongo/db/client.h"
#include "mongo/db/clientcursor.h"
#include "mongo/db/commands.h"
#include "mongo/db/commands/cursor_responses.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/exec/working_set_common.h"
#include "mongo/db/service_context.h"
#include "mongo/db/query/explain.h"
#include "mongo/db/query/find.h"
#include "mongo/db/query/get_executor.h"
#include "mongo/db/server_parameters.h"
#include "mongo/db/stats/counters.h"
#include "mongo/s/d_state.h"
#include "mongo/s/stale_exception.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

namespace mongo {

    /**
     * A command for running .find() queries.
     */
    class FindCmd : public Command {
        MONGO_DISALLOW_COPYING(FindCmd);
    public:
        FindCmd() : Command("find") { }

        bool isWriteCommandForConfigServer() const override { return false; }

        bool slaveOk() const override { return false; }

        bool slaveOverrideOk() const override { return true; }

        bool maintenanceOk() const override { return false; }

        bool adminOnly() const override { return false; }

        void help(std::stringstream& help) const override {
            help << "query for documents";
        }

        /**
         * A find command does not increment the command counter, but rather increments the
         * query counter.
         */
        bool shouldAffectCommandCounter() const override { return false; }

        Status checkAuthForCommand(ClientBasic* client,
                                   const std::string& dbname,
                                   const BSONObj& cmdObj) override {
            AuthorizationSession* authzSession = AuthorizationSession::get(client);
            ResourcePattern pattern = parseResourcePattern(dbname, cmdObj);

            if (authzSession->isAuthorizedForActionsOnResource(pattern, ActionType::find)) {
                return Status::OK();
            }

            return Status(ErrorCodes::Unauthorized, "unauthorized");
        }

        Status explain(OperationContext* txn,
                       const std::string& dbname,
                       const BSONObj& cmdObj,
                       ExplainCommon::Verbosity verbosity,
                       BSONObjBuilder* out) const override {
            const std::string fullns = parseNs(dbname, cmdObj);
            const NamespaceString nss(fullns);

            // Parse the command BSON to a LiteParsedQuery.
            std::unique_ptr<LiteParsedQuery> lpq;
            {
                LiteParsedQuery* rawLpq;
                const bool isExplain = true;
                Status lpqStatus = LiteParsedQuery::make(fullns, cmdObj, isExplain, &rawLpq);
                if (!lpqStatus.isOK()) {
                    return lpqStatus;
                }
                lpq.reset(rawLpq);
            }

            // Finish the parsing step by using the LiteParsedQuery to create a CanonicalQuery.
            std::unique_ptr<CanonicalQuery> cq;
            {
                CanonicalQuery* rawCq;
                WhereCallbackReal whereCallback(txn, nss.db());
                Status canonStatus = CanonicalQuery::canonicalize(lpq.release(),
                                                                  &rawCq,
                                                                  whereCallback);
                if (!canonStatus.isOK()) {
                    return canonStatus;
                }
                cq.reset(rawCq);
            }

            AutoGetCollectionForRead ctx(txn, nss);
            // The collection may be NULL. If so, getExecutor() should handle it by returning
            // an execution tree with an EOFStage.
            Collection* collection = ctx.getCollection();

            // We have a parsed query. Time to get the execution plan for it.
            std::unique_ptr<PlanExecutor> exec;
            {
                PlanExecutor* rawExec;
                Status execStatus = getExecutorFind(txn,
                                                    collection,
                                                    nss,
                                                    cq.release(),
                                                    PlanExecutor::YIELD_AUTO,
                                                    &rawExec);
                if (!execStatus.isOK()) {
                    return execStatus;
                }
                exec.reset(rawExec);
            }

            // Got the execution tree. Explain it.
            Explain::explainStages(exec.get(), verbosity, out);
            return Status::OK();
        }

        /**
         * Runs a query using the following steps:
         *   1) Parsing.
         *   2) Acquire locks.
         *   3) Plan query, obtaining an executor that can run it.
         *   4) Setup a cursor for the query, which may be used on subsequent getMores.
         *   5) Generate the first batch.
         *   6) Save state for getMore.
         *   7) Generate response to send to the client.
         *
         * TODO: Rather than using the sharding version available in thread-local storage
         * (i.e. call to shardingState.needCollectionMetadata() below), shard version
         * information should be passed as part of the command parameter.
         */
        bool run(OperationContext* txn,
                 const std::string& dbname,
                 BSONObj& cmdObj,
                 int options,
                 std::string& errmsg,
                 BSONObjBuilder& result) override {
            const std::string fullns = parseNs(dbname, cmdObj);
            const NamespaceString nss(fullns);

            // Although it is a command, a find command gets counted as a query.
            globalOpCounters.gotQuery();

            if (txn->getClient()->isInDirectClient()) {
                return appendCommandStatus(result,
                                           Status(ErrorCodes::IllegalOperation,
                                                  "Cannot run find command from eval()"));
            }

            // 1a) Parse the command BSON to a LiteParsedQuery.
            std::unique_ptr<LiteParsedQuery> lpq;
            {
                LiteParsedQuery* rawLpq;
                const bool isExplain = false;
                Status lpqStatus = LiteParsedQuery::make(fullns, cmdObj, isExplain, &rawLpq);
                if (!lpqStatus.isOK()) {
                    return appendCommandStatus(result, lpqStatus);
                }
                lpq.reset(rawLpq);
            }

            // Fill out curop information.
            beginQueryOp(nss, cmdObj, lpq->getNumToReturn(), lpq->getSkip(), txn->getCurOp());

            // 1b) Finish the parsing step by using the LiteParsedQuery to create a CanonicalQuery.
            std::unique_ptr<CanonicalQuery> cq;
            {
                CanonicalQuery* rawCq;
                WhereCallbackReal whereCallback(txn, nss.db());
                Status canonStatus = CanonicalQuery::canonicalize(lpq.release(),
                                                                  &rawCq,
                                                                  whereCallback);
                if (!canonStatus.isOK()) {
                    return appendCommandStatus(result, canonStatus);
                }
                cq.reset(rawCq);
            }

            // 2) Acquire locks.
            AutoGetCollectionForRead ctx(txn, nss);
            Collection* collection = ctx.getCollection();

            const int dbProfilingLevel = ctx.getDb() ? ctx.getDb()->getProfilingLevel() :
                                                       serverGlobalParams.defaultProfile;

            // It is possible that the sharding version will change during yield while we are
            // retrieving a plan executor. If this happens we will throw an error and mongos will
            // retry.
            const ChunkVersion shardingVersionAtStart = shardingState.getVersion(nss.ns());

            // 3) Get the execution plan for the query.
            std::unique_ptr<PlanExecutor> execHolder;
            {
                PlanExecutor* rawExec;
                Status execStatus = getExecutorFind(txn,
                                                    collection,
                                                    nss,
                                                    cq.release(),
                                                    PlanExecutor::YIELD_AUTO,
                                                    &rawExec);
                if (!execStatus.isOK()) {
                    return appendCommandStatus(result, execStatus);
                }
                execHolder.reset(rawExec);
            }

            // TODO: Currently, chunk ranges are kept around until all ClientCursors created while
            // the chunk belonged on this node are gone. Separating chunk lifetime management from
            // ClientCursor should allow this check to go away.
            if (!shardingState.getVersion(nss.ns()).isWriteCompatibleWith(shardingVersionAtStart)) {
                // Version changed while retrieving a PlanExecutor. Terminate the operation,
                // signaling that mongos should retry.
                throw SendStaleConfigException(nss.ns(),
                                               "version changed during find command",
                                               shardingVersionAtStart,
                                               shardingState.getVersion(nss.ns()));
            }

            if (!collection) {
                // No collection. Just fill out curop indicating that there were zero results and
                // there is no ClientCursor id, and then return.
                const int numResults = 0;
                const CursorId cursorId = 0;
                endQueryOp(execHolder.get(), dbProfilingLevel, numResults, cursorId,
                           txn->getCurOp());
                appendCursorResponseObject(cursorId, nss.ns(), BSONArray(), &result);
                return true;
            }

            const LiteParsedQuery& pq = execHolder->getCanonicalQuery()->getParsed();

            // 4) If possible, register the execution plan inside a ClientCursor, and pin that
            // cursor. In this case, ownership of the PlanExecutor is transferred to the
            // ClientCursor, and 'exec' becomes null.
            //
            // First unregister the PlanExecutor so it can be re-registered with ClientCursor.
            execHolder->deregisterExec();

            // Create a ClientCursor containing this plan executor. We don't have to worry
            // about leaking it as it's inserted into a global map by its ctor.
            ClientCursor* cursor = new ClientCursor(collection->getCursorManager(),
                                                    execHolder.release(),
                                                    nss.ns(),
                                                    pq.getOptions(),
                                                    pq.getFilter());
            CursorId cursorId = cursor->cursorid();
            ClientCursorPin ccPin(collection->getCursorManager(), cursorId);

            // On early return, get rid of the the cursor.
            ScopeGuard cursorFreer = MakeGuard(&ClientCursorPin::deleteUnderlying, ccPin);

            invariant(!execHolder);
            PlanExecutor* exec = cursor->getExecutor();

            // 5) Stream query results, adding them to a BSONArray as we go.
            //
            // TODO: Handle result sets larger than 16MB.
            BSONArrayBuilder firstBatch;
            BSONObj obj;
            PlanExecutor::ExecState state;
            int numResults = 0;
            while (PlanExecutor::ADVANCED == (state = exec->getNext(&obj, NULL))) {
                // Add result to output buffer.
                firstBatch.append(obj);
                numResults++;

                if (enoughForFirstBatch(pq, numResults, firstBatch.len())) {
                    break;
                }
            }

            // Throw an assertion if query execution fails for any reason.
            if (PlanExecutor::FAILURE == state) {
                const std::unique_ptr<PlanStageStats> stats(exec->getStats());
                error() << "Plan executor error, stats: " << Explain::statsToBSON(*stats);
                return appendCommandStatus(result,
                                           Status(ErrorCodes::OperationFailed,
                                                  str::stream() << "Executor error: "
                                                  << WorkingSetCommon::toStatusString(obj)));
            }

            // 6) Set up the cursor for getMore.
            if (shouldSaveCursor(txn, collection, state, exec)) {
                // State will be restored on getMore.
                exec->saveState();

                cursor->setLeftoverMaxTimeMicros(txn->getCurOp()->getRemainingMaxTimeMicros());
                cursor->setPos(numResults);

                // Don't stash the RU for tailable cursors at EOF, let them get a new RU on their
                // next getMore.
                if (!(pq.isTailable() && state == PlanExecutor::IS_EOF)) {
                    // We stash away the RecoveryUnit in the ClientCursor. It's used for
                    // subsequent getMore requests. The calling OpCtx gets a fresh RecoveryUnit.
                    txn->recoveryUnit()->commitAndRestart();
                    cursor->setOwnedRecoveryUnit(txn->releaseRecoveryUnit());
                    StorageEngine* engine = getGlobalServiceContext()->getGlobalStorageEngine();
                    txn->setRecoveryUnit(engine->newRecoveryUnit());
                }
            }
            else {
                cursorId = 0;
            }

            // Fill out curop based on the results.
            endQueryOp(exec, dbProfilingLevel, numResults, cursorId, txn->getCurOp());

            // 7) Generate the response object to send to the client.
            appendCursorResponseObject(cursorId, nss.ns(), firstBatch.arr(), &result);
            if (cursorId) {
                cursorFreer.Dismiss();
            }
            return true;
        }

    } findCmd;

} // namespace mongo
