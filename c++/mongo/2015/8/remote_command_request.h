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

#pragma once

#include <string>

#include "mongo/db/jsobj.h"
#include "mongo/rpc/metadata.h"
#include "mongo/rpc/request_interface.h"
#include "mongo/util/net/hostandport.h"
#include "mongo/util/time_support.h"

namespace mongo {
namespace executor {

/**
 * Type of object describing a command to execute against a remote MongoDB node.
 */
struct RemoteCommandRequest {
    // Indicates that there is no timeout for the request to complete
    static const Milliseconds kNoTimeout;

    // Indicates that there is no expiration time by when the request needs to complete
    static const Date_t kNoExpirationDate;

    RemoteCommandRequest() = default;

    RemoteCommandRequest(const HostAndPort& theTarget,
                         const std::string& theDbName,
                         const BSONObj& theCmdObj,
                         const BSONObj& metadataObj,
                         const Milliseconds timeoutMillis = kNoTimeout)
        : target(theTarget),
          dbname(theDbName),
          metadata(metadataObj),
          cmdObj(theCmdObj),
          timeout(timeoutMillis) {
        if (timeoutMillis == kNoTimeout) {
            expirationDate = kNoExpirationDate;
        }
    }

    RemoteCommandRequest(const HostAndPort& theTarget,
                         const std::string& theDbName,
                         const BSONObj& theCmdObj,
                         const Milliseconds timeoutMillis = kNoTimeout)
        : RemoteCommandRequest(
              theTarget, theDbName, theCmdObj, rpc::makeEmptyMetadata(), timeoutMillis) {}

    RemoteCommandRequest(const HostAndPort& theTarget,
                         const rpc::RequestInterface& request,
                         const Milliseconds timeoutMillis = kNoTimeout)
        : RemoteCommandRequest(theTarget,
                               request.getDatabase().toString(),
                               request.getCommandArgs(),
                               request.getMetadata(),
                               timeoutMillis) {}

    std::string toString() const;

    HostAndPort target;
    std::string dbname;
    BSONObj metadata{rpc::makeEmptyMetadata()};
    BSONObj cmdObj;
    Milliseconds timeout = kNoTimeout;

    // Deadline by when the request must be completed
    Date_t expirationDate = kNoExpirationDate;
};

}  // namespace executor
}  // namespace mongo
