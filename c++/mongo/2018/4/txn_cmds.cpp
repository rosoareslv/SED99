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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kCommand

#include "mongo/platform/basic.h"

#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/commands.h"
#include "mongo/db/commands/test_commands_enabled.h"
#include "mongo/db/op_observer.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/db/session_catalog.h"
#include "mongo/util/fail_point_service.h"

namespace mongo {
namespace {

class CmdCommitTxn : public BasicCommand {
public:
    CmdCommitTxn() : BasicCommand("commitTransaction") {}

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kAlways;
    }

    virtual bool adminOnly() const {
        return true;
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return true;
    }

    std::string help() const override {
        return "Commits a transaction";
    }

    Status checkAuthForOperation(OperationContext* opCtx,
                                 const std::string& dbname,
                                 const BSONObj& cmdObj) const override {
        return Status::OK();
    }

    bool run(OperationContext* opCtx,
             const std::string& dbname,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) override {
        auto session = OperationContextSession::get(opCtx);
        uassert(
            ErrorCodes::CommandFailed, "commitTransaction must be run within a session", session);

        // commitTransaction is retryable.
        if (session->transactionIsCommitted()) {
            return true;
        }

        uassert(ErrorCodes::NoSuchTransaction,
                "Transaction isn't in progress",
                session->inMultiDocumentTransaction());

        session->commitTransaction(opCtx);

        return true;
    }

} commitTxn;

MONGO_FP_DECLARE(pauseAfterTransactionPrepare);

// TODO: This is a stub for testing storage prepare functionality.
class CmdPrepareTxn : public BasicCommand {
public:
    CmdPrepareTxn() : BasicCommand("prepareTransaction") {}

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kNever;
    }

    virtual bool adminOnly() const {
        return true;
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return true;
    }

    std::string help() const override {
        return "Preprares a transaction. THIS IS A STUB FOR TESTING.";
    }

    Status checkAuthForOperation(OperationContext* opCtx,
                                 const std::string& dbname,
                                 const BSONObj& cmdObj) const override {
        return Status::OK();
    }

    bool run(OperationContext* opCtx,
             const std::string& dbname,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) override {
        auto session = OperationContextSession::get(opCtx);
        uassert(
            ErrorCodes::CommandFailed, "prepareTransaction must be run within a session", session);

        uassert(ErrorCodes::NoSuchTransaction,
                "Transaction isn't in progress",
                session->inMultiDocumentTransaction());

        auto opObserver = opCtx->getServiceContext()->getOpObserver();
        invariant(opObserver);
        opObserver->onTransactionPrepare(opCtx);

        // For testing purposes, this command prepares and immediately aborts the transaction,
        // Running commit after prepare is not allowed yet.
        // Prepared units of work cannot be released by the session, so we immediately abort here.
        opCtx->getWriteUnitOfWork()->prepare();
        // This failpoint will cause readers of prepared documents to return prepare conflicts.
        MONGO_FAIL_POINT_PAUSE_WHILE_SET(pauseAfterTransactionPrepare);
        session->abortActiveTransaction(opCtx);
        return true;
    }
};

MONGO_INITIALIZER(RegisterPrepareTxnCmd)(InitializerContext* context) {
    if (getTestCommandsEnabled()) {
        new CmdPrepareTxn();
    }
    return Status::OK();
}

class CmdAbortTxn : public BasicCommand {
public:
    CmdAbortTxn() : BasicCommand("abortTransaction") {}

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kAlways;
    }

    virtual bool adminOnly() const {
        return true;
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return true;
    }

    std::string help() const override {
        return "Aborts a transaction";
    }

    Status checkAuthForOperation(OperationContext* opCtx,
                                 const std::string& dbname,
                                 const BSONObj& cmdObj) const override {
        return Status::OK();
    }

    bool run(OperationContext* opCtx,
             const std::string& dbname,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) override {
        auto session = OperationContextSession::get(opCtx);
        uassert(
            ErrorCodes::CommandFailed, "abortTransaction must be run within a session", session);

        // TODO SERVER-33501 Change this when abortTransaction is retryable.
        uassert(ErrorCodes::NoSuchTransaction,
                "Transaction isn't in progress",
                session->inMultiDocumentTransaction());

        session->abortActiveTransaction(opCtx);
        return true;
    }

} abortTxn;

}  // namespace
}  // namespace mongo
