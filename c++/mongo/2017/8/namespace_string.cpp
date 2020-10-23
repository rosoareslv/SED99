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

#include "mongo/platform/basic.h"

#include "mongo/db/namespace_string.h"

#include <ostream>

#include "mongo/base/parse_number.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

using std::string;

namespace {

/**
 *  A map of characters to escape. Instead of printing certain characters we output
 *  based on the following table.
 */
const string escapeTable[256] = {
    ".00",  ".01",  ".02",  ".03",  ".04",  ".05",  ".06",  ".07",  ".08",  ".09",  ".10",  ".11",
    ".12",  ".13",  ".14",  ".15",  ".16",  ".17",  ".18",  ".19",  ".20",  ".21",  ".22",  ".23",
    ".24",  ".25",  ".26",  ".27",  ".28",  ".29",  ".30",  ".31",  ".32",  ".33",  ".34",  ".35",
    ".36",  ".37",  ".38",  ".39",  ".40",  ".41",  ".42",  ".43",  ".44",  ".45",  ".",    ".47",
    "0",    "1",    "2",    "3",    "4",    "5",    "6",    "7",    "8",    "9",    ".58",  ".59",
    ".60",  ".61",  ".62",  ".63",  ".64",  "A",    "B",    "C",    "D",    "E",    "F",    "G",
    "H",    "I",    "J",    "K",    "L",    "M",    "N",    "O",    "P",    "Q",    "R",    "S",
    "T",    "U",    "V",    "W",    "X",    "Y",    "Z",    ".91",  ".92",  ".93",  ".94",  "_",
    ".96",  "a",    "b",    "c",    "d",    "e",    "f",    "g",    "h",    "i",    "j",    "k",
    "l",    "m",    "n",    "o",    "p",    "q",    "r",    "s",    "t",    "u",    "v",    "w",
    "x",    "y",    "z",    ".123", ".124", ".125", ".126", ".127", ".128", ".129", ".130", ".131",
    ".132", ".133", ".134", ".135", ".136", ".137", ".138", ".139", ".140", ".141", ".142", ".143",
    ".144", ".145", ".146", ".147", ".148", ".149", ".150", ".151", ".152", ".153", ".154", ".155",
    ".156", ".157", ".158", ".159", ".160", ".161", ".162", ".163", ".164", ".165", ".166", ".167",
    ".168", ".169", ".170", ".171", ".172", ".173", ".174", ".175", ".176", ".177", ".178", ".179",
    ".180", ".181", ".182", ".183", ".184", ".185", ".186", ".187", ".188", ".189", ".190", ".191",
    ".192", ".193", ".194", ".195", ".196", ".197", ".198", ".199", ".200", ".201", ".202", ".203",
    ".204", ".205", ".206", ".207", ".208", ".209", ".210", ".211", ".212", ".213", ".214", ".215",
    ".216", ".217", ".218", ".219", ".220", ".221", ".222", ".223", ".224", ".225", ".226", ".227",
    ".228", ".229", ".230", ".231", ".232", ".233", ".234", ".235", ".236", ".237", ".238", ".239",
    ".240", ".241", ".242", ".243", ".244", ".245", ".246", ".247", ".248", ".249", ".250", ".251",
    ".252", ".253", ".254", ".255"};

const char kServerConfiguration[] = "admin.system.version";
const char kLogicalTimeKeysCollection[] = "admin.system.keys";

constexpr auto listCollectionsCursorCol = "$cmd.listCollections"_sd;
constexpr auto listIndexesCursorNSPrefix = "$cmd.listIndexes."_sd;
constexpr auto collectionlessAggregateCursorCol = "$cmd.aggregate"_sd;
constexpr auto dropPendingNSPrefix = "system.drop."_sd;

}  // namespace

constexpr StringData NamespaceString::kAdminDb;
constexpr StringData NamespaceString::kLocalDb;
constexpr StringData NamespaceString::kConfigDb;
constexpr StringData NamespaceString::kSystemDotViewsCollectionName;
constexpr StringData NamespaceString::kShardConfigCollectionsCollectionName;

const NamespaceString NamespaceString::kServerConfigurationNamespace(kServerConfiguration);
const NamespaceString NamespaceString::kSessionTransactionsTableNamespace(
    NamespaceString::kConfigDb, "transactions");
const NamespaceString NamespaceString::kRsOplogNamespace(NamespaceString::kLocalDb, "oplog.rs");

bool NamespaceString::isListCollectionsCursorNS() const {
    return coll() == listCollectionsCursorCol;
}

bool NamespaceString::isListIndexesCursorNS() const {
    return coll().size() > listIndexesCursorNSPrefix.size() &&
        coll().startsWith(listIndexesCursorNSPrefix);
}

bool NamespaceString::isCollectionlessAggregateNS() const {
    return coll() == collectionlessAggregateCursorCol;
}

bool NamespaceString::isLegalClientSystemNS() const {
    if (db() == "admin") {
        if (ns() == "admin.system.roles")
            return true;
        if (ns() == kServerConfiguration)
            return true;
        if (ns() == kLogicalTimeKeysCollection)
            return true;
        if (ns() == "admin.system.new_users")
            return true;
        if (ns() == "admin.system.backup_users")
            return true;
        if (ns() == "admin.system.sessions")
            return true;
    }
    if (ns() == "local.system.replset")
        return true;

    if (coll() == "system.users")
        return true;
    if (coll() == "system.js")
        return true;

    if (coll() == kSystemDotViewsCollectionName)
        return true;

    return false;
}

NamespaceString NamespaceString::makeListCollectionsNSS(StringData dbName) {
    NamespaceString nss(dbName, listCollectionsCursorCol);
    dassert(nss.isValid());
    dassert(nss.isListCollectionsCursorNS());
    return nss;
}

NamespaceString NamespaceString::makeListIndexesNSS(StringData dbName, StringData collectionName) {
    NamespaceString nss(dbName, str::stream() << listIndexesCursorNSPrefix << collectionName);
    dassert(nss.isValid());
    dassert(nss.isListIndexesCursorNS());
    return nss;
}

NamespaceString NamespaceString::makeCollectionlessAggregateNSS(StringData dbname) {
    NamespaceString nss(dbname, collectionlessAggregateCursorCol);
    dassert(nss.isValid());
    dassert(nss.isCollectionlessAggregateNS());
    return nss;
}

NamespaceString NamespaceString::getTargetNSForListIndexes() const {
    dassert(isListIndexesCursorNS());
    return NamespaceString(db(), coll().substr(listIndexesCursorNSPrefix.size()));
}

boost::optional<NamespaceString> NamespaceString::getTargetNSForGloballyManagedNamespace() const {
    // Globally managed namespaces are of the form '$cmd.commandName.<targetNs>' or simply
    // '$cmd.commandName'.
    dassert(isGloballyManagedNamespace());
    const size_t indexOfNextDot = coll().find('.', 5);
    if (indexOfNextDot == std::string::npos) {
        return boost::none;
    }
    return NamespaceString{db(), coll().substr(indexOfNextDot + 1)};
}

bool NamespaceString::isDropPendingNamespace() const {
    return coll().startsWith(dropPendingNSPrefix);
}

NamespaceString NamespaceString::makeDropPendingNamespace(const repl::OpTime& opTime) const {
    mongo::StringBuilder ss;
    ss << db() << "." << dropPendingNSPrefix;
    ss << opTime.getSecs() << "i" << opTime.getTimestamp().getInc() << "t" << opTime.getTerm();
    ss << "." << coll();
    return NamespaceString(ss.stringData().substr(0, MaxNsCollectionLen));
}

StatusWith<repl::OpTime> NamespaceString::getDropPendingNamespaceOpTime() const {
    if (!isDropPendingNamespace()) {
        return Status(ErrorCodes::BadValue,
                      str::stream() << "Not a drop-pending namespace: " << _ns);
    }

    auto collectionName = coll();
    auto opTimeBeginIndex = dropPendingNSPrefix.size();
    auto opTimeEndIndex = collectionName.find('.', opTimeBeginIndex);
    auto opTimeStr = std::string::npos == opTimeEndIndex
        ? collectionName.substr(opTimeBeginIndex)
        : collectionName.substr(opTimeBeginIndex, opTimeEndIndex - opTimeBeginIndex);

    auto incrementSeparatorIndex = opTimeStr.find('i');
    if (std::string::npos == incrementSeparatorIndex) {
        return Status(ErrorCodes::FailedToParse,
                      str::stream() << "Missing 'i' separator in drop-pending namespace: " << _ns);
    }

    auto termSeparatorIndex = opTimeStr.find('t', incrementSeparatorIndex);
    if (std::string::npos == termSeparatorIndex) {
        return Status(ErrorCodes::FailedToParse,
                      str::stream() << "Missing 't' separator in drop-pending namespace: " << _ns);
    }

    long long seconds;
    auto status = parseNumberFromString(opTimeStr.substr(0, incrementSeparatorIndex), &seconds);
    if (!status.isOK()) {
        return Status(
            status.code(),
            str::stream() << "Invalid timestamp seconds in drop-pending namespace: " << _ns << ": "
                          << status.reason());
    }

    unsigned int increment;
    status =
        parseNumberFromString(opTimeStr.substr(incrementSeparatorIndex + 1,
                                               termSeparatorIndex - (incrementSeparatorIndex + 1)),
                              &increment);
    if (!status.isOK()) {
        return Status(status.code(),
                      str::stream() << "Invalid timestamp increment in drop-pending namespace: "
                                    << _ns
                                    << ": "
                                    << status.reason());
    }

    long long term;
    status = mongo::parseNumberFromString(opTimeStr.substr(termSeparatorIndex + 1), &term);
    if (!status.isOK()) {
        return Status(status.code(),
                      str::stream() << "Invalid term in drop-pending namespace: " << _ns << ": "
                                    << status.reason());
    }

    return repl::OpTime(Timestamp(Seconds(seconds), increment), term);
}

Status NamespaceString::checkLengthForRename(
    const std::string::size_type longestIndexNameLength) const {
    auto longestAllowed =
        std::min(std::string::size_type(NamespaceString::MaxNsCollectionLen),
                 std::string::size_type(NamespaceString::MaxNsLen - 2U /*strlen(".$")*/ -
                                        longestIndexNameLength));
    if (size() > longestAllowed) {
        StringBuilder sb;
        sb << "collection name length of " << size() << " exceeds maximum length of "
           << longestAllowed << ", allowing for index names";
        return Status(ErrorCodes::InvalidLength, sb.str());
    }
    return Status::OK();
}

string NamespaceString::escapeDbName(const StringData dbname) {
    std::string escapedDbName;

    // pre-alloc the return string as it will always be the same as dbname at a minimum.
    escapedDbName.reserve(dbname.size());

    for (unsigned char c : dbname) {
        escapedDbName += escapeTable[c];
    }
    return escapedDbName;
}


std::ostream& operator<<(std::ostream& stream, const NamespaceString& nss) {
    return stream << nss.toString();
}

}  // namespace mongo
