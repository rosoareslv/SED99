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

#include "mongo/base/init.h"
#include "mongo/db/auth/action_set.h"
#include "mongo/db/auth/action_type.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/auth/privilege.h"
#include "mongo/db/client.h"
#include "mongo/db/commands.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/logical_session_cache.h"
#include "mongo/db/logical_session_id.h"
#include "mongo/db/logical_session_id_helpers.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/stats/top.h"

namespace mongo {

class StartSessionCommand final : public BasicCommand {
    MONGO_DISALLOW_COPYING(StartSessionCommand);

public:
    StartSessionCommand() : BasicCommand("startSession") {}

    bool slaveOk() const override {
        return true;
    }
    bool adminOnly() const override {
        return false;
    }
    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }
    void help(std::stringstream& help) const override {
        help << "start a logical session";
    }
    Status checkAuthForOperation(OperationContext* opCtx,
                                 const std::string& dbname,
                                 const BSONObj& cmdObj) override {
        // Anybody may start a session. The command body below checks
        // that only a single user is logged in.
        return Status::OK();
    }

    virtual bool run(OperationContext* opCtx,
                     const std::string& db,
                     const BSONObj& cmdObj,
                     BSONObjBuilder& result) override {

        auto client = opCtx->getClient();
        ServiceContext* serviceContext = client->getServiceContext();

        auto lsCache = LogicalSessionCache::get(serviceContext);
        boost::optional<LogicalSessionRecord> record;

        try {
            record = makeLogicalSessionRecord(opCtx, lsCache->now());
        } catch (...) {
            auto status = exceptionToStatus();

            return appendCommandStatus(result, status);
        }

        Status startSessionStatus = lsCache->startSession(opCtx, record.get());

        if (!startSessionStatus.isOK()) {
            return appendCommandStatus(result, startSessionStatus);
        }

        makeLogicalSessionToClient(record->getId()).serialize(&result);

        return true;
    }
} startSessionCommand;

}  // namespace mongo
