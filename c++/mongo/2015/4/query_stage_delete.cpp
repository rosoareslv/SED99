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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

/**
 * This file tests db/exec/delete.cpp.
 */

#include <boost/scoped_ptr.hpp>

#include "mongo/db/catalog/collection.h"
#include "mongo/db/catalog/database.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/exec/collection_scan.h"
#include "mongo/db/exec/delete.h"
#include "mongo/db/exec/queued_data_stage.h"
#include "mongo/db/operation_context_impl.h"
#include "mongo/db/query/canonical_query.h"
#include "mongo/db/service_context.h"
#include "mongo/dbtests/dbtests.h"
#include "mongo/stdx/memory.h"

namespace QueryStageDelete {

    using boost::scoped_ptr;
    using std::vector;

    //
    // Stage-specific tests.
    //

    class QueryStageDeleteBase {
    public:
        QueryStageDeleteBase() : _client(&_txn) {
            OldClientWriteContext ctx(&_txn, ns());

            for (size_t i = 0; i < numObj(); ++i) {
                BSONObjBuilder bob;
                bob.append("_id", static_cast<long long int>(i));
                bob.append("foo", static_cast<long long int>(i));
                _client.insert(ns(), bob.obj());
            }
        }

        virtual ~QueryStageDeleteBase() {
            OldClientWriteContext ctx(&_txn, ns());
            _client.dropCollection(ns());
        }

        void remove(const BSONObj& obj) {
            _client.remove(ns(), obj);
        }

        void getLocs(Collection* collection,
                     CollectionScanParams::Direction direction,
                     vector<RecordId>* out) {
            WorkingSet ws;

            CollectionScanParams params;
            params.collection = collection;
            params.direction = direction;
            params.tailable = false;

            scoped_ptr<CollectionScan> scan(new CollectionScan(&_txn, params, &ws, NULL));
            while (!scan->isEOF()) {
                WorkingSetID id = WorkingSet::INVALID_ID;
                PlanStage::StageState state = scan->work(&id);
                if (PlanStage::ADVANCED == state) {
                    WorkingSetMember* member = ws.get(id);
                    verify(member->hasLoc());
                    out->push_back(member->loc);
                }
            }
        }

        CanonicalQuery* canonicalize(const BSONObj& query) {
            CanonicalQuery* cq;
            Status status = CanonicalQuery::canonicalize(ns(), query, &cq);
            ASSERT_OK(status);
            return cq;
        }

        static size_t numObj() { return 50; }

        static const char* ns() { return "unittests.QueryStageDelete"; }

    protected:
        OperationContextImpl _txn;

    private:
        DBDirectClient _client;
    };

    //
    // Test invalidation for the delete stage.  Use the delete stage to delete some objects
    // retrieved by a collscan, then invalidate the upcoming object, then expect the delete stage to
    // skip over it and successfully delete the rest.
    //
    class QueryStageDeleteInvalidateUpcomingObject : public QueryStageDeleteBase {
    public:
        void run() {
            OldClientWriteContext ctx(&_txn, ns());

            Collection* coll = ctx.getCollection();

            // Get the RecordIds that would be returned by an in-order scan.
            vector<RecordId> locs;
            getLocs(coll, CollectionScanParams::FORWARD, &locs);

            // Configure the scan.
            CollectionScanParams collScanParams;
            collScanParams.collection = coll;
            collScanParams.direction = CollectionScanParams::FORWARD;
            collScanParams.tailable = false;

            // Configure the delete stage.
            DeleteStageParams deleteStageParams;
            deleteStageParams.isMulti = true;
            deleteStageParams.shouldCallLogOp = false;

            WorkingSet ws;
            DeleteStage deleteStage(&_txn, deleteStageParams, &ws, coll,
                                    new CollectionScan(&_txn, collScanParams, &ws, NULL));

            const DeleteStats* stats =
                static_cast<const DeleteStats*>(deleteStage.getSpecificStats());

            const size_t targetDocIndex = 10;

            while (stats->docsDeleted < targetDocIndex) {
                WorkingSetID id = WorkingSet::INVALID_ID;
                PlanStage::StageState state = deleteStage.work(&id);
                ASSERT_EQUALS(PlanStage::NEED_TIME, state);
            }

            // Remove locs[targetDocIndex];
            deleteStage.saveState();
            deleteStage.invalidate(&_txn, locs[targetDocIndex], INVALIDATION_DELETION);
            BSONObj targetDoc = coll->docFor(&_txn, locs[targetDocIndex]).value();
            ASSERT(!targetDoc.isEmpty());
            remove(targetDoc);
            deleteStage.restoreState(&_txn);

            // Remove the rest.
            while (!deleteStage.isEOF()) {
                WorkingSetID id = WorkingSet::INVALID_ID;
                PlanStage::StageState state = deleteStage.work(&id);
                invariant(PlanStage::NEED_TIME == state || PlanStage::IS_EOF == state);
            }

            ASSERT_EQUALS(numObj() - 1, stats->docsDeleted);
        }
    };

    /**
     * Test that the delete stage returns an owned copy of the original document if returnDeleted is
     * specified.
     */
    class QueryStageDeleteReturnOldDoc : public QueryStageDeleteBase {
    public:
        void run() {
            // Various variables we'll need.
            OldClientWriteContext ctx(&_txn, ns());
            Collection* coll = ctx.getCollection();
            const NamespaceString nss(ns());
            const int targetDocIndex = 0;
            const BSONObj query = BSON("foo" << BSON("$gte" << targetDocIndex));
            const std::unique_ptr<WorkingSet> ws(stdx::make_unique<WorkingSet>());
            const std::unique_ptr<CanonicalQuery> cq(canonicalize(query));

            // Get the RecordIds that would be returned by an in-order scan.
            vector<RecordId> locs;
            getLocs(coll, CollectionScanParams::FORWARD, &locs);

            // Configure a QueuedDataStage to pass the first object in the collection back in a
            // LOC_AND_UNOWNED_OBJ state.
            std::unique_ptr<QueuedDataStage> qds(stdx::make_unique<QueuedDataStage>(ws.get()));
            WorkingSetMember member;
            member.loc = locs[targetDocIndex];
            member.state = WorkingSetMember::LOC_AND_UNOWNED_OBJ;
            const BSONObj oldDoc = BSON("_id" << targetDocIndex << "foo" << targetDocIndex);
            member.obj = Snapshotted<BSONObj>(SnapshotId(), oldDoc);
            qds->pushBack(member);

            // Configure the delete.
            DeleteStageParams deleteParams;
            deleteParams.returnDeleted = true;
            deleteParams.canonicalQuery = cq.get();

            const std::unique_ptr<DeleteStage> deleteStage(
                stdx::make_unique<DeleteStage>(&_txn, deleteParams, ws.get(), coll, qds.release()));

            const DeleteStats* stats =
                static_cast<const DeleteStats*>(deleteStage->getSpecificStats());

            // Should return advanced.
            WorkingSetID id = WorkingSet::INVALID_ID;
            PlanStage::StageState state = deleteStage->work(&id);
            ASSERT_EQUALS(PlanStage::ADVANCED, state);

            // Make sure the returned value is what we expect it to be.

            // Should give us back a valid id.
            ASSERT_TRUE(WorkingSet::INVALID_ID != id);
            WorkingSetMember* resultMember = ws->get(id);
            // With an owned copy of the object, with no RecordId.
            ASSERT_TRUE(resultMember->hasOwnedObj());
            ASSERT_FALSE(resultMember->hasLoc());
            ASSERT_EQUALS(resultMember->state, WorkingSetMember::OWNED_OBJ);
            ASSERT_TRUE(resultMember->obj.value().isOwned());

            // Should be the old value.
            ASSERT_EQUALS(resultMember->obj.value(), oldDoc);

            // Should have done the delete.
            ASSERT_EQUALS(stats->docsDeleted, 1U);
            // That should be it.
            id = WorkingSet::INVALID_ID;
            ASSERT_EQUALS(PlanStage::IS_EOF, deleteStage->work(&id));
        }
    };

    /**
     * Test that the delete stage does not delete or return WorkingSetMembers that it gets back from
     * a child in the OWNED_OBJ state.
     */
    class QueryStageDeleteSkipOwnedObjects : public QueryStageDeleteBase {
    public:
        void run() {
            // Various variables we'll need.
            OldClientWriteContext ctx(&_txn, ns());
            Collection* coll = ctx.getCollection();
            const BSONObj query = BSONObj();
            const std::unique_ptr<WorkingSet> ws(stdx::make_unique<WorkingSet>());
            const std::unique_ptr<CanonicalQuery> cq(canonicalize(query));

            // Configure a QueuedDataStage to pass an OWNED_OBJ to the delete stage.
            std::unique_ptr<QueuedDataStage> qds(stdx::make_unique<QueuedDataStage>(ws.get()));
            WorkingSetMember member;
            member.state = WorkingSetMember::OWNED_OBJ;
            member.obj = Snapshotted<BSONObj>(SnapshotId(), fromjson("{x: 1}"));
            qds->pushBack(member);

            // Configure the delete.
            DeleteStageParams deleteParams;
            deleteParams.isMulti = false;
            deleteParams.returnDeleted = true;
            deleteParams.canonicalQuery = cq.get();

            const std::unique_ptr<DeleteStage> deleteStage(
                stdx::make_unique<DeleteStage>(&_txn, deleteParams, ws.get(), coll, qds.release()));
            const DeleteStats* stats =
                static_cast<const DeleteStats*>(deleteStage->getSpecificStats());

            // Call work, passing the set up member to the delete stage.
            WorkingSetID id = WorkingSet::INVALID_ID;
            PlanStage::StageState state = deleteStage->work(&id);

            // Should return NEED_TIME, not deleting anything.
            ASSERT_EQUALS(PlanStage::NEED_TIME, state);
            ASSERT_EQUALS(stats->docsDeleted, 0U);

            id = WorkingSet::INVALID_ID;
            state = deleteStage->work(&id);
            ASSERT_EQUALS(PlanStage::IS_EOF, state);
        }
    };

    class All : public Suite {
    public:
        All() : Suite("query_stage_delete") {}

        void setupTests() {
            // Stage-specific tests below.
            add<QueryStageDeleteInvalidateUpcomingObject>();
            add<QueryStageDeleteReturnOldDoc>();
            add<QueryStageDeleteSkipOwnedObjects>();
        }
    };

    SuiteInstance<All> all;

} // namespace QueryStageDelete
