/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
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

#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {

const auto sessionTransactionTableDecoration = ServiceContext::declareDecoration<SessionCatalog>();

const auto operationSessionDecoration =
    OperationContext::declareDecoration<boost::optional<SessionCatalog::ScopedCheckedOutSession>>();

}  // namespace

SessionCatalog::~SessionCatalog() {
    stdx::lock_guard<stdx::mutex> lg(_mutex);
    for (const auto& entry : _sessions) {
        ObservableSession session(lg, entry.second->session);
        invariant(!session.currentOperation());
        invariant(!session._killed());
    }
}

void SessionCatalog::reset_forTest() {
    stdx::lock_guard<stdx::mutex> lg(_mutex);
    _sessions.clear();
}

SessionCatalog* SessionCatalog::get(OperationContext* opCtx) {
    return get(opCtx->getServiceContext());
}

SessionCatalog* SessionCatalog::get(ServiceContext* service) {
    auto& sessionTransactionTable = sessionTransactionTableDecoration(service);
    return &sessionTransactionTable;
}

SessionCatalog::ScopedCheckedOutSession SessionCatalog::_checkOutSession(OperationContext* opCtx) {
    // This method is not supposed to be called with an already checked-out session due to risk of
    // deadlock
    invariant(opCtx->getLogicalSessionId());
    invariant(!operationSessionDecoration(opCtx));
    invariant(!opCtx->lockState()->inAWriteUnitOfWork());
    invariant(!opCtx->lockState()->isLocked());

    stdx::unique_lock<stdx::mutex> ul(_mutex);
    auto sri = _getOrCreateSessionRuntimeInfo(ul, opCtx, *opCtx->getLogicalSessionId());

    // Wait until the session is no longer checked out and until the previously scheduled kill has
    // completed
    opCtx->waitForConditionOrInterrupt(sri->availableCondVar, ul, [&ul, &sri]() {
        ObservableSession osession(ul, sri->session);
        return !osession.currentOperation() && !osession._killed();
    });

    sri->session._checkoutOpCtx = opCtx;

    return ScopedCheckedOutSession(
        *this, std::move(sri), boost::none /* Not checked out for kill */);
}

SessionCatalog::SessionToKill SessionCatalog::checkOutSessionForKill(OperationContext* opCtx,
                                                                     KillToken killToken) {
    // This method is not supposed to be called with an already checked-out session due to risk of
    // deadlock
    invariant(!operationSessionDecoration(opCtx));
    invariant(!opCtx->getTxnNumber());

    stdx::unique_lock<stdx::mutex> ul(_mutex);
    auto sri = _getOrCreateSessionRuntimeInfo(ul, opCtx, killToken.lsidToKill);
    invariant(ObservableSession(ul, sri->session)._killed());

    // Wait until the session is no longer checked out
    opCtx->waitForConditionOrInterrupt(sri->availableCondVar, ul, [&ul, &sri]() {
        return !ObservableSession(ul, sri->session).currentOperation();
    });

    sri->session._checkoutOpCtx = opCtx;

    return SessionToKill(ScopedCheckedOutSession(*this, std::move(sri), std::move(killToken)));
}

void SessionCatalog::scanSessions(const SessionKiller::Matcher& matcher,
                                  const ScanSessionsCallbackFn& workerFn) {
    stdx::lock_guard<stdx::mutex> lg(_mutex);

    LOG(2) << "Beginning scanSessions. Scanning " << _sessions.size() << " sessions.";

    for (auto& sessionEntry : _sessions) {
        if (matcher.match(sessionEntry.first)) {
            workerFn({lg, sessionEntry.second->session});
        }
    }
}

SessionCatalog::KillToken SessionCatalog::killSession(const LogicalSessionId& lsid) {
    stdx::lock_guard<stdx::mutex> lg(_mutex);
    auto it = _sessions.find(lsid);
    uassert(ErrorCodes::NoSuchSession, "Session not found", it != _sessions.end());

    auto& sri = it->second;
    return ObservableSession(lg, sri->session).kill();
}

std::shared_ptr<SessionCatalog::SessionRuntimeInfo> SessionCatalog::_getOrCreateSessionRuntimeInfo(
    WithLock, OperationContext* opCtx, const LogicalSessionId& lsid) {
    auto it = _sessions.find(lsid);
    if (it == _sessions.end()) {
        it = _sessions.emplace(lsid, std::make_shared<SessionRuntimeInfo>(lsid)).first;
    }

    return it->second;
}

void SessionCatalog::_releaseSession(std::shared_ptr<SessionCatalog::SessionRuntimeInfo> sri,
                                     boost::optional<SessionCatalog::KillToken> killToken) {
    stdx::lock_guard<stdx::mutex> lg(_mutex);

    // Make sure we have exactly the same session on the map and that it is still associated with an
    // operation context (meaning checked-out)
    invariant(_sessions[sri->session.getSessionId()] == sri);
    invariant(sri->session._checkoutOpCtx);
    sri->session._checkoutOpCtx = nullptr;
    sri->availableCondVar.notify_all();

    if (killToken) {
        invariant(sri->session._killsRequested > 0);
        --sri->session._killsRequested;
    }
}

OperationContext* ObservableSession::currentOperation() const {
    return _session->_checkoutOpCtx;
}

SessionCatalog::KillToken ObservableSession::kill(ErrorCodes::Error reason) const {
    const bool firstKiller = (0 == _session->_killsRequested);
    ++_session->_killsRequested;

    // For currently checked-out sessions, interrupt the operation context so that the current owner
    // can release the session
    if (firstKiller && _session->_checkoutOpCtx) {
        invariant(_clientLock);
        const auto serviceContext = _session->_checkoutOpCtx->getServiceContext();
        serviceContext->killOperation(_clientLock, _session->_checkoutOpCtx, reason);
    }

    return SessionCatalog::KillToken(getSessionId());
}

bool ObservableSession::_killed() const {
    return _session->_killsRequested > 0;
}

OperationContextSession::OperationContextSession(OperationContext* opCtx) : _opCtx(opCtx) {
    auto& checkedOutSession = operationSessionDecoration(opCtx);
    if (checkedOutSession) {
        // The only case where a session can be checked-out more than once is due to DBDirectClient
        // reentrancy
        invariant(opCtx->getClient()->isInDirectClient());
        return;
    }

    checkOut(opCtx);
}

OperationContextSession::~OperationContextSession() {
    // Only release the checked out session at the end of the top-level request from the client, not
    // at the end of a nested DBDirectClient call
    if (_opCtx->getClient()->isInDirectClient()) {
        return;
    }

    auto& checkedOutSession = operationSessionDecoration(_opCtx);
    if (!checkedOutSession)
        return;

    checkIn(_opCtx);
}

Session* OperationContextSession::get(OperationContext* opCtx) {
    auto& checkedOutSession = operationSessionDecoration(opCtx);
    if (checkedOutSession) {
        return checkedOutSession->get();
    }

    return nullptr;
}

void OperationContextSession::checkIn(OperationContext* opCtx) {
    auto& checkedOutSession = operationSessionDecoration(opCtx);
    invariant(checkedOutSession);

    // Removing the checkedOutSession from the OperationContext must be done under the Client lock,
    // but destruction of the checkedOutSession must not be, as it takes the SessionCatalog mutex,
    // and other code may take the Client lock while holding that mutex.
    stdx::unique_lock<Client> lk(*opCtx->getClient());
    SessionCatalog::ScopedCheckedOutSession sessionToReleaseOutOfLock(
        std::move(*checkedOutSession));

    // This destroys the moved-from ScopedCheckedOutSession, and must be done within the client lock
    checkedOutSession = boost::none;
    lk.unlock();
}

void OperationContextSession::checkOut(OperationContext* opCtx) {
    auto& checkedOutSession = operationSessionDecoration(opCtx);
    invariant(!checkedOutSession);

    const auto catalog = SessionCatalog::get(opCtx);
    auto scopedCheckedOutSession = catalog->_checkOutSession(opCtx);

    // We acquire a Client lock here to guard the construction of this session so that references to
    // this session are safe to use while the lock is held
    stdx::lock_guard<Client> lk(*opCtx->getClient());
    checkedOutSession.emplace(std::move(scopedCheckedOutSession));
}

}  // namespace mongo
