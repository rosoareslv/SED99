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

#include <cstddef>
#include <deque>
#include <string>

#include "mongo/base/status.h"
#include "mongo/base/disallow_copying.h"
#include "mongo/util/concurrency/mutex.h"

namespace mongo {
    class BSONObj;
    class Collection;
    struct CollectionOptions;
    class Database;
    class NamespaceString;
    class OperationContext;
    class Timestamp;
    class RecordId;

namespace repl {
    class ReplicationCoordinator;
 
    // Create a new capped collection for the oplog if it doesn't yet exist.
    // This will be either local.oplog.rs (replica sets) or local.oplog.$main (master/slave)
    // If the collection already exists, set the 'last' OpTime if master/slave (side effect!)
    void createOplog(OperationContext* txn);

    // This function writes ops into the replica-set oplog;
    // used internally by replication secondaries after they have applied ops.  Updates the global
    // optime.
    // Returns the optime for the last op inserted.
    Timestamp writeOpsToOplog(OperationContext* txn,
                           const std::deque<BSONObj>& ops);

    extern std::string rsOplogName;
    extern std::string masterSlaveOplogName;

    extern int OPLOG_VERSION;

    /** Log an operation to the local oplog 
     *
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
     */
    void _logOp(OperationContext* txn,
                const char *opstr,
                const char *ns,
                const BSONObj& obj,
                BSONObj *o2,
                bool fromMigrate);

    // Flush out the cached pointers to the local database and oplog.
    // Used by the closeDatabase command to ensure we don't cache closed things.
    void oplogCheckCloseDatabase(OperationContext* txn, Database * db);

    /**
     * take an op and apply locally
     * used for applying from an oplog
     * @param convertUpdateToUpsert convert some updates to upserts for idempotency reasons
     * Returns failure status if the op was an update that could not be applied.
     */
    Status applyOperation_inlock(OperationContext* txn,
                                 Database* db,
                                 const BSONObj& op,
                                 bool convertUpdateToUpsert = false);

    /**
     * Waits one second for the Timestamp from the oplog to change.
     */
    void waitUpToOneSecondForTimestampChange(const Timestamp& referenceTime);

    /**
     * Initializes the global OpTime with the value from the timestamp of the last oplog entry.
     */
    void initOpTimeFromOplog(OperationContext* txn, const std::string& oplogNS);

    /**
     * Sets the global OpTime to be 'newTime'.
     */
    void setNewOptime(const Timestamp& newTime);

    /**
     * Detects the current replication mode and sets the "_oplogCollectionName" accordingly.
     */
    void setOplogCollectionName();
} // namespace repl
} // namespace mongo
