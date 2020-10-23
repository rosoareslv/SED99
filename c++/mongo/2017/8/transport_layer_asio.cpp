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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include "mongo/transport/transport_layer_asio.h"

#include "boost/algorithm/string.hpp"

#include "asio.hpp"

#include "mongo/config.h"
#ifdef MONGO_CONFIG_SSL
#include "asio/ssl.hpp"
#endif

#include "mongo/base/checked_cast.h"
#include "mongo/base/system_error.h"
#include "mongo/db/server_options.h"
#include "mongo/db/service_context.h"
#include "mongo/transport/asio_utils.h"
#include "mongo/transport/service_entry_point.h"
#include "mongo/transport/ticket.h"
#include "mongo/transport/ticket_asio.h"
#include "mongo/util/log.h"
#include "mongo/util/net/hostandport.h"
#include "mongo/util/net/message.h"
#include "mongo/util/net/sock.h"
#include "mongo/util/net/sockaddr.h"
#include "mongo/util/net/ssl_manager.h"
#include "mongo/util/net/ssl_options.h"

// session_asio.h has some header dependencies that require it to be the last header.
#include "mongo/transport/session_asio.h"

namespace mongo {
namespace transport {

TransportLayerASIO::Options::Options(const ServerGlobalParams* params)
    : port(params->port),
      ipList(params->bind_ip),
#ifndef _WIN32
      useUnixSockets(!params->noUnixSocket),
#endif
      enableIPv6(params->enableIPv6),
      maxConns(params->maxConns) {
}

std::shared_ptr<TransportLayerASIO::ASIOSession> TransportLayerASIO::createSession() {
    GenericSocket socket(*_ioContext);
    std::shared_ptr<ASIOSession> ret(new ASIOSession(this, std::move(socket)));
    return ret;
}

TransportLayerASIO::TransportLayerASIO(const TransportLayerASIO::Options& opts,
                                       ServiceEntryPoint* sep)
    : _ioContext(std::make_shared<asio::io_context>()),
#ifdef MONGO_CONFIG_SSL
      _sslContext(nullptr),
#endif
      _sep(sep),
      _listenerOptions(opts) {
}

TransportLayerASIO::~TransportLayerASIO() = default;

Ticket TransportLayerASIO::sourceMessage(const SessionHandle& session,
                                         Message* message,
                                         Date_t expiration) {
    auto asioSession = checked_pointer_cast<ASIOSession>(session);
    auto ticket = stdx::make_unique<ASIOSourceTicket>(asioSession, expiration, message);
    return {this, std::move(ticket)};
}

Ticket TransportLayerASIO::sinkMessage(const SessionHandle& session,
                                       const Message& message,
                                       Date_t expiration) {
    auto asioSession = checked_pointer_cast<ASIOSession>(session);
    auto ticket = stdx::make_unique<ASIOSinkTicket>(asioSession, expiration, message);
    return {this, std::move(ticket)};
}

Status TransportLayerASIO::wait(Ticket&& ticket) {
    auto ownedASIOTicket = getOwnedTicketImpl(std::move(ticket));
    auto asioTicket = checked_cast<ASIOTicket*>(ownedASIOTicket.get());

    Status waitStatus = Status::OK();
    asioTicket->fill(true, [&waitStatus](Status result) { waitStatus = result; });

    return waitStatus;
}

void TransportLayerASIO::asyncWait(Ticket&& ticket, TicketCallback callback) {
    auto ownedASIOTicket = std::shared_ptr<TicketImpl>(getOwnedTicketImpl(std::move(ticket)));
    auto asioTicket = checked_cast<ASIOTicket*>(ownedASIOTicket.get());

    asioTicket->fill(
        false,
        [ callback = std::move(callback),
          ownedASIOTicket = std::move(ownedASIOTicket) ](Status status) { callback(status); });
}

TransportLayer::Stats TransportLayerASIO::sessionStats() {
    TransportLayer::Stats ret;
    auto sessionCount = _currentConnections.load();
    ret.numOpenSessions = sessionCount;
    ret.numCreatedSessions = _createdConnections.load();
    ret.numAvailableSessions = static_cast<size_t>(_listenerOptions.maxConns) - sessionCount;
    return ret;
}

// Must not be called while holding the TransportLayerASIO mutex.
void TransportLayerASIO::end(const SessionHandle& session) {
    auto asioSession = checked_pointer_cast<ASIOSession>(session);
    asioSession->shutdown();
}

Status TransportLayerASIO::setup() {
    std::vector<std::string> listenAddrs;
    if (_listenerOptions.ipList.empty()) {
        listenAddrs = {"127.0.0.1"};
        if (_listenerOptions.enableIPv6) {
            listenAddrs.emplace_back("::1");
        }
    } else {
        boost::split(
            listenAddrs, _listenerOptions.ipList, boost::is_any_of(","), boost::token_compress_on);
    }

#ifndef _WIN32
    if (_listenerOptions.useUnixSockets) {
        listenAddrs.emplace_back(makeUnixSockPath(_listenerOptions.port));
    }
#endif
    for (auto& ip : listenAddrs) {
        std::error_code ec;
        if (ip.empty()) {
            warning() << "Skipping empty bind address";
            continue;
        }

        const auto addrs = SockAddr::createAll(
            ip, _listenerOptions.port, _listenerOptions.enableIPv6 ? AF_UNSPEC : AF_INET);
        if (addrs.empty()) {
            warning() << "Found no addresses for " << ip;
            continue;
        }

        for (const auto& addr : addrs) {
            asio::generic::stream_protocol::endpoint endpoint(addr.raw(), addr.addressSize);

#ifndef _WIN32
            if (addr.getType() == AF_UNIX) {
                if (::unlink(ip.c_str()) == -1 && errno != ENOENT) {
                    error() << "Failed to unlink socket file " << ip << " "
                            << errnoWithDescription(errno);
                    fassertFailedNoTrace(40486);
                }
            }
#endif
            if (addr.getType() == AF_INET6 && !_listenerOptions.enableIPv6) {
                error() << "Specified ipv6 bind address, but ipv6 is disabled";
                fassertFailedNoTrace(40488);
            }

            GenericAcceptor acceptor(*_ioContext);
            acceptor.open(endpoint.protocol());
            acceptor.set_option(GenericAcceptor::reuse_address(true));

            acceptor.non_blocking(true, ec);
            if (ec) {
                return errorCodeToStatus(ec);
            }

            acceptor.bind(endpoint, ec);
            if (ec) {
                return errorCodeToStatus(ec);
            }

#ifndef _WIN32
            if (addr.getType() == AF_UNIX) {
                if (::chmod(ip.c_str(), serverGlobalParams.unixSocketPermissions) == -1) {
                    error() << "Failed to chmod socket file " << ip << " "
                            << errnoWithDescription(errno);
                    fassertFailedNoTrace(40487);
                }
            }
#endif
            _acceptors.emplace_back(std::move(acceptor));
        }
    }

    if (_acceptors.empty()) {
        return Status(ErrorCodes::SocketException, "No available addresses/ports to bind to");
    }

#ifdef MONGO_CONFIG_SSL
    const auto& sslParams = getSSLGlobalParams();
    _sslMode = static_cast<SSLParams::SSLModes>(sslParams.sslMode.load());

    if (_sslMode != SSLParams::SSLMode_disabled) {
        _sslContext = stdx::make_unique<asio::ssl::context>(asio::ssl::context::sslv23);

        const auto sslManager = getSSLManager();
        sslManager
            ->initSSLContext(_sslContext->native_handle(),
                             sslParams,
                             SSLManagerInterface::ConnectionDirection::kOutgoing)
            .transitional_ignore();
    }
#endif

    return Status::OK();
}

Status TransportLayerASIO::start() {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    _running.store(true);

    // If we're in async mode then the ServiceExecutor will handle calling run_one() in a pool
    // of threads. Otherwise we need a thread to just handle the async_accept calls.
    if (!_listenerOptions.async) {
        _listenerThread = stdx::thread([this] {
            setThreadName("listener");
            while (_running.load()) {
                try {
                    _ioContext->run();
                    _ioContext->reset();
                } catch (...) {
                    severe() << "Uncaught exception in the listener: " << exceptionToStatus();
                    fassertFailed(40491);
                }
            }
        });
    }

    for (auto& acceptor : _acceptors) {
        acceptor.listen(serverGlobalParams.listenBacklog);
        _acceptConnection(acceptor);
    }

    const char* ssl = "";
#ifdef MONGO_CONFIG_SSL
    if (_sslMode != SSLParams::SSLMode_disabled) {
        ssl = " ssl";
    }
#endif
    log() << "waiting for connections on port " << _listenerOptions.port << ssl;

    return Status::OK();
}

void TransportLayerASIO::shutdown() {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    _running.store(false);

    // Loop through the acceptors and cancel their calls to async_accept. This will prevent new
    // connections from being opened.
    for (auto& acceptor : _acceptors) {
        acceptor.cancel();
    }

    // If the listener thread is joinable (that is, we created/started a listener thread), then
    // the io_context is owned exclusively by the TransportLayer and we should stop it and join
    // the listener thread.
    //
    // Otherwise the ServiceExecutor may need to continue running the io_context to drain running
    // connections, so we just cancel the acceptors and return.
    if (_listenerThread.joinable()) {
        // We should only have started a listener if the TransportLayer is in sync mode.
        dassert(!_listenerOptions.async);
        _ioContext->stop();
        _listenerThread.join();
    }
}

const std::shared_ptr<asio::io_context>& TransportLayerASIO::getIOContext() {
    return _ioContext;
}

void TransportLayerASIO::_acceptConnection(GenericAcceptor& acceptor) {
    auto session = createSession();
    if (!session) {
        _acceptConnection(acceptor);
        return;
    }

    auto& socket = session->getSocket();
    auto acceptCb = [ this, session = std::move(session), &acceptor ](std::error_code ec) mutable {
        if (!_running.load())
            return;

        if (ec) {
            log() << "Error accepting new connection on "
                  << endpointToHostAndPort(acceptor.local_endpoint()) << ": " << ec.message();
            _acceptConnection(acceptor);
            return;
        }

        size_t connCount = _currentConnections.addAndFetch(1);
        if (connCount > _listenerOptions.maxConns) {
            log() << "connection refused because too many open connections: " << connCount;
            _currentConnections.subtractAndFetch(1);
            _acceptConnection(acceptor);
            return;
        }

        session->postAcceptSetup(_listenerOptions.async);

        _createdConnections.addAndFetch(1);
        if (!serverGlobalParams.quiet.load()) {
            const auto word = (connCount == 1 ? " connection"_sd : " connections"_sd);
            log() << "connection accepted from " << session->remote() << " #" << session->id()
                  << " (" << connCount << word << " now open)";
        }

        _sep->startSession(std::move(session));
        _acceptConnection(acceptor);
    };

    acceptor.async_accept(socket, std::move(acceptCb));
}

}  // namespace transport
}  // namespace mongo
