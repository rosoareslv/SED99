/**
 *    Copyright 2015 MongoDB Inc.
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

#include <memory>

#include "mongo/db/client.h"
#include "mongo/db/concurrency/d_concurrency.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/curop.h"
#include "mongo/db/dbhelpers.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/repl/replication_coordinator_mock.h"
#include "mongo/db/repl/storage_interface_impl.h"
#include "mongo/db/service_context_d_test_fixture.h"
#include "mongo/db/storage/recovery_unit_noop.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"

namespace {

using namespace mongo;
using namespace mongo::repl;

/**
 * Returns min valid document.
 */
BSONObj getMinValidDocument(OperationContext* txn, const NamespaceString& minValidNss) {
    MONGO_WRITE_CONFLICT_RETRY_LOOP_BEGIN {
        ScopedTransaction transaction(txn, MODE_IS);
        Lock::DBLock dblk(txn->lockState(), minValidNss.db(), MODE_IS);
        Lock::CollectionLock lk(txn->lockState(), minValidNss.ns(), MODE_IS);
        BSONObj mv;
        if (Helpers::getSingleton(txn, minValidNss.ns().c_str(), mv)) {
            return mv;
        }
    }
    MONGO_WRITE_CONFLICT_RETRY_LOOP_END(txn, "getMinValidDocument", minValidNss.ns());
    return BSONObj();
}


class StorageInterfaceImplTest : public ServiceContextMongoDTest {
protected:
    Client* getClient() const;

private:
    void setUp() override;
};

/**
 * Recovery unit that tracks if waitUntilDurable() is called.
 */
class RecoveryUnitWithDurabilityTracking : public RecoveryUnitNoop {
public:
    bool waitUntilDurable() override;
    bool waitUntilDurableCalled = false;
};

void StorageInterfaceImplTest::setUp() {
    ServiceContextMongoDTest::setUp();
    // Initializes cc() used in ServiceContextMongoD::_newOpCtx().
    Client::initThreadIfNotAlready("StorageInterfaceImplTest");

    ReplSettings settings;
    settings.setOplogSizeBytes(5 * 1024 * 1024);
    settings.setReplSetString("mySet/node1:12345");
    ReplicationCoordinator::set(getGlobalServiceContext(),
                                stdx::make_unique<ReplicationCoordinatorMock>(settings));
}

Client* StorageInterfaceImplTest::getClient() const {
    return &cc();
}

bool RecoveryUnitWithDurabilityTracking::waitUntilDurable() {
    waitUntilDurableCalled = true;
    return RecoveryUnitNoop::waitUntilDurable();
}

TEST_F(StorageInterfaceImplTest, ServiceContextDecorator) {
    auto serviceContext = getGlobalServiceContext();
    ASSERT_FALSE(StorageInterface::get(serviceContext));
    StorageInterface* storageInterface = new StorageInterfaceImpl();
    StorageInterface::set(serviceContext, std::unique_ptr<StorageInterface>(storageInterface));
    ASSERT_TRUE(storageInterface == StorageInterface::get(serviceContext));
}

TEST_F(StorageInterfaceImplTest, DefaultMinValidNamespace) {
    ASSERT_EQUALS(NamespaceString(StorageInterfaceImpl::kDefaultMinValidNamespace),
                  StorageInterfaceImpl().getMinValidNss());
}

TEST_F(StorageInterfaceImplTest, InitialSyncFlag) {
    NamespaceString nss("local.StorageInterfaceImplTest_InitialSyncFlag");

    StorageInterfaceImpl storageInterface(nss);
    auto txn = getClient()->makeOperationContext();

    // Initial sync flag should be unset after initializing a new storage engine.
    ASSERT_FALSE(storageInterface.getInitialSyncFlag(txn.get()));

    // Setting initial sync flag should affect getInitialSyncFlag() result.
    storageInterface.setInitialSyncFlag(txn.get());
    ASSERT_TRUE(storageInterface.getInitialSyncFlag(txn.get()));

    // Check min valid document using storage engine interface.
    auto minValidDocument = getMinValidDocument(txn.get(), nss);
    ASSERT_TRUE(minValidDocument.hasField(StorageInterfaceImpl::kInitialSyncFlagFieldName));
    ASSERT_TRUE(minValidDocument.getBoolField(StorageInterfaceImpl::kInitialSyncFlagFieldName));

    // Clearing initial sync flag should affect getInitialSyncFlag() result.
    storageInterface.clearInitialSyncFlag(txn.get());
    ASSERT_FALSE(storageInterface.getInitialSyncFlag(txn.get()));
}

TEST_F(StorageInterfaceImplTest, MinValid) {
    NamespaceString nss("local.StorageInterfaceImplTest_MinValid");

    StorageInterfaceImpl storageInterface(nss);
    auto txn = getClient()->makeOperationContext();

    // MinValid boundaries should be {null optime, null optime} after initializing a new storage
    // engine.
    auto minValid = storageInterface.getMinValid(txn.get());
    ASSERT_TRUE(minValid.start.isNull());
    ASSERT_TRUE(minValid.end.isNull());

    // Setting min valid boundaries should affect getMinValid() result.
    OpTime startOpTime({Seconds(123), 0}, 1LL);
    OpTime endOpTime({Seconds(456), 0}, 1LL);
    storageInterface.setMinValid(txn.get(), {startOpTime, endOpTime});
    minValid = storageInterface.getMinValid(txn.get());
    ASSERT_EQUALS(BatchBoundaries(startOpTime, endOpTime), minValid);

    // Check min valid document using storage engine interface.
    auto minValidDocument = getMinValidDocument(txn.get(), nss);
    ASSERT_TRUE(minValidDocument.hasField(StorageInterfaceImpl::kBeginFieldName));
    ASSERT_TRUE(minValidDocument[StorageInterfaceImpl::kBeginFieldName].isABSONObj());
    ASSERT_EQUALS(startOpTime,
                  unittest::assertGet(OpTime::parseFromOplogEntry(
                      minValidDocument[StorageInterfaceImpl::kBeginFieldName].Obj())));
    ASSERT_EQUALS(endOpTime, unittest::assertGet(OpTime::parseFromOplogEntry(minValidDocument)));

    // Recovery unit will be owned by "txn".
    RecoveryUnitWithDurabilityTracking* recoveryUnit = new RecoveryUnitWithDurabilityTracking();
    txn->setRecoveryUnit(recoveryUnit, OperationContext::kNotInUnitOfWork);

    // Set min valid without waiting for the changes to be durable.
    OpTime endOpTime2({Seconds(789), 0}, 1LL);
    storageInterface.setMinValid(txn.get(), endOpTime2, DurableRequirement::None);
    minValid = storageInterface.getMinValid(txn.get());
    ASSERT_TRUE(minValid.start.isNull());
    ASSERT_EQUALS(endOpTime2, minValid.end);
    ASSERT_FALSE(recoveryUnit->waitUntilDurableCalled);

    // Set min valid and wait for the changes to be durable.
    OpTime endOpTime3({Seconds(999), 0}, 1LL);
    storageInterface.setMinValid(txn.get(), endOpTime3, DurableRequirement::Strong);
    minValid = storageInterface.getMinValid(txn.get());
    ASSERT_TRUE(minValid.start.isNull());
    ASSERT_EQUALS(endOpTime3, minValid.end);
    ASSERT_TRUE(recoveryUnit->waitUntilDurableCalled);
}

TEST_F(StorageInterfaceImplTest, SnapshotNotSupported) {
    auto txn = getClient()->makeOperationContext();
    Status status = txn->recoveryUnit()->setReadFromMajorityCommittedSnapshot();
    ASSERT_EQUALS(status, ErrorCodes::CommandNotSupported);
}

}  // namespace
