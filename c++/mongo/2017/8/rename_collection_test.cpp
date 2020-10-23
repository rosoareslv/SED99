/**
 *    Copyright 2017 MongoDB Inc.
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

#include <set>

#include "mongo/db/catalog/collection_catalog_entry.h"
#include "mongo/db/catalog/collection_options.h"
#include "mongo/db/catalog/rename_collection.h"
#include "mongo/db/client.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/op_observer.h"
#include "mongo/db/op_observer_noop.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/repl/drop_pending_collection_reaper.h"
#include "mongo/db/repl/oplog.h"
#include "mongo/db/repl/optime.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/repl/replication_coordinator_mock.h"
#include "mongo/db/repl/storage_interface_mock.h"
#include "mongo/db/service_context_d_test_fixture.h"
#include "mongo/stdx/memory.h"
#include "mongo/unittest/death_test.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/mongoutils/str.h"

namespace {

using namespace mongo;

/**
 * Mock OpObserver that tracks dropped collections and databases.
 * Since this class is used exclusively to test dropDatabase(), we will also check the drop-pending
 * flag in the Database object being tested (if provided).
 */
class OpObserverMock : public OpObserverNoop {
public:
    repl::OpTime onRenameCollection(OperationContext* opCtx,
                                    const NamespaceString& fromCollection,
                                    const NamespaceString& toCollection,
                                    OptionalCollectionUUID uuid,
                                    bool dropTarget,
                                    OptionalCollectionUUID dropTargetUUID,
                                    OptionalCollectionUUID dropSourceUUID,
                                    bool stayTemp) override;

    bool onRenameCollectionCalled = false;
    repl::OpTime renameOpTime = {Timestamp(Seconds(100), 1U), 1LL};
};

repl::OpTime OpObserverMock::onRenameCollection(OperationContext* opCtx,
                                                const NamespaceString& fromCollection,
                                                const NamespaceString& toCollection,
                                                OptionalCollectionUUID uuid,
                                                bool dropTarget,
                                                OptionalCollectionUUID dropTargetUUID,
                                                OptionalCollectionUUID dropSourceUUID,
                                                bool stayTemp) {
    onRenameCollectionCalled = true;
    return renameOpTime;
}

class RenameCollectionTest : public ServiceContextMongoDTest {
public:
    static ServiceContext::UniqueOperationContext makeOpCtx();

private:
    void setUp() override;
    void tearDown() override;

protected:
    ServiceContext::UniqueOperationContext _opCtx;
    repl::ReplicationCoordinatorMock* _replCoord = nullptr;
    OpObserverMock* _opObserver = nullptr;
    NamespaceString _sourceNss;
    NamespaceString _targetNss;
    NamespaceString _targetNssDifferentDb;
};

// static
ServiceContext::UniqueOperationContext RenameCollectionTest::makeOpCtx() {
    return cc().makeOperationContext();
}

void RenameCollectionTest::setUp() {
    // Set up mongod.
    ServiceContextMongoDTest::setUp();

    auto service = getServiceContext();
    _opCtx = cc().makeOperationContext();

    repl::StorageInterface::set(service, stdx::make_unique<repl::StorageInterfaceMock>());
    repl::DropPendingCollectionReaper::set(
        service,
        stdx::make_unique<repl::DropPendingCollectionReaper>(repl::StorageInterface::get(service)));

    // Set up ReplicationCoordinator and create oplog.
    auto replCoord = stdx::make_unique<repl::ReplicationCoordinatorMock>(service);
    _replCoord = replCoord.get();
    repl::ReplicationCoordinator::set(service, std::move(replCoord));
    repl::setOplogCollectionName();
    repl::createOplog(_opCtx.get());

    // Ensure that we are primary.
    ASSERT_OK(_replCoord->setFollowerMode(repl::MemberState::RS_PRIMARY));

    // Use OpObserverMock to track notifications for collection and database drops.
    auto opObserver = stdx::make_unique<OpObserverMock>();
    _opObserver = opObserver.get();
    service->setOpObserver(std::move(opObserver));

    _sourceNss = NamespaceString("test.foo");
    _targetNss = NamespaceString("test.bar");
    _targetNssDifferentDb = NamespaceString("test2.bar");
}

void RenameCollectionTest::tearDown() {
    _targetNss = {};
    _sourceNss = {};
    _opObserver = nullptr;
    _replCoord = nullptr;
    _opCtx = {};

    auto service = getServiceContext();
    repl::DropPendingCollectionReaper::set(service, {});
    repl::StorageInterface::set(service, {});

    ServiceContextMongoDTest::tearDown();
}

/**
 * Creates a collection without any namespace restrictions.
 */
void _createCollection(OperationContext* opCtx,
                       const NamespaceString& nss,
                       const CollectionOptions options = {}) {
    writeConflictRetry(opCtx, "_createCollection", nss.ns(), [=] {
        AutoGetOrCreateDb autoDb(opCtx, nss.db(), MODE_X);
        auto db = autoDb.getDb();
        ASSERT_TRUE(db) << "Cannot create collection " << nss << " because database " << nss.db()
                        << " does not exist.";

        WriteUnitOfWork wuow(opCtx);
        ASSERT_TRUE(db->createCollection(opCtx, nss.ns(), options))
            << "Failed to create collection " << nss << " due to unknown error.";
        wuow.commit();
    });

    ASSERT_TRUE(AutoGetCollectionForRead(opCtx, nss).getCollection());
}

/**
 * Returns a collection options with a generated UUID.
 */
CollectionOptions _makeCollectionOptionsWithUuid() {
    CollectionOptions options;
    options.uuid = UUID::gen();
    return options;
}

/**
 * Returns true if collection exists.
 */
bool _collectionExists(OperationContext* opCtx, const NamespaceString& nss) {
    return AutoGetCollectionForRead(opCtx, nss).getCollection() != nullptr;
}

/**
 * Returns collection options.
 */
CollectionOptions _getCollectionOptions(OperationContext* opCtx, const NamespaceString& nss) {
    AutoGetCollectionForRead autoColl(opCtx, nss);
    auto collection = autoColl.getCollection();
    ASSERT_TRUE(collection) << "Unable to get collections options for " << nss
                            << " because collection does not exist.";
    auto catalogEntry = collection->getCatalogEntry();
    return catalogEntry->getCollectionOptions(opCtx);
}

/**
 * Returns UUID of collection.
 */
CollectionUUID _getCollectionUuid(OperationContext* opCtx, const NamespaceString& nss) {
    auto options = _getCollectionOptions(opCtx, nss);
    ASSERT_TRUE(options.uuid);
    return *(options.uuid);
}

/**
 * Returns true if namespace refers to a temporary collection.
 */
bool _isTempCollection(OperationContext* opCtx, const NamespaceString& nss) {
    AutoGetCollectionForRead autoColl(opCtx, nss);
    auto collection = autoColl.getCollection();
    ASSERT_TRUE(collection) << "Unable to check if " << nss
                            << " is a temporary collection because collection does not exist.";
    auto catalogEntry = collection->getCatalogEntry();
    auto options = catalogEntry->getCollectionOptions(opCtx);
    return options.temp;
}

TEST_F(RenameCollectionTest, RenameCollectionReturnsNamespaceNotFoundIfDatabaseDoesNotExist) {
    ASSERT_FALSE(AutoGetDb(_opCtx.get(), _sourceNss.db(), MODE_X).getDb());
    ASSERT_EQUALS(ErrorCodes::NamespaceNotFound,
                  renameCollection(_opCtx.get(), _sourceNss, _targetNss, {}));
}

TEST_F(RenameCollectionTest, RenameCollectionReturnsNotMasterIfNotPrimary) {
    _createCollection(_opCtx.get(), _sourceNss);
    ASSERT_OK(_replCoord->setFollowerMode(repl::MemberState::RS_SECONDARY));
    ASSERT_TRUE(_opCtx->writesAreReplicated());
    ASSERT_FALSE(_replCoord->canAcceptWritesForDatabase(_opCtx.get(), _sourceNss.db()));
    ASSERT_EQUALS(ErrorCodes::NotMaster,
                  renameCollection(_opCtx.get(), _sourceNss, _targetNss, {}));
}

TEST_F(RenameCollectionTest, RenameCollectionAcrossDatabaseWithoutUuid) {
    _createCollection(_opCtx.get(), _sourceNss);
    ASSERT_OK(renameCollection(_opCtx.get(), _sourceNss, _targetNssDifferentDb, {}));
    ASSERT_FALSE(_collectionExists(_opCtx.get(), _sourceNss));
    ASSERT_FALSE(_getCollectionOptions(_opCtx.get(), _targetNssDifferentDb).uuid);
}

TEST_F(RenameCollectionTest, RenameCollectionAcrossDatabaseWithUuid) {
    auto options = _makeCollectionOptionsWithUuid();
    _createCollection(_opCtx.get(), _sourceNss, options);
    ASSERT_OK(renameCollection(_opCtx.get(), _sourceNss, _targetNssDifferentDb, {}));
    ASSERT_FALSE(_collectionExists(_opCtx.get(), _sourceNss));
    ASSERT_NOT_EQUALS(options.uuid, _getCollectionUuid(_opCtx.get(), _targetNssDifferentDb));
}

TEST_F(RenameCollectionTest, RenameCollectionForApplyOpsAcrossDatabaseWithTargetUuid) {
    _createCollection(_opCtx.get(), _sourceNss);
    auto dbName = _sourceNss.db().toString();
    auto uuid = UUID::gen();
    auto uuidDoc = BSON("ui" << uuid);
    auto cmd = BSON("renameCollection" << _sourceNss.ns() << "to" << _targetNssDifferentDb.ns()
                                       << "dropTarget"
                                       << true);
    ASSERT_OK(renameCollectionForApplyOps(_opCtx.get(), dbName, uuidDoc["ui"], cmd, {}));
    ASSERT_FALSE(_collectionExists(_opCtx.get(), _sourceNss));
    ASSERT_EQUALS(uuid, _getCollectionUuid(_opCtx.get(), _targetNssDifferentDb));
}

TEST_F(RenameCollectionTest,
       RenameCollectionReturnsNamespaceExitsIfTargetExistsAndDropTargetIsFalse) {
    _createCollection(_opCtx.get(), _sourceNss);
    _createCollection(_opCtx.get(), _targetNss);
    RenameCollectionOptions options;
    ASSERT_FALSE(options.dropTarget);
    ASSERT_EQUALS(ErrorCodes::NamespaceExists,
                  renameCollection(_opCtx.get(), _sourceNss, _targetNss, options));
}

TEST_F(RenameCollectionTest, RenameCollectionMakesTargetCollectionDropPendingIfDropTargetIsTrue) {
    _createCollection(_opCtx.get(), _sourceNss);
    _createCollection(_opCtx.get(), _targetNss);
    RenameCollectionOptions options;
    options.dropTarget = true;
    ASSERT_OK(renameCollection(_opCtx.get(), _sourceNss, _targetNss, options));
    ASSERT_FALSE(_collectionExists(_opCtx.get(), _sourceNss))
        << "source collection " << _sourceNss << " still exists after successful rename";
    ASSERT_TRUE(_collectionExists(_opCtx.get(), _targetNss)) << "target collection " << _targetNss
                                                             << " missing after successful rename";

    ASSERT_TRUE(_opObserver->onRenameCollectionCalled);

    auto renameOpTime = _opObserver->renameOpTime;
    ASSERT_GREATER_THAN(renameOpTime, repl::OpTime());

    // Confirm that the target collection has been renamed to a drop-pending collection.
    auto dpns = _targetNss.makeDropPendingNamespace(renameOpTime);
    ASSERT_TRUE(_collectionExists(_opCtx.get(), dpns))
        << "target collection " << _targetNss
        << " not renamed to drop-pending collection after successful rename";
}

/**
 * Sets up ReplicationCoordinator for master/slave.
 */
void _setUpMasterSlave(ServiceContext* service) {
    repl::ReplSettings settings;
    settings.setOplogSizeBytes(10 * 1024 * 1024);
    settings.setMaster(true);
    repl::ReplicationCoordinator::set(
        service, stdx::make_unique<repl::ReplicationCoordinatorMock>(service, settings));
    auto replCoord = repl::ReplicationCoordinator::get(service);
    ASSERT_TRUE(repl::ReplicationCoordinator::modeMasterSlave == replCoord->getReplicationMode());
}

TEST_F(RenameCollectionTest,
       RenameCollectionDropsTargetCollectionIfDropTargetIsTrueAndReplModeIsMasterSlave) {
    _setUpMasterSlave(getServiceContext());

    _createCollection(_opCtx.get(), _sourceNss);
    _createCollection(_opCtx.get(), _targetNss);
    RenameCollectionOptions options;
    options.dropTarget = true;
    ASSERT_OK(renameCollection(_opCtx.get(), _sourceNss, _targetNss, options));
    ASSERT_FALSE(_collectionExists(_opCtx.get(), _sourceNss))
        << "source collection " << _sourceNss << " still exists after successful rename";
    ASSERT_TRUE(_collectionExists(_opCtx.get(), _targetNss)) << "target collection " << _targetNss
                                                             << " missing after successful rename";

    ASSERT_TRUE(_opObserver->onRenameCollectionCalled);

    auto renameOpTime = _opObserver->renameOpTime;
    ASSERT_GREATER_THAN(renameOpTime, repl::OpTime());

    // Confirm that the target collection is not renamed to a drop-pending collection under
    // master/slave.
    auto dpns = _targetNss.makeDropPendingNamespace(renameOpTime);
    ASSERT_FALSE(_collectionExists(_opCtx.get(), dpns))
        << "target collection " << _targetNss
        << " renamed to drop-pending collection after successful rename";
}

TEST_F(RenameCollectionTest, RenameCollectionForApplyOpsRejectsRenameOpTimeIfWritesAreReplicated) {
    ASSERT_TRUE(_opCtx->writesAreReplicated());

    _createCollection(_opCtx.get(), _sourceNss);
    auto dbName = _sourceNss.db().toString();
    auto cmd = BSON("renameCollection" << _sourceNss.ns() << "to" << _targetNss.ns());
    auto renameOpTime = _opObserver->renameOpTime;
    ASSERT_EQUALS(ErrorCodes::BadValue,
                  renameCollectionForApplyOps(_opCtx.get(), dbName, {}, cmd, renameOpTime));
}

TEST_F(RenameCollectionTest,
       RenameCollectionForApplyOpsMakesTargetCollectionDropPendingIfDropTargetIsTrue) {
    repl::UnreplicatedWritesBlock uwb(_opCtx.get());
    ASSERT_FALSE(_opCtx->writesAreReplicated());

    // OpObserver::onRenameCollection() must return a null OpTime when writes are not replicated.
    _opObserver->renameOpTime = {};

    _createCollection(_opCtx.get(), _sourceNss);
    _createCollection(_opCtx.get(), _targetNss);
    auto dbName = _sourceNss.db().toString();
    auto cmd = BSON("renameCollection" << _sourceNss.ns() << "to" << _targetNss.ns() << "dropTarget"
                                       << true);

    repl::OpTime renameOpTime = {Timestamp(Seconds(200), 1U), 1LL};
    ASSERT_OK(renameCollectionForApplyOps(_opCtx.get(), dbName, {}, cmd, renameOpTime));

    // Confirm that the target collection has been renamed to a drop-pending collection.
    auto dpns = _targetNss.makeDropPendingNamespace(renameOpTime);
    ASSERT_TRUE(_collectionExists(_opCtx.get(), dpns))
        << "target collection " << _targetNss
        << " not renamed to drop-pending collection after successful rename for applyOps";
}

DEATH_TEST_F(RenameCollectionTest,
             RenameCollectionForApplyOpsTriggersFatalAssertionIfLogOpReturnsValidOpTime,
             "unexpected renameCollection oplog entry written to the oplog with optime") {
    repl::UnreplicatedWritesBlock uwb(_opCtx.get());
    ASSERT_FALSE(_opCtx->writesAreReplicated());

    _createCollection(_opCtx.get(), _sourceNss);
    _createCollection(_opCtx.get(), _targetNss);
    auto dbName = _sourceNss.db().toString();
    auto cmd = BSON("renameCollection" << _sourceNss.ns() << "to" << _targetNss.ns() << "dropTarget"
                                       << true);

    repl::OpTime renameOpTime = {Timestamp(Seconds(200), 1U), 1LL};
    ASSERT_OK(renameCollectionForApplyOps(_opCtx.get(), dbName, {}, cmd, renameOpTime));
}

void _testRenameCollectionStayTemp(OperationContext* opCtx,
                                   const NamespaceString& sourceNss,
                                   const NamespaceString& targetNss,
                                   bool stayTemp,
                                   bool isSourceCollectionTemporary) {
    CollectionOptions collectionOptions;
    collectionOptions.temp = isSourceCollectionTemporary;
    _createCollection(opCtx, sourceNss, collectionOptions);

    RenameCollectionOptions options;
    options.stayTemp = stayTemp;
    ASSERT_OK(renameCollection(opCtx, sourceNss, targetNss, options));
    ASSERT_FALSE(_collectionExists(opCtx, sourceNss)) << "source collection " << sourceNss
                                                      << " still exists after successful rename";

    if (!isSourceCollectionTemporary) {
        ASSERT_FALSE(_isTempCollection(opCtx, targetNss))
            << "target collection " << targetNss
            << " cannot not be temporary after rename if source collection is not temporary.";
    } else if (stayTemp) {
        ASSERT_TRUE(_isTempCollection(opCtx, targetNss))
            << "target collection " << targetNss
            << " is no longer temporary after rename with stayTemp set to true.";
    } else {
        ASSERT_FALSE(_isTempCollection(opCtx, targetNss))
            << "target collection " << targetNss
            << " still temporary after rename with stayTemp set to false.";
    }
}

TEST_F(RenameCollectionTest, RenameSameDatabaseStayTempFalse) {
    _testRenameCollectionStayTemp(_opCtx.get(), _sourceNss, _targetNss, false, true);
}

TEST_F(RenameCollectionTest, RenameSameDatabaseStayTempTrue) {
    _testRenameCollectionStayTemp(_opCtx.get(), _sourceNss, _targetNss, true, true);
}

TEST_F(RenameCollectionTest, RenameDifferentDatabaseStayTempFalse) {
    _testRenameCollectionStayTemp(_opCtx.get(), _sourceNss, _targetNssDifferentDb, false, true);
}

TEST_F(RenameCollectionTest, RenameDifferentDatabaseStayTempTrue) {
    _testRenameCollectionStayTemp(_opCtx.get(), _sourceNss, _targetNssDifferentDb, true, true);
}

TEST_F(RenameCollectionTest, RenameSameDatabaseStayTempFalseSourceNotTemporary) {
    _testRenameCollectionStayTemp(_opCtx.get(), _sourceNss, _targetNss, false, false);
}

TEST_F(RenameCollectionTest, RenameSameDatabaseStayTempTrueSourceNotTemporary) {
    _testRenameCollectionStayTemp(_opCtx.get(), _sourceNss, _targetNss, true, false);
}

TEST_F(RenameCollectionTest, RenameDifferentDatabaseStayTempFalseSourceNotTemporary) {
    _testRenameCollectionStayTemp(_opCtx.get(), _sourceNss, _targetNssDifferentDb, false, false);
}

TEST_F(RenameCollectionTest, RenameDifferentDatabaseStayTempTrueSourceNotTemporary) {
    _testRenameCollectionStayTemp(_opCtx.get(), _sourceNss, _targetNssDifferentDb, true, false);
}

}  // namespace
