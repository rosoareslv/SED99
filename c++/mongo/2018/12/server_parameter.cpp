/**
 *    Copyright (C) 2018-present MongoDB, Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kControl

#include "mongo/idl/server_parameter.h"

#include "mongo/util/log.h"

namespace mongo {
using SPT = ServerParameterType;

MONGO_INITIALIZER_GROUP(BeginServerParameterRegistration,
                        MONGO_NO_PREREQUISITES,
                        ("EndServerParameterRegistration"))
MONGO_INITIALIZER_GROUP(EndServerParameterRegistration,
                        ("BeginServerParameterRegistration"),
                        ("BeginStartupOptionHandling"))

IDLServerParameter::IDLServerParameter(StringData name, ServerParameterType paramType)
    : ServerParameter(ServerParameterSet::getGlobal(),
                      name,
                      paramType == SPT::kStartupOnly || paramType == SPT::kStartupAndRuntime,
                      paramType == SPT::kRuntimeOnly || paramType == SPT::kStartupAndRuntime) {}

void IDLServerParameter::append(OperationContext* opCtx,
                                BSONObjBuilder& b,
                                const std::string& name) {
    invariant(_appendBSON,
              "append() called on IDLServerParamter with no appendBSON implementation");
    _appendBSON(opCtx, &b, name);
}

IDLServerParameterDeprecatedAlias::IDLServerParameterDeprecatedAlias(StringData name,
                                                                     ServerParameter* sp)
    : ServerParameter(ServerParameterSet::getGlobal(),
                      name,
                      sp->allowedToChangeAtStartup(),
                      sp->allowedToChangeAtRuntime()),
      _sp(sp) {
    if (_sp->isTestOnly()) {
        setTestOnly();
    }
}

Status IDLServerParameter::set(const BSONElement& newValueElement) try {
    if (_fromBSON) {
        return _fromBSON(newValueElement);
    } else {
        // Default fallback behavior: Cast to string and use 'from_string' method.
        return setFromString(newValueElement.String());
    }
} catch (const AssertionException& ex) {
    return {ErrorCodes::BadValue,
            str::stream() << "Invalid value '" << newValueElement << "' for setParameter '"
                          << name()
                          << "': "
                          << ex.what()};
}

Status IDLServerParameter::setFromString(const std::string& str) {
    invariant(_fromString,
              "setFromString() called on IDLServerParamter with no setFromString implementation");
    return _fromString(str);
}

void IDLServerParameterDeprecatedAlias::append(OperationContext* opCtx,
                                               BSONObjBuilder& b,
                                               const std::string& fieldName) {
    warning() << "Use of deprecated server parameter '" << name() << "', please use '"
              << _sp->name() << "' instead.";
    _sp->append(opCtx, b, fieldName);
}

Status IDLServerParameterDeprecatedAlias::set(const BSONElement& newValueElement) {
    warning() << "Use of deprecared server parameter '" << name() << "', please use '"
              << _sp->name() << "' instead.";
    return _sp->set(newValueElement);
}

Status IDLServerParameterDeprecatedAlias::setFromString(const std::string& str) {
    warning() << "Use of deprecared server parameter '" << name() << "', please use '"
              << _sp->name() << "' instead.";
    return _sp->setFromString(str);
}

}  // namespace mongo
