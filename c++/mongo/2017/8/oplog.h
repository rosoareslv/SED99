/**
*    Copyright (C) 2008 10gen Inc.
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

#pragma once

#include <string>
#include <vector>

#include "mongo/base/status.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/timestamp.h"
#include "mongo/db/catalog/collection.h"
#include "mongo/db/catalog/collection_options.h"
#include "mongo/db/repl/optime.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/stdx/functional.h"

namespace mongo {
class Collection;
class Database;
class NamespaceString;
class OperationContext;

namespace repl {
class ReplSettings;

struct PreAndPostImageTimestamps {
    PreAndPostImageTimestamps() = default;
    PreAndPostImageTimestamps(Timestamp _preImageTs, Timestamp _postImageTs)
        : preImageTs(std::move(_preImageTs)), postImageTs(std::move(_postImageTs)) {}

    Timestamp preImageTs;
    Timestamp postImageTs;
};

/**
 * Create a new capped collection for the oplog if it doesn't yet exist.
 * If the collection already exists (and isReplSet is false),
 * set the 'last' Timestamp from the last entry of the oplog collection (side effect!)
 */
void createOplog(OperationContext* opCtx, const std::string& oplogCollectionName, bool isReplSet);

/*
 * Shortcut for above function using oplogCollectionName = _oplogCollectionName,
 * and replEnabled = replCoord::isReplSet();
 */
void createOplog(OperationContext* opCtx);

extern std::string masterSlaveOplogName;

extern int OPLOG_VERSION;

/**
 * Log insert(s) to the local oplog.
 * Returns the OpTime of the last insert.
 */
OpTime logInsertOps(OperationContext* opCtx,
                    const NamespaceString& nss,
                    OptionalCollectionUUID uuid,
                    std::vector<InsertStatement>::const_iterator begin,
                    std::vector<InsertStatement>::const_iterator end,
                    bool fromMigrate);

/**
 * @param opstr
 *  "i" insert
 *  "u" update
 *  "d" delete
 *  "c" db cmd
 *  "n" no-op
 *  "db" declares presence of a database (ns is set to the db name + '.')
 *
 * For 'u' records, 'obj' captures the mutation made to the object but not
 * the object itself. 'o2' captures the the criteria for the object that will be modified.
 *
 * preAndPostImageTs this contains the timestamp of the oplog entry that contains the document
 *   before/after update was applied. The timestamps are ignored if isNull() is true.
 *
 * Returns the optime of the oplog entry written to the oplog.
 * Returns a null optime if oplog was not modified.
 */
OpTime logOp(OperationContext* opCtx,
             const char* opstr,
             const NamespaceString& ns,
             OptionalCollectionUUID uuid,
             const BSONObj& obj,
             const BSONObj* o2,
             bool fromMigrate,
             StmtId stmtId,
             const PreAndPostImageTimestamps& preAndPostTs);

// Flush out the cached pointers to the local database and oplog.
// Used by the closeDatabase command to ensure we don't cache closed things.
void oplogCheckCloseDatabase(OperationContext* opCtx, Database* db);

using IncrementOpsAppliedStatsFn = stdx::function<void()>;
/**
 * Take the object field of a BSONObj, the BSONObj, and the namespace of
 * the operation and perform necessary validation to ensure the BSONObj is a
 * properly-formed command to insert into system.indexes. This is only to
 * be used for insert operations into system.indexes. It is called via applyOps.
 */
std::pair<BSONObj, NamespaceString> prepForApplyOpsIndexInsert(const BSONElement& fieldO,
                                                               const BSONObj& op,
                                                               const NamespaceString& requestNss);
/**
 * Take a non-command op and apply it locally
 * Used for applying from an oplog
 * @param inSteadyStateReplication convert some updates to upserts for idempotency reasons
 * @param incrementOpsAppliedStats is called whenever an op is applied.
 * Returns failure status if the op was an update that could not be applied.
 */
Status applyOperation_inlock(OperationContext* opCtx,
                             Database* db,
                             const BSONObj& op,
                             bool inSteadyStateReplication = false,
                             IncrementOpsAppliedStatsFn incrementOpsAppliedStats = {});

/**
 * Take a command op and apply it locally
 * Used for applying from an oplog
 * inSteadyStateReplication indicates whether we are in steady state replication, rather than
 * initial sync.
 * Returns failure status if the op that could not be applied.
 */
Status applyCommand_inlock(OperationContext* opCtx,
                           const BSONObj& op,
                           bool inSteadyStateReplication);

/**
 * Initializes the global Timestamp with the value from the timestamp of the last oplog entry.
 */
void initTimestampFromOplog(OperationContext* opCtx, const std::string& oplogNS);

/**
 * Sets the global Timestamp to be 'newTime'.
 */
void setNewTimestamp(ServiceContext* opCtx, const Timestamp& newTime);

/**
 * Detects the current replication mode and sets the "_oplogCollectionName" accordingly.
 */
void setOplogCollectionName();

/**
 * Signal any waiting AwaitData queries on the oplog that there is new data or metadata available.
 */
void signalOplogWaiters();

/**
 * Creates a new index in the given namespace.
 */
void createIndexForApplyOps(OperationContext* opCtx,
                            const BSONObj& indexSpec,
                            const NamespaceString& indexNss,
                            IncrementOpsAppliedStatsFn incrementOpsAppliedStats);

}  // namespace repl
}  // namespace mongo
