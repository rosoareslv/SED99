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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include <boost/optional.hpp>

#include "mongo/base/status_with.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/wire_version.h"
#include "mongo/executor/async_mock_stream_factory.h"
#include "mongo/executor/network_interface_asio.h"
#include "mongo/executor/test_network_connection_hook.h"
#include "mongo/rpc/legacy_reply_builder.h"
#include "mongo/stdx/future.h"
#include "mongo/stdx/memory.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

namespace mongo {
namespace executor {
namespace {

HostAndPort testHost{"localhost", 20000};

class NetworkInterfaceASIOTest : public mongo::unittest::Test {
public:
    void setUp() override {
        auto factory = stdx::make_unique<AsyncMockStreamFactory>();
        // keep unowned pointer, but pass ownership to NIA
        _streamFactory = factory.get();
        _net = stdx::make_unique<NetworkInterfaceASIO>(std::move(factory));
        _net->startup();
    }

    void tearDown() override {
        if (!_net->inShutdown()) {
            _net->shutdown();
        }
    }

    NetworkInterface& net() {
        return *_net;
    }

    AsyncMockStreamFactory& streamFactory() {
        return *_streamFactory;
    }

    void simulateServerReply(AsyncMockStreamFactory::MockStream* stream,
                             rpc::Protocol proto,
                             const stdx::function<RemoteCommandResponse(RemoteCommandRequest)>) {}

protected:
    AsyncMockStreamFactory* _streamFactory;
    std::unique_ptr<NetworkInterfaceASIO> _net;
};

TEST_F(NetworkInterfaceASIOTest, StartCommand) {
    TaskExecutor::CallbackHandle cb{};

    HostAndPort testHost{"localhost", 20000};

    stdx::promise<RemoteCommandResponse> prom{};

    bool callbackCalled = false;

    net().startCommand(cb,
                       RemoteCommandRequest(testHost, "testDB", BSON("foo" << 1), BSON("bar" << 1)),
                       [&](StatusWith<RemoteCommandResponse> resp) {
                           callbackCalled = true;

                           try {
                               prom.set_value(uassertStatusOK(resp));
                           } catch (...) {
                               prom.set_exception(std::current_exception());
                           }
                       });

    auto fut = prom.get_future();

    auto stream = streamFactory().blockUntilStreamExists(testHost);

    // Allow stream to connect.
    ConnectEvent{stream}.skip();

    // simulate isMaster reply.
    stream->simulateServer(
        rpc::Protocol::kOpQuery,
        [](RemoteCommandRequest request) -> RemoteCommandResponse {
            ASSERT_EQ(std::string{request.cmdObj.firstElementFieldName()}, "isMaster");
            ASSERT_EQ(request.dbname, "admin");

            RemoteCommandResponse response;
            response.data = BSON("minWireVersion" << mongo::minWireVersion << "maxWireVersion"
                                                  << mongo::maxWireVersion);
            return response;
        });

    auto expectedMetadata = BSON("meep"
                                 << "beep");
    auto expectedCommandReply = BSON("boop"
                                     << "bop"
                                     << "ok" << 1.0);

    // simulate user command
    stream->simulateServer(rpc::Protocol::kOpCommandV1,
                           [&](RemoteCommandRequest request) -> RemoteCommandResponse {
                               ASSERT_EQ(std::string{request.cmdObj.firstElementFieldName()},
                                         "foo");
                               ASSERT_EQ(request.dbname, "testDB");

                               RemoteCommandResponse response;
                               response.data = expectedCommandReply;
                               response.metadata = expectedMetadata;
                               return response;
                           });

    auto res = fut.get();

    ASSERT(callbackCalled);
    ASSERT_EQ(res.data, expectedCommandReply);
    ASSERT_EQ(res.metadata, expectedMetadata);
}

class NetworkInterfaceASIOConnectionHookTest : public NetworkInterfaceASIOTest {
public:
    void setUp() override {}

    void start(std::unique_ptr<NetworkConnectionHook> hook) {
        auto factory = stdx::make_unique<AsyncMockStreamFactory>();
        // keep unowned pointer, but pass ownership to NIA
        _streamFactory = factory.get();
        _net = stdx::make_unique<NetworkInterfaceASIO>(std::move(factory), std::move(hook));
        _net->startup();
    }
};

TEST_F(NetworkInterfaceASIOConnectionHookTest, ValidateHostInvalid) {
    bool validateCalled = false;
    bool hostCorrect = false;
    bool isMasterReplyCorrect = false;
    bool makeRequestCalled = false;
    bool handleReplyCalled = false;

    auto validationFailedStatus = Status(ErrorCodes::AlreadyInitialized, "blahhhhh");

    start(makeTestHook(
        [&](const HostAndPort& remoteHost, const RemoteCommandResponse& isMasterReply) {
            validateCalled = true;
            hostCorrect = (remoteHost == testHost);
            isMasterReplyCorrect = (isMasterReply.data["TESTKEY"].str() == "TESTVALUE");
            return validationFailedStatus;
        },
        [&](const HostAndPort& remoteHost) -> StatusWith<boost::optional<RemoteCommandRequest>> {
            makeRequestCalled = true;
            return {boost::none};
        },
        [&](const HostAndPort& remoteHost, RemoteCommandResponse&& response) {
            handleReplyCalled = true;
            return Status::OK();
        }));

    stdx::promise<void> done;
    bool statusCorrect = false;
    auto doneFuture = done.get_future();

    net().startCommand({},
                       {testHost,
                        "blah",
                        BSON("foo"
                             << "bar")},
                       [&](StatusWith<RemoteCommandResponse> result) {
                           statusCorrect = (result == validationFailedStatus);
                           done.set_value();
                       });

    auto stream = streamFactory().blockUntilStreamExists(testHost);

    ConnectEvent{stream}.skip();

    // simulate isMaster reply.
    stream->simulateServer(rpc::Protocol::kOpQuery,
                           [](RemoteCommandRequest request) -> RemoteCommandResponse {
                               RemoteCommandResponse response;
                               response.data = BSON("minWireVersion"
                                                    << mongo::minWireVersion << "maxWireVersion"
                                                    << mongo::maxWireVersion << "TESTKEY"
                                                    << "TESTVALUE");
                               return response;
                           });

    // we should stop here.
    doneFuture.get();
    ASSERT(statusCorrect);
    ASSERT(validateCalled);
    ASSERT(hostCorrect);
    ASSERT(isMasterReplyCorrect);

    ASSERT(!makeRequestCalled);
    ASSERT(!handleReplyCalled);
}

TEST_F(NetworkInterfaceASIOConnectionHookTest, MakeRequestReturnsError) {
    bool makeRequestCalled = false;
    bool handleReplyCalled = false;

    Status makeRequestError{ErrorCodes::DBPathInUse, "bloooh"};

    start(makeTestHook(
        [&](const HostAndPort& remoteHost, const RemoteCommandResponse& isMasterReply)
            -> Status { return Status::OK(); },
        [&](const HostAndPort& remoteHost) -> StatusWith<boost::optional<RemoteCommandRequest>> {
            makeRequestCalled = true;
            return makeRequestError;
        },
        [&](const HostAndPort& remoteHost, RemoteCommandResponse&& response) {
            handleReplyCalled = true;
            return Status::OK();
        }));

    stdx::promise<void> done;
    bool statusCorrect = false;
    auto doneFuture = done.get_future();

    net().startCommand({},
                       {testHost,
                        "blah",
                        BSON("foo"
                             << "bar")},
                       [&](StatusWith<RemoteCommandResponse> result) {
                           statusCorrect = (result == makeRequestError);
                           done.set_value();
                       });

    auto stream = streamFactory().blockUntilStreamExists(testHost);
    ConnectEvent{stream}.skip();

    // simulate isMaster reply.
    stream->simulateServer(rpc::Protocol::kOpQuery,
                           [](RemoteCommandRequest request) -> RemoteCommandResponse {
                               RemoteCommandResponse response;
                               response.data = BSON("minWireVersion" << mongo::minWireVersion
                                                                     << "maxWireVersion"
                                                                     << mongo::maxWireVersion);
                               return response;
                           });

    // We should stop here.
    doneFuture.get();
    ASSERT(statusCorrect);
    ASSERT(makeRequestCalled);
    ASSERT(!handleReplyCalled);
}

TEST_F(NetworkInterfaceASIOConnectionHookTest, MakeRequestReturnsNone) {
    bool makeRequestCalled = false;
    bool handleReplyCalled = false;

    start(makeTestHook(
        [&](const HostAndPort& remoteHost, const RemoteCommandResponse& isMasterReply)
            -> Status { return Status::OK(); },
        [&](const HostAndPort& remoteHost) -> StatusWith<boost::optional<RemoteCommandRequest>> {
            makeRequestCalled = true;
            return {boost::none};
        },
        [&](const HostAndPort& remoteHost, RemoteCommandResponse&& response) {
            handleReplyCalled = true;
            return Status::OK();
        }));

    stdx::promise<void> done;
    auto doneFuture = done.get_future();
    bool statusCorrect = false;

    auto commandRequest = BSON("foo"
                               << "bar");

    auto commandReply = BSON("foo"
                             << "boo"
                             << "ok" << 1.0);

    auto metadata = BSON("aaa"
                         << "bbb");

    net().startCommand({},
                       {testHost, "blah", commandRequest},
                       [&](StatusWith<RemoteCommandResponse> result) {
                           statusCorrect =
                               (result.isOK() && (result.getValue().data == commandReply) &&
                                (result.getValue().metadata == metadata));
                           done.set_value();
                       });


    auto stream = streamFactory().blockUntilStreamExists(testHost);
    ConnectEvent{stream}.skip();

    // simulate isMaster reply.
    stream->simulateServer(rpc::Protocol::kOpQuery,
                           [](RemoteCommandRequest request) -> RemoteCommandResponse {
                               RemoteCommandResponse response;
                               response.data = BSON("minWireVersion" << mongo::minWireVersion
                                                                     << "maxWireVersion"
                                                                     << mongo::maxWireVersion);
                               return response;
                           });


    // Simulate user command.
    stream->simulateServer(rpc::Protocol::kOpCommandV1,
                           [&](RemoteCommandRequest request) -> RemoteCommandResponse {
                               ASSERT_EQ(commandRequest, request.cmdObj);

                               RemoteCommandResponse response;
                               response.data = commandReply;
                               response.metadata = metadata;
                               return response;
                           });

    // We should get back the reply now.
    doneFuture.get();
    ASSERT(statusCorrect);
}

TEST_F(NetworkInterfaceASIOConnectionHookTest, HandleReplyReturnsError) {
    bool makeRequestCalled = false;

    bool handleReplyCalled = false;
    bool handleReplyArgumentCorrect = false;

    BSONObj hookCommandRequest = BSON("1ddd"
                                      << "fff");
    BSONObj hookRequestMetadata = BSON("wdwd" << 1212);

    BSONObj hookCommandReply = BSON("blah"
                                    << "blah"
                                    << "ok" << 1.0);
    BSONObj hookReplyMetadata = BSON("1111" << 2222);

    Status handleReplyError{ErrorCodes::AuthSchemaIncompatible, "daowdjkpowkdjpow"};

    start(makeTestHook(
        [&](const HostAndPort& remoteHost, const RemoteCommandResponse& isMasterReply)
            -> Status { return Status::OK(); },
        [&](const HostAndPort& remoteHost) -> StatusWith<boost::optional<RemoteCommandRequest>> {
            makeRequestCalled = true;
            return {boost::make_optional<RemoteCommandRequest>(
                {testHost, "foo", hookCommandRequest, hookRequestMetadata})};

        },
        [&](const HostAndPort& remoteHost, RemoteCommandResponse&& response) {
            handleReplyCalled = true;
            handleReplyArgumentCorrect =
                (response.data == hookCommandReply) && (response.metadata == hookReplyMetadata);
            return handleReplyError;
        }));

    stdx::promise<void> done;
    auto doneFuture = done.get_future();
    bool statusCorrect = false;
    auto commandRequest = BSON("foo"
                               << "bar");
    net().startCommand({},
                       {testHost, "blah", commandRequest},
                       [&](StatusWith<RemoteCommandResponse> result) {
                           statusCorrect = (result == handleReplyError);
                           done.set_value();
                       });

    auto stream = streamFactory().blockUntilStreamExists(testHost);
    ConnectEvent{stream}.skip();

    // simulate isMaster reply.
    stream->simulateServer(rpc::Protocol::kOpQuery,
                           [](RemoteCommandRequest request) -> RemoteCommandResponse {
                               RemoteCommandResponse response;
                               response.data = BSON("minWireVersion" << mongo::minWireVersion
                                                                     << "maxWireVersion"
                                                                     << mongo::maxWireVersion);
                               return response;
                           });

    // Simulate hook reply
    stream->simulateServer(rpc::Protocol::kOpCommandV1,
                           [&](RemoteCommandRequest request) -> RemoteCommandResponse {
                               ASSERT_EQ(request.cmdObj, hookCommandRequest);
                               ASSERT_EQ(request.metadata, hookRequestMetadata);

                               RemoteCommandResponse response;
                               response.data = hookCommandReply;
                               response.metadata = hookReplyMetadata;
                               return response;
                           });

    doneFuture.get();
    ASSERT(statusCorrect);
    ASSERT(makeRequestCalled);
    ASSERT(handleReplyCalled);
    ASSERT(handleReplyArgumentCorrect);
}

TEST_F(NetworkInterfaceASIOTest, setAlarm) {
    stdx::promise<bool> nearFuture;
    stdx::future<bool> executed = nearFuture.get_future();

    // set a first alarm, to execute after "expiration"
    Date_t expiration = net().now() + Milliseconds(100);
    net().setAlarm(
        expiration,
        [this, expiration, &nearFuture]() { nearFuture.set_value(net().now() >= expiration); });

    // wait enough time for first alarm to execute
    auto status = executed.wait_for(Milliseconds(5000));

    // assert that not only did it execute, but executed after "expiration"
    ASSERT(status == stdx::future_status::ready);
    ASSERT(executed.get());

    // set an alarm for the future, kill interface, ensure it didn't execute
    stdx::promise<bool> farFuture;
    stdx::future<bool> executed2 = farFuture.get_future();

    expiration = net().now() + Milliseconds(99999999);
    net().setAlarm(expiration, [this, &farFuture]() { farFuture.set_value(true); });

    net().shutdown();

    status = executed2.wait_for(Milliseconds(0));
    ASSERT(status == stdx::future_status::timeout);
}

}  // namespace
}  // namespace executor
}  // namespace mongo
