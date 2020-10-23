/**
 *    Copyright (C) 2017 MongoDB Inc.
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

#include "mongo/client/dbclientinterface.h"
#include "mongo/client/index_spec.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/logical_session_id.h"
#include "mongo/db/logical_session_id_helpers.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/sessions_collection.h"
#include "mongo/db/sessions_collection_standalone.h"
#include "mongo/dbtests/dbtests.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/time_support.h"

namespace LogicalSessionTests {

namespace {
constexpr StringData kTestNS = "admin.system.sessions"_sd;

LogicalSessionRecord makeRecord(Date_t time = Date_t::now()) {
    auto record = makeLogicalSessionRecordForTest();
    record.setLastUse(time);
    return record;
}

Status insertRecord(OperationContext* opCtx, LogicalSessionRecord record) {
    DBDirectClient client(opCtx);

    client.insert(kTestNS.toString(), record.toBSON());
    auto errorString = client.getLastError();
    if (errorString.empty()) {
        return Status::OK();
    }

    return {ErrorCodes::DuplicateSession, errorString};
}

BSONObj lsidQuery(const LogicalSessionId& lsid) {
    return BSON(LogicalSessionRecord::kIdFieldName << lsid.toBSON());
}

StatusWith<LogicalSessionRecord> fetchRecord(OperationContext* opCtx,
                                             const LogicalSessionId& lsid) {
    DBDirectClient client(opCtx);
    auto cursor = client.query(kTestNS.toString(), lsidQuery(lsid), 1);
    if (!cursor->more()) {
        return {ErrorCodes::NoSuchSession, "No matching record in the sessions collection"};
    }

    try {
        IDLParserErrorContext ctx("LogicalSessionRecord");
        return LogicalSessionRecord::parse(ctx, cursor->next());
    } catch (...) {
        return exceptionToStatus();
    }
}

}  // namespace

class SessionsCollectionStandaloneTest {
public:
    SessionsCollectionStandaloneTest()
        : _collection(stdx::make_unique<SessionsCollectionStandalone>()) {
        _opCtx = cc().makeOperationContext();
        DBDirectClient db(opCtx());
        db.remove(ns(), BSONObj());
    }

    virtual ~SessionsCollectionStandaloneTest() {
        DBDirectClient db(opCtx());
        db.remove(ns(), BSONObj());
        _opCtx.reset();
    }

    SessionsCollectionStandalone* collection() {
        return _collection.get();
    }

    OperationContext* opCtx() {
        return _opCtx.get();
    }

    std::string ns() {
        return SessionsCollection::kSessionsFullNS.toString();
    }

private:
    std::unique_ptr<SessionsCollectionStandalone> _collection;
    ServiceContext::UniqueOperationContext _opCtx;
};

// Test that removal from this collection works.
class SessionsCollectionStandaloneRemoveTest : public SessionsCollectionStandaloneTest {
public:
    void run() {
        auto record1 = makeRecord();
        auto record2 = makeRecord();

        auto res = insertRecord(opCtx(), record1);
        ASSERT_OK(res);
        res = insertRecord(opCtx(), record2);
        ASSERT_OK(res);

        // Remove one record, the other stays
        res = collection()->removeRecords(opCtx(), {record1.getId()});
        ASSERT_OK(res);

        auto swRecord = fetchRecord(opCtx(), record1.getId());
        ASSERT(!swRecord.isOK());

        swRecord = fetchRecord(opCtx(), record2.getId());
        ASSERT(swRecord.isOK());
    }
};

// Test that refreshing entries in this collection works.
class SessionsCollectionStandaloneRefreshTest : public SessionsCollectionStandaloneTest {
public:
    void run() {
        DBDirectClient db(opCtx());

        auto now = Date_t::now();
        auto thePast = now - Minutes(5);

        // Attempt to refresh with no active records, should succeed (and do nothing).
        auto resRefresh = collection()->refreshSessions(opCtx(), LogicalSessionRecordSet{}, now);
        ASSERT(resRefresh.isOK());

        // Attempt to refresh one active record, should succeed.
        auto record1 = makeRecord(thePast);
        auto res = insertRecord(opCtx(), record1);
        ASSERT_OK(res);
        resRefresh = collection()->refreshSessions(opCtx(), {record1}, now);
        ASSERT(resRefresh.isOK());

        // The timestamp on the refreshed record should be updated.
        auto swRecord = fetchRecord(opCtx(), record1.getId());
        ASSERT(swRecord.isOK());
        ASSERT_EQ(swRecord.getValue().getLastUse(), now);

        // Clear the collection.
        db.remove(ns(), BSONObj());

        // Attempt to refresh a record that is not present, should upsert it.
        auto record2 = makeRecord(thePast);
        resRefresh = collection()->refreshSessions(opCtx(), {record2}, now);
        ASSERT(resRefresh.isOK());

        swRecord = fetchRecord(opCtx(), record2.getId());
        ASSERT(swRecord.isOK());

        // Clear the collection.
        db.remove(ns(), BSONObj());

        // Attempt a refresh of many records, split into batches.
        LogicalSessionRecordSet toRefresh;
        int recordCount = 5000;
        for (int i = 0; i < recordCount; i++) {
            auto record = makeRecord(thePast);
            res = insertRecord(opCtx(), record);

            // Refresh some of these records.
            if (i % 4 == 0) {
                toRefresh.insert(record);
            }
        }

        // Run the refresh, should succeed.
        resRefresh = collection()->refreshSessions(opCtx(), toRefresh, now);
        ASSERT(resRefresh.isOK());

        // Ensure that the right number of timestamps were updated.
        auto n = db.count(ns(), BSON("lastUse" << now));
        ASSERT_EQ(n, toRefresh.size());
    }
};

class All : public Suite {
public:
    All() : Suite("logical_sessions") {}

    void setupTests() {
        add<SessionsCollectionStandaloneRemoveTest>();
        add<SessionsCollectionStandaloneRefreshTest>();
    }
};

SuiteInstance<All> all;

}  // namespace LogicalSessionTests
