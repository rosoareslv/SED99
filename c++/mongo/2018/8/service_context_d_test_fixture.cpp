/**
 *    Copyright (C) 2016-2018 MongoDB Inc.
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

#include "mongo/db/service_context_d_test_fixture.h"

#include <memory>

#include "mongo/base/checked_cast.h"
#include "mongo/db/catalog/catalog_control.h"
#include "mongo/db/catalog/uuid_catalog.h"
#include "mongo/db/concurrency/d_concurrency.h"
#include "mongo/db/logical_clock.h"
#include "mongo/db/op_observer_registry.h"
#include "mongo/db/service_entry_point_mongod.h"
#include "mongo/db/storage/storage_engine_init.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/mock_periodic_runner_impl.h"

#include "mongo/db/catalog/database_holder.h"

namespace mongo {

ServiceContextMongoDTest::ServiceContextMongoDTest()
    : ServiceContextMongoDTest("ephemeralForTest") {}

ServiceContextMongoDTest::ServiceContextMongoDTest(std::string engine)
    : ServiceContextMongoDTest(engine, RepairAction::kNoRepair) {}

ServiceContextMongoDTest::ServiceContextMongoDTest(std::string engine, RepairAction repair)
    : _tempDir("service_context_d_test_fixture") {

    _stashedStorageParams.engine = std::exchange(storageGlobalParams.engine, std::move(engine));
    _stashedStorageParams.engineSetByUser =
        std::exchange(storageGlobalParams.engineSetByUser, true);
    _stashedStorageParams.repair =
        std::exchange(storageGlobalParams.repair, (repair == RepairAction::kRepair));

    auto const serviceContext = getServiceContext();
    serviceContext->setServiceEntryPoint(std::make_unique<ServiceEntryPointMongod>(serviceContext));
    auto logicalClock = std::make_unique<LogicalClock>(serviceContext);
    LogicalClock::set(serviceContext, std::move(logicalClock));

    // Set up a fake no-op PeriodicRunner. No jobs will ever get run, which is
    // desired behavior for unit tests unrelated to background jobs.
    auto runner = std::make_unique<MockPeriodicRunnerImpl>();
    serviceContext->setPeriodicRunner(std::move(runner));

    storageGlobalParams.dbpath = _tempDir.path();

    initializeStorageEngine(serviceContext, StorageEngineInitFlags::kNone);

    // Set up UUID Catalog observer. This is necessary because the Collection destructor contains an
    // invariant to ensure the UUID corresponding to that Collection object is no longer associated
    // with that Collection object in the UUIDCatalog. UUIDs may be registered in the UUIDCatalog
    // directly in certain code paths, but they can only be removed from the UUIDCatalog via a
    // UUIDCatalogObserver. It is therefore necessary to install the observer to ensure the
    // invariant in the Collection destructor is not triggered.
    auto observerRegistry = checked_cast<OpObserverRegistry*>(serviceContext->getOpObserver());
    observerRegistry->addObserver(std::make_unique<UUIDCatalogObserver>());
}

ServiceContextMongoDTest::~ServiceContextMongoDTest() {
    {
        auto opCtx = getClient()->makeOperationContext();
        Lock::GlobalLock glk(opCtx.get(), MODE_X);
        DatabaseHolder::getDatabaseHolder().closeAll(opCtx.get(), "all databases dropped");
    }
    shutdownGlobalStorageEngineCleanly(getGlobalServiceContext());
    std::swap(storageGlobalParams.engine, _stashedStorageParams.engine);
    std::swap(storageGlobalParams.engineSetByUser, _stashedStorageParams.engineSetByUser);
    std::swap(storageGlobalParams.repair, _stashedStorageParams.repair);
}

}  // namespace mongo
