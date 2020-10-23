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

#include "mongo/db/s/collection_range_deleter.h"

#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/client/query.h"
#include "mongo/client/remote_command_targeter_mock.h"
#include "mongo/db/client.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/index/index_descriptor.h"
#include "mongo/db/keypattern.h"
#include "mongo/db/repl/replication_coordinator_global.h"
#include "mongo/db/repl/replication_coordinator_mock.h"
#include "mongo/db/s/collection_sharding_state.h"
#include "mongo/db/s/sharding_state.h"
#include "mongo/db/service_context_d_test_fixture.h"
#include "mongo/s/catalog/dist_lock_catalog_impl.h"
#include "mongo/s/catalog/dist_lock_manager_mock.h"
#include "mongo/s/catalog/sharding_catalog_client_mock.h"
#include "mongo/s/chunk_version.h"
#include "mongo/s/client/shard_registry.h"
#include "mongo/s/sharding_mongod_test_fixture.h"

namespace mongo {

using unittest::assertGet;

const NamespaceString kNss = NamespaceString("foo", "bar");
const std::string kPattern = "_id";
const BSONObj kKeyPattern = BSON(kPattern << 1);
const std::string kShardName{"a"};
const HostAndPort dummyHost("dummy", 123);

class CollectionRangeDeleterTest : public ShardingMongodTestFixture {
protected:
    bool next(CollectionRangeDeleter& rangeDeleter, int maxToDelete) {
        return CollectionRangeDeleter::cleanUpNextRange(
            operationContext(), kNss, maxToDelete, &rangeDeleter);
    }
    std::shared_ptr<RemoteCommandTargeterMock> configTargeter() {
        return RemoteCommandTargeterMock::get(shardRegistry()->getConfigShard()->getTargeter());
    }


private:
    void setUp() override;
    void tearDown() override;

    std::unique_ptr<DistLockCatalog> makeDistLockCatalog(ShardRegistry* shardRegistry) override {
        invariant(shardRegistry);
        return stdx::make_unique<DistLockCatalogImpl>(shardRegistry);
    }

    std::unique_ptr<DistLockManager> makeDistLockManager(
        std::unique_ptr<DistLockCatalog> distLockCatalog) override {
        return stdx::make_unique<DistLockManagerMock>(std::move(distLockCatalog));
    }

    std::unique_ptr<ShardingCatalogClient> makeShardingCatalogClient(
        std::unique_ptr<DistLockManager> distLockManager) override {
        return stdx::make_unique<ShardingCatalogClientMock>(std::move(distLockManager));
    }
};

void CollectionRangeDeleterTest::setUp() {
    serverGlobalParams.clusterRole = ClusterRole::ShardServer;
    ShardingMongodTestFixture::setUp();
    replicationCoordinator()->alwaysAllowWrites(true);
    initializeGlobalShardingStateForMongodForTest(ConnectionString(dummyHost));

    // RemoteCommandTargeterMock::get(shardRegistry()->getConfigShard()->getTargeter())
    //     ->setConnectionStringReturnValue(kConfigConnStr);

    configTargeter()->setFindHostReturnValue(dummyHost);

    DBDirectClient(operationContext()).createCollection(kNss.ns());
    {
        AutoGetCollection autoColl(operationContext(), kNss, MODE_IX);
        auto collectionShardingState = CollectionShardingState::get(operationContext(), kNss);
        const OID epoch = OID::gen();
        collectionShardingState->refreshMetadata(
            operationContext(),
            stdx::make_unique<CollectionMetadata>(
                kKeyPattern,
                ChunkVersion(1, 0, epoch),
                ChunkVersion(0, 0, epoch),
                SimpleBSONObjComparator::kInstance.makeBSONObjIndexedMap<CachedChunkInfo>()));
    }
}

void CollectionRangeDeleterTest::tearDown() {
    {
        AutoGetCollection autoColl(operationContext(), kNss, MODE_IX);
        auto collectionShardingState = CollectionShardingState::get(operationContext(), kNss);
        collectionShardingState->refreshMetadata(operationContext(), nullptr);
    }
    ShardingMongodTestFixture::tearDown();
}

namespace {

// Tests the case that there is nothing in the database.
TEST_F(CollectionRangeDeleterTest, EmptyDatabase) {
    CollectionRangeDeleter rangeDeleter;
    ASSERT_FALSE(next(rangeDeleter, 1));
}

// Tests the case that there is data, but it is not in a range to clean.
TEST_F(CollectionRangeDeleterTest, NoDataInGivenRangeToClean) {
    CollectionRangeDeleter rangeDeleter;
    const BSONObj insertedDoc = BSON(kPattern << 25);
    DBDirectClient dbclient(operationContext());
    dbclient.insert(kNss.toString(), insertedDoc);
    ASSERT_BSONOBJ_EQ(insertedDoc, dbclient.findOne(kNss.toString(), QUERY(kPattern << 25)));

    rangeDeleter.add(ChunkRange(BSON(kPattern << 0), BSON(kPattern << 10)));
    ASSERT_TRUE(next(rangeDeleter, 1));

    ASSERT_BSONOBJ_EQ(insertedDoc, dbclient.findOne(kNss.toString(), QUERY(kPattern << 25)));

    ASSERT_FALSE(next(rangeDeleter, 1));
}

// Tests the case that there is a single document within a range to clean.
TEST_F(CollectionRangeDeleterTest, OneDocumentInOneRangeToClean) {
    CollectionRangeDeleter rangeDeleter;
    const BSONObj insertedDoc = BSON(kPattern << 5);
    DBDirectClient dbclient(operationContext());
    dbclient.insert(kNss.toString(), BSON(kPattern << 5));
    ASSERT_BSONOBJ_EQ(insertedDoc, dbclient.findOne(kNss.toString(), QUERY(kPattern << 5)));

    rangeDeleter.add(ChunkRange(BSON(kPattern << 0), BSON(kPattern << 10)));

    ASSERT_TRUE(next(rangeDeleter, 1));
    ASSERT_TRUE(next(rangeDeleter, 1));
    ASSERT_TRUE(dbclient.findOne(kNss.toString(), QUERY(kPattern << 5)).isEmpty());
    ASSERT_FALSE(next(rangeDeleter, 1));
}

// Tests the case that there are multiple documents within a range to clean.
TEST_F(CollectionRangeDeleterTest, MultipleDocumentsInOneRangeToClean) {
    CollectionRangeDeleter rangeDeleter;
    DBDirectClient dbclient(operationContext());
    dbclient.insert(kNss.toString(), BSON(kPattern << 1));
    dbclient.insert(kNss.toString(), BSON(kPattern << 2));
    dbclient.insert(kNss.toString(), BSON(kPattern << 3));
    ASSERT_EQUALS(3ULL, dbclient.count(kNss.toString(), BSON(kPattern << LT << 5)));

    rangeDeleter.add(ChunkRange(BSON(kPattern << 0), BSON(kPattern << 10)));

    ASSERT_TRUE(next(rangeDeleter, 100));
    ASSERT_TRUE(next(rangeDeleter, 100));
    ASSERT_EQUALS(0ULL, dbclient.count(kNss.toString(), BSON(kPattern << LT << 5)));
    ASSERT_FALSE(next(rangeDeleter, 100));
}

// Tests the case that there are multiple documents within a range to clean, and the range deleter
// has a max deletion rate of one document per run.
TEST_F(CollectionRangeDeleterTest, MultipleCleanupNextRangeCalls) {
    CollectionRangeDeleter rangeDeleter;
    DBDirectClient dbclient(operationContext());
    dbclient.insert(kNss.toString(), BSON(kPattern << 1));
    dbclient.insert(kNss.toString(), BSON(kPattern << 2));
    dbclient.insert(kNss.toString(), BSON(kPattern << 3));
    ASSERT_EQUALS(3ULL, dbclient.count(kNss.toString(), BSON(kPattern << LT << 5)));

    rangeDeleter.add(ChunkRange(BSON(kPattern << 0), BSON(kPattern << 10)));

    ASSERT_TRUE(next(rangeDeleter, 1));
    ASSERT_EQUALS(2ULL, dbclient.count(kNss.toString(), BSON(kPattern << LT << 5)));

    ASSERT_TRUE(next(rangeDeleter, 1));
    ASSERT_EQUALS(1ULL, dbclient.count(kNss.toString(), BSON(kPattern << LT << 5)));

    ASSERT_TRUE(next(rangeDeleter, 1));
    ASSERT_TRUE(next(rangeDeleter, 1));
    ASSERT_EQUALS(0ULL, dbclient.count(kNss.toString(), BSON(kPattern << LT << 5)));
    ASSERT_FALSE(next(rangeDeleter, 1));
}


// Tests the case that there are two ranges to clean, each containing multiple documents.
TEST_F(CollectionRangeDeleterTest, MultipleDocumentsInMultipleRangesToClean) {
    CollectionRangeDeleter rangeDeleter;
    DBDirectClient dbclient(operationContext());
    dbclient.insert(kNss.toString(), BSON(kPattern << 1));
    dbclient.insert(kNss.toString(), BSON(kPattern << 2));
    dbclient.insert(kNss.toString(), BSON(kPattern << 3));
    dbclient.insert(kNss.toString(), BSON(kPattern << 4));
    dbclient.insert(kNss.toString(), BSON(kPattern << 5));
    dbclient.insert(kNss.toString(), BSON(kPattern << 6));
    ASSERT_EQUALS(6ULL, dbclient.count(kNss.toString(), BSON(kPattern << LT << 10)));

    const ChunkRange chunkRange1 = ChunkRange(BSON(kPattern << 0), BSON(kPattern << 4));
    const ChunkRange chunkRange2 = ChunkRange(BSON(kPattern << 4), BSON(kPattern << 7));
    rangeDeleter.add(chunkRange1);
    rangeDeleter.add(chunkRange2);

    ASSERT_TRUE(next(rangeDeleter, 100));
    ASSERT_EQUALS(0ULL, dbclient.count(kNss.toString(), BSON(kPattern << LT << 4)));
    ASSERT_EQUALS(3ULL, dbclient.count(kNss.toString(), BSON(kPattern << LT << 10)));

    ASSERT_TRUE(next(rangeDeleter, 100));  // discover there are no more < 4, pop range 1
    ASSERT_TRUE(next(rangeDeleter, 100));  // delete the remaining documents
    ASSERT_TRUE(next(rangeDeleter, 1));    // discover there are no more, pop range 2
    ASSERT_EQUALS(0ULL, dbclient.count(kNss.toString(), BSON(kPattern << LT << 10)));
    ASSERT_FALSE(next(rangeDeleter, 1));  // discover there are no more ranges
}

}  // unnamed namespace
}  // namespace mongo
