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
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include "mongo/base/disallow_copying.h"
#include "mongo/db/repl/optime.h"
#include "mongo/db/repl/optime_with.h"
#include "mongo/rpc/metadata/repl_set_metadata.h"
#include "mongo/util/net/hostandport.h"
#include "mongo/util/time_support.h"

namespace mongo {
namespace repl {

/**
 * Holds current term and last committed optime necessary to populate find/getMore command requests.
 */
using OpTimeWithTerm = OpTimeWith<long long>;

/**
 * This class represents the interface the DataReplicator uses to interact with the
 * rest of the system.  All functionality of the DataReplicator that would introduce
 * dependencies on large sections of the server code and thus break the unit testability of
 * DataReplicator should be moved here.
 */
class DataReplicatorExternalState {
    MONGO_DISALLOW_COPYING(DataReplicatorExternalState);

public:
    DataReplicatorExternalState() = default;

    virtual ~DataReplicatorExternalState() = default;

    /**
     * Returns the current term and last committed optime.
     * Returns (OpTime::kUninitializedTerm, OpTime()) if not available.
     */
    virtual OpTimeWithTerm getCurrentTermAndLastCommittedOpTime() = 0;

    /**
     * Forwards the parsed metadata in the query results to the replication system.
     */
    virtual void processMetadata(const rpc::ReplSetMetadata& metadata) = 0;

    /**
     * Evaluates quality of sync source. Accepts the current sync source; the last optime on this
     * sync source (from metadata); and whether this sync source has a sync source (also from
     * metadata).
     */
    virtual bool shouldStopFetching(const HostAndPort& source,
                                    const OpTime& sourceOpTime,
                                    bool sourceHasSyncSource) = 0;
};

}  // namespace repl
}  // namespace mongo
