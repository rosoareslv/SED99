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

#include "mongo/platform/basic.h"

#include "mongo/transport/transport_layer_mock.h"

#include "mongo/base/status.h"
#include "mongo/stdx/memory.h"
#include "mongo/transport/session.h"
#include "mongo/transport/ticket.h"
#include "mongo/transport/ticket_impl.h"
#include "mongo/transport/transport_layer.h"
#include "mongo/util/net/message.h"
#include "mongo/util/time_support.h"

namespace mongo {
namespace transport {

TransportLayerMock::TicketMock::TicketMock(const Session* session,
                                           Message* message,
                                           Date_t expiration)
    : _session(session), _message(message), _expiration(expiration) {}

TransportLayerMock::TicketMock::TicketMock(const Session* session, Date_t expiration)
    : _session(session), _expiration(expiration) {}

Session::Id TransportLayerMock::TicketMock::sessionId() const {
    return _session->id();
}

Date_t TransportLayerMock::TicketMock::expiration() const {
    return _expiration;
}

boost::optional<Message*> TransportLayerMock::TicketMock::msg() const {
    return _message;
}

TransportLayerMock::TransportLayerMock() : _shutdown(false) {}

Ticket TransportLayerMock::sourceMessage(Session& session, Message* message, Date_t expiration) {
    if (inShutdown()) {
        return Ticket(TransportLayer::ShutdownStatus);
    } else if (!owns(session.id())) {
        return Ticket(TransportLayer::SessionUnknownStatus);
    } else if (session.ended()) {
        return Ticket(Session::ClosedStatus);
    }

    return Ticket(this,
                  stdx::make_unique<TransportLayerMock::TicketMock>(&session, message, expiration));
}

Ticket TransportLayerMock::sinkMessage(Session& session,
                                       const Message& message,
                                       Date_t expiration) {
    if (inShutdown()) {
        return Ticket(TransportLayer::ShutdownStatus);
    } else if (!owns(session.id())) {
        return Ticket(TransportLayer::SessionUnknownStatus);
    } else if (session.ended()) {
        return Ticket(Session::ClosedStatus);
    }

    return Ticket(this, stdx::make_unique<TransportLayerMock::TicketMock>(&session, expiration));
}

Status TransportLayerMock::wait(Ticket&& ticket) {
    if (inShutdown()) {
        return ShutdownStatus;
    } else if (!ticket.valid()) {
        return ticket.status();
    } else if (!owns(ticket.sessionId())) {
        return TicketSessionUnknownStatus;
    } else if (get(ticket.sessionId())->ended()) {
        return Ticket::SessionClosedStatus;
    }

    return Status::OK();
}

void TransportLayerMock::asyncWait(Ticket&& ticket, TicketCallback callback) {
    callback(Status::OK());
}

SSLPeerInfo TransportLayerMock::getX509PeerInfo(const Session& session) const {
    return _sessions.at(session.id()).peerInfo;
}


void TransportLayerMock::setX509PeerInfo(const Session& session, SSLPeerInfo peerInfo) {
    _sessions[session.id()].peerInfo = std::move(peerInfo);
}

TransportLayer::Stats TransportLayerMock::sessionStats() {
    return Stats();
}

void TransportLayerMock::registerTags(const Session& session) {}

Session* TransportLayerMock::createSession() {
    std::unique_ptr<Session> session =
        stdx::make_unique<Session>(HostAndPort(), HostAndPort(), this);
    Session::Id sessionId = session->id();

    _sessions[sessionId] = Connection{std::move(session), SSLPeerInfo()};

    return _sessions[sessionId].session.get();
}

Session* TransportLayerMock::get(Session::Id id) {
    if (!owns(id))
        return nullptr;

    return _sessions[id].session.get();
}

bool TransportLayerMock::owns(Session::Id id) {
    return _sessions.count(id) > 0;
}

void TransportLayerMock::end(Session& session) {
    session.end();
}

void TransportLayerMock::endAllSessions(Session::TagMask tags) {
    auto it = _sessions.begin();
    while (it != _sessions.end()) {
        end(*it->second.session.get());
        it++;
    }
}

Status TransportLayerMock::start() {
    return Status::OK();
}

void TransportLayerMock::shutdown() {
    if (!inShutdown()) {
        _shutdown = true;
        endAllSessions(Session::kEmptyTagMask);
    }
}

bool TransportLayerMock::inShutdown() const {
    return _shutdown;
}

TransportLayerMock::~TransportLayerMock() {
    shutdown();
}

}  // namespace transport
}  // namespace mongo
