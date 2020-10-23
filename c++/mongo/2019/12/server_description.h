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

#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <map>
#include <ostream>
#include <set>
#include <utility>

#include "mongo/bson/oid.h"
#include "mongo/client/sdam/sdam_datatypes.h"
#include "mongo/db/repl/optime.h"
#include "mongo/platform/basic.h"
#include "mongo/util/clock_source.h"

namespace mongo::sdam {
class ServerDescription {
    ServerDescription() = delete;

public:
    /**
     * Construct an unknown ServerDescription with default values except the server's address.
     */
    ServerDescription(ServerAddress address)
        : _address(std::move(address)), _type(ServerType::kUnknown) {
        boost::to_lower(_address);
    }

    /**
     * Build a new ServerDescription according to the rules of the SDAM spec based on the
     * last RTT to the server and isMaster response.
     */
    ServerDescription(ClockSource* clockSource,
                      const IsMasterOutcome& isMasterOutcome,
                      boost::optional<IsMasterRTT> lastRtt = boost::none);

    /**
     * This determines if a server description is equivalent according to the Server Discovery and
     * Monitoring specification. Members marked with (=) are used to determine equality. Note that
     * these members do not include RTT or the server's address.
     */
    bool isEquivalent(const ServerDescription& other) const;

    // server identity
    const ServerAddress& getAddress() const;
    ServerType getType() const;
    const boost::optional<ServerAddress>& getMe() const;
    const boost::optional<std::string>& getSetName() const;
    const std::map<std::string, std::string>& getTags() const;

    // network attributes
    const boost::optional<std::string>& getError() const;
    const boost::optional<IsMasterRTT>& getRtt() const;
    const boost::optional<int>& getLogicalSessionTimeoutMinutes() const;

    // server capabilities
    int getMinWireVersion() const;
    int getMaxWireVersion() const;
    bool isDataBearingServer() const;

    // server 'time'
    const Date_t getLastUpdateTime() const;
    const boost::optional<Date_t>& getLastWriteDate() const;
    const boost::optional<repl::OpTime>& getOpTime() const;

    // topology membership
    const boost::optional<ServerAddress>& getPrimary() const;
    const std::set<ServerAddress>& getHosts() const;
    const std::set<ServerAddress>& getPassives() const;
    const std::set<ServerAddress>& getArbiters() const;
    const boost::optional<int>& getSetVersion() const;
    const boost::optional<OID>& getElectionId() const;

    BSONObj toBson() const;
    std::string toString() const;

private:
    /**
     * Classify the server's type based on the ismaster response.
     * @param isMaster - reply information for the ismaster command
     */
    void parseTypeFromIsMaster(const BSONObj isMaster);


    void calculateRtt(const IsMasterRTT currentRtt, const boost::optional<IsMasterRTT> lastRtt);
    void saveLastWriteInfo(BSONObj lastWriteBson);

    void storeHostListIfPresent(const std::string key,
                                const BSONObj response,
                                std::set<ServerAddress>& destination);
    void saveHosts(const BSONObj response);
    void saveTags(BSONObj tagsObj);
    void saveElectionId(BSONElement electionId);

    static inline const std::string kIsDbGrid = "isdbgrid";
    static inline const double kRttAlpha = 0.2;

    // address: the hostname or IP, and the port number, that the client connects to. Note that this
    // is not the server's ismaster.me field, in the case that the server reports an address
    // different from the address the client uses.
    ServerAddress _address;

    // error: information about the last error related to this server. Default null.
    boost::optional<std::string> _error;

    // roundTripTime: the duration of the ismaster call. Default null.
    boost::optional<IsMasterRTT> _rtt;

    // lastWriteDate: a 64-bit BSON datetime or null. The "lastWriteDate" from the server's most
    // recent ismaster response.
    boost::optional<Date_t> _lastWriteDate;

    // opTime: an ObjectId or null. The last opTime reported by the server; an ObjectId or null.
    // (Only mongos and shard servers record this field when monitoring config servers as replica
    // sets.)
    boost::optional<repl::OpTime> _opTime;

    // (=) type: a ServerType enum value. Default Unknown.
    ServerType _type;

    // (=) minWireVersion, maxWireVersion: the wire protocol version range supported by the server.
    // Both default to 0. Use min and maxWireVersion only to determine compatibility.
    int _minWireVersion = 0;
    int _maxWireVersion = 0;

    // (=) me: The hostname or IP, and the port number, that this server was configured with in the
    // replica set. Default null.
    boost::optional<ServerAddress> _me;

    // (=) hosts, passives, arbiters: Sets of addresses. This server's opinion of the replica set's
    // members, if any. These hostnames are normalized to lower-case. Default empty. The client
    // monitors all three types of servers in a replica set.
    std::set<ServerAddress> _hosts;
    std::set<ServerAddress> _passives;
    std::set<ServerAddress> _arbiters;

    // (=) tags: map from string to string. Default empty.
    std::map<std::string, std::string> _tags;

    // (=) setName: string or null. Default null.
    boost::optional<std::string> _setName;

    // (=) setVersion: integer or null. Default null.
    boost::optional<int> _setVersion;

    // (=) electionId: an ObjectId, if this is a MongoDB 2.6+ replica set member that believes it is
    // primary. See using setVersion and electionId to detect stale primaries. Default null.
    boost::optional<OID> _electionId;

    // (=) primary: an address. This server's opinion of who the primary is. Default null.
    boost::optional<ServerAddress> _primary;

    // lastUpdateTime: when this server was last checked. Default "infinity ago".
    boost::optional<Date_t> _lastUpdateTime = Date_t::min();

    // (=) logicalSessionTimeoutMinutes: integer or null. Default null.
    boost::optional<int> _logicalSessionTimeoutMinutes;

    friend class ServerDescriptionBuilder;
};

bool operator==(const mongo::sdam::ServerDescription& a, const mongo::sdam::ServerDescription& b);
bool operator!=(const mongo::sdam::ServerDescription& a, const mongo::sdam::ServerDescription& b);
std::ostream& operator<<(std::ostream& os, const ServerDescriptionPtr& description);
std::ostream& operator<<(std::ostream& os, const ServerDescription& description);
};  // namespace mongo::sdam
