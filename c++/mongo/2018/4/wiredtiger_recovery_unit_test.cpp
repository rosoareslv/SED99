/**
 *    Copyright (C) 2018 MongoDB Inc.
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

#include "mongo/db/storage/wiredtiger/wiredtiger_recovery_unit.h"
#include "mongo/base/checked_cast.h"
#include "mongo/db/storage/recovery_unit_test_harness.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_kv_engine.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_record_store.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_session_cache.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_util.h"
#include "mongo/unittest/temp_dir.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/clock_source_mock.h"

namespace mongo {
namespace {

class WiredTigerRecoveryUnitHarnessHelper final : public RecoveryUnitHarnessHelper {
public:
    WiredTigerRecoveryUnitHarnessHelper()
        : _dbpath("wt_test"),
          _engine(kWiredTigerEngineName,  // .canonicalName
                  _dbpath.path(),         // .path
                  &_cs,                   // .cs
                  "",                     // .extraOpenOptions
                  1,                      // .cacheSizeGB
                  false,                  // .durable
                  false,                  // .ephemeral
                  false,                  // .repair
                  false                   // .readOnly
                  ) {}

    ~WiredTigerRecoveryUnitHarnessHelper() {}

    virtual std::unique_ptr<RecoveryUnit> newRecoveryUnit() final {
        return std::unique_ptr<RecoveryUnit>(_engine.newRecoveryUnit());
    }

    virtual std::unique_ptr<RecordStore> createRecordStore(OperationContext* opCtx,
                                                           const std::string& ns) final {
        std::string uri = "table:" + ns;
        const bool prefixed = false;
        StatusWith<std::string> result = WiredTigerRecordStore::generateCreateString(
            kWiredTigerEngineName, ns, CollectionOptions(), "", prefixed);
        ASSERT_TRUE(result.isOK());
        std::string config = result.getValue();

        {
            WriteUnitOfWork uow(opCtx);
            WiredTigerRecoveryUnit* ru =
                checked_cast<WiredTigerRecoveryUnit*>(opCtx->recoveryUnit());
            WT_SESSION* s = ru->getSession()->getSession();
            invariantWTOK(s->create(s, uri.c_str(), config.c_str()));
            uow.commit();
        }

        WiredTigerRecordStore::Params params;
        params.ns = ns;
        params.uri = uri;
        params.engineName = kWiredTigerEngineName;
        params.isCapped = false;
        params.isEphemeral = false;
        params.cappedMaxSize = -1;
        params.cappedMaxDocs = -1;
        params.cappedCallback = nullptr;
        params.sizeStorer = nullptr;
        params.isReadOnly = false;

        auto ret = stdx::make_unique<StandardWiredTigerRecordStore>(&_engine, opCtx, params);
        ret->postConstructorInit(opCtx);
        return std::move(ret);
    }

private:
    unittest::TempDir _dbpath;
    ClockSourceMock _cs;
    WiredTigerKVEngine _engine;
};

std::unique_ptr<HarnessHelper> makeHarnessHelper() {
    return stdx::make_unique<WiredTigerRecoveryUnitHarnessHelper>();
}

MONGO_INITIALIZER(RegisterHarnessFactory)(InitializerContext* const) {
    mongo::registerHarnessHelperFactory(makeHarnessHelper);
    return Status::OK();
}

class WiredTigerRecoveryUnitTestFixture : public unittest::Test {
public:
    typedef std::pair<ServiceContext::UniqueClient, ServiceContext::UniqueOperationContext>
        ClientAndCtx;

    ClientAndCtx makeClientAndOpCtx(RecoveryUnitHarnessHelper* harnessHelper,
                                    const std::string& clientName) {
        auto sc = harnessHelper->serviceContext();
        auto client = sc->makeClient(clientName);
        auto opCtx = client->makeOperationContext();
        opCtx->setRecoveryUnit(harnessHelper->newRecoveryUnit().release(),
                               WriteUnitOfWork::RecoveryUnitState::kNotInUnitOfWork);
        return std::make_pair(std::move(client), std::move(opCtx));
    }

    void getCursor(WiredTigerRecoveryUnit* ru, WT_CURSOR** cursor) {
        WT_SESSION* wt_session = ru->getSession()->getSession();
        invariantWTOK(wt_session->create(wt_session, wt_uri, wt_config));
        invariantWTOK(wt_session->open_cursor(wt_session, wt_uri, NULL, NULL, cursor));
    }

    void setUp() override {
        harnessHelper = newRecoveryUnitHarnessHelper();
        clientAndCtx1 = makeClientAndOpCtx(harnessHelper.get(), "writer");
        clientAndCtx2 = makeClientAndOpCtx(harnessHelper.get(), "reader");
        ru1 = checked_cast<WiredTigerRecoveryUnit*>(clientAndCtx1.second->recoveryUnit());
        ru2 = checked_cast<WiredTigerRecoveryUnit*>(clientAndCtx2.second->recoveryUnit());
    }

    std::unique_ptr<RecoveryUnitHarnessHelper> harnessHelper;
    ClientAndCtx clientAndCtx1, clientAndCtx2;
    WiredTigerRecoveryUnit *ru1, *ru2;

private:
    const char* wt_uri = "table:prepare_transaction";
    const char* wt_config = "key_format=S,value_format=S";
};

TEST_F(WiredTigerRecoveryUnitTestFixture,
       LocalReadOnADocumentBeingPreparedTriggersPrepareConflict) {
    // Prepare but don't commit a transaction
    ru1->setReadConcernLevelAndReplicationMode(repl::ReadConcernLevel::kLocalReadConcern,
                                               repl::ReplicationCoordinator::modeNone);
    ru1->beginUnitOfWork(clientAndCtx1.second.get());
    WT_CURSOR* cursor;
    getCursor(ru1, &cursor);
    cursor->set_key(cursor, "key");
    cursor->set_value(cursor, "value");
    invariantWTOK(cursor->insert(cursor));
    ru1->setPrepareTimestamp({1, 1});
    ru1->prepareUnitOfWork();

    // Transaction with local readConcern triggers WT_PREPARE_CONFLICT
    ru2->beginUnitOfWork(clientAndCtx2.second.get());
    ru2->setReadConcernLevelAndReplicationMode(repl::ReadConcernLevel::kLocalReadConcern,
                                               repl::ReplicationCoordinator::modeNone);
    getCursor(ru2, &cursor);
    cursor->set_key(cursor, "key");
    int ret = cursor->search(cursor);
    ASSERT_EQ(WT_PREPARE_CONFLICT, ret);

    ru1->abortUnitOfWork();
    ru2->abortUnitOfWork();
}

TEST_F(WiredTigerRecoveryUnitTestFixture,
       AvailableReadOnADocumentBeingPreparedDoesNotTriggerPrepareConflict) {
    // Prepare but don't commit a transaction
    ru1->setReadConcernLevelAndReplicationMode(repl::ReadConcernLevel::kLocalReadConcern,
                                               repl::ReplicationCoordinator::modeNone);
    ru1->beginUnitOfWork(clientAndCtx1.second.get());
    WT_CURSOR* cursor;
    getCursor(ru1, &cursor);
    cursor->set_key(cursor, "key");
    cursor->set_value(cursor, "value");
    invariantWTOK(cursor->insert(cursor));
    ru1->setPrepareTimestamp({1, 1});
    ru1->prepareUnitOfWork();

    // Transaction with available readConcern wouldn't trigger
    // WT_PREPARE_CONFLICT
    ru2->beginUnitOfWork(clientAndCtx2.second.get());
    ru2->setReadConcernLevelAndReplicationMode(repl::ReadConcernLevel::kAvailableReadConcern,
                                               repl::ReplicationCoordinator::modeNone);
    getCursor(ru2, &cursor);
    cursor->set_key(cursor, "key");
    int ret = cursor->search(cursor);
    ASSERT_EQ(WT_NOTFOUND, ret);

    ru1->abortUnitOfWork();
    ru2->abortUnitOfWork();
}

TEST_F(WiredTigerRecoveryUnitTestFixture, WriteOnADocumentBeingPreparedTriggersWTRollback) {
    // Prepare but don't commit a transaction
    ru1->setReadConcernLevelAndReplicationMode(repl::ReadConcernLevel::kLocalReadConcern,
                                               repl::ReplicationCoordinator::modeNone);
    ru1->beginUnitOfWork(clientAndCtx1.second.get());
    WT_CURSOR* cursor;
    getCursor(ru1, &cursor);
    cursor->set_key(cursor, "key");
    cursor->set_value(cursor, "value");
    invariantWTOK(cursor->insert(cursor));
    ru1->setPrepareTimestamp({1, 1});
    ru1->prepareUnitOfWork();

    // Another transaction with wirte triggers WT_ROLLBACK
    ru2->beginUnitOfWork(clientAndCtx2.second.get());
    getCursor(ru2, &cursor);
    cursor->set_key(cursor, "key");
    cursor->set_value(cursor, "value2");
    int ret = cursor->insert(cursor);
    ASSERT_EQ(WT_ROLLBACK, ret);

    ru1->abortUnitOfWork();
    ru2->abortUnitOfWork();
}

}  // namespace
}  // namespace mongo
