/**
 *    Copyright (C) 2015 MongoDB Inc.
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

#include <boost/optional.hpp>
#include <vector>

#include "mongo/bson/bsonobj.h"
#include "mongo/db/cursor_id.h"
#include "mongo/db/namespace_string.h"

namespace mongo {

struct ClusterClientCursorParams {
    /**
     * Contains any CCC parameters that are specified per-remote node.
     */
    struct Remote {
        /**
         * Use when a new cursor should be created on the remote.
         */
        Remote(HostAndPort hostAndPort, BSONObj cmdObj)
            : hostAndPort(std::move(hostAndPort)), cmdObj(std::move(cmdObj)) {}

        /**
         * Use when an a cursor already exists on the remote.  The resulting CCC will take ownership
         * of the existing remote cursor, generating results based on its current state.
         *
         * Note that any results already generated from this cursor will not be returned by the
         * resulting CCC.  The caller is responsible for ensuring that results previously generated
         * by this cursor have been processed.
         */
        Remote(HostAndPort hostAndPort, CursorId cursorId)
            : hostAndPort(std::move(hostAndPort)), cursorId(cursorId) {}

        // How the networking layer should contact this remote.
        HostAndPort hostAndPort;

        // The raw command parameters to send to this remote (e.g. the find command specification).
        //
        // Exactly one of 'cmdObj' or 'cursorId' must be set.
        boost::optional<BSONObj> cmdObj;

        // The cursorId for the remote node, if one already exists.
        //
        // Exactly one of 'cmdObj' or 'cursorId' must be set.
        boost::optional<CursorId> cursorId;
    };

    ClusterClientCursorParams() {}

    ClusterClientCursorParams(NamespaceString nss) : nsString(std::move(nss)) {}

    NamespaceString nsString;

    // Per-remote node data.
    std::vector<Remote> remotes;

    // The sort specification. Leave empty if there is no sort.
    BSONObj sort;

    // The number of results to skip. Optional. Should not be forwarded to the remote hosts in
    // 'cmdObj'.
    boost::optional<long long> skip;

    // The number of results per batch. Optional. If specified, will be specified as the batch for
    // each getMore.
    boost::optional<long long> batchSize;

    // Limits the number of results returned by the ClusterClientCursor to this many. Optional.
    // Should be forwarded to the remote hosts in 'cmdObj'.
    boost::optional<long long> limit;

    // Whether this cursor is tailing a capped collection.
    bool isTailable = false;
};

}  // mongo
