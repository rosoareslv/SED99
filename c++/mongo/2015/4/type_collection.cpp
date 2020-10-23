/**
 *    Copyright (C) 2012 10gen Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/s/catalog/type_collection.h"

#include "mongo/base/status_with.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/util/bson_extract.h"
#include "mongo/db/namespace_string.h"
#include "mongo/util/assert_util.h"

namespace mongo {

    const std::string CollectionType::ConfigNS = "config.collections";

    const BSONField<std::string> CollectionType::fullNs("_id");
    const BSONField<OID> CollectionType::epoch("lastmodEpoch");
    const BSONField<Date_t> CollectionType::updatedAt("lastmod");
    const BSONField<BSONObj> CollectionType::keyPattern("key");
    const BSONField<bool> CollectionType::unique("unique");
    const BSONField<bool> CollectionType::noBalance("noBalance");
    const BSONField<bool> CollectionType::dropped("dropped");


    CollectionType::CollectionType() {
        clear();
    }

    StatusWith<CollectionType> CollectionType::fromBSON(const BSONObj& source) {
        CollectionType coll;

        {
            std::string collFullNs;
            Status status = bsonExtractStringField(source, fullNs.name(), &collFullNs);
            if (!status.isOK()) return status;

            coll._fullNs = collFullNs;
        }

        {
            OID collEpoch;
            Status status = bsonExtractOIDField(source, epoch.name(), &collEpoch);
            if (!status.isOK()) return status;

            coll._epoch = collEpoch;
        }

        {
            BSONElement collUpdatedAt;
            Status status = bsonExtractTypedField(source, updatedAt.name(), Date, &collUpdatedAt);
            if (!status.isOK()) return status;

            coll._updatedAt = collUpdatedAt.Date();
        }

        {
            bool collDropped;

            // Dropped can be missing in which case it is presumed false
            Status status =
                bsonExtractBooleanFieldWithDefault(source, dropped.name(), false, &collDropped);
            if (!status.isOK()) return status;

            coll._dropped = collDropped;
        }

        {
            BSONElement collKeyPattern;
            Status status =
                    bsonExtractTypedField(source, keyPattern.name(), Object, &collKeyPattern);
            if (status.isOK()) {
                BSONObj obj = collKeyPattern.Obj();
                if (obj.isEmpty()) {
                    return Status(ErrorCodes::ShardKeyNotFound, "invalid shard key");
                }

                coll._keyPattern = obj.getOwned();
            }
            else if ((status == ErrorCodes::NoSuchKey) && coll.getDropped()) {
                // Sharding key can be missing if the collection is dropped
            }
            else {
                return status;
            }
        }

        {
            bool collUnique;

            // Key uniqueness can be missing in which case it is presumed false
            Status status =
                bsonExtractBooleanFieldWithDefault(source, unique.name(), false, &collUnique);
            if (!status.isOK()) return status;

            coll._unique = collUnique;
        }

        {
            bool collNoBalance;

            // No balance can be missing in which case it is presumed as false
            Status status = bsonExtractBooleanFieldWithDefault(source,
                                                               noBalance.name(),
                                                               false,
                                                               &collNoBalance);
            if (!status.isOK()) return status;

            coll._allowBalance = !collNoBalance;
        }

        return StatusWith<CollectionType>(coll);
    }

    Status CollectionType::validate() const {
        // These fields must always be set
        if (!_fullNs.is_initialized() || _fullNs->empty()) {
            return Status(ErrorCodes::NoSuchKey, "missing ns");
        }

        const NamespaceString nss(_fullNs.get());
        if (!nss.isValid()) {
            return Status(ErrorCodes::BadValue, "invalid namespace " + nss.toString());
        }

        if (!_epoch.is_initialized()) {
            return Status(ErrorCodes::NoSuchKey, "missing epoch");
        }

        if (!_updatedAt.is_initialized()) {
            return Status(ErrorCodes::NoSuchKey, "missing updated at timestamp");
        }

        if (!_dropped.get_value_or(false)) {
            if (!_epoch->isSet()) {
                return Status(ErrorCodes::BadValue, "invalid epoch");
            }

            if (!_updatedAt.get()) {
                return Status(ErrorCodes::BadValue, "invalid updated at timestamp");
            }

            if (!_keyPattern.is_initialized()) {
                return Status(ErrorCodes::NoSuchKey, "missing key pattern");
            }
            else {
                invariant(!_keyPattern->isEmpty());
            }
        }

        return Status::OK();
    }

    BSONObj CollectionType::toBSON() const {
        BSONObjBuilder builder;

        builder.append(fullNs.name(), _fullNs.get_value_or(""));
        builder.append(epoch.name(), _epoch.get_value_or(OID()));
        builder.append(updatedAt.name(), _updatedAt.get_value_or(0));

        // These fields are optional, so do not include them in the metadata for the purposes of
        // consuming less space on the config servers.

        if (_dropped.is_initialized()) {
            builder.append(dropped.name(), _dropped.get());
        }

        if (_keyPattern.is_initialized()) {
            builder.append(keyPattern.name(), _keyPattern.get());
        }

        if (_unique.is_initialized()) {
            builder.append(unique.name(), _unique.get());
        }

        if (_allowBalance.is_initialized()) {
            builder.append(noBalance.name(), !_allowBalance.get());
        }

        return builder.obj();
    }

    void CollectionType::clear() {
        _fullNs.reset();
        _epoch.reset();
        _updatedAt.reset();
        _keyPattern.reset();
        _unique.reset();
        _allowBalance.reset();
        _dropped.reset();
    }

    std::string CollectionType::toString() const {
        return toBSON().toString();
    }

    void CollectionType::setNs(const std::string& fullNs) {
        invariant(!fullNs.empty());
        _fullNs = fullNs;
    }

    void CollectionType::setEpoch(OID epoch) {
        _epoch = epoch;
    }

    void CollectionType::setUpdatedAt(Date_t updatedAt) {
        _updatedAt = updatedAt;
    }

    void CollectionType::setKeyPattern(const BSONObj& keyPattern) {
        invariant(!keyPattern.isEmpty());
        _keyPattern = keyPattern;
    }

} // namespace mongo
