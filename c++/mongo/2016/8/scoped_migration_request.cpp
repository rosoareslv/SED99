/**
 *    Copyright (C) 2016 MongoDB Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/platform/basic.h"

#include "mongo/s/balancer/scoped_migration_request.h"

#include "mongo/db/repl/replication_coordinator_global.h"
#include "mongo/db/write_concern_options.h"
#include "mongo/s/balancer/type_migration.h"
#include "mongo/s/catalog/sharding_catalog_client.h"
#include "mongo/s/grid.h"
#include "mongo/util/log.h"

namespace mongo {

namespace {
const WriteConcernOptions kMajorityWriteConcern(WriteConcernOptions::kMajority,
                                                WriteConcernOptions::SyncMode::UNSET,
                                                Seconds(15));
}

ScopedMigrationRequest::ScopedMigrationRequest(OperationContext* txn,
                                               const NamespaceString& nss,
                                               const BSONObj& minKey)
    : _txn(txn), _nss(nss), _minKey(minKey) {}

ScopedMigrationRequest::~ScopedMigrationRequest() {
    if (!_txn) {
        // If the txn object was cleared, nothing should happen in the destructor.
        return;
    }

    // Try to delete the entry in the config.migrations collection. If the command fails, that is
    // okay.
    BSONObj migrationDocumentIdentifier =
        BSON(MigrationType::ns(_nss.ns()) << MigrationType::min(_minKey));
    Status result = grid.catalogClient(_txn)->removeConfigDocuments(
        _txn, MigrationType::ConfigNS, migrationDocumentIdentifier, kMajorityWriteConcern);

    if (!result.isOK()) {
        LOG(0) << "Failed to remove config.migrations document for migration '"
               << migrationDocumentIdentifier.toString() << "'" << causedBy(redact(result));
    }
}

ScopedMigrationRequest::ScopedMigrationRequest(ScopedMigrationRequest&& other) {
    *this = std::move(other);
    // Set txn to null so that the destructor will do nothing.
    other._txn = nullptr;
}

ScopedMigrationRequest& ScopedMigrationRequest::operator=(ScopedMigrationRequest&& other) {
    if (this != &other) {
        _txn = other._txn;
        _nss = other._nss;
        _minKey = other._minKey;
        // Set txn to null so that the destructor will do nothing.
        other._txn = nullptr;
    }

    return *this;
}

StatusWith<ScopedMigrationRequest> ScopedMigrationRequest::writeMigration(
    OperationContext* txn,
    const MigrateInfo& migrateInfo,
    const ChunkVersion& chunkVersion,
    const ChunkVersion& collectionVersion) {

    // Try to write a unique migration document to config.migrations.
    MigrationType migrationType(migrateInfo, chunkVersion, collectionVersion);
    Status result = grid.catalogClient(txn)->insertConfigDocument(
        txn, MigrationType::ConfigNS, migrationType.toBSON(), kMajorityWriteConcern);

    if (result == ErrorCodes::DuplicateKey) {
        return result;
    }

    // As long as there isn't a DuplicateKey error, the document may have been written, and it's
    // safe (won't delete another migration's document) and necessary to try to clean up the
    // document via the destructor.
    ScopedMigrationRequest scopedMigrationRequest(
        txn, NamespaceString(migrateInfo.ns), migrateInfo.minKey);

    // If there was a write error, let the object go out of scope and clean up in the destructor.
    if (!result.isOK()) {
        return result;
    }

    return std::move(scopedMigrationRequest);
}

ScopedMigrationRequest ScopedMigrationRequest::createForRecovery(OperationContext* txn,
                                                                 const NamespaceString& nss,
                                                                 const BSONObj& minKey) {
    return ScopedMigrationRequest(txn, nss, minKey);
}

void ScopedMigrationRequest::keepDocumentOnDestruct() {
    _txn = nullptr;
}

}  // namespace mongo
