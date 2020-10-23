/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#pragma once

#include <boost/optional.hpp>
#include <chrono>
#include <string>

#include "mongo/bson/bsonobj.h"
#include "mongo/util/duration.h"


/**
 * The data structures in this file are defined in the "Server Discovery & Monitoring"
 * specification, which governs how topology changes are detected in a cluster. See
 * https://github.com/mongodb/specifications/blob/master/source/server-discovery-and-monitoring/server-discovery-and-monitoring.rst
 * for more information.
 */
namespace mongo::sdam {
enum class TopologyType {
    kSingle,
    kReplicaSetNoPrimary,
    kReplicaSetWithPrimary,
    kSharded,
    kUnknown
};
const std::vector<TopologyType> allTopologyTypes();
std::string toString(const TopologyType topologyType);
std::ostream& operator<<(std::ostream& os, const TopologyType topologyType);

enum class ServerType {
    kStandalone,
    kMongos,
    kRSPrimary,
    kRSSecondary,
    kRSArbiter,
    kRSOther,
    kRSGhost,
    kUnknown
};
const std::vector<ServerType> allServerTypes();
std::string toString(const ServerType serverType);
StatusWith<ServerType> parseServerType(StringData strServerType);
std::ostream& operator<<(std::ostream& os, const ServerType serverType);

using ServerAddress = std::string;
using IsMasterRTT = mongo::Nanoseconds;

// The result of an attempt to call the "ismaster" command on a server.
class IsMasterOutcome {
    IsMasterOutcome() = delete;

public:
    // success constructor
    IsMasterOutcome(ServerAddress server, BSONObj response, IsMasterRTT rtt)
        : _server(std::move(server)), _success(true), _response(response), _rtt(rtt) {}

    // failure constructor
    IsMasterOutcome(ServerAddress server, std::string errorMsg)
        : _server(std::move(server)), _success(false), _errorMsg(errorMsg) {}

    const ServerAddress& getServer() const;
    bool isSuccess() const;
    const boost::optional<BSONObj>& getResponse() const;
    const boost::optional<IsMasterRTT>& getRtt() const;
    const std::string& getErrorMsg() const;

private:
    ServerAddress _server;
    // indicating the success or failure of the attempt
    bool _success;
    // an error message in case of failure
    std::string _errorMsg;
    // a document containing the command response (or boost::none if it failed)
    boost::optional<BSONObj> _response;
    // the round trip time to execute the command (or null if it failed)
    boost::optional<IsMasterRTT> _rtt;
};

class ServerDescription;
using ServerDescriptionPtr = std::shared_ptr<ServerDescription>;

class TopologyDescription;
using TopologyDescriptionPtr = std::shared_ptr<TopologyDescription>;
};  // namespace mongo::sdam
