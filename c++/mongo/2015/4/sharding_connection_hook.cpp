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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/platform/basic.h"

#include "mongo/s/client/sharding_connection_hook.h"

#include <string>

#include "mongo/db/audit.h"
#include "mongo/db/auth/authorization_manager_global.h"
#include "mongo/db/auth/internal_user_auth.h"
#include "mongo/s/client/scc_fast_query_handler.h"
#include "mongo/s/cluster_last_error_info.h"
#include "mongo/s/version_manager.h"
#include "mongo/util/log.h"

namespace mongo {

    using std::string;

namespace {

    bool initWireVersion(DBClientBase* conn, std::string* errMsg) {
        BSONObj response;
        if (!conn->runCommand("admin", BSON("isMaster" << 1), response)) {
            *errMsg = str::stream() << "Failed to determine wire version "
                                    << "for internal connection: " << response;
            return false;
        }

        if (response.hasField("minWireVersion") && response.hasField("maxWireVersion")) {
            int minWireVersion = response["minWireVersion"].numberInt();
            int maxWireVersion = response["maxWireVersion"].numberInt();
            conn->setWireVersions(minWireVersion, maxWireVersion);
        }

        return true;
    }

} // namespace


    ShardingConnectionHook::ShardingConnectionHook(bool shardedConnections)
        : _shardedConnections(shardedConnections) {

    }

    void ShardingConnectionHook::onCreate(DBClientBase * conn) {

        // Authenticate as the first thing we do
        // NOTE: Replica set authentication allows authentication against *any* online host
        if (getGlobalAuthorizationManager()->isAuthEnabled()) {
            LOG(2) << "calling onCreate auth for " << conn->toString();

            bool result = authenticateInternalUser(conn);

            uassert(15847, str::stream() << "can't authenticate to server "
                << conn->getServerAddress(),
                result);
        }

        // Initialize the wire version of single connections
        if (conn->type() == ConnectionString::MASTER) {

            LOG(2) << "checking wire version of new connection " << conn->toString();

            // Initialize the wire protocol version of the connection to find out if we
            // can send write commands to this connection.
            string errMsg;
            if (!initWireVersion(conn, &errMsg)) {
                uasserted(17363, errMsg);
            }
        }

        if (_shardedConnections) {
            // For every DBClient created by mongos, add a hook that will capture the response from
            // commands we pass along from the client, so that we can target the correct node when
            // subsequent getLastError calls are made by mongos.
            conn->setPostRunCommandHook(stdx::bind(&saveGLEStats, stdx::placeholders::_1, stdx::placeholders::_2));
        }

        // For every DBClient created by mongos, add a hook that will append impersonated users
        // to the end of every runCommand.  mongod uses this information to produce auditing
        // records attributed to the proper authenticated user(s).
        conn->setRunCommandHook(stdx::bind(&audit::appendImpersonatedUsers, stdx::placeholders::_1));

        // For every SCC created, add a hook that will allow fastest-config-first config reads if
        // the appropriate server options are set.
        if (conn->type() == ConnectionString::SYNC) {
            SyncClusterConnection* scc = dynamic_cast<SyncClusterConnection*>(conn);
            if (scc) {
                scc->attachQueryHandler(new SCCFastQueryHandler);
            }
        }
    }

    void ShardingConnectionHook::onDestroy(DBClientBase * conn) {
        if (_shardedConnections && versionManager.isVersionableCB(conn)){
            versionManager.resetShardVersionCB(conn);
        }
    }

    void ShardingConnectionHook::onRelease(DBClientBase* conn) {
        // This is currently for making the replica set connections release
        // secondary connections to the pool.
        conn->reset();
    }

} // namespace mongo
