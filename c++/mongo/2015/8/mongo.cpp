/**
 * Copyright (C) 2015 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/scripting/mozjs/mongo.h"

#include "mongo/client/dbclientinterface.h"
#include "mongo/client/native_sasl_client_session.h"
#include "mongo/client/sasl_client_authenticate.h"
#include "mongo/client/sasl_client_session.h"
#include "mongo/db/namespace_string.h"
#include "mongo/scripting/mozjs/cursor.h"
#include "mongo/scripting/mozjs/implscope.h"
#include "mongo/scripting/mozjs/objectwrapper.h"
#include "mongo/scripting/mozjs/valuereader.h"
#include "mongo/scripting/mozjs/valuewriter.h"
#include "mongo/stdx/memory.h"

namespace mongo {
namespace mozjs {

const JSFunctionSpec MongoBase::methods[] = {
    MONGO_ATTACH_JS_FUNCTION(auth),
    MONGO_ATTACH_JS_FUNCTION(copyDatabaseWithSCRAM),
    MONGO_ATTACH_JS_FUNCTION(cursorFromId),
    MONGO_ATTACH_JS_FUNCTION(cursorHandleFromId),
    MONGO_ATTACH_JS_FUNCTION(find),
    MONGO_ATTACH_JS_FUNCTION(getClientRPCProtocols),
    MONGO_ATTACH_JS_FUNCTION(getServerRPCProtocols),
    MONGO_ATTACH_JS_FUNCTION(insert),
    MONGO_ATTACH_JS_FUNCTION(logout),
    MONGO_ATTACH_JS_FUNCTION(remove),
    MONGO_ATTACH_JS_FUNCTION(runCommand),
    MONGO_ATTACH_JS_FUNCTION(runCommandWithMetadata),
    MONGO_ATTACH_JS_FUNCTION(setClientRPCProtocols),
    MONGO_ATTACH_JS_FUNCTION(update),
    JS_FS_END,
};

const char* const MongoBase::className = "Mongo";

const JSFunctionSpec MongoExternalInfo::freeFunctions[3] = {
    MONGO_ATTACH_JS_FUNCTION(load), MONGO_ATTACH_JS_FUNCTION(quit), JS_FS_END,
};

namespace {
DBClientBase* getConnection(JS::CallArgs& args) {
    return static_cast<std::shared_ptr<DBClientBase>*>(JS_GetPrivate(args.thisv().toObjectOrNull()))
        ->get();
}

void setCursor(JS::HandleObject target,
               std::unique_ptr<DBClientCursor> cursor,
               JS::CallArgs& args) {
    auto client =
        static_cast<std::shared_ptr<DBClientBase>*>(JS_GetPrivate(args.thisv().toObjectOrNull()));

    // Copy the client shared pointer to up the refcount
    JS_SetPrivate(target, new CursorInfo::CursorHolder(std::move(cursor), *client));
}

void setCursorHandle(JS::HandleObject target, long long cursorId, JS::CallArgs& args) {
    auto client =
        static_cast<std::shared_ptr<DBClientBase>*>(JS_GetPrivate(args.thisv().toObjectOrNull()));

    // Copy the client shared pointer to up the refcount.
    JS_SetPrivate(target, new CursorHandleInfo::CursorTracker(cursorId, *client));
}
}  // namespace

void MongoBase::finalize(JSFreeOp* fop, JSObject* obj) {
    auto conn = static_cast<std::shared_ptr<DBClientBase>*>(JS_GetPrivate(obj));

    if (conn) {
        delete conn;
    }
}

void MongoBase::Functions::runCommand(JSContext* cx, JS::CallArgs args) {
    if (args.length() != 3)
        uasserted(ErrorCodes::BadValue, "runCommand needs 3 args");

    if (!args.get(0).isString())
        uasserted(ErrorCodes::BadValue, "the database parameter to runCommand must be a string");

    if (!args.get(1).isObject())
        uasserted(ErrorCodes::BadValue, "the cmdObj parameter to runCommand must be an object");

    if (!args.get(2).isNumber())
        uasserted(ErrorCodes::BadValue, "the options parameter to runCommand must be a number");

    auto conn = getConnection(args);

    std::string database = ValueWriter(cx, args.get(0)).toString();

    BSONObj cmdObj = ValueWriter(cx, args.get(1)).toBSON();

    int queryOptions = ValueWriter(cx, args.get(2)).toInt32();
    BSONObj cmdRes;
    conn->runCommand(database, cmdObj, cmdRes, queryOptions);

    // the returned object is not read only as some of our tests depend on modifying it.
    ValueReader(cx, args.rval()).fromBSON(cmdRes, false /* read only */);
}

void MongoBase::Functions::runCommandWithMetadata(JSContext* cx, JS::CallArgs args) {
    if (args.length() != 4)
        uasserted(ErrorCodes::BadValue, "runCommandWithMetadata needs 4 args");

    if (!args.get(0).isString())
        uasserted(ErrorCodes::BadValue,
                  "the database parameter to runCommandWithMetadata must be a string");

    if (!args.get(1).isString())
        uasserted(ErrorCodes::BadValue,
                  "the commandName parameter to runCommandWithMetadata must be a string");

    if (!args.get(2).isObject())
        uasserted(ErrorCodes::BadValue,
                  "the metadata argument to runCommandWithMetadata must be an object");

    if (!args.get(3).isObject())
        uasserted(ErrorCodes::BadValue,
                  "the commandArgs argument to runCommandWithMetadata must be an object");

    std::string database = ValueWriter(cx, args.get(0)).toString();
    std::string commandName = ValueWriter(cx, args.get(1)).toString();
    BSONObj metadata = ValueWriter(cx, args.get(2)).toBSON();
    BSONObj commandArgs = ValueWriter(cx, args.get(3)).toBSON();

    auto conn = getConnection(args);
    auto res = conn->runCommandWithMetadata(database, commandName, metadata, commandArgs);

    BSONObjBuilder mergedResultBob;
    mergedResultBob.append("commandReply", res->getCommandReply());
    mergedResultBob.append("metadata", res->getMetadata());

    auto mergedResult = mergedResultBob.done();
    ValueReader(cx, args.rval()).fromBSON(mergedResult, false);
}

void MongoBase::Functions::find(JSContext* cx, JS::CallArgs args) {
    auto scope = getScope(cx);

    if (args.length() != 7)
        uasserted(ErrorCodes::BadValue, "find needs 7 args");

    if (!args.get(1).isObject())
        uasserted(ErrorCodes::BadValue, "needs to be an object");

    auto conn = getConnection(args);

    std::string ns = ValueWriter(cx, args.get(0)).toString();

    BSONObj fields;
    BSONObj q = ValueWriter(cx, args.get(1)).toBSON();

    bool haveFields = false;

    if (args.get(2).isObject()) {
        size_t i = 0;

        JS::RootedObject obj(cx, args.get(2).toObjectOrNull());

        ObjectWrapper(cx, obj).enumerate([&i](jsid) { ++i; });

        if (i > 0)
            haveFields = true;
    }

    if (haveFields)
        fields = ValueWriter(cx, args.get(2)).toBSON();

    int nToReturn = ValueWriter(cx, args.get(3)).toInt32();
    int nToSkip = ValueWriter(cx, args.get(4)).toInt32();
    int batchSize = ValueWriter(cx, args.get(5)).toInt32();
    int options = ValueWriter(cx, args.get(6)).toInt32();

    std::unique_ptr<DBClientCursor> cursor(
        conn->query(ns, q, nToReturn, nToSkip, haveFields ? &fields : NULL, options, batchSize));
    if (!cursor.get()) {
        uasserted(ErrorCodes::InternalError, "error doing query: failed");
    }

    JS::RootedObject c(cx);
    scope->getCursorProto().newInstance(&c);

    setCursor(c, std::move(cursor), args);

    args.rval().setObjectOrNull(c);
}

void MongoBase::Functions::insert(JSContext* cx, JS::CallArgs args) {
    auto scope = getScope(cx);

    if (args.length() != 3)
        uasserted(ErrorCodes::BadValue, "insert needs 3 args");

    if (!args.get(1).isObject())
        uasserted(ErrorCodes::BadValue, "attempted to insert a non-object");

    ObjectWrapper o(cx, args.thisv());

    if (o.hasField("readOnly") && o.getBoolean("readOnly"))
        uasserted(ErrorCodes::BadValue, "js db in read only mode");

    auto conn = getConnection(args);

    std::string ns = ValueWriter(cx, args.get(0)).toString();

    int flags = ValueWriter(cx, args.get(2)).toInt32();

    auto addId = [cx, scope](JS::HandleValue value) {
        if (!value.isObject())
            uasserted(ErrorCodes::BadValue, "attempted to insert a non-object type");

        JS::RootedObject elementObj(cx, value.toObjectOrNull());

        ObjectWrapper ele(cx, elementObj);

        if (!ele.hasField("_id")) {
            JS::RootedValue value(cx);
            scope->getOidProto().newInstance(&value);
            ele.setValue("_id", value);
        }

        return ValueWriter(cx, value).toBSON();
    };

    if (args.get(1).isObject() && JS_IsArrayObject(cx, args.get(1))) {
        JS::RootedObject obj(cx, args.get(1).toObjectOrNull());
        ObjectWrapper array(cx, obj);

        std::vector<BSONObj> bos;

        bool foundElement = false;

        array.enumerate([&](JS::HandleId id) {
            foundElement = true;

            JS::RootedValue value(cx);
            array.getValue(id, &value);

            bos.push_back(addId(value));
        });

        if (!foundElement)
            uasserted(ErrorCodes::BadValue, "attempted to insert an empty array");

        conn->insert(ns, bos, flags);
    } else {
        conn->insert(ns, addId(args.get(1)));
    }

    args.rval().setUndefined();
}

void MongoBase::Functions::remove(JSContext* cx, JS::CallArgs args) {
    if (!(args.length() == 2 || args.length() == 3))
        uasserted(ErrorCodes::BadValue, "remove needs 2 or 3 args");

    if (!(args.get(1).isObject()))
        uasserted(ErrorCodes::BadValue, "attempted to remove a non-object");

    ObjectWrapper o(cx, args.thisv());

    if (o.hasField("readOnly") && o.getBoolean("readOnly"))
        uasserted(ErrorCodes::BadValue, "js db in read only mode");

    auto conn = getConnection(args);
    std::string ns = ValueWriter(cx, args.get(0)).toString();

    BSONObj bson = ValueWriter(cx, args.get(1)).toBSON();

    bool justOne = false;
    if (args.length() > 2) {
        justOne = args.get(2).toBoolean();
    }

    conn->remove(ns, bson, justOne);
    args.rval().setUndefined();
}

void MongoBase::Functions::update(JSContext* cx, JS::CallArgs args) {
    if (args.length() < 3)
        uasserted(ErrorCodes::BadValue, "update needs at least 3 args");

    if (!args.get(1).isObject())
        uasserted(ErrorCodes::BadValue, "1st param to update has to be an object");

    if (!args.get(2).isObject())
        uasserted(ErrorCodes::BadValue, "2nd param to update has to be an object");

    ObjectWrapper o(cx, args.thisv());

    if (o.hasField("readOnly") && o.getBoolean("readOnly"))
        uasserted(ErrorCodes::BadValue, "js db in read only mode");

    auto conn = getConnection(args);
    std::string ns = ValueWriter(cx, args.get(0)).toString();

    BSONObj q1 = ValueWriter(cx, args.get(1)).toBSON();
    BSONObj o1 = ValueWriter(cx, args.get(2)).toBSON();

    bool upsert = args.length() > 3 && args.get(3).isBoolean() && args.get(3).toBoolean();
    bool multi = args.length() > 4 && args.get(4).isBoolean() && args.get(4).toBoolean();

    conn->update(ns, q1, o1, upsert, multi);
    args.rval().setUndefined();
}

void MongoBase::Functions::auth(JSContext* cx, JS::CallArgs args) {
    auto conn = getConnection(args);
    if (!conn)
        uasserted(ErrorCodes::BadValue, "no connection");

    BSONObj params;
    switch (args.length()) {
        case 1:
            params = ValueWriter(cx, args.get(0)).toBSON();
            break;
        case 3:
            params = BSON(saslCommandMechanismFieldName
                          << "MONGODB-CR" << saslCommandUserDBFieldName
                          << ValueWriter(cx, args[0]).toString() << saslCommandUserFieldName
                          << ValueWriter(cx, args[1]).toString() << saslCommandPasswordFieldName
                          << ValueWriter(cx, args[2]).toString());
            break;
        default:
            uasserted(ErrorCodes::BadValue, "mongoAuth takes 1 object or 3 string arguments");
    }

    conn->auth(params);

    args.rval().setBoolean(true);
}

void MongoBase::Functions::logout(JSContext* cx, JS::CallArgs args) {
    if (args.length() != 1)
        uasserted(ErrorCodes::BadValue, "logout needs 1 arg");

    BSONObj ret;

    std::string db = ValueWriter(cx, args.get(0)).toString();

    auto conn = getConnection(args);
    if (conn) {
        conn->logout(db, ret);
    }

    ValueReader(cx, args.rval()).fromBSON(ret, false);
}

void MongoBase::Functions::cursorFromId(JSContext* cx, JS::CallArgs args) {
    auto scope = getScope(cx);

    if (!(args.length() == 2 || args.length() == 3))
        uasserted(ErrorCodes::BadValue, "cursorFromId needs 2 or 3 args");

    if (!scope->getNumberLongProto().instanceOf(args.get(1)))
        uasserted(ErrorCodes::BadValue, "2nd arg must be a NumberLong");

    if (!(args.get(2).isNumber() || args.get(2).isUndefined()))
        uasserted(ErrorCodes::BadValue, "3rd arg must be a js Number");

    auto conn = getConnection(args);

    std::string ns = ValueWriter(cx, args.get(0)).toString();

    long long cursorId = NumberLongInfo::ToNumberLong(cx, args.get(1));

    auto cursor = stdx::make_unique<DBClientCursor>(conn, ns, cursorId, 0, 0);

    if (args.get(2).isNumber())
        cursor->setBatchSize(ValueWriter(cx, args.get(2)).toInt32());

    JS::RootedObject c(cx);
    scope->getCursorProto().newInstance(&c);

    setCursor(c, std::move(cursor), args);

    args.rval().setObjectOrNull(c);
}

void MongoBase::Functions::cursorHandleFromId(JSContext* cx, JS::CallArgs args) {
    auto scope = getScope(cx);

    if (args.length() != 1) {
        uasserted(ErrorCodes::BadValue, "cursorHandleFromId needs 1 arg");
    }
    if (!scope->getNumberLongProto().instanceOf(args.get(0))) {
        uasserted(ErrorCodes::BadValue, "1st arg must be a NumberLong");
    }

    long long cursorId = NumberLongInfo::ToNumberLong(cx, args.get(0));

    JS::RootedObject c(cx);
    scope->getCursorHandleProto().newInstance(&c);

    setCursorHandle(c, cursorId, args);

    args.rval().setObjectOrNull(c);
}

void MongoBase::Functions::copyDatabaseWithSCRAM(JSContext* cx, JS::CallArgs args) {
    auto conn = getConnection(args);

    if (!conn)
        uasserted(ErrorCodes::BadValue, "no connection");

    if (args.length() != 5)
        uasserted(ErrorCodes::BadValue, "copyDatabase needs 5 arg");

    // copyDatabase(fromdb, todb, fromhost, username, password);
    std::string fromDb = ValueWriter(cx, args.get(0)).toString();
    std::string toDb = ValueWriter(cx, args.get(1)).toString();
    std::string fromHost = ValueWriter(cx, args.get(2)).toString();
    std::string user = ValueWriter(cx, args.get(3)).toString();
    std::string password = ValueWriter(cx, args.get(4)).toString();

    std::string hashedPwd = DBClientWithCommands::createPasswordDigest(user, password);

    std::unique_ptr<SaslClientSession> session(new NativeSaslClientSession());

    session->setParameter(SaslClientSession::parameterMechanism, "SCRAM-SHA-1");
    session->setParameter(SaslClientSession::parameterUser, user);
    session->setParameter(SaslClientSession::parameterPassword, hashedPwd);
    session->initialize();

    BSONObj saslFirstCommandPrefix =
        BSON("copydbsaslstart" << 1 << "fromhost" << fromHost << "fromdb" << fromDb
                               << saslCommandMechanismFieldName << "SCRAM-SHA-1");

    BSONObj saslFollowupCommandPrefix =
        BSON("copydb" << 1 << "fromhost" << fromHost << "fromdb" << fromDb << "todb" << toDb);

    BSONObj saslCommandPrefix = saslFirstCommandPrefix;
    BSONObj inputObj = BSON(saslCommandPayloadFieldName << "");
    bool isServerDone = false;

    while (!session->isDone()) {
        std::string payload;
        BSONType type;

        Status status = saslExtractPayload(inputObj, &payload, &type);
        uassertStatusOK(status);

        std::string responsePayload;
        status = session->step(payload, &responsePayload);
        uassertStatusOK(status);

        BSONObjBuilder commandBuilder;

        commandBuilder.appendElements(saslCommandPrefix);
        commandBuilder.appendBinData(saslCommandPayloadFieldName,
                                     static_cast<int>(responsePayload.size()),
                                     BinDataGeneral,
                                     responsePayload.c_str());
        BSONElement conversationId = inputObj[saslCommandConversationIdFieldName];
        if (!conversationId.eoo())
            commandBuilder.append(conversationId);

        BSONObj command = commandBuilder.obj();

        bool ok = conn->runCommand("admin", command, inputObj);

        ErrorCodes::Error code =
            ErrorCodes::fromInt(inputObj[saslCommandCodeFieldName].numberInt());

        if (!ok || code != ErrorCodes::OK) {
            if (code == ErrorCodes::OK)
                code = ErrorCodes::UnknownError;

            ValueReader(cx, args.rval()).fromBSON(inputObj, true);
            return;
        }

        isServerDone = inputObj[saslCommandDoneFieldName].trueValue();
        saslCommandPrefix = saslFollowupCommandPrefix;
    }

    if (!isServerDone) {
        uasserted(ErrorCodes::InternalError, "copydb client finished before server.");
    }

    ValueReader(cx, args.rval()).fromBSON(inputObj, true);
}

void MongoBase::Functions::getClientRPCProtocols(JSContext* cx, JS::CallArgs args) {
    auto conn = getConnection(args);

    if (args.length() != 0)
        uasserted(ErrorCodes::BadValue, "getClientRPCProtocols takes no args");

    auto clientRPCProtocols = rpc::toString(conn->getClientRPCProtocols());
    uassertStatusOK(clientRPCProtocols);

    auto protoStr = clientRPCProtocols.getValue().toString();

    ValueReader(cx, args.rval()).fromStringData(protoStr);
}

void MongoBase::Functions::setClientRPCProtocols(JSContext* cx, JS::CallArgs args) {
    auto conn = getConnection(args);

    if (args.length() != 1)
        uasserted(ErrorCodes::BadValue, "setClientRPCProtocols needs 1 arg");
    if (!args.get(0).isString())
        uasserted(ErrorCodes::BadValue, "first argument to setClientRPCProtocols must be a string");

    std::string rpcProtosStr = ValueWriter(cx, args.get(0)).toString();

    auto clientRPCProtocols = rpc::parseProtocolSet(rpcProtosStr);
    uassertStatusOK(clientRPCProtocols);

    conn->setClientRPCProtocols(clientRPCProtocols.getValue());

    args.rval().setUndefined();
}

void MongoBase::Functions::getServerRPCProtocols(JSContext* cx, JS::CallArgs args) {
    auto conn = getConnection(args);

    if (args.length() != 0)
        uasserted(ErrorCodes::BadValue, "getServerRPCProtocols takes no args");

    auto serverRPCProtocols = rpc::toString(conn->getServerRPCProtocols());
    uassertStatusOK(serverRPCProtocols);

    auto protoStr = serverRPCProtocols.getValue().toString();

    ValueReader(cx, args.rval()).fromStringData(protoStr);
}

void MongoLocalInfo::construct(JSContext* cx, JS::CallArgs args) {
    auto scope = getScope(cx);

    if (args.length() != 0)
        uasserted(ErrorCodes::BadValue, "local Mongo constructor takes no args");

    std::unique_ptr<DBClientBase> conn;

    conn.reset(createDirectClient(scope->getOpContext()));

    JS::RootedObject thisv(cx);
    scope->getMongoLocalProto().newObject(&thisv);
    ObjectWrapper o(cx, thisv);

    JS_SetPrivate(thisv, new std::shared_ptr<DBClientBase>(conn.release()));

    o.setBoolean("slaveOk", false);
    o.setString("host", "EMBEDDED");

    args.rval().setObjectOrNull(thisv);
}

void MongoExternalInfo::construct(JSContext* cx, JS::CallArgs args) {
    auto scope = getScope(cx);

    std::string host("127.0.0.1");

    if (args.length() > 0 && args.get(0).isString()) {
        host = ValueWriter(cx, args.get(0)).toString();
    }

    auto statusWithHost = ConnectionString::parse(host);
    uassertStatusOK(statusWithHost);

    const ConnectionString cs(statusWithHost.getValue());

    std::string errmsg;
    std::unique_ptr<DBClientBase> conn(cs.connect(errmsg));

    if (!conn.get()) {
        uasserted(ErrorCodes::InternalError, errmsg);
    }

    JS::RootedObject thisv(cx);
    scope->getMongoExternalProto().newObject(&thisv);
    ObjectWrapper o(cx, thisv);

    JS_SetPrivate(thisv, new std::shared_ptr<DBClientBase>(conn.release()));

    o.setBoolean("slaveOk", false);
    o.setString("host", host);

    args.rval().setObjectOrNull(thisv);
}

void MongoExternalInfo::Functions::load(JSContext* cx, JS::CallArgs args) {
    auto scope = getScope(cx);

    for (unsigned i = 0; i < args.length(); ++i) {
        std::string filename = ValueWriter(cx, args.get(i)).toString();

        if (!scope->execFile(filename, false, true)) {
            uasserted(ErrorCodes::BadValue, std::string("error loading js file: ") + filename);
        }
    }

    args.rval().setBoolean(true);
}

void MongoExternalInfo::Functions::quit(JSContext* cx, JS::CallArgs args) {
    auto scope = getScope(cx);

    scope->setQuickExit(args.get(0).isNumber() ? args.get(0).toNumber() : 0);

    uasserted(ErrorCodes::JSUncatchableError, "Calling Quit");
}

}  // namespace mozjs
}  // namespace mongo
