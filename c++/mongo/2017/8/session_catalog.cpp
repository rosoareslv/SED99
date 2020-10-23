/**
 *    Copyright (C) 2017 MongoDB, Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kWrite

#include "mongo/platform/basic.h"

#include "mongo/db/session_catalog.h"

#include <boost/optional.hpp>

#include "mongo/db/db_raii.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/rpc/get_status_from_command_result.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {

struct CheckedOutSession {
    CheckedOutSession(ScopedCheckedOutSession&& session) : scopedSession(std::move(session)) {}

    ScopedCheckedOutSession scopedSession;

    // This number gets incremented every time a request tries to check out this session, including
    // the cases when it was already checked out. Level of 0 means that it's available or is
    // completely released.
    int checkOutNestingLevel = 0;
};

const auto sessionTransactionTableDecoration =
    ServiceContext::declareDecoration<boost::optional<SessionCatalog>>();

const auto operationSessionDecoration =
    OperationContext::declareDecoration<boost::optional<CheckedOutSession>>();

}  // namespace

SessionCatalog::SessionCatalog(ServiceContext* serviceContext) : _serviceContext(serviceContext) {}

SessionCatalog::~SessionCatalog() = default;

void SessionCatalog::create(ServiceContext* service) {
    auto& sessionTransactionTable = sessionTransactionTableDecoration(service);
    invariant(!sessionTransactionTable);

    sessionTransactionTable.emplace(service);
}

void SessionCatalog::reset_forTest(ServiceContext* service) {
    auto& sessionTransactionTable = sessionTransactionTableDecoration(service);
    sessionTransactionTable.reset();
}

SessionCatalog* SessionCatalog::get(OperationContext* opCtx) {
    return get(opCtx->getServiceContext());
}

SessionCatalog* SessionCatalog::get(ServiceContext* service) {
    auto& sessionTransactionTable = sessionTransactionTableDecoration(service);
    invariant(sessionTransactionTable);

    return sessionTransactionTable.get_ptr();
}

boost::optional<UUID> SessionCatalog::getTransactionTableUUID(OperationContext* opCtx) {
    AutoGetCollection autoColl(opCtx, NamespaceString::kSessionTransactionsTableNamespace, MODE_IS);

    const auto coll = autoColl.getCollection();
    if (coll == nullptr) {
        return boost::none;
    }

    return coll->uuid();
}

void SessionCatalog::onStepUp(OperationContext* opCtx) {
    DBDirectClient client(opCtx);

    const size_t initialExtentSize = 0;
    const bool capped = false;
    const bool maxSize = 0;

    BSONObj result;

    if (client.createCollection(NamespaceString::kSessionTransactionsTableNamespace.ns(),
                                initialExtentSize,
                                capped,
                                maxSize,
                                &result)) {
        return;
    }

    const auto status = getStatusFromCommandResult(result);

    if (status == ErrorCodes::NamespaceExists) {
        return;
    }

    uasserted(status.code(),
              str::stream() << "Failed to create the "
                            << NamespaceString::kSessionTransactionsTableNamespace.ns()
                            << " collection due to "
                            << status.reason());
}

ScopedCheckedOutSession SessionCatalog::checkOutSession(OperationContext* opCtx) {
    invariant(!opCtx->lockState()->isLocked());
    invariant(opCtx->getLogicalSessionId());

    stdx::unique_lock<stdx::mutex> ul(_mutex);

    auto sri = _getOrCreateSessionRuntimeInfo_inlock(opCtx, *opCtx->getLogicalSessionId(), ul);

    // Wait until the session is no longer in use
    opCtx->waitForConditionOrInterrupt(
        sri->availableCondVar, ul, [sri]() { return sri->state != SessionRuntimeInfo::kInUse; });

    invariant(sri->state == SessionRuntimeInfo::kAvailable);
    sri->state = SessionRuntimeInfo::kInUse;

    return ScopedCheckedOutSession(opCtx, ScopedSession(std::move(sri)));
}

ScopedSession SessionCatalog::getOrCreateSession(OperationContext* opCtx,
                                                 const LogicalSessionId& lsid) {
    stdx::unique_lock<stdx::mutex> ul(_mutex);

    return ScopedSession(_getOrCreateSessionRuntimeInfo_inlock(opCtx, lsid, ul));
}

void SessionCatalog::resetSessions() {
    stdx::lock_guard<stdx::mutex> lg(_mutex);
    for (const auto& it : _txnTable) {
        it.second->txnState.reset();
    }
}

std::shared_ptr<SessionCatalog::SessionRuntimeInfo>
SessionCatalog::_getOrCreateSessionRuntimeInfo_inlock(OperationContext* opCtx,
                                                      const LogicalSessionId& lsid,
                                                      stdx::unique_lock<stdx::mutex>& ul) {
    invariant(!opCtx->lockState()->inAWriteUnitOfWork());

    auto it = _txnTable.find(lsid);
    if (it == _txnTable.end()) {
        it = _txnTable.emplace(lsid, std::make_shared<SessionRuntimeInfo>(lsid)).first;
    }

    return it->second;
}

void SessionCatalog::_releaseSession(const LogicalSessionId& lsid) {
    stdx::lock_guard<stdx::mutex> lg(_mutex);

    auto it = _txnTable.find(lsid);
    invariant(it != _txnTable.end());

    auto& sri = it->second;
    invariant(sri->state == SessionRuntimeInfo::kInUse);

    sri->state = SessionRuntimeInfo::kAvailable;
    sri->availableCondVar.notify_one();
}

OperationContextSession::OperationContextSession(OperationContext* opCtx) : _opCtx(opCtx) {
    if (!opCtx->getLogicalSessionId()) {
        return;
    }

    auto& checkedOutSession = operationSessionDecoration(opCtx);
    if (!checkedOutSession) {
        auto sessionTransactionTable = SessionCatalog::get(opCtx);
        checkedOutSession.emplace(sessionTransactionTable->checkOutSession(opCtx));
    }

    const auto session = checkedOutSession->scopedSession.get();
    invariant(opCtx->getLogicalSessionId() == session->getSessionId());

    checkedOutSession->checkOutNestingLevel++;

    if (checkedOutSession->checkOutNestingLevel > 1) {
        return;
    }

    if (opCtx->getTxnNumber()) {
        checkedOutSession->scopedSession->begin(opCtx, opCtx->getTxnNumber().get());
    }
}

OperationContextSession::~OperationContextSession() {
    auto& checkedOutSession = operationSessionDecoration(_opCtx);
    if (checkedOutSession) {
        invariant(checkedOutSession->checkOutNestingLevel > 0);
        if (--checkedOutSession->checkOutNestingLevel == 0) {
            checkedOutSession.reset();
        }
    }
}

Session* OperationContextSession::get(OperationContext* opCtx) {
    auto& checkedOutSession = operationSessionDecoration(opCtx);
    if (checkedOutSession) {
        return checkedOutSession->scopedSession.get();
    }

    return nullptr;
}

}  // namespace mongo
