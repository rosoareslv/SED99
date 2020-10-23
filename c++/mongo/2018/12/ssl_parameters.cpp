
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include "mongo/client/authenticate.h"
#include "mongo/config.h"
#include "mongo/db/auth/sasl_command_constants.h"
#include "mongo/db/server_options.h"
#include "mongo/util/log.h"
#include "mongo/util/net/ssl_options.h"
#include "mongo/util/net/ssl_parameters.h"

namespace mongo {
namespace {

std::string clusterAuthModeFormat() {
    switch (serverGlobalParams.clusterAuthMode.load()) {
        case ServerGlobalParams::ClusterAuthMode_keyFile:
            return "keyFile";
        case ServerGlobalParams::ClusterAuthMode_sendKeyFile:
            return "sendKeyFile";
        case ServerGlobalParams::ClusterAuthMode_sendX509:
            return "sendX509";
        case ServerGlobalParams::ClusterAuthMode_x509:
            return "x509";
        default:
            // Default case because clusterAuthMode is an AtomicInt32 and not bound by enum rules.
            return "undefined";
    }
}

StatusWith<ServerGlobalParams::ClusterAuthModes> clusterAuthModeParse(StringData strMode) {
    if (strMode == "keyFile") {
        return ServerGlobalParams::ClusterAuthMode_keyFile;
    } else if (strMode == "sendKeyFile") {
        return ServerGlobalParams::ClusterAuthMode_sendKeyFile;
    } else if (strMode == "sendX509") {
        return ServerGlobalParams::ClusterAuthMode_sendX509;
    } else if (strMode == "x509") {
        return ServerGlobalParams::ClusterAuthMode_x509;
    } else {
        return Status(
            ErrorCodes::BadValue,
            str::stream() << "Invalid clusterAuthMode '" << strMode
                          << "', expected one of: 'keyFile', 'sendKeyFile', 'sendX509', or 'x509'");
    }
}


template <typename T, typename U>
StatusWith<SSLParams::SSLModes> checkTLSModeTransition(T modeToString,
                                                       U stringToMode,
                                                       StringData parameterName,
                                                       StringData strMode) {
    auto mode = stringToMode(strMode);
    if (!mode.isOK()) {
        return mode.getStatus();
    }
    auto oldMode = sslGlobalParams.sslMode.load();
    if ((mode == SSLParams::SSLMode_preferSSL) && (oldMode == SSLParams::SSLMode_allowSSL)) {
        return mode;
    } else if ((mode == SSLParams::SSLMode_requireSSL) &&
               (oldMode == SSLParams::SSLMode_preferSSL)) {
        return mode;
    } else {
        return {ErrorCodes::BadValue,
                str::stream() << "Illegal state transition for " << parameterName
                              << ", attempt to change from "
                              << modeToString(static_cast<SSLParams::SSLModes>(oldMode))
                              << " to "
                              << strMode};
    }
}

}  // namespace
}  // namespace mongo

mongo::Status mongo::validateOpensslCipherConfig(const std::string&) {
    if (!sslGlobalParams.sslCipherConfig.empty()) {
        return {ErrorCodes::BadValue,
                "opensslCipherConfig setParameter is incompatible with net.tls.tlsCipherConfig"};
    }
    // Note that there is very little validation that we can do here.
    // OpenSSL exposes no API to validate a cipher config string. The only way to figure out
    // what a string maps to is to make an SSL_CTX object, set the string on it, then parse the
    // resulting STACK_OF object. If provided an invalid entry in the string, it will silently
    // ignore it. Because an entry in the string may map to multiple ciphers, or remove ciphers
    // from the final set produced by the full string, we can't tell if any entry failed
    // to parse.
    return Status::OK();
}

mongo::Status mongo::validateDisableNonTLSConnectionLogging(const bool&) {
    if (sslGlobalParams.disableNonSSLConnectionLoggingSet) {
        return {ErrorCodes::BadValue,
                "Error parsing command line: Multiple occurrences of option "
                "disableNonTLSConnectionLogging"};
    }
    return Status::OK();
}

mongo::Status mongo::onUpdateDisableNonTLSConnectionLogging(const bool&) {
    // disableNonSSLConnectionLogging is a write-once setting.
    // Once we've updated it, we're not allowed to specify the set-param again.
    // Record that update in a second bool value.
    sslGlobalParams.disableNonSSLConnectionLoggingSet = true;
    return Status::OK();
}

void mongo::appendSSLModeToBSON(OperationContext*, BSONObjBuilder* builder, StringData fieldName) {
    warning() << "Use of deprecared server parameter 'sslMode', please use 'tlsMode' instead.";
    builder->append(fieldName, SSLParams::sslModeFormat(sslGlobalParams.sslMode.load()));
}

void mongo::appendTLSModeToBSON(OperationContext*, BSONObjBuilder* builder, StringData fieldName) {
    builder->append(
        fieldName,
        SSLParams::tlsModeFormat(static_cast<SSLParams::SSLModes>(sslGlobalParams.sslMode.load())));
}

mongo::Status mongo::setSSLModeFromString(StringData strMode) {
#ifndef MONGO_CONFIG_SSL
    return {ErrorCodes::IllegalOperation,
            "Unable to set sslMode, SSL support is not compiled into server"};
#endif

    warning() << "Use of deprecared server parameter 'sslMode', please use 'tlsMode' instead.";

    auto swNewMode = checkTLSModeTransition(
        SSLParams::sslModeFormat, SSLParams::sslModeParse, "sslMode", strMode);
    if (!swNewMode.isOK()) {
        return swNewMode.getStatus();
    }
    sslGlobalParams.sslMode.store(swNewMode.getValue());
    return Status::OK();
}

mongo::Status mongo::setTLSModeFromString(StringData strMode) {
#ifndef MONGO_CONFIG_SSL
    return {ErrorCodes::IllegalOperation,
            "Unable to set tlsMode, TLS support is not compiled into server"};
#endif
    auto swNewMode = checkTLSModeTransition(
        SSLParams::tlsModeFormat, SSLParams::tlsModeParse, "tlsMode", strMode);
    if (!swNewMode.isOK()) {
        return swNewMode.getStatus();
    }
    sslGlobalParams.sslMode.store(swNewMode.getValue());
    return Status::OK();
}


void mongo::appendClusterAuthModeToBSON(OperationContext*,
                                        BSONObjBuilder* builder,
                                        StringData fieldName) {
    builder->append(fieldName, clusterAuthModeFormat());
}

mongo::Status mongo::setClusterAuthModeFromString(StringData strMode) {
#ifndef MONGO_CONFIG_SSL
    return {ErrorCodes::IllegalOperation,
            "Unable to set clusterAuthMode, SSL support is not compiled into server"};
#endif

    auto swMode = clusterAuthModeParse(strMode);
    if (!swMode.isOK()) {
        return swMode.getStatus();
    }

    auto mode = swMode.getValue();
    auto oldMode = serverGlobalParams.clusterAuthMode.load();
    auto sslMode = sslGlobalParams.sslMode.load();
    if ((mode == ServerGlobalParams::ClusterAuthMode_sendX509) &&
        (oldMode == ServerGlobalParams::ClusterAuthMode_sendKeyFile)) {
        if (sslMode == SSLParams::SSLMode_disabled || sslMode == SSLParams::SSLMode_allowSSL) {
            return {ErrorCodes::BadValue,
                    "Illegal state transition for clusterAuthMode, need to enable SSL for outgoing "
                    "connections"};
        }
        serverGlobalParams.clusterAuthMode.store(mode);
#ifdef MONGO_CONFIG_SSL
        auth::setInternalUserAuthParams(
            BSON(saslCommandMechanismFieldName << "MONGODB-X509" << saslCommandUserDBFieldName
                                               << "$external"));
#endif
    } else if ((mode == ServerGlobalParams::ClusterAuthMode_x509) &&
               (oldMode == ServerGlobalParams::ClusterAuthMode_sendX509)) {
        serverGlobalParams.clusterAuthMode.store(mode);
    } else {
        return {ErrorCodes::BadValue,
                str::stream() << "Illegal state transition for clusterAuthMode, change from "
                              << clusterAuthModeFormat()
                              << " to "
                              << strMode};
    }

    return Status::OK();
}
