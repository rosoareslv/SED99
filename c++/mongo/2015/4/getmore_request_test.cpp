/**
 *    Copyright 2015 MongoDB Inc.
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

#include <string>

#include "mongo/db/query/getmore_request.h"
#include "mongo/db/jsobj.h"

#include "mongo/unittest/unittest.h"

namespace {

    using namespace mongo;

    TEST(GetMoreRequestTest, parseFromBSONEmptyCommandObject) {
        StatusWith<GetMoreRequest> result = GetMoreRequest::parseFromBSON("db", BSONObj());
        ASSERT_NOT_OK(result.getStatus());
        ASSERT_EQUALS(ErrorCodes::FailedToParse, result.getStatus().code());
    }

    TEST(GetMoreRequestTest, parseFromBSONCursorIdNotNumeric) {
        StatusWith<GetMoreRequest> result = GetMoreRequest::parseFromBSON(
            "db",
            BSON("getMore" << "not a number" <<
                 "collection" << "coll"));
        ASSERT_NOT_OK(result.getStatus());
        ASSERT_EQUALS(ErrorCodes::TypeMismatch, result.getStatus().code());
    }

    TEST(GetMoreRequestTest, parseFromBSONCursorIdNotLongLong) {
        StatusWith<GetMoreRequest> result = GetMoreRequest::parseFromBSON(
            "db",
            BSON("getMore" << "not a number" <<
                 "collection" << 123));
        ASSERT_NOT_OK(result.getStatus());
        ASSERT_EQUALS(ErrorCodes::TypeMismatch, result.getStatus().code());
    }

    TEST(GetMoreRequestTest, parseFromBSONMissingCollection) {
        StatusWith<GetMoreRequest> result = GetMoreRequest::parseFromBSON(
            "db",
            BSON("getMore" << CursorId(123)));
        ASSERT_NOT_OK(result.getStatus());
        ASSERT_EQUALS(ErrorCodes::TypeMismatch, result.getStatus().code());
    }

    TEST(GetMoreRequestTest, parseFromBSONCollectionNotString) {
        StatusWith<GetMoreRequest> result = GetMoreRequest::parseFromBSON(
            "db",
            BSON("getMore" << CursorId(123) << "collection" << 456));
        ASSERT_NOT_OK(result.getStatus());
        ASSERT_EQUALS(ErrorCodes::TypeMismatch, result.getStatus().code());
    }

    TEST(GetMoreRequestTest, parseFromBSONBatchSizeNotInteger) {
        StatusWith<GetMoreRequest> result = GetMoreRequest::parseFromBSON(
            "db",
            BSON("getMore" << CursorId(123) <<
                 "collection" << "coll" <<
                 "batchSize" << "not a number"));
        ASSERT_NOT_OK(result.getStatus());
        ASSERT_EQUALS(ErrorCodes::TypeMismatch, result.getStatus().code());
    }

    TEST(GetMoreRequestTest, parseFromBSONInvalidDbName) {
        StatusWith<GetMoreRequest> result = GetMoreRequest::parseFromBSON(
            "",
            BSON("getMore" << CursorId(123) << "collection" << "coll"));
        ASSERT_NOT_OK(result.getStatus());
        ASSERT_EQUALS(ErrorCodes::BadValue, result.getStatus().code());
    }

    TEST(GetMoreRequestTest, parseFromBSONInvalidCursorId) {
        StatusWith<GetMoreRequest> result = GetMoreRequest::parseFromBSON(
            "db",
            BSON("getMore" << CursorId(0) << "collection" << "coll"));
        ASSERT_NOT_OK(result.getStatus());
        ASSERT_EQUALS(ErrorCodes::BadValue, result.getStatus().code());
    }

    TEST(GetMoreRequestTest, parseFromBSONNegativeCursorId) {
        StatusWith<GetMoreRequest> result = GetMoreRequest::parseFromBSON(
            "db",
            BSON("getMore" << CursorId(-123) << "collection" << "coll"));
        ASSERT_OK(result.getStatus());
        ASSERT_EQUALS("db.coll", result.getValue().nss.toString());
        ASSERT_EQUALS(CursorId(-123), result.getValue().cursorid);
        ASSERT_EQUALS(GetMoreRequest::kDefaultBatchSize, result.getValue().batchSize);
    }

    TEST(GetMoreRequestTest, parseFromBSONUnrecognizedFieldName) {
        StatusWith<GetMoreRequest> result = GetMoreRequest::parseFromBSON(
            "db",
            BSON("getMore" << CursorId(123) <<
                 "collection" << "coll" <<
                 "unknown_field" << 1));
        ASSERT_OK(result.getStatus());
        ASSERT_EQUALS("db.coll", result.getValue().nss.toString());
        ASSERT_EQUALS(CursorId(123), result.getValue().cursorid);
        ASSERT_EQUALS(GetMoreRequest::kDefaultBatchSize, result.getValue().batchSize);
    }

    TEST(GetMoreRequestTest, parseFromBSONInvalidBatchSize) {
        StatusWith<GetMoreRequest> result = GetMoreRequest::parseFromBSON(
            "db",
            BSON("getMore" << CursorId(123) << "collection" << "coll" << "batchSize" << -1));
        ASSERT_NOT_OK(result.getStatus());
        ASSERT_EQUALS(ErrorCodes::BadValue, result.getStatus().code());
    }

    TEST(GetMoreRequestTest, parseFromBSONDefaultBatchSize) {
        StatusWith<GetMoreRequest> result = GetMoreRequest::parseFromBSON(
            "db",
            BSON("getMore" << CursorId(123) << "collection" << "coll"));
        ASSERT_OK(result.getStatus());
        ASSERT_EQUALS("db.coll", result.getValue().nss.toString());
        ASSERT_EQUALS(CursorId(123), result.getValue().cursorid);
        ASSERT_EQUALS(GetMoreRequest::kDefaultBatchSize, result.getValue().batchSize);
    }

    TEST(GetMoreRequestTest, parseFromBSONBatchSizeProvided) {
        StatusWith<GetMoreRequest> result = GetMoreRequest::parseFromBSON(
            "db",
            BSON("getMore" << CursorId(123) << "collection" << "coll" << "batchSize" << 200));
        ASSERT_EQUALS("db.coll", result.getValue().nss.toString());
        ASSERT_EQUALS(CursorId(123), result.getValue().cursorid);
        ASSERT_EQUALS(200, result.getValue().batchSize);
    }

} // namespace
