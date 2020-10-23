/**
 *    Copyright 2015 (C) MongoDB Inc.
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

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "mongo/db/catalog/collection.h"
#include "mongo/db/catalog/collection_catalog_entry.h"
#include "mongo/db/catalog/collection_options.h"
#include "mongo/db/catalog/database.h"
#include "mongo/db/catalog/database_holder.h"
#include "mongo/db/catalog/document_validation.h"
#include "mongo/db/client.h"
#include "mongo/db/concurrency/d_concurrency.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/curop.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/query/internal_plans.h"
#include "mongo/db/repl/bgsync.h"
#include "mongo/db/repl/drop_pending_collection_reaper.h"
#include "mongo/db/repl/idempotency_test_fixture.h"
#include "mongo/db/repl/oplog.h"
#include "mongo/db/repl/oplog_interface_local.h"
#include "mongo/db/repl/replication_consistency_markers_mock.h"
#include "mongo/db/repl/replication_coordinator_global.h"
#include "mongo/db/repl/replication_coordinator_mock.h"
#include "mongo/db/repl/replication_process.h"
#include "mongo/db/repl/storage_interface.h"
#include "mongo/db/repl/storage_interface_mock.h"
#include "mongo/db/repl/sync_tail.h"
#include "mongo/db/service_context.h"
#include "mongo/db/service_context_d_test_fixture.h"
#include "mongo/db/session_catalog.h"
#include "mongo/stdx/mutex.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/concurrency/old_thread_pool.h"
#include "mongo/util/md5.hpp"
#include "mongo/util/scopeguard.h"
#include "mongo/util/string_map.h"

namespace mongo {
namespace repl {
namespace {

/**
 * Testing-only SyncTail that returns user-provided "document" for getMissingDoc().
 */
class SyncTailWithLocalDocumentFetcher : public SyncTail {
public:
    SyncTailWithLocalDocumentFetcher(const BSONObj& document);
    BSONObj getMissingDoc(OperationContext* opCtx, const BSONObj& o) override;

private:
    BSONObj _document;
};

/**
 * Testing-only SyncTail that checks the operation context in fetchAndInsertMissingDocument().
 */
class SyncTailWithOperationContextChecker : public SyncTail {
public:
    SyncTailWithOperationContextChecker();
    bool fetchAndInsertMissingDocument(OperationContext* opCtx, const BSONObj& o) override;
};

SyncTailWithLocalDocumentFetcher::SyncTailWithLocalDocumentFetcher(const BSONObj& document)
    : SyncTail(nullptr, SyncTail::MultiSyncApplyFunc(), nullptr), _document(document) {}

BSONObj SyncTailWithLocalDocumentFetcher::getMissingDoc(OperationContext*, const BSONObj&) {
    return _document;
}

SyncTailWithOperationContextChecker::SyncTailWithOperationContextChecker()
    : SyncTail(nullptr, SyncTail::MultiSyncApplyFunc(), nullptr) {}

bool SyncTailWithOperationContextChecker::fetchAndInsertMissingDocument(OperationContext* opCtx,
                                                                        const BSONObj&) {
    ASSERT_FALSE(opCtx->writesAreReplicated());
    ASSERT_FALSE(opCtx->lockState()->shouldConflictWithSecondaryBatchApplication());
    ASSERT_TRUE(documentValidationDisabled(opCtx));
    return false;
}

/**
 * Creates collection options suitable for oplog.
 */
CollectionOptions createOplogCollectionOptions() {
    CollectionOptions options;
    options.capped = true;
    options.cappedSize = 64 * 1024 * 1024LL;
    options.autoIndexId = CollectionOptions::NO;
    return options;
}

/**
 * Create test collection.
 * Returns collection.
 */
void createCollection(OperationContext* opCtx,
                      const NamespaceString& nss,
                      const CollectionOptions& options) {
    writeConflictRetry(opCtx, "createCollection", nss.ns(), [&] {
        Lock::DBLock dblk(opCtx, nss.db(), MODE_X);
        OldClientContext ctx(opCtx, nss.ns());
        auto db = ctx.db();
        ASSERT_TRUE(db);
        mongo::WriteUnitOfWork wuow(opCtx);
        auto coll = db->createCollection(opCtx, nss.ns(), options);
        ASSERT_TRUE(coll);
        wuow.commit();
    });
}

TEST_F(SyncTailTest, SyncApplyNoNamespaceBadOp) {
    const BSONObj op = BSON("op"
                            << "x");
    ASSERT_EQUALS(ErrorCodes::BadValue,
                  SyncTail::syncApply(_opCtx.get(), op, false, _applyOp, _applyCmd, _incOps));
    ASSERT_EQUALS(0U, _opsApplied);
}

TEST_F(SyncTailTest, SyncApplyNoNamespaceNoOp) {
    ASSERT_OK(SyncTail::syncApply(_opCtx.get(),
                                  BSON("op"
                                       << "n"),
                                  false));
    ASSERT_EQUALS(0U, _opsApplied);
}

TEST_F(SyncTailTest, SyncApplyBadOp) {
    const BSONObj op = BSON("op"
                            << "x"
                            << "ns"
                            << "test.t");
    ASSERT_EQUALS(
        ErrorCodes::BadValue,
        SyncTail::syncApply(_opCtx.get(), op, false, _applyOp, _applyCmd, _incOps).code());
    ASSERT_EQUALS(0U, _opsApplied);
}

TEST_F(SyncTailTest, SyncApplyNoOp) {
    const BSONObj op = BSON("op"
                            << "n"
                            << "ns"
                            << "test.t");
    bool applyOpCalled = false;
    SyncTail::ApplyOperationInLockFn applyOp = [&](OperationContext* opCtx,
                                                   Database* db,
                                                   const BSONObj& theOperation,
                                                   bool inSteadyStateReplication,
                                                   stdx::function<void()>) {
        applyOpCalled = true;
        ASSERT_TRUE(opCtx);
        ASSERT_TRUE(opCtx->lockState()->isDbLockedForMode("test", MODE_X));
        ASSERT_FALSE(opCtx->writesAreReplicated());
        ASSERT_TRUE(documentValidationDisabled(opCtx));
        ASSERT_TRUE(db);
        ASSERT_BSONOBJ_EQ(op, theOperation);
        ASSERT_FALSE(inSteadyStateReplication);
        return Status::OK();
    };
    ASSERT_TRUE(_opCtx->writesAreReplicated());
    ASSERT_FALSE(documentValidationDisabled(_opCtx.get()));
    ASSERT_OK(SyncTail::syncApply(_opCtx.get(), op, false, applyOp, failedApplyCommand, _incOps));
    ASSERT_TRUE(applyOpCalled);
}

TEST_F(SyncTailTest, SyncApplyNoOpApplyOpThrowsException) {
    const BSONObj op = BSON("op"
                            << "n"
                            << "ns"
                            << "test.t");
    int applyOpCalled = 0;
    SyncTail::ApplyOperationInLockFn applyOp = [&](OperationContext* opCtx,
                                                   Database* db,
                                                   const BSONObj& theOperation,
                                                   bool inSteadyStateReplication,
                                                   stdx::function<void()>) {
        applyOpCalled++;
        if (applyOpCalled < 5) {
            throw WriteConflictException();
        }
        return Status::OK();
    };
    ASSERT_OK(SyncTail::syncApply(_opCtx.get(), op, false, applyOp, failedApplyCommand, _incOps));
    ASSERT_EQUALS(5, applyOpCalled);
}

TEST_F(SyncTailTest, SyncApplyInsertDocumentDatabaseMissing) {
    ASSERT_THROWS_CODE(_testSyncApplyInsertDocument(ErrorCodes::OK),
                       AssertionException,
                       ErrorCodes::NamespaceNotFound);
}

TEST_F(SyncTailTest, SyncApplyInsertDocumentCollectionMissing) {
    {
        Lock::GlobalWrite globalLock(_opCtx.get());
        bool justCreated = false;
        Database* db = dbHolder().openDb(_opCtx.get(), "test", &justCreated);
        ASSERT_TRUE(db);
        ASSERT_TRUE(justCreated);
    }
    // Even though the collection doesn't exist, this is handled in the actual application function,
    // which in the case of this test just ignores such errors. This tests mostly that we don't
    // implicitly create the collection and lock the database in MODE_X.
    _testSyncApplyInsertDocument(ErrorCodes::OK);
}

TEST_F(SyncTailTest, SyncApplyInsertDocumentCollectionExists) {
    {
        Lock::GlobalWrite globalLock(_opCtx.get());
        bool justCreated = false;
        Database* db = dbHolder().openDb(_opCtx.get(), "test", &justCreated);
        ASSERT_TRUE(db);
        ASSERT_TRUE(justCreated);
        Collection* collection = db->createCollection(_opCtx.get(), "test.t");
        ASSERT_TRUE(collection);
    }
    _testSyncApplyInsertDocument(ErrorCodes::OK);
}

TEST_F(SyncTailTest, SyncApplyInsertDocumentCollectionLockedByUUID) {
    CollectionOptions options;
    options.uuid = UUID::gen();
    {
        Lock::GlobalWrite globalLock(_opCtx.get());
        bool justCreated;
        Database* db = dbHolder().openDb(_opCtx.get(), "test", &justCreated);
        ASSERT_TRUE(db);
        ASSERT_TRUE(justCreated);
        Collection* collection = db->createCollection(_opCtx.get(), "test.t", options);
        ASSERT_TRUE(collection);
    }

    // Test that the collection to lock is determined by the UUID and not the 'ns' field.
    const BSONObj op = BSON("op"
                            << "i"
                            << "ns"
                            << "test.othername"
                            << "ui"
                            << options.uuid.get());
    _testSyncApplyInsertDocument(ErrorCodes::OK, &op);
}

TEST_F(SyncTailTest, SyncApplyIndexBuild) {
    const BSONObj op = BSON("op"
                            << "i"
                            << "ns"
                            << "test.system.indexes");
    bool applyOpCalled = false;
    SyncTail::ApplyOperationInLockFn applyOp = [&](OperationContext* opCtx,
                                                   Database* db,
                                                   const BSONObj& theOperation,
                                                   bool inSteadyStateReplication,
                                                   stdx::function<void()>) {
        applyOpCalled = true;
        ASSERT_TRUE(opCtx);
        ASSERT_TRUE(opCtx->lockState()->isDbLockedForMode("test", MODE_X));
        ASSERT_FALSE(opCtx->writesAreReplicated());
        ASSERT_TRUE(documentValidationDisabled(opCtx));
        ASSERT_TRUE(db);
        ASSERT_BSONOBJ_EQ(op, theOperation);
        ASSERT_FALSE(inSteadyStateReplication);
        return Status::OK();
    };
    ASSERT_TRUE(_opCtx->writesAreReplicated());
    ASSERT_FALSE(documentValidationDisabled(_opCtx.get()));
    ASSERT_OK(SyncTail::syncApply(_opCtx.get(), op, false, applyOp, failedApplyCommand, _incOps));
    ASSERT_TRUE(applyOpCalled);
}

TEST_F(SyncTailTest, SyncApplyCommand) {
    const BSONObj op = BSON("op"
                            << "c"
                            << "ns"
                            << "test.t");
    bool applyCmdCalled = false;
    SyncTail::ApplyOperationInLockFn applyOp = [&](OperationContext* opCtx,
                                                   Database* db,
                                                   const BSONObj& theOperation,
                                                   bool inSteadyStateReplication,
                                                   stdx::function<void()>) {
        FAIL("applyOperation unexpectedly invoked.");
        return Status::OK();
    };
    SyncTail::ApplyCommandInLockFn applyCmd =
        [&](OperationContext* opCtx, const BSONObj& theOperation, bool inSteadyStateReplication) {
            applyCmdCalled = true;
            ASSERT_TRUE(opCtx);
            ASSERT_TRUE(opCtx->lockState()->isW());
            ASSERT_TRUE(opCtx->writesAreReplicated());
            ASSERT_FALSE(documentValidationDisabled(opCtx));
            ASSERT_BSONOBJ_EQ(op, theOperation);
            return Status::OK();
        };
    ASSERT_TRUE(_opCtx->writesAreReplicated());
    ASSERT_FALSE(documentValidationDisabled(_opCtx.get()));
    ASSERT_OK(SyncTail::syncApply(_opCtx.get(), op, false, applyOp, applyCmd, _incOps));
    ASSERT_TRUE(applyCmdCalled);
    ASSERT_EQUALS(1U, _opsApplied);
}

TEST_F(SyncTailTest, SyncApplyCommandThrowsException) {
    const BSONObj op = BSON("op"
                            << "c"
                            << "ns"
                            << "test.t");
    int applyCmdCalled = 0;
    SyncTail::ApplyOperationInLockFn applyOp = [&](OperationContext* opCtx,
                                                   Database* db,
                                                   const BSONObj& theOperation,
                                                   bool inSteadyStateReplication,
                                                   stdx::function<void()>) {
        FAIL("applyOperation unexpectedly invoked.");
        return Status::OK();
    };
    SyncTail::ApplyCommandInLockFn applyCmd =
        [&](OperationContext* opCtx, const BSONObj& theOperation, bool inSteadyStateReplication) {
            applyCmdCalled++;
            if (applyCmdCalled < 5) {
                throw WriteConflictException();
            }
            return Status::OK();
        };
    ASSERT_OK(SyncTail::syncApply(_opCtx.get(), op, false, applyOp, applyCmd, _incOps));
    ASSERT_EQUALS(5, applyCmdCalled);
    ASSERT_EQUALS(1U, _opsApplied);
}

TEST_F(SyncTailTest, MultiApplyReturnsBadValueOnNullOperationContext) {
    auto writerPool = SyncTail::makeWriterPool();
    auto op = makeCreateCollectionOplogEntry({Timestamp(Seconds(1), 0), 1LL});
    auto status = multiApply(nullptr, writerPool.get(), {op}, noopApplyOperationFn).getStatus();
    ASSERT_EQUALS(ErrorCodes::BadValue, status);
    ASSERT_STRING_CONTAINS(status.reason(), "invalid operation context");
}

TEST_F(SyncTailTest, MultiApplyReturnsBadValueOnNullWriterPool) {
    auto op = makeCreateCollectionOplogEntry({Timestamp(Seconds(1), 0), 1LL});
    auto status = multiApply(_opCtx.get(), nullptr, {op}, noopApplyOperationFn).getStatus();
    ASSERT_EQUALS(ErrorCodes::BadValue, status);
    ASSERT_STRING_CONTAINS(status.reason(), "invalid worker pool");
}

TEST_F(SyncTailTest, MultiApplyReturnsEmptyArrayOperationWhenNoOperationsAreGiven) {
    auto writerPool = SyncTail::makeWriterPool();
    auto status = multiApply(_opCtx.get(), writerPool.get(), {}, noopApplyOperationFn).getStatus();
    ASSERT_EQUALS(ErrorCodes::EmptyArrayOperation, status);
    ASSERT_STRING_CONTAINS(status.reason(), "no operations provided to multiApply");
}

TEST_F(SyncTailTest, MultiApplyReturnsBadValueOnNullApplyOperation) {
    auto writerPool = SyncTail::makeWriterPool();
    MultiApplier::ApplyOperationFn nullApplyOperationFn;
    auto op = makeCreateCollectionOplogEntry({Timestamp(Seconds(1), 0), 1LL});
    auto status =
        multiApply(_opCtx.get(), writerPool.get(), {op}, nullApplyOperationFn).getStatus();
    ASSERT_EQUALS(ErrorCodes::BadValue, status);
    ASSERT_STRING_CONTAINS(status.reason(), "invalid apply operation function");
}

bool _testOplogEntryIsForCappedCollection(OperationContext* opCtx,
                                          const NamespaceString& nss,
                                          const CollectionOptions& options) {
    auto writerPool = SyncTail::makeWriterPool();
    MultiApplier::Operations operationsApplied;
    auto applyOperationFn =
        [&operationsApplied](MultiApplier::OperationPtrs* operationsToApply) -> Status {
        for (auto&& opPtr : *operationsToApply) {
            operationsApplied.push_back(*opPtr);
        }
        return Status::OK();
    };
    createCollection(opCtx, nss, options);

    auto op = makeInsertDocumentOplogEntry({Timestamp(Seconds(1), 0), 1LL}, nss, BSON("a" << 1));
    ASSERT_FALSE(op.isForCappedCollection);

    auto lastOpTime =
        unittest::assertGet(multiApply(opCtx, writerPool.get(), {op}, applyOperationFn));
    ASSERT_EQUALS(op.getOpTime(), lastOpTime);

    ASSERT_EQUALS(1U, operationsApplied.size());
    const auto& opApplied = operationsApplied.front();
    ASSERT_EQUALS(op, opApplied);
    // "isForCappedCollection" is not parsed from raw oplog entry document.
    return opApplied.isForCappedCollection;
}

TEST_F(
    SyncTailTest,
    MultiApplyDoesNotSetOplogEntryIsForCappedCollectionWhenProcessingNonCappedCollectionInsertOperation) {
    NamespaceString nss("local." + _agent.getSuiteName() + "_" + _agent.getTestName());
    ASSERT_FALSE(_testOplogEntryIsForCappedCollection(_opCtx.get(), nss, CollectionOptions()));
}

TEST_F(SyncTailTest,
       MultiApplySetsOplogEntryIsForCappedCollectionWhenProcessingCappedCollectionInsertOperation) {
    NamespaceString nss("local." + _agent.getSuiteName() + "_" + _agent.getTestName());
    ASSERT_TRUE(
        _testOplogEntryIsForCappedCollection(_opCtx.get(), nss, createOplogCollectionOptions()));
}

TEST_F(SyncTailTest, MultiApplyAssignsOperationsToWriterThreadsBasedOnNamespaceHash) {
    // This test relies on implementation details of how multiApply uses hashing to distribute ops
    // to threads. It is possible for this test to fail, even if the implementation of multiApply is
    // correct. If it fails, consider adjusting the namespace names (to adjust the hash values) or
    // the number of threads in the pool.
    NamespaceString nss1("test.t0");
    NamespaceString nss2("test.t1");
    OldThreadPool writerPool(2);

    stdx::mutex mutex;
    std::vector<MultiApplier::Operations> operationsApplied;
    auto applyOperationFn = [&mutex, &operationsApplied](
        MultiApplier::OperationPtrs* operationsForWriterThreadToApply) -> Status {
        stdx::lock_guard<stdx::mutex> lock(mutex);
        operationsApplied.emplace_back();
        for (auto&& opPtr : *operationsForWriterThreadToApply) {
            operationsApplied.back().push_back(*opPtr);
        }
        return Status::OK();
    };

    auto op1 = makeInsertDocumentOplogEntry({Timestamp(Seconds(1), 0), 1LL}, nss1, BSON("x" << 1));
    auto op2 = makeInsertDocumentOplogEntry({Timestamp(Seconds(2), 0), 1LL}, nss2, BSON("x" << 2));

    NamespaceString nssForInsert;
    std::vector<InsertStatement> operationsWrittenToOplog;
    _storageInterface->insertDocumentsFn = [&mutex, &nssForInsert, &operationsWrittenToOplog](
        OperationContext* opCtx,
        const NamespaceString& nss,
        const std::vector<InsertStatement>& docs) {
        stdx::lock_guard<stdx::mutex> lock(mutex);
        nssForInsert = nss;
        operationsWrittenToOplog = docs;
        return Status::OK();
    };

    auto lastOpTime =
        unittest::assertGet(multiApply(_opCtx.get(), &writerPool, {op1, op2}, applyOperationFn));
    ASSERT_EQUALS(op2.getOpTime(), lastOpTime);

    // Each writer thread should be given exactly one operation to apply.
    std::vector<OpTime> seen;
    {
        stdx::lock_guard<stdx::mutex> lock(mutex);
        ASSERT_EQUALS(operationsApplied.size(), 2U);
        for (auto&& operationsAppliedByThread : operationsApplied) {
            ASSERT_EQUALS(1U, operationsAppliedByThread.size());
            const auto& oplogEntry = operationsAppliedByThread.front();
            ASSERT_TRUE(std::find(seen.cbegin(), seen.cend(), oplogEntry.getOpTime()) ==
                        seen.cend());
            ASSERT_TRUE(oplogEntry == op1 || oplogEntry == op2);
            seen.push_back(oplogEntry.getOpTime());
        }
    }

    // Check ops in oplog.
    stdx::lock_guard<stdx::mutex> lock(mutex);
    ASSERT_EQUALS(2U, operationsWrittenToOplog.size());
    ASSERT_EQUALS(NamespaceString::kRsOplogNamespace, nssForInsert);
    ASSERT_BSONOBJ_EQ(op1.raw, operationsWrittenToOplog[0].doc);
    ASSERT_BSONOBJ_EQ(op2.raw, operationsWrittenToOplog[1].doc);
}

TEST_F(SyncTailTest, MultiApplyUpdatesTheTransactionTable) {
    // Set up the transactions collection, which can only be done by the primary.
    ASSERT_OK(ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_PRIMARY));
    SessionCatalog::create(_opCtx->getServiceContext());
    SessionCatalog::get(_opCtx->getServiceContext())->onStepUp(_opCtx.get());
    ON_BLOCK_EXIT([&] { SessionCatalog::reset_forTest(_opCtx->getServiceContext()); });
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_SECONDARY));

    // Entries with a session id and a txnNumber update the transaction table.
    auto lsidSingle = makeLogicalSessionIdForTest();
    auto opSingle = makeInsertDocumentOplogEntry(
        {Timestamp(Seconds(1), 0), 1LL}, NamespaceString("test.0"), BSON("x" << 1));
    appendSessionTransactionInfo(opSingle, lsidSingle, 5LL, 0);

    // For entries with the same session, the entry with a larger txnNumber is saved.
    auto lsidDiffTxn = makeLogicalSessionIdForTest();
    auto opDiffTxnSmaller = makeInsertDocumentOplogEntry(
        {Timestamp(Seconds(2), 0), 1LL}, NamespaceString("test.1"), BSON("x" << 0));
    appendSessionTransactionInfo(opDiffTxnSmaller, lsidDiffTxn, 10LL, 1);
    auto opDiffTxnLarger = makeInsertDocumentOplogEntry(
        {Timestamp(Seconds(3), 0), 1LL}, NamespaceString("test.1"), BSON("x" << 1));
    appendSessionTransactionInfo(opDiffTxnLarger, lsidDiffTxn, 20LL, 1);

    // For entries with the same session and txnNumber, the later optime is saved.
    auto lsidSameTxn = makeLogicalSessionIdForTest();
    auto opSameTxnLater = makeInsertDocumentOplogEntry(
        {Timestamp(Seconds(6), 0), 1LL}, NamespaceString("test.2"), BSON("x" << 0));
    appendSessionTransactionInfo(opSameTxnLater, lsidSameTxn, 30LL, 0);
    auto opSameTxnSooner = makeInsertDocumentOplogEntry(
        {Timestamp(Seconds(5), 0), 1LL}, NamespaceString("test.2"), BSON("x" << 1));
    appendSessionTransactionInfo(opSameTxnSooner, lsidSameTxn, 30LL, 1);

    // Entries with a session id but no txnNumber do not lead to updates.
    auto lsidNoTxn = makeLogicalSessionIdForTest();
    auto opNoTxn = makeInsertDocumentOplogEntry(
        {Timestamp(Seconds(7), 0), 1LL}, NamespaceString("test.3"), BSON("x" << 0));
    auto info = opNoTxn.getOperationSessionInfo();
    info.setSessionId(lsidNoTxn);
    opNoTxn.setOperationSessionInfo(info);

    // Apply the batch and verify the transaction collection was properly updated for each scenario.
    auto writerPool = SyncTail::makeWriterPool();
    ASSERT_OK(multiApply(
        _opCtx.get(),
        writerPool.get(),
        {opSingle, opDiffTxnSmaller, opDiffTxnLarger, opSameTxnLater, opSameTxnSooner, opNoTxn},
        noopApplyOperationFn));

    DBDirectClient client(_opCtx.get());

    // The txnNum and optime of the only write were saved.
    auto resultSingle =
        client.findOne(NamespaceString::kSessionTransactionsTableNamespace.ns(),
                       BSON(SessionTxnRecord::kSessionIdFieldName << lsidSingle.toBSON()));
    ASSERT_TRUE(!resultSingle.isEmpty());
    ASSERT_EQ(resultSingle[SessionTxnRecord::kTxnNumFieldName].numberLong(), 5LL);
    ASSERT_EQ(resultSingle[SessionTxnRecord::kLastWriteOpTimeTsFieldName].timestamp(),
              Timestamp(Seconds(1), 0));

    // The txnNum and optime of the write with the larger txnNum were saved.
    auto resultDiffTxn =
        client.findOne(NamespaceString::kSessionTransactionsTableNamespace.ns(),
                       BSON(SessionTxnRecord::kSessionIdFieldName << lsidDiffTxn.toBSON()));
    ASSERT_TRUE(!resultDiffTxn.isEmpty());
    ASSERT_EQ(resultDiffTxn[SessionTxnRecord::kTxnNumFieldName].numberLong(), 20LL);
    ASSERT_EQ(resultDiffTxn[SessionTxnRecord::kLastWriteOpTimeTsFieldName].timestamp(),
              Timestamp(Seconds(3), 0));

    // The txnNum and optime of the write with the later optime were saved.
    auto resultSameTxn =
        client.findOne(NamespaceString::kSessionTransactionsTableNamespace.ns(),
                       BSON(SessionTxnRecord::kSessionIdFieldName << lsidSameTxn.toBSON()));
    ASSERT_TRUE(!resultSameTxn.isEmpty());
    ASSERT_EQ(resultSameTxn[SessionTxnRecord::kTxnNumFieldName].numberLong(), 30LL);
    ASSERT_EQ(resultSameTxn[SessionTxnRecord::kLastWriteOpTimeTsFieldName].timestamp(),
              Timestamp(Seconds(6), 0));

    // There is no entry for the write with no txnNumber.
    auto resultNoTxn =
        client.findOne(NamespaceString::kSessionTransactionsTableNamespace.ns(),
                       BSON(SessionTxnRecord::kSessionIdFieldName << lsidNoTxn.toBSON()));
    ASSERT_TRUE(resultNoTxn.isEmpty());
}

TEST_F(SyncTailTest, MultiSyncApplyUsesSyncApplyToApplyOperation) {
    NamespaceString nss("local." + _agent.getSuiteName() + "_" + _agent.getTestName());
    auto op = makeCreateCollectionOplogEntry({Timestamp(Seconds(1), 0), 1LL}, nss);
    _opCtx.reset();

    MultiApplier::OperationPtrs ops = {&op};
    multiSyncApply(&ops, nullptr);
    // Collection should be created after SyncTail::syncApply() processes operation.
    _opCtx = cc().makeOperationContext();
    ASSERT_TRUE(AutoGetCollectionForReadCommand(_opCtx.get(), nss).getCollection());
}

TEST_F(SyncTailTest, MultiSyncApplyDisablesDocumentValidationWhileApplyingOperations) {
    NamespaceString nss("local." + _agent.getSuiteName() + "_" + _agent.getTestName());
    auto syncApply = [](OperationContext* opCtx, const BSONObj&, bool convertUpdatesToUpserts) {
        ASSERT_FALSE(opCtx->writesAreReplicated());
        ASSERT_FALSE(opCtx->lockState()->shouldConflictWithSecondaryBatchApplication());
        ASSERT_TRUE(documentValidationDisabled(opCtx));
        ASSERT_TRUE(convertUpdatesToUpserts);
        return Status::OK();
    };
    auto op = makeUpdateDocumentOplogEntry(
        {Timestamp(Seconds(1), 0), 1LL}, nss, BSON("_id" << 0), BSON("_id" << 0 << "x" << 2));
    MultiApplier::OperationPtrs ops = {&op};
    ASSERT_OK(multiSyncApply_noAbort(_opCtx.get(), &ops, syncApply));
}

TEST_F(SyncTailTest, MultiSyncApplyPassesThroughSyncApplyErrorAfterFailingToApplyOperation) {
    NamespaceString nss("local." + _agent.getSuiteName() + "_" + _agent.getTestName());
    OplogEntry op(OpTime(Timestamp(1, 1), 1), 1LL, OpTypeEnum::kDelete, nss, BSONObj());
    auto syncApply = [](OperationContext*, const BSONObj&, bool) -> Status {
        return {ErrorCodes::OperationFailed, ""};
    };
    MultiApplier::OperationPtrs ops = {&op};
    ASSERT_EQUALS(ErrorCodes::OperationFailed,
                  multiSyncApply_noAbort(_opCtx.get(), &ops, syncApply));
}

TEST_F(SyncTailTest, MultiSyncApplyPassesThroughSyncApplyException) {
    NamespaceString nss("local." + _agent.getSuiteName() + "_" + _agent.getTestName());
    OplogEntry op(OpTime(Timestamp(1, 1), 1), 1LL, OpTypeEnum::kDelete, nss, BSONObj());
    auto syncApply = [](OperationContext*, const BSONObj&, bool) -> Status {
        uasserted(ErrorCodes::OperationFailed, "");
        MONGO_UNREACHABLE;
    };
    MultiApplier::OperationPtrs ops = {&op};
    ASSERT_EQUALS(ErrorCodes::OperationFailed,
                  multiSyncApply_noAbort(_opCtx.get(), &ops, syncApply));
}

TEST_F(SyncTailTest, MultiSyncApplySortsOperationsStablyByNamespaceBeforeApplying) {
    int x = 0;
    auto makeOp = [&x](const char* ns) -> OplogEntry {
        return OplogEntry(
            OpTime(Timestamp(1, 1), 1), 1LL, OpTypeEnum::kDelete, NamespaceString(ns), BSONObj());
    };
    auto op1 = makeOp("test.t1");
    auto op2 = makeOp("test.t1");
    auto op3 = makeOp("test.t2");
    auto op4 = makeOp("test.t3");
    MultiApplier::Operations operationsApplied;
    auto syncApply = [&operationsApplied](OperationContext*, const BSONObj& op, bool) {
        operationsApplied.push_back(OplogEntry(op));
        return Status::OK();
    };
    MultiApplier::OperationPtrs ops = {&op4, &op1, &op3, &op2};
    ASSERT_OK(multiSyncApply_noAbort(_opCtx.get(), &ops, syncApply));
    ASSERT_EQUALS(4U, operationsApplied.size());
    ASSERT_EQUALS(op1, operationsApplied[0]);
    ASSERT_EQUALS(op2, operationsApplied[1]);
    ASSERT_EQUALS(op3, operationsApplied[2]);
    ASSERT_EQUALS(op4, operationsApplied[3]);
}

TEST_F(SyncTailTest, MultiSyncApplyGroupsInsertOperationByNamespaceBeforeApplying) {
    int seconds = 0;
    auto makeOp = [&seconds](const NamespaceString& nss) {
        return makeInsertDocumentOplogEntry(
            {Timestamp(Seconds(seconds), 0), 1LL}, nss, BSON("_id" << seconds++));
    };
    NamespaceString nss1("test." + _agent.getSuiteName() + "_" + _agent.getTestName() + "_1");
    NamespaceString nss2("test." + _agent.getSuiteName() + "_" + _agent.getTestName() + "_2");
    auto createOp1 = makeCreateCollectionOplogEntry({Timestamp(Seconds(seconds++), 0), 1LL}, nss1);
    auto createOp2 = makeCreateCollectionOplogEntry({Timestamp(Seconds(seconds++), 0), 1LL}, nss2);
    auto insertOp1a = makeOp(nss1);
    auto insertOp1b = makeOp(nss1);
    auto insertOp2a = makeOp(nss2);
    auto insertOp2b = makeOp(nss2);
    std::vector<BSONObj> operationsApplied;
    auto syncApply = [&operationsApplied](OperationContext*, const BSONObj& op, bool) {
        operationsApplied.push_back(op.copy());
        return Status::OK();
    };

    MultiApplier::OperationPtrs ops = {
        &createOp1, &createOp2, &insertOp1a, &insertOp2a, &insertOp1b, &insertOp2b};
    ASSERT_OK(multiSyncApply_noAbort(_opCtx.get(), &ops, syncApply));

    ASSERT_EQUALS(4U, operationsApplied.size());
    ASSERT_BSONOBJ_EQ(createOp1.raw, operationsApplied[0]);
    ASSERT_BSONOBJ_EQ(createOp2.raw, operationsApplied[1]);

    // Check grouped insert operations in namespace "nss1".
    ASSERT_EQUALS(insertOp1a.getOpTime(), OpTime::parseFromOplogEntry(operationsApplied[2]));
    ASSERT_EQUALS(insertOp1a.getNamespace().ns(), operationsApplied[2]["ns"].valuestrsafe());
    ASSERT_EQUALS(BSONType::Array, operationsApplied[2]["o"].type());
    auto group1 = operationsApplied[2]["o"].Array();
    ASSERT_EQUALS(2U, group1.size());
    ASSERT_BSONOBJ_EQ(insertOp1a.getObject(), group1[0].Obj());
    ASSERT_BSONOBJ_EQ(insertOp1b.getObject(), group1[1].Obj());

    // Check grouped insert operations in namespace "nss2".
    ASSERT_EQUALS(insertOp2a.getOpTime(), OpTime::parseFromOplogEntry(operationsApplied[3]));
    ASSERT_EQUALS(insertOp2a.getNamespace().ns(), operationsApplied[3]["ns"].valuestrsafe());
    ASSERT_EQUALS(BSONType::Array, operationsApplied[3]["o"].type());
    auto group2 = operationsApplied[3]["o"].Array();
    ASSERT_EQUALS(2U, group2.size());
    ASSERT_BSONOBJ_EQ(insertOp2a.getObject(), group2[0].Obj());
    ASSERT_BSONOBJ_EQ(insertOp2b.getObject(), group2[1].Obj());
}

TEST_F(SyncTailTest, MultiSyncApplyLimitsBatchCountWhenGroupingInsertOperation) {
    int seconds = 0;
    auto makeOp = [&seconds](const NamespaceString& nss) {
        return makeInsertDocumentOplogEntry(
            {Timestamp(Seconds(seconds), 0), 1LL}, nss, BSON("_id" << seconds++));
    };
    NamespaceString nss("test." + _agent.getSuiteName() + "_" + _agent.getTestName() + "_1");
    auto createOp = makeCreateCollectionOplogEntry({Timestamp(Seconds(seconds++), 0), 1LL}, nss);

    // Generate operations to apply:
    // {create}, {insert_1}, {insert_2}, .. {insert_(limit)}, {insert_(limit+1)}
    std::size_t limit = 64;
    MultiApplier::Operations insertOps;
    for (std::size_t i = 0; i < limit + 1; ++i) {
        insertOps.push_back(makeOp(nss));
    }
    MultiApplier::Operations operationsToApply;
    operationsToApply.push_back(createOp);
    std::copy(insertOps.begin(), insertOps.end(), std::back_inserter(operationsToApply));
    std::vector<BSONObj> operationsApplied;
    auto syncApply = [&operationsApplied](OperationContext*, const BSONObj& op, bool) {
        operationsApplied.push_back(op.copy());
        return Status::OK();
    };

    MultiApplier::OperationPtrs ops;
    for (auto&& op : operationsToApply) {
        ops.push_back(&op);
    }
    ASSERT_OK(multiSyncApply_noAbort(_opCtx.get(), &ops, syncApply));

    // multiSyncApply should combine operations as follows:
    // {create}, {grouped_insert}, {insert_(limit+1)}
    ASSERT_EQUALS(3U, operationsApplied.size());
    ASSERT_BSONOBJ_EQ(createOp.raw, operationsApplied[0]);

    const auto& groupedInsertOp = operationsApplied[1];
    ASSERT_EQUALS(insertOps.front().getOpTime(), OpTime::parseFromOplogEntry(groupedInsertOp));
    ASSERT_EQUALS(insertOps.front().getNamespace().ns(), groupedInsertOp["ns"].valuestrsafe());
    ASSERT_EQUALS(BSONType::Array, groupedInsertOp["o"].type());
    auto groupedInsertDocuments = groupedInsertOp["o"].Array();
    ASSERT_EQUALS(limit, groupedInsertDocuments.size());
    for (std::size_t i = 0; i < limit; ++i) {
        const auto& insertOp = insertOps[i];
        ASSERT_BSONOBJ_EQ(insertOp.getObject(), groupedInsertDocuments[i].Obj());
    }

    // (limit + 1)-th insert operations should not be included in group of first (limit) inserts.
    ASSERT_BSONOBJ_EQ(insertOps.back().raw, operationsApplied[2]);
}

// Create an 'insert' oplog operation of an approximate size in bytes. The '_id' of the oplog entry
// and its optime in seconds are given by the 'id' argument.
OplogEntry makeSizedInsertOp(const NamespaceString& nss, int size, int id) {
    return makeInsertDocumentOplogEntry({Timestamp(Seconds(id), 0), 1LL},
                                        nss,
                                        BSON("_id" << id << "data" << std::string(size, '*')));
};

TEST_F(SyncTailTest, MultiSyncApplyLimitsBatchSizeWhenGroupingInsertOperations) {

    int seconds = 0;
    NamespaceString nss("test." + _agent.getSuiteName() + "_" + _agent.getTestName());
    auto createOp = makeCreateCollectionOplogEntry({Timestamp(Seconds(seconds++), 0), 1LL}, nss);

    // Create a sequence of insert ops that are too large to fit in one group.
    int maxBatchSize = insertVectorMaxBytes;
    int opsPerBatch = 3;
    int opSize = maxBatchSize / opsPerBatch - 500;  // Leave some room for other oplog fields.

    // Create the insert ops.
    MultiApplier::Operations insertOps;
    int numOps = 4;
    for (int i = 0; i < numOps; i++) {
        insertOps.push_back(makeSizedInsertOp(nss, opSize, seconds++));
    }

    MultiApplier::Operations operationsToApply;
    operationsToApply.push_back(createOp);
    std::copy(insertOps.begin(), insertOps.end(), std::back_inserter(operationsToApply));

    MultiApplier::OperationPtrs ops;
    for (auto&& op : operationsToApply) {
        ops.push_back(&op);
    }

    std::vector<BSONObj> operationsApplied;
    auto syncApply = [&operationsApplied](OperationContext*, const BSONObj& op, bool) {
        operationsApplied.push_back(op.copy());
        return Status::OK();
    };

    // Apply the ops.
    ASSERT_OK(multiSyncApply_noAbort(_opCtx.get(), &ops, syncApply));

    // Applied ops should be as follows:
    // [ {create}, INSERT_GROUP{insert 1, insert 2, insert 3}, {insert 4} ]
    ASSERT_EQ(3U, operationsApplied.size());
    auto groupedInsertOp = operationsApplied[1];
    ASSERT_EQUALS(BSONType::Array, groupedInsertOp["o"].type());
    // Make sure the insert group was created correctly.
    for (int i = 0; i < opsPerBatch; ++i) {
        auto groupedInsertOpArray = groupedInsertOp["o"].Array();
        ASSERT_BSONOBJ_EQ(insertOps[i].getObject(), groupedInsertOpArray[i].Obj());
    }

    // Check that the last op was applied individually.
    ASSERT_BSONOBJ_EQ(insertOps[3].raw, operationsApplied[2]);
}

TEST_F(SyncTailTest, MultiSyncApplyAppliesOpIndividuallyWhenOpIndividuallyExceedsBatchSize) {

    int seconds = 0;
    NamespaceString nss("test." + _agent.getSuiteName() + "_" + _agent.getTestName());
    auto createOp = makeCreateCollectionOplogEntry({Timestamp(Seconds(seconds++), 0), 1LL}, nss);

    int maxBatchSize = insertVectorMaxBytes;
    // Create an insert op that exceeds the maximum batch size by itself.
    auto insertOpLarge = makeSizedInsertOp(nss, maxBatchSize, seconds++);
    auto insertOpSmall = makeSizedInsertOp(nss, 100, seconds++);

    MultiApplier::Operations operationsToApply = {createOp, insertOpLarge, insertOpSmall};

    MultiApplier::OperationPtrs ops;
    for (auto&& op : operationsToApply) {
        ops.push_back(&op);
    }

    std::vector<BSONObj> operationsApplied;
    auto syncApply = [&operationsApplied](OperationContext*, const BSONObj& op, bool) {
        operationsApplied.push_back(op.copy());
        return Status::OK();
    };

    // Apply the ops.
    ASSERT_OK(multiSyncApply_noAbort(_opCtx.get(), &ops, syncApply));

    // Applied ops should be as follows:
    // [ {create}, {large insert} {small insert} ]
    ASSERT_EQ(operationsToApply.size(), operationsApplied.size());
    ASSERT_BSONOBJ_EQ(createOp.raw, operationsApplied[0]);
    ASSERT_BSONOBJ_EQ(insertOpLarge.raw, operationsApplied[1]);
    ASSERT_BSONOBJ_EQ(insertOpSmall.raw, operationsApplied[2]);
}

TEST_F(SyncTailTest, MultiSyncApplyAppliesInsertOpsIndividuallyWhenUnableToCreateGroupByNamespace) {

    int seconds = 0;
    auto makeOp = [&seconds](const NamespaceString& nss) {
        return makeInsertDocumentOplogEntry(
            {Timestamp(Seconds(seconds), 0), 1LL}, nss, BSON("_id" << seconds++));
    };

    auto testNs = "test." + _agent.getSuiteName() + "_" + _agent.getTestName();

    // Create a sequence of 3 'insert' ops that can't be grouped because they are from different
    // namespaces.
    MultiApplier::Operations operationsToApply = {makeOp(NamespaceString(testNs + "_1")),
                                                  makeOp(NamespaceString(testNs + "_2")),
                                                  makeOp(NamespaceString(testNs + "_3"))};

    std::vector<BSONObj> operationsApplied;
    auto syncApply = [&operationsApplied](OperationContext*, const BSONObj& op, bool) {
        operationsApplied.push_back(op.copy());
        return Status::OK();
    };

    MultiApplier::OperationPtrs ops;
    for (auto&& op : operationsToApply) {
        ops.push_back(&op);
    }

    // Apply the ops.
    ASSERT_OK(multiSyncApply_noAbort(_opCtx.get(), &ops, syncApply));

    // Applied ops should be as follows i.e. no insert grouping:
    // [{insert 1}, {insert 2}, {insert 3}]
    ASSERT_EQ(operationsToApply.size(), operationsApplied.size());
    for (std::size_t i = 0; i < operationsToApply.size(); i++) {
        ASSERT_BSONOBJ_EQ(operationsToApply[i].raw, operationsApplied[i]);
    }
}

TEST_F(SyncTailTest, MultiSyncApplyFallsBackOnApplyingInsertsIndividuallyWhenGroupedInsertFails) {
    int seconds = 0;
    auto makeOp = [&seconds](const NamespaceString& nss) {
        return makeInsertDocumentOplogEntry(
            {Timestamp(Seconds(seconds), 0), 1LL}, nss, BSON("_id" << seconds++));
    };
    NamespaceString nss("test." + _agent.getSuiteName() + "_" + _agent.getTestName() + "_1");
    auto createOp = makeCreateCollectionOplogEntry({Timestamp(Seconds(seconds++), 0), 1LL}, nss);

    // Generate operations to apply:
    // {create}, {insert_1}, {insert_2}, .. {insert_(limit)}, {insert_(limit+1)}
    std::size_t limit = 64;
    MultiApplier::Operations insertOps;
    for (std::size_t i = 0; i < limit + 1; ++i) {
        insertOps.push_back(makeOp(nss));
    }
    MultiApplier::Operations operationsToApply;
    operationsToApply.push_back(createOp);
    std::copy(insertOps.begin(), insertOps.end(), std::back_inserter(operationsToApply));

    std::size_t numFailedGroupedInserts = 0;
    MultiApplier::Operations operationsApplied;
    auto syncApply = [&numFailedGroupedInserts,
                      &operationsApplied](OperationContext*, const BSONObj& op, bool) -> Status {
        // Reject grouped insert operations.
        if (op["o"].type() == BSONType::Array) {
            numFailedGroupedInserts++;
            return {ErrorCodes::OperationFailed, "grouped inserts not supported"};
        }
        operationsApplied.push_back(OplogEntry(op));
        return Status::OK();
    };

    MultiApplier::OperationPtrs ops;
    for (auto&& op : operationsToApply) {
        ops.push_back(&op);
    }
    ASSERT_OK(multiSyncApply_noAbort(_opCtx.get(), &ops, syncApply));

    // On failing to apply the grouped insert operation, multiSyncApply should apply the operations
    // as given in "operationsToApply":
    // {create}, {insert_1}, {insert_2}, .. {insert_(limit)}, {insert_(limit+1)}
    ASSERT_EQUALS(limit + 2, operationsApplied.size());
    ASSERT_EQUALS(createOp, operationsApplied[0]);

    for (std::size_t i = 0; i < limit + 1; ++i) {
        const auto& insertOp = insertOps[i];
        ASSERT_EQUALS(insertOp, operationsApplied[i + 1]);
    }

    // Ensure that multiSyncApply does not attempt to group remaining operations in first failed
    // grouped insert operation.
    ASSERT_EQUALS(1U, numFailedGroupedInserts);
}

TEST_F(SyncTailTest, MultiInitialSyncApplyDisablesDocumentValidationWhileApplyingOperations) {
    SyncTailWithOperationContextChecker syncTail;
    NamespaceString nss("test.t");
    createCollection(_opCtx.get(), nss, {});
    auto op = makeUpdateDocumentOplogEntry(
        {Timestamp(Seconds(1), 0), 1LL}, nss, BSON("_id" << 0), BSON("_id" << 0 << "x" << 2));
    MultiApplier::OperationPtrs ops = {&op};
    AtomicUInt32 fetchCount(0);
    ASSERT_OK(multiInitialSyncApply_noAbort(_opCtx.get(), &ops, &syncTail, &fetchCount));
    ASSERT_EQUALS(fetchCount.load(), 1U);
}

TEST_F(SyncTailTest, MultiInitialSyncApplyIgnoresUpdateOperationIfDocumentIsMissingFromSyncSource) {
    BSONObj emptyDoc;
    SyncTailWithLocalDocumentFetcher syncTail(emptyDoc);
    NamespaceString nss("test.t");
    {
        Lock::GlobalWrite globalLock(_opCtx.get());
        bool justCreated = false;
        Database* db = dbHolder().openDb(_opCtx.get(), nss.db(), &justCreated);
        ASSERT_TRUE(db);
        ASSERT_TRUE(justCreated);
    }
    auto op = makeUpdateDocumentOplogEntry(
        {Timestamp(Seconds(1), 0), 1LL}, nss, BSON("_id" << 0), BSON("_id" << 0 << "x" << 2));
    MultiApplier::OperationPtrs ops = {&op};
    AtomicUInt32 fetchCount(0);
    ASSERT_OK(multiInitialSyncApply_noAbort(_opCtx.get(), &ops, &syncTail, &fetchCount));

    // Since the missing document is not found on the sync source, the collection referenced by
    // the failed operation should not be automatically created.
    ASSERT_FALSE(AutoGetCollectionForReadCommand(_opCtx.get(), nss).getCollection());
    ASSERT_EQUALS(fetchCount.load(), 1U);
}

TEST_F(SyncTailTest, MultiInitialSyncApplySkipsDocumentOnNamespaceNotFound) {
    BSONObj emptyDoc;
    SyncTailWithLocalDocumentFetcher syncTail(emptyDoc);
    NamespaceString nss("local." + _agent.getSuiteName() + "_" + _agent.getTestName());
    NamespaceString badNss("local." + _agent.getSuiteName() + "_" + _agent.getTestName() + "bad");
    auto doc1 = BSON("_id" << 1);
    auto doc2 = BSON("_id" << 2);
    auto doc3 = BSON("_id" << 3);
    auto op0 = makeCreateCollectionOplogEntry({Timestamp(Seconds(1), 0), 1LL}, nss);
    auto op1 = makeInsertDocumentOplogEntry({Timestamp(Seconds(2), 0), 1LL}, nss, doc1);
    auto op2 = makeInsertDocumentOplogEntry({Timestamp(Seconds(3), 0), 1LL}, badNss, doc2);
    auto op3 = makeInsertDocumentOplogEntry({Timestamp(Seconds(4), 0), 1LL}, nss, doc3);
    MultiApplier::OperationPtrs ops = {&op0, &op1, &op2, &op3};
    AtomicUInt32 fetchCount(0);
    ASSERT_OK(multiInitialSyncApply_noAbort(_opCtx.get(), &ops, &syncTail, &fetchCount));
    ASSERT_EQUALS(fetchCount.load(), 0U);

    OplogInterfaceLocal collectionReader(_opCtx.get(), nss.ns());
    auto iter = collectionReader.makeIterator();
    ASSERT_BSONOBJ_EQ(doc3, unittest::assertGet(iter->next()).first);
    ASSERT_BSONOBJ_EQ(doc1, unittest::assertGet(iter->next()).first);
    ASSERT_EQUALS(ErrorCodes::CollectionIsEmpty, iter->next().getStatus());
}

TEST_F(SyncTailTest, MultiInitialSyncApplySkipsIndexCreationOnNamespaceNotFound) {
    BSONObj emptyDoc;
    SyncTailWithLocalDocumentFetcher syncTail(emptyDoc);
    NamespaceString nss("local." + _agent.getSuiteName() + "_" + _agent.getTestName());
    NamespaceString badNss("local." + _agent.getSuiteName() + "_" + _agent.getTestName() + "bad");
    auto doc1 = BSON("_id" << 1);
    auto keyPattern = BSON("a" << 1);
    auto doc3 = BSON("_id" << 3);
    auto op0 = makeCreateCollectionOplogEntry({Timestamp(Seconds(1), 0), 1LL}, nss);
    auto op1 = makeInsertDocumentOplogEntry({Timestamp(Seconds(2), 0), 1LL}, nss, doc1);
    auto op2 =
        makeCreateIndexOplogEntry({Timestamp(Seconds(3), 0), 1LL}, badNss, "a_1", keyPattern);
    auto op3 = makeInsertDocumentOplogEntry({Timestamp(Seconds(4), 0), 1LL}, nss, doc3);
    MultiApplier::OperationPtrs ops = {&op0, &op1, &op2, &op3};
    AtomicUInt32 fetchCount(0);
    ASSERT_OK(multiInitialSyncApply_noAbort(_opCtx.get(), &ops, &syncTail, &fetchCount));
    ASSERT_EQUALS(fetchCount.load(), 0U);

    OplogInterfaceLocal collectionReader(_opCtx.get(), nss.ns());
    auto iter = collectionReader.makeIterator();
    ASSERT_BSONOBJ_EQ(doc3, unittest::assertGet(iter->next()).first);
    ASSERT_BSONOBJ_EQ(doc1, unittest::assertGet(iter->next()).first);
    ASSERT_EQUALS(ErrorCodes::CollectionIsEmpty, iter->next().getStatus());

    // 'badNss' collection should not be implicitly created while attempting to create an index.
    ASSERT_FALSE(AutoGetCollectionForReadCommand(_opCtx.get(), badNss).getCollection());
}

TEST_F(SyncTailTest,
       MultiInitialSyncApplyFetchesMissingDocumentIfDocumentIsAvailableFromSyncSource) {
    SyncTailWithLocalDocumentFetcher syncTail(BSON("_id" << 0 << "x" << 1));
    NamespaceString nss("test.t");
    createCollection(_opCtx.get(), nss, {});
    auto updatedDocument = BSON("_id" << 0 << "x" << 1);
    auto op = makeUpdateDocumentOplogEntry(
        {Timestamp(Seconds(1), 0), 1LL}, nss, BSON("_id" << 0), updatedDocument);
    MultiApplier::OperationPtrs ops = {&op};
    AtomicUInt32 fetchCount(0);
    ASSERT_OK(multiInitialSyncApply_noAbort(_opCtx.get(), &ops, &syncTail, &fetchCount));
    ASSERT_EQUALS(fetchCount.load(), 1U);

    // The collection referenced by "ns" in the failed operation is automatically created to hold
    // the missing document fetched from the sync source. We verify the contents of the collection
    // with the OplogInterfaceLocal class.
    OplogInterfaceLocal collectionReader(_opCtx.get(), nss.ns());
    auto iter = collectionReader.makeIterator();
    ASSERT_BSONOBJ_EQ(updatedDocument, unittest::assertGet(iter->next()).first);
    ASSERT_EQUALS(ErrorCodes::CollectionIsEmpty, iter->next().getStatus());
}

TEST_F(IdempotencyTest, Geo2dsphereIndexFailedOnUpdate) {
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_RECOVERING));
    ASSERT_OK(runOp(createCollection()));
    auto insertOp = insert(fromjson("{_id: 1, loc: 'hi'}"));
    auto updateOp = update(1, fromjson("{$set: {loc: [1, 2]}}"));
    auto indexOp = buildIndex(fromjson("{loc: '2dsphere'}"), BSON("2dsphereIndexVersion" << 3));

    auto ops = {insertOp, updateOp, indexOp};
    testOpsAreIdempotent(ops);

    ASSERT_OK(ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_PRIMARY));
    auto status = runOps(ops);
    ASSERT_EQ(status.code(), 16755);
}

TEST_F(IdempotencyTest, Geo2dsphereIndexFailedOnIndexing) {
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_RECOVERING));
    ASSERT_OK(runOp(createCollection()));
    auto indexOp = buildIndex(fromjson("{loc: '2dsphere'}"), BSON("2dsphereIndexVersion" << 3));
    auto dropIndexOp = dropIndex("loc_index");
    auto insertOp = insert(fromjson("{_id: 1, loc: 'hi'}"));

    auto ops = {indexOp, dropIndexOp, insertOp};
    testOpsAreIdempotent(ops);

    ASSERT_OK(ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_PRIMARY));
    auto status = runOps(ops);
    ASSERT_EQ(status.code(), 16755);
}

TEST_F(IdempotencyTest, Geo2dIndex) {
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_RECOVERING));
    ASSERT_OK(runOp(createCollection()));
    auto insertOp = insert(fromjson("{_id: 1, loc: [1]}"));
    auto updateOp = update(1, fromjson("{$set: {loc: [1, 2]}}"));
    auto indexOp = buildIndex(fromjson("{loc: '2d'}"));

    auto ops = {insertOp, updateOp, indexOp};
    testOpsAreIdempotent(ops);

    ASSERT_OK(ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_PRIMARY));
    auto status = runOps(ops);
    ASSERT_EQ(status.code(), 13068);
}

TEST_F(IdempotencyTest, UniqueKeyIndex) {
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_RECOVERING));
    ASSERT_OK(runOp(createCollection()));
    auto insertOp = insert(fromjson("{_id: 1, x: 5}"));
    auto updateOp = update(1, fromjson("{$set: {x: 6}}"));
    auto insertOp2 = insert(fromjson("{_id: 2, x: 5}"));
    auto indexOp = buildIndex(fromjson("{x: 1}"), fromjson("{unique: true}"));

    auto ops = {insertOp, updateOp, insertOp2, indexOp};
    testOpsAreIdempotent(ops);

    ASSERT_OK(ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_PRIMARY));
    auto status = runOps(ops);
    ASSERT_EQ(status.code(), ErrorCodes::DuplicateKey);
}

TEST_F(IdempotencyTest, ParallelArrayError) {
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_RECOVERING));

    ASSERT_OK(runOp(createCollection()));
    ASSERT_OK(runOp(insert(fromjson("{_id: 1}"))));

    auto updateOp1 = update(1, fromjson("{$set: {x: [1, 2]}}"));
    auto updateOp2 = update(1, fromjson("{$set: {x: 1}}"));
    auto updateOp3 = update(1, fromjson("{$set: {y: [3, 4]}}"));
    auto indexOp = buildIndex(fromjson("{x: 1, y: 1}"));

    auto ops = {updateOp1, updateOp2, updateOp3, indexOp};
    testOpsAreIdempotent(ops);

    ASSERT_OK(ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_PRIMARY));
    auto status = runOps(ops);
    ASSERT_EQ(status.code(), ErrorCodes::CannotIndexParallelArrays);
}

TEST_F(IdempotencyTest, IndexKeyTooLongError) {
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_RECOVERING));

    ASSERT_OK(runOp(createCollection()));
    ASSERT_OK(runOp(insert(fromjson("{_id: 1}"))));

    // Key size limit is 1024 for ephemeral storage engine, so two 800 byte fields cannot
    // co-exist.
    std::string longStr(800, 'a');
    auto updateOp1 = update(1, BSON("$set" << BSON("x" << longStr)));
    auto updateOp2 = update(1, fromjson("{$set: {x: 1}}"));
    auto updateOp3 = update(1, BSON("$set" << BSON("y" << longStr)));
    auto indexOp = buildIndex(fromjson("{x: 1, y: 1}"));

    auto ops = {updateOp1, updateOp2, updateOp3, indexOp};
    testOpsAreIdempotent(ops);

    ASSERT_OK(ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_PRIMARY));
    auto status = runOps(ops);
    ASSERT_EQ(status.code(), ErrorCodes::KeyTooLong);
}

TEST_F(IdempotencyTest, IndexWithDifferentOptions) {
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_RECOVERING));

    ASSERT_OK(runOp(createCollection()));
    ASSERT_OK(runOp(insert(fromjson("{_id: 1, x: 'hi'}"))));

    auto indexOp1 = buildIndex(fromjson("{x: 'text'}"), fromjson("{default_language: 'spanish'}"));
    auto dropIndexOp = dropIndex("x_index");
    auto indexOp2 = buildIndex(fromjson("{x: 'text'}"), fromjson("{default_language: 'english'}"));

    auto ops = {indexOp1, dropIndexOp, indexOp2};
    testOpsAreIdempotent(ops);

    ASSERT_OK(ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_PRIMARY));
    auto status = runOps(ops);
    ASSERT_EQ(status.code(), ErrorCodes::IndexOptionsConflict);
}

TEST_F(IdempotencyTest, TextIndexDocumentHasNonStringLanguageField) {
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_RECOVERING));

    ASSERT_OK(runOp(createCollection()));
    auto insertOp = insert(fromjson("{_id: 1, x: 'words to index', language: 1}"));
    auto updateOp = update(1, fromjson("{$unset: {language: 1}}"));
    auto indexOp = buildIndex(fromjson("{x: 'text'}"), BSONObj());

    auto ops = {insertOp, updateOp, indexOp};
    testOpsAreIdempotent(ops);

    ASSERT_OK(ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_PRIMARY));
    auto status = runOps(ops);
    ASSERT_EQ(status.code(), 17261);
}

TEST_F(IdempotencyTest, InsertDocumentWithNonStringLanguageFieldWhenTextIndexExists) {
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_RECOVERING));

    ASSERT_OK(runOp(createCollection()));
    auto indexOp = buildIndex(fromjson("{x: 'text'}"), BSONObj());
    auto dropIndexOp = dropIndex("x_index");
    auto insertOp = insert(fromjson("{_id: 1, x: 'words to index', language: 1}"));

    auto ops = {indexOp, dropIndexOp, insertOp};
    testOpsAreIdempotent(ops);

    ASSERT_OK(ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_PRIMARY));
    auto status = runOps(ops);
    ASSERT_EQ(status.code(), 17261);
}

TEST_F(IdempotencyTest, TextIndexDocumentHasNonStringLanguageOverrideField) {
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_RECOVERING));

    ASSERT_OK(runOp(createCollection()));
    auto insertOp = insert(fromjson("{_id: 1, x: 'words to index', y: 1}"));
    auto updateOp = update(1, fromjson("{$unset: {y: 1}}"));
    auto indexOp = buildIndex(fromjson("{x: 'text'}"), fromjson("{language_override: 'y'}"));

    auto ops = {insertOp, updateOp, indexOp};
    testOpsAreIdempotent(ops);

    ASSERT_OK(ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_PRIMARY));
    auto status = runOps(ops);
    ASSERT_EQ(status.code(), 17261);
}

TEST_F(IdempotencyTest, InsertDocumentWithNonStringLanguageOverrideFieldWhenTextIndexExists) {
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_RECOVERING));

    ASSERT_OK(runOp(createCollection()));
    auto indexOp = buildIndex(fromjson("{x: 'text'}"), fromjson("{language_override: 'y'}"));
    auto dropIndexOp = dropIndex("x_index");
    auto insertOp = insert(fromjson("{_id: 1, x: 'words to index', y: 1}"));

    auto ops = {indexOp, dropIndexOp, insertOp};
    testOpsAreIdempotent(ops);

    ASSERT_OK(ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_PRIMARY));
    auto status = runOps(ops);
    ASSERT_EQ(status.code(), 17261);
}

TEST_F(IdempotencyTest, TextIndexDocumentHasUnknownLanguage) {
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_RECOVERING));

    ASSERT_OK(runOp(createCollection()));
    auto insertOp = insert(fromjson("{_id: 1, x: 'words to index', language: 'bad'}"));
    auto updateOp = update(1, fromjson("{$unset: {language: 1}}"));
    auto indexOp = buildIndex(fromjson("{x: 'text'}"), BSONObj());

    auto ops = {insertOp, updateOp, indexOp};
    testOpsAreIdempotent(ops);

    ASSERT_OK(ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_PRIMARY));
    auto status = runOps(ops);
    ASSERT_EQ(status.code(), 17262);
}

TEST_F(IdempotencyTest, CreateCollectionWithValidation) {
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_RECOVERING));
    const BSONObj uuidObj = UUID::gen().toBSON();

    auto runOpsAndValidate = [this, uuidObj]() {
        auto options1 = fromjson("{'validator' : {'phone' : {'$type' : 'string' } } }");
        auto createColl1 = makeCreateCollectionOplogEntry(nextOpTime(), nss, options1);
        auto dropColl = makeCommandOplogEntry(nextOpTime(), nss, BSON("drop" << nss.coll()));
        auto options2 = fromjson("{'validator' : {'phone' : {'$type' : 'number' } } }");
        // The first collection will be dropped, so won't affect final validation. However, the
        // final collection should have the correct UUID.
        options2 = options2.addField(uuidObj.firstElement());

        auto createColl2 = makeCreateCollectionOplogEntry(nextOpTime(), nss, options2);

        auto ops = {createColl1, dropColl, createColl2};
        ASSERT_OK(runOps(ops));
        auto state = validate();

        return state;
    };

    auto state1 = runOpsAndValidate();
    auto state2 = runOpsAndValidate();
    ASSERT_EQUALS(state1, state2);
}

TEST_F(IdempotencyTest, CreateCollectionWithCollation) {
    ASSERT_OK(getGlobalReplicationCoordinator()->setFollowerMode(MemberState::RS_RECOVERING));
    ASSERT_OK(runOp(createCollection()));
    CollectionUUID uuid = UUID::gen();

    auto runOpsAndValidate = [this, uuid]() {
        auto insertOp1 = insert(fromjson("{ _id: 'foo' }"));
        auto insertOp2 = insert(fromjson("{ _id: 'Foo', x: 1 }"));
        auto updateOp = update("foo", BSON("$set" << BSON("x" << 2)));
        auto dropColl = makeCommandOplogEntry(nextOpTime(), nss, BSON("drop" << nss.coll()));
        auto options = BSON("collation" << BSON("locale"
                                                << "en"
                                                << "caseLevel"
                                                << false
                                                << "caseFirst"
                                                << "off"
                                                << "strength"
                                                << 1
                                                << "numericOrdering"
                                                << false
                                                << "alternate"
                                                << "non-ignorable"
                                                << "maxVariable"
                                                << "punct"
                                                << "normalization"
                                                << false
                                                << "backwards"
                                                << false
                                                << "version"
                                                << "57.1")
                                        << "uuid"
                                        << uuid);
        auto createColl = makeCreateCollectionOplogEntry(nextOpTime(), nss, options);

        auto ops = {insertOp1, insertOp2, updateOp, dropColl, createColl};
        ASSERT_OK(runOps(ops));
        auto state = validate();

        return state;
    };

    auto state1 = runOpsAndValidate();
    auto state2 = runOpsAndValidate();
    ASSERT_EQUALS(state1, state2);
}

TEST_F(IdempotencyTest, CreateCollectionWithIdIndex) {
    ASSERT_OK(getGlobalReplicationCoordinator()->setFollowerMode(MemberState::RS_RECOVERING));

    auto options1 = BSON("idIndex" << BSON("key" << fromjson("{_id: 1}") << "name"
                                                 << "_id_"
                                                 << "v"
                                                 << 2
                                                 << "ns"
                                                 << nss.ns()));
    auto createColl1 = makeCreateCollectionOplogEntry(nextOpTime(), nss, options1);
    ASSERT_OK(runOp(createColl1));

    CollectionUUID uuid = UUID::gen();
    auto runOpsAndValidate = [this, uuid]() {
        auto insertOp = insert(BSON("_id" << Decimal128(1)));
        auto dropColl = makeCommandOplogEntry(nextOpTime(), nss, BSON("drop" << nss.coll()));
        auto createColl2 = createCollection(uuid);

        auto ops = {insertOp, dropColl, createColl2};
        ASSERT_OK(runOps(ops));
        auto state = validate();

        return state;
    };

    auto state1 = runOpsAndValidate();
    auto state2 = runOpsAndValidate();
    ASSERT_EQUALS(state1, state2);
}

TEST_F(IdempotencyTest, CreateCollectionWithView) {
    ASSERT_OK(getGlobalReplicationCoordinator()->setFollowerMode(MemberState::RS_RECOVERING));

    // Create data collection
    ASSERT_OK(runOp(createCollection()));
    // Create "system.views" collection
    auto viewNss = NamespaceString(nss.db(), "system.views");
    ASSERT_OK(runOp(makeCreateCollectionOplogEntry(nextOpTime(), viewNss)));

    auto viewDoc =
        BSON("_id" << NamespaceString(nss.db(), "view").ns() << "viewOn" << nss.coll() << "pipeline"
                   << fromjson("[ { '$project' : { 'x' : 1 } } ]"));
    auto insertViewOp = makeInsertDocumentOplogEntry(nextOpTime(), viewNss, viewDoc);
    auto dropColl = makeCommandOplogEntry(nextOpTime(), nss, BSON("drop" << nss.coll()));

    auto ops = {insertViewOp, dropColl};
    testOpsAreIdempotent(ops);
}

TEST_F(IdempotencyTest, CollModNamespaceNotFound) {
    ASSERT_OK(getGlobalReplicationCoordinator()->setFollowerMode(MemberState::RS_RECOVERING));

    ASSERT_OK(runOp(createCollection()));
    ASSERT_OK(runOp(buildIndex(BSON("createdAt" << 1), BSON("expireAfterSeconds" << 3600))));

    auto indexChange = fromjson("{keyPattern: {createdAt:1}, expireAfterSeconds:4000}}");
    auto collModCmd = BSON("collMod" << nss.coll() << "index" << indexChange);
    auto collModOp = makeCommandOplogEntry(nextOpTime(), nss, collModCmd);
    auto dropCollOp = makeCommandOplogEntry(nextOpTime(), nss, BSON("drop" << nss.coll()));

    auto ops = {collModOp, dropCollOp};
    testOpsAreIdempotent(ops);
}

TEST_F(IdempotencyTest, CollModIndexNotFound) {
    ASSERT_OK(getGlobalReplicationCoordinator()->setFollowerMode(MemberState::RS_RECOVERING));

    ASSERT_OK(runOp(createCollection()));
    ASSERT_OK(runOp(buildIndex(BSON("createdAt" << 1), BSON("expireAfterSeconds" << 3600))));

    auto indexChange = fromjson("{keyPattern: {createdAt:1}, expireAfterSeconds:4000}}");
    auto collModCmd = BSON("collMod" << nss.coll() << "index" << indexChange);
    auto collModOp = makeCommandOplogEntry(nextOpTime(), nss, collModCmd);
    auto dropIndexOp = dropIndex("createdAt_index");

    auto ops = {collModOp, dropIndexOp};
    testOpsAreIdempotent(ops);
}

TEST_F(IdempotencyTest, ResyncOnRenameCollection) {
    ASSERT_OK(
        ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_RECOVERING));

    auto cmd = BSON("renameCollection" << nss.ns() << "to"
                                       << "test.bar"
                                       << "stayTemp"
                                       << false
                                       << "dropTarget"
                                       << false);
    auto op = makeCommandOplogEntry(nextOpTime(), nss, cmd);
    ASSERT_EQUALS(runOp(op), ErrorCodes::OplogOperationUnsupported);
}

}  // namespace
}  // namespace repl
}  // namespace mongo
