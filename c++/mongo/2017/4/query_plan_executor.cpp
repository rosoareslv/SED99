/**
 *    Copyright (C) 2013-2014 MongoDB Inc.
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


#include "mongo/platform/basic.h"

#include "mongo/db/catalog/collection.h"
#include "mongo/db/catalog/database.h"
#include "mongo/db/client.h"
#include "mongo/db/clientcursor.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/exec/collection_scan.h"
#include "mongo/db/exec/fetch.h"
#include "mongo/db/exec/index_scan.h"
#include "mongo/db/exec/pipeline_proxy.h"
#include "mongo/db/exec/plan_stage.h"
#include "mongo/db/exec/working_set_common.h"
#include "mongo/db/json.h"
#include "mongo/db/matcher/expression_parser.h"
#include "mongo/db/matcher/extensions_callback_disallow_extensions.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/pipeline/document_source_cursor.h"
#include "mongo/db/pipeline/expression_context_for_test.h"
#include "mongo/db/pipeline/pipeline.h"
#include "mongo/db/query/plan_executor.h"
#include "mongo/db/query/query_solution.h"
#include "mongo/dbtests/dbtests.h"
#include "mongo/stdx/memory.h"

namespace QueryPlanExecutor {

using std::shared_ptr;
using std::string;
using std::unique_ptr;
using stdx::make_unique;

static const NamespaceString nss("unittests.QueryPlanExecutor");

class PlanExecutorBase {
public:
    PlanExecutorBase() : _client(&_opCtx) {}

    virtual ~PlanExecutorBase() {
        _client.dropCollection(nss.ns());
    }

    void addIndex(const BSONObj& obj) {
        ASSERT_OK(dbtests::createIndex(&_opCtx, nss.ns(), obj));
    }

    void insert(const BSONObj& obj) {
        _client.insert(nss.ns(), obj);
    }

    void remove(const BSONObj& obj) {
        _client.remove(nss.ns(), obj);
    }

    void dropCollection() {
        _client.dropCollection(nss.ns());
    }

    void update(BSONObj& query, BSONObj& updateSpec) {
        _client.update(nss.ns(), query, updateSpec, false, false);
    }

    /**
     * Given a match expression, represented as the BSON object 'filterObj', create a PlanExecutor
     * capable of executing a simple collection scan.
     */
    unique_ptr<PlanExecutor, PlanExecutor::Deleter> makeCollScanExec(Collection* coll,
                                                                     BSONObj& filterObj) {
        CollectionScanParams csparams;
        csparams.collection = coll;
        csparams.direction = CollectionScanParams::FORWARD;
        unique_ptr<WorkingSet> ws(new WorkingSet());

        // Canonicalize the query.
        auto qr = stdx::make_unique<QueryRequest>(nss);
        qr->setFilter(filterObj);
        auto statusWithCQ = CanonicalQuery::canonicalize(
            &_opCtx, std::move(qr), ExtensionsCallbackDisallowExtensions());
        verify(statusWithCQ.isOK());
        unique_ptr<CanonicalQuery> cq = std::move(statusWithCQ.getValue());
        verify(NULL != cq.get());

        // Make the stage.
        unique_ptr<PlanStage> root(
            new CollectionScan(&_opCtx, csparams, ws.get(), cq.get()->root()));

        // Hand the plan off to the executor.
        auto statusWithPlanExecutor = PlanExecutor::make(&_opCtx,
                                                         std::move(ws),
                                                         std::move(root),
                                                         std::move(cq),
                                                         coll,
                                                         PlanExecutor::YIELD_MANUAL);
        ASSERT_OK(statusWithPlanExecutor.getStatus());
        return std::move(statusWithPlanExecutor.getValue());
    }

    /**
     * @param indexSpec -- a BSONObj giving the index over which to
     *   scan, e.g. {_id: 1}.
     * @param start -- the lower bound (inclusive) at which to start
     *   the index scan
     * @param end -- the lower bound (inclusive) at which to end the
     *   index scan
     *
     * Returns a PlanExecutor capable of executing an index scan
     * over the specified index with the specified bounds.
     */
    unique_ptr<PlanExecutor, PlanExecutor::Deleter> makeIndexScanExec(Database* db,
                                                                      BSONObj& indexSpec,
                                                                      int start,
                                                                      int end) {
        // Build the index scan stage.
        IndexScanParams ixparams;
        ixparams.descriptor = getIndex(db, indexSpec);
        ixparams.bounds.isSimpleRange = true;
        ixparams.bounds.startKey = BSON("" << start);
        ixparams.bounds.endKey = BSON("" << end);
        ixparams.bounds.boundInclusion = BoundInclusion::kIncludeBothStartAndEndKeys;
        ixparams.direction = 1;

        const Collection* coll = db->getCollection(&_opCtx, nss);

        unique_ptr<WorkingSet> ws(new WorkingSet());
        IndexScan* ix = new IndexScan(&_opCtx, ixparams, ws.get(), NULL);
        unique_ptr<PlanStage> root(new FetchStage(&_opCtx, ws.get(), ix, NULL, coll));

        auto qr = stdx::make_unique<QueryRequest>(nss);
        auto statusWithCQ = CanonicalQuery::canonicalize(
            &_opCtx, std::move(qr), ExtensionsCallbackDisallowExtensions());
        verify(statusWithCQ.isOK());
        unique_ptr<CanonicalQuery> cq = std::move(statusWithCQ.getValue());
        verify(NULL != cq.get());

        // Hand the plan off to the executor.
        auto statusWithPlanExecutor = PlanExecutor::make(&_opCtx,
                                                         std::move(ws),
                                                         std::move(root),
                                                         std::move(cq),
                                                         coll,
                                                         PlanExecutor::YIELD_MANUAL);
        ASSERT_OK(statusWithPlanExecutor.getStatus());
        return std::move(statusWithPlanExecutor.getValue());
    }

    size_t numCursors() {
        AutoGetCollectionForReadCommand ctx(&_opCtx, nss);
        Collection* collection = ctx.getCollection();
        if (!collection)
            return 0;
        return collection->getCursorManager()->numCursors();
    }

protected:
    const ServiceContext::UniqueOperationContext _opCtxPtr = cc().makeOperationContext();
    OperationContext& _opCtx = *_opCtxPtr;

private:
    IndexDescriptor* getIndex(Database* db, const BSONObj& obj) {
        Collection* collection = db->getCollection(&_opCtx, nss);
        std::vector<IndexDescriptor*> indexes;
        collection->getIndexCatalog()->findIndexesByKeyPattern(&_opCtx, obj, false, &indexes);
        ASSERT_LTE(indexes.size(), 1U);
        return indexes.size() == 0 ? nullptr : indexes[0];
    }

    DBDirectClient _client;
};

/**
 * Test dropping the collection while the
 * PlanExecutor is doing a collection scan.
 */
class DropCollScan : public PlanExecutorBase {
public:
    void run() {
        OldClientWriteContext ctx(&_opCtx, nss.ns());
        insert(BSON("_id" << 1));
        insert(BSON("_id" << 2));

        BSONObj filterObj = fromjson("{_id: {$gt: 0}}");

        Collection* coll = ctx.getCollection();
        auto exec = makeCollScanExec(coll, filterObj);

        BSONObj objOut;
        ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&objOut, NULL));
        ASSERT_EQUALS(1, objOut["_id"].numberInt());

        // After dropping the collection, the plan executor should be dead.
        dropCollection();
        ASSERT_EQUALS(PlanExecutor::DEAD, exec->getNext(&objOut, NULL));
    }
};

/**
 * Test dropping the collection while the PlanExecutor is doing an index scan.
 */
class DropIndexScan : public PlanExecutorBase {
public:
    void run() {
        OldClientWriteContext ctx(&_opCtx, nss.ns());
        insert(BSON("_id" << 1 << "a" << 6));
        insert(BSON("_id" << 2 << "a" << 7));
        insert(BSON("_id" << 3 << "a" << 8));
        BSONObj indexSpec = BSON("a" << 1);
        addIndex(indexSpec);

        auto exec = makeIndexScanExec(ctx.db(), indexSpec, 7, 10);

        BSONObj objOut;
        ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&objOut, NULL));
        ASSERT_EQUALS(7, objOut["a"].numberInt());

        // After dropping the collection, the plan executor should be dead.
        dropCollection();
        ASSERT_EQUALS(PlanExecutor::DEAD, exec->getNext(&objOut, NULL));
    }
};

/**
 * Test dropping the collection while an agg PlanExecutor is doing an index scan.
 */
class DropIndexScanAgg : public PlanExecutorBase {
public:
    void run() {
        OldClientWriteContext ctx(&_opCtx, nss.ns());

        insert(BSON("_id" << 1 << "a" << 6));
        insert(BSON("_id" << 2 << "a" << 7));
        insert(BSON("_id" << 3 << "a" << 8));
        BSONObj indexSpec = BSON("a" << 1);
        addIndex(indexSpec);

        Collection* collection = ctx.getCollection();

        // Create the aggregation pipeline.
        std::vector<BSONObj> rawPipeline = {fromjson("{$match: {a: {$gte: 7, $lte: 10}}}")};
        boost::intrusive_ptr<ExpressionContextForTest> expCtx =
            new ExpressionContextForTest(&_opCtx, AggregationRequest(nss, rawPipeline));

        // Create an "inner" plan executor and register it with the cursor manager so that it can
        // get notified when the collection is dropped.
        unique_ptr<PlanExecutor, PlanExecutor::Deleter> innerExec(
            makeIndexScanExec(ctx.db(), indexSpec, 7, 10));

        // Wrap the "inner" plan executor in a DocumentSourceCursor and add it as the first source
        // in the pipeline.
        innerExec->saveState();
        auto cursorSource = DocumentSourceCursor::create(collection, std::move(innerExec), expCtx);
        auto pipeline = assertGet(Pipeline::create({cursorSource}, expCtx));

        // Create the output PlanExecutor that pulls results from the pipeline.
        auto ws = make_unique<WorkingSet>();
        auto proxy = make_unique<PipelineProxyStage>(&_opCtx, std::move(pipeline), ws.get());

        auto statusWithPlanExecutor = PlanExecutor::make(
            &_opCtx, std::move(ws), std::move(proxy), collection, PlanExecutor::NO_YIELD);
        ASSERT_OK(statusWithPlanExecutor.getStatus());
        auto outerExec = std::move(statusWithPlanExecutor.getValue());

        dropCollection();

        // Verify that the aggregation pipeline returns an error because its "inner" plan executor
        // has been killed due to the collection being dropped.
        BSONObj objOut;
        ASSERT_THROWS_CODE(
            outerExec->getNext(&objOut, nullptr), UserException, ErrorCodes::QueryPlanKilled);
    }
};

class SnapshotBase : public PlanExecutorBase {
protected:
    void setupCollection() {
        insert(BSON("_id" << 1 << "a" << 1));
        insert(BSON("_id" << 2 << "a" << 2 << "payload"
                          << "x"));
        insert(BSON("_id" << 3 << "a" << 3));
        insert(BSON("_id" << 4 << "a" << 4));
    }

    /**
     * Increases a document's size dramatically such that the document
     * exceeds the available padding and must be moved to the end of
     * the collection.
     */
    void forceDocumentMove() {
        BSONObj query = BSON("_id" << 2);
        BSONObj updateSpec = BSON("$set" << BSON("payload" << payload8k()));
        update(query, updateSpec);
    }

    std::string payload8k() {
        return std::string(8 * 1024, 'x');
    }

    /**
     * Given an array of ints, 'expectedIds', and a PlanExecutor,
     * 'exec', uses the executor to iterate through the collection. While
     * iterating, asserts that the _id of each successive document equals
     * the respective integer in 'expectedIds'.
     */
    void checkIds(int* expectedIds, PlanExecutor* exec) {
        BSONObj objOut;
        int idcount = 0;
        PlanExecutor::ExecState state;
        while (PlanExecutor::ADVANCED == (state = exec->getNext(&objOut, NULL))) {
            ASSERT_EQUALS(expectedIds[idcount], objOut["_id"].numberInt());
            ++idcount;
        }

        ASSERT_EQUALS(PlanExecutor::IS_EOF, state);
    }
};

/**
 * Create a scenario in which the same document is returned
 * twice due to a concurrent document move and collection
 * scan.
 */
class SnapshotControl : public SnapshotBase {
public:
    void run() {
        OldClientWriteContext ctx(&_opCtx, nss.ns());
        setupCollection();

        BSONObj filterObj = fromjson("{a: {$gte: 2}}");

        Collection* coll = ctx.getCollection();
        auto exec = makeCollScanExec(coll, filterObj);

        BSONObj objOut;
        ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&objOut, NULL));
        ASSERT_EQUALS(2, objOut["a"].numberInt());

        forceDocumentMove();

        int ids[] = {3, 4, 2};
        checkIds(ids, exec.get());
    }
};

/**
 * A snapshot is really just a hint that means scan the _id index.
 * Make sure that we do not see the document move with an _id
 * index scan.
 */
class SnapshotTest : public SnapshotBase {
public:
    void run() {
        OldClientWriteContext ctx(&_opCtx, nss.ns());
        setupCollection();
        BSONObj indexSpec = BSON("_id" << 1);
        addIndex(indexSpec);

        BSONObj filterObj = fromjson("{a: {$gte: 2}}");
        auto exec = makeIndexScanExec(ctx.db(), indexSpec, 2, 5);

        BSONObj objOut;
        ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&objOut, NULL));
        ASSERT_EQUALS(2, objOut["a"].numberInt());

        forceDocumentMove();

        // Since this time we're scanning the _id index,
        // we should not see the moved document again.
        int ids[] = {3, 4};
        checkIds(ids, exec.get());
    }
};

namespace ClientCursor {

using mongo::ClientCursor;

/**
 * Tests that invalidating a cursor without dropping the collection while the cursor is not in use
 * will keep the cursor registered. After being invalidated, pinning the cursor should take
 * ownership of the cursor and calling getNext() on its PlanExecutor should return an error
 * including the error message.
 */
class Invalidate : public PlanExecutorBase {
public:
    void run() {
        {
            AutoGetCollection autoColl(&_opCtx, nss, MODE_IX);
            insert(BSON("a" << 1 << "b" << 1));
        }

        CursorId cursorId;
        Collection* coll;
        {
            AutoGetCollectionForRead autoColl(&_opCtx, nss);
            BSONObj filterObj = fromjson("{_id: {$gt: 0}, b: {$gt: 0}}");
            coll = autoColl.getCollection();
            auto exec = makeCollScanExec(coll, filterObj);

            // Make a client cursor from the plan executor.
            auto cursorPin = coll->getCursorManager()->registerCursor(
                &_opCtx, {std::move(exec), nss, {}, false, BSONObj()});

            cursorId = cursorPin.getCursor()->cursorid();
            cursorPin.release();
        }

        ASSERT_EQUALS(1U, numCursors());
        {
            // The collection must be locked exclusively in order to call invalidateAll().
            AutoGetCollection autoColl(&_opCtx, nss, MODE_IX, MODE_X);
            auto invalidateReason = "Invalidate Test";
            const bool collectionGoingAway = false;
            coll->getCursorManager()->invalidateAll(&_opCtx, collectionGoingAway, invalidateReason);
        }
        // Since the collection is not going away, the cursor should remain open, but be killed.
        ASSERT_EQUALS(1U, numCursors());

        // Pinning a killed cursor should result in an error and clean up the cursor.
        {
            AutoGetCollectionForRead autoColl(&_opCtx, nss);
            ASSERT_EQ(ErrorCodes::QueryPlanKilled,
                      coll->getCursorManager()->pinCursor(&_opCtx, cursorId).getStatus());
        }
        ASSERT_EQUALS(0U, numCursors());
    }
};

/**
 * Tests that invalidating a cursor and dropping the collection while the cursor is not in use will
 * not keep the cursor registered.
 */
class InvalidateWithDrop : public PlanExecutorBase {
public:
    void run() {
        {
            AutoGetCollection autoColl(&_opCtx, nss, MODE_IX);
            insert(BSON("a" << 1 << "b" << 1));
        }

        CursorId cursorId;
        Collection* coll;
        {
            AutoGetCollectionForRead autoColl(&_opCtx, nss);
            BSONObj filterObj = fromjson("{_id: {$gt: 0}, b: {$gt: 0}}");
            coll = autoColl.getCollection();
            auto exec = makeCollScanExec(coll, filterObj);

            // Make a client cursor from the plan executor.
            auto cursorPin = coll->getCursorManager()->registerCursor(
                &_opCtx, {std::move(exec), nss, {}, false, BSONObj()});

            cursorId = cursorPin.getCursor()->cursorid();
            cursorPin.release();
        }

        ASSERT_EQUALS(1U, numCursors());
        {
            // The collection must be locked exclusively in order to call invalidateAll().
            AutoGetCollection autoColl(&_opCtx, nss, MODE_IX, MODE_X);
            auto invalidateReason = "Invalidate Test";
            const bool collectionGoingAway = true;
            coll->getCursorManager()->invalidateAll(&_opCtx, collectionGoingAway, invalidateReason);
        }

        // Since the collection is going away, the cursor should not remain open.
        {
            AutoGetCollectionForRead autoColl(&_opCtx, nss);
            ASSERT_EQ(ErrorCodes::CursorNotFound,
                      coll->getCursorManager()->pinCursor(&_opCtx, cursorId).getStatus());
        }
        ASSERT_EQUALS(0U, numCursors());
    }
};

/**
 * Tests that invalidating a cursor while it is in use will deregister it from the cursor manager,
 * transferring ownership to the pinned cursor.
 */
class InvalidatePinned : public PlanExecutorBase {
public:
    void run() {
        {
            AutoGetCollection autoColl(&_opCtx, nss, MODE_IX);
            insert(BSON("a" << 1 << "b" << 1));
        }

        boost::optional<AutoGetCollection> writeLock;
        boost::optional<AutoGetCollectionForRead> readLock;

        readLock.emplace(&_opCtx, nss);
        Collection* collection = readLock->getCollection();
        BSONObj filterObj = fromjson("{_id: {$gt: 0}, b: {$gt: 0}}");
        auto exec = makeCollScanExec(collection, filterObj);

        // Make a client cursor from the plan executor.
        auto ccPin = collection->getCursorManager()->registerCursor(
            &_opCtx, {std::move(exec), nss, {}, false, BSONObj()});

        // If the cursor is pinned, it sticks around, even after invalidation.
        readLock.reset();
        ASSERT_EQUALS(1U, numCursors());
        // The collection must be locked exclusively in order to call invalidateAll().
        writeLock.emplace(&_opCtx, nss, MODE_IX, MODE_X);
        const std::string invalidateReason("InvalidatePinned Test");
        collection->getCursorManager()->invalidateAll(&_opCtx, false, invalidateReason);
        writeLock.reset();
        readLock.emplace(&_opCtx, nss);
        ASSERT_EQUALS(0U, numCursors());

        // The invalidation should have killed the plan executor.
        BSONObj objOut;
        ASSERT_EQUALS(PlanExecutor::DEAD, ccPin.getCursor()->getExecutor()->getNext(&objOut, NULL));
        ASSERT(WorkingSetCommon::isValidStatusMemberObject(objOut));
        const Status status = WorkingSetCommon::getMemberObjectStatus(objOut);
        ASSERT(status.reason().find(invalidateReason) != string::npos);

        // Deleting the underlying cursor should cause the
        // number of cursors to return to 0.
        ccPin.deleteUnderlying();
        readLock.reset();
        ASSERT_EQUALS(0U, numCursors());
    }
};

/**
 * Test that client cursors time out and get deleted.
 */
class ShouldTimeout : public PlanExecutorBase {
public:
    void run() {
        {
            AutoGetCollection autoColl(&_opCtx, nss, MODE_IX);
            insert(BSON("a" << 1 << "b" << 1));
        }

        {
            AutoGetCollectionForReadCommand ctx(&_opCtx, nss);
            Collection* collection = ctx.getCollection();

            BSONObj filterObj = fromjson("{_id: {$gt: 0}, b: {$gt: 0}}");
            auto exec = makeCollScanExec(collection, filterObj);

            // Make a client cursor from the plan executor.
            collection->getCursorManager()->registerCursor(
                &_opCtx, {std::move(exec), nss, {}, false, BSONObj()});
        }

        // There should be one cursor before timeout,
        // and zero cursors after timeout.
        ASSERT_EQUALS(1U, numCursors());
        CursorManager::timeoutCursorsGlobal(&_opCtx, 600001);
        ASSERT_EQUALS(0U, numCursors());
    }
};

/**
 * Test that client cursors which have been marked as killed time out and get deleted.
 */
class KilledCursorsShouldTimeout : public PlanExecutorBase {
public:
    void run() {
        {
            OldClientWriteContext ctx(&_opCtx, nss.ns());
            insert(BSON("a" << 1 << "b" << 1));
        }

        {
            AutoGetCollectionForReadCommand ctx(&_opCtx, nss);
            Collection* collection = ctx.getCollection();

            BSONObj filterObj = fromjson("{_id: {$gt: 0}, b: {$gt: 0}}");
            auto exec = makeCollScanExec(collection, filterObj);

            // Make a client cursor from the plan executor, and immediately kill it.
            auto* cursorManager = collection->getCursorManager();
            cursorManager->registerCursor(&_opCtx, {std::move(exec), nss, {}, false, BSONObj()});
        }

        {
            // The collection must be locked exclusively in order to call invalidateAll().
            AutoGetCollection autoColl(&_opCtx, nss, MODE_IX, MODE_X);
            auto* cursorManager = autoColl.getCollection()->getCursorManager();
            const bool collectionGoingAway = false;
            cursorManager->invalidateAll(
                &_opCtx, collectionGoingAway, "KilledCursorsShouldTimeoutTest");
        }

        // There should be one cursor before timeout,
        // and zero cursors after timeout.
        ASSERT_EQUALS(1U, numCursors());
        CursorManager::timeoutCursorsGlobal(&_opCtx, 600001);
        ASSERT_EQUALS(0U, numCursors());
    }
};

}  // namespace ClientCursor

class All : public Suite {
public:
    All() : Suite("query_plan_executor") {}

    void setupTests() {
        add<DropCollScan>();
        add<DropIndexScan>();
        add<DropIndexScanAgg>();
        add<SnapshotControl>();
        add<SnapshotTest>();
        add<ClientCursor::Invalidate>();
        add<ClientCursor::InvalidateWithDrop>();
        add<ClientCursor::InvalidatePinned>();
        add<ClientCursor::ShouldTimeout>();
        add<ClientCursor::KilledCursorsShouldTimeout>();
    }
};

SuiteInstance<All> queryPlanExecutorAll;

}  // namespace QueryPlanExecutor
