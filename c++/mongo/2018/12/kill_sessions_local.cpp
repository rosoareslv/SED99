
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kCommand

#include "mongo/platform/basic.h"

#include "mongo/db/kill_sessions_local.h"

#include "mongo/db/client.h"
#include "mongo/db/cursor_manager.h"
#include "mongo/db/kill_sessions_common.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/db/session_catalog.h"
#include "mongo/db/transaction_participant.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {

/**
 * Shortcut method shared by the various forms of session kill below. Every session kill operation
 * consists of the following stages:
 *  1) Select the sessions to kill, based on their lsid or owning user account (achieved through the
 *     'matcher') and further refining that list through the 'filterFn'.
 *  2) If any of the selected sessions are currently checked out, interrupt the owning operation
 *     context with 'reason' as the code.
 *  3) Finish killing the selected and interrupted sessions through the 'killSessionFn'.
 */
void killSessionsAction(OperationContext* opCtx,
                        const SessionKiller::Matcher& matcher,
                        const stdx::function<bool(Session*)>& filterFn,
                        const stdx::function<void(Session*)>& killSessionFn,
                        ErrorCodes::Error reason = ErrorCodes::Interrupted) {
    const auto catalog = SessionCatalog::get(opCtx);

    std::vector<Session::KillToken> sessionKillTokens;
    catalog->scanSessions(matcher, [&](WithLock sessionCatalogLock, Session* session) {
        if (filterFn(session))
            sessionKillTokens.emplace_back(session->kill(sessionCatalogLock, reason));
    });

    for (auto& sessionKillToken : sessionKillTokens) {
        auto session = catalog->checkOutSessionForKill(opCtx, std::move(sessionKillToken));

        // TODO (SERVER-33850): Rename KillAllSessionsByPattern and
        // ScopedKillAllSessionsByPatternImpersonator to not refer to session kill
        const KillAllSessionsByPattern* pattern = matcher.match(session->getSessionId());
        invariant(pattern);

        ScopedKillAllSessionsByPatternImpersonator impersonator(opCtx, *pattern);
        killSessionFn(session.get());
    }
}

}  // namespace

void killSessionsLocalKillTransactions(OperationContext* opCtx,
                                       const SessionKiller::Matcher& matcher,
                                       ErrorCodes::Error reason) {
    killSessionsAction(
        opCtx,
        matcher,
        [](Session*) { return true; },
        [](Session* session) { TransactionParticipant::get(session)->abortArbitraryTransaction(); },
        reason);
}

SessionKiller::Result killSessionsLocal(OperationContext* opCtx,
                                        const SessionKiller::Matcher& matcher,
                                        SessionKiller::UniformRandomBitGenerator* urbg) {
    killSessionsLocalKillTransactions(opCtx, matcher);
    uassertStatusOK(killSessionsLocalKillOps(opCtx, matcher));

    auto res = CursorManager::killCursorsWithMatchingSessions(opCtx, matcher);
    uassertStatusOK(res.first);

    return {std::vector<HostAndPort>{}};
}

void killAllExpiredTransactions(OperationContext* opCtx) {
    SessionKiller::Matcher matcherAllSessions(
        KillAllSessionsByPatternSet{makeKillAllSessionsByPattern(opCtx)});
    killSessionsAction(
        opCtx,
        matcherAllSessions,
        [](Session* session) {
            const auto txnParticipant = TransactionParticipant::get(session);

            return txnParticipant->expired();
        },
        [](Session* session) {
            const auto txnParticipant = TransactionParticipant::get(session);

            LOG(0)
                << "Aborting transaction with txnNumber " << txnParticipant->getActiveTxnNumber()
                << " on session " << session->getSessionId().getId()
                << " because it has been running for longer than 'transactionLifetimeLimitSeconds'";

            // The try/catch block below is necessary because expired() in the filterFn above could
            // return true for expired, but unprepared transaction, but by the time we get to
            // actually kill it, the participant could theoretically become prepared (being under
            // the SessionCatalog mutex doesn't prevent the concurrently running thread from doing
            // preparing the participant).
            //
            // Then when the execution reaches the killSessionFn, it would find the transaction is
            // prepared and not allowed to be killed, which would cause the exception below
            try {
                txnParticipant->abortArbitraryTransaction();
            } catch (const DBException& ex) {
                warning() << "May have failed to abort expired transaction on session "
                          << session->getSessionId().getId() << " due to " << redact(ex.toStatus());
            }
        },
        ErrorCodes::ExceededTimeLimit);
}

void killSessionsLocalShutdownAllTransactions(OperationContext* opCtx) {
    SessionKiller::Matcher matcherAllSessions(
        KillAllSessionsByPatternSet{makeKillAllSessionsByPattern(opCtx)});
    killSessionsAction(opCtx,
                       matcherAllSessions,
                       [](Session*) { return true; },
                       [](Session* session) { TransactionParticipant::get(session)->shutdown(); },
                       ErrorCodes::InterruptedAtShutdown);
}

void killSessionsAbortAllPreparedTransactions(OperationContext* opCtx) {
    SessionKiller::Matcher matcherAllSessions(
        KillAllSessionsByPatternSet{makeKillAllSessionsByPattern(opCtx)});
    killSessionsAction(
        opCtx,
        matcherAllSessions,
        [](Session* session) {
            // Filter for sessions that have a prepared transaction.
            const auto txnParticipant = TransactionParticipant::get(session);
            return txnParticipant->transactionIsPrepared();
        },
        [](Session* session) {
            // Abort the prepared transaction and invalidate the session it is
            // associated with.
            TransactionParticipant::get(session)->abortPreparedTransactionForRollback();
        });
}

}  // namespace mongo
