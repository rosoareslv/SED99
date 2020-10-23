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

#pragma once

#include <string>

#include "mongo/base/status_with.h"
#include "mongo/util/uuid.h"

namespace mongo {

class TxnId;

/**
 * A 128-bit identifier for a logical session.
 */
class LogicalSessionId {
public:
    LogicalSessionId() = delete;

    /**
     * Construct a new LogicalSessionId out of a txnId received with an operation.
     */
    static StatusWith<LogicalSessionId> parse(const TxnId& txnId);

    /**
     * If the given string represents a valid LogicalSessionId, constructs and returns,
     * the id, otherwise returns an error.
     */
    static StatusWith<LogicalSessionId> parse(const std::string& s);

    /**
     * Returns a string representation of this session id.
     */
    std::string toString() const;

    inline bool operator==(const LogicalSessionId& rhs) const {
        return _id == rhs._id;
    }

    inline bool operator!=(const LogicalSessionId& rhs) const {
        return !(*this == rhs);
    }

    /**
     * Custom hasher so LogicalSessionIds can be used in unordered data structures.
     *
     * ex: std::unordered_set<LogicalSessionId, LogicalSessionId::Hash> lsidSet;
     */
    struct Hash {
        std::size_t operator()(const LogicalSessionId& lsid) const {
            return _hasher(lsid._id);
        }

    private:
        UUID::Hash _hasher;
    };


private:
    UUID _id;

    /**
     * Construct a LogicalSessionId from a UUID.
     */
    LogicalSessionId(UUID id);
};

inline std::ostream& operator<<(std::ostream& s, const LogicalSessionId& lsid) {
    return (s << lsid.toString());
}

inline StringBuilder& operator<<(StringBuilder& s, const LogicalSessionId& lsid) {
    return (s << lsid.toString());
}

}  // namespace mongo
