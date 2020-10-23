/**
 *    Copyright (C) 2009-2016 MongoDB Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kCommand

#include "mongo/platform/basic.h"

#include "mongo/db/commands.h"

#include <string>
#include <vector>

#include "mongo/bson/mutable/document.h"
#include "mongo/bson/timestamp.h"
#include "mongo/db/audit.h"
#include "mongo/db/auth/action_set.h"
#include "mongo/db/auth/action_type.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/auth/privilege.h"
#include "mongo/db/client.h"
#include "mongo/db/command_generic_argument.h"
#include "mongo/db/curop.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/server_parameters.h"
#include "mongo/rpc/write_concern_error_detail.h"
#include "mongo/s/stale_exception.h"
#include "mongo/util/invariant.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

using logger::LogComponent;

namespace {

const char kWriteConcernField[] = "writeConcern";
const WriteConcernOptions kMajorityWriteConcern(
    WriteConcernOptions::kMajority,
    // Note: Even though we're setting UNSET here, kMajority implies JOURNAL if journaling is
    // supported by the mongod.
    WriteConcernOptions::SyncMode::UNSET,
    Seconds(60));

}  // namespace


//////////////////////////////////////////////////////////////
// CommandHelpers

BSONObj CommandHelpers::runCommandDirectly(OperationContext* opCtx, const OpMsgRequest& request) {
    auto command = globalCommandRegistry()->findCommand(request.getCommandName());
    invariant(command);
    BufBuilder bb;
    CommandReplyBuilder crb(BSONObjBuilder{bb});
    try {
        auto invocation = command->parse(opCtx, request);
        invocation->run(opCtx, &crb);
        auto body = crb.getBodyBuilder();
        CommandHelpers::extractOrAppendOk(body);
    } catch (const StaleConfigException&) {
        // These exceptions are intended to be handled at a higher level.
        throw;
    } catch (const DBException& ex) {
        auto body = crb.getBodyBuilder();
        body.resetToEmpty();
        appendCommandStatus(body, ex.toStatus());
    }
    return BSONObj(bb.release());
}

void CommandHelpers::logAuthViolation(OperationContext* opCtx,
                                      const Command* command,
                                      const OpMsgRequest& request,
                                      ErrorCodes::Error err) {
    struct Hook final : public audit::CommandInterface {
    public:
        Hook(const Command* command, const OpMsgRequest& request)
            : _command{command}, _request{request} {}

        void redactForLogging(mutablebson::Document* cmdObj) const override {
            _command->redactForLogging(cmdObj);
        }

        NamespaceString ns() const override {
            return NamespaceString(
                _command->parseNs(_request.getDatabase().toString(), _request.body));
        }

    private:
        const Command* _command;
        const OpMsgRequest& _request;
    };
    audit::logCommandAuthzCheck(opCtx->getClient(), request, Hook(command, request), err);
}

void CommandHelpers::uassertNoDocumentSequences(StringData commandName,
                                                const OpMsgRequest& request) {
    uassert(40472,
            str::stream() << "The " << commandName
                          << " command does not support document sequences.",
            request.sequences.empty());
}

std::string CommandHelpers::parseNsFullyQualified(const BSONObj& cmdObj) {
    BSONElement first = cmdObj.firstElement();
    uassert(ErrorCodes::BadValue,
            str::stream() << "collection name has invalid type " << typeName(first.type()),
            first.canonicalType() == canonicalizeBSONType(mongo::String));
    const NamespaceString nss(first.valueStringData());
    uassert(ErrorCodes::InvalidNamespace,
            str::stream() << "Invalid namespace specified '" << nss.ns() << "'",
            nss.isValid());
    return nss.ns();
}

NamespaceString CommandHelpers::parseNsCollectionRequired(StringData dbname,
                                                          const BSONObj& cmdObj) {
    // Accepts both BSON String and Symbol for collection name per SERVER-16260
    // TODO(kangas) remove Symbol support in MongoDB 3.0 after Ruby driver audit
    BSONElement first = cmdObj.firstElement();
    uassert(ErrorCodes::InvalidNamespace,
            str::stream() << "collection name has invalid type " << typeName(first.type()),
            first.canonicalType() == canonicalizeBSONType(mongo::String));
    const NamespaceString nss(dbname, first.valueStringData());
    uassert(ErrorCodes::InvalidNamespace,
            str::stream() << "Invalid namespace specified '" << nss.ns() << "'",
            nss.isValid());
    return nss;
}

NamespaceStringOrUUID CommandHelpers::parseNsOrUUID(StringData dbname, const BSONObj& cmdObj) {
    BSONElement first = cmdObj.firstElement();
    if (first.type() == BinData && first.binDataType() == BinDataType::newUUID) {
        return {dbname.toString(), uassertStatusOK(UUID::parse(first))};
    } else {
        // Ensure collection identifier is not a Command
        const NamespaceString nss(parseNsCollectionRequired(dbname, cmdObj));
        uassert(ErrorCodes::InvalidNamespace,
                str::stream() << "Invalid collection name specified '" << nss.ns() << "'",
                nss.isNormal());
        return nss;
    }
}

Command* CommandHelpers::findCommand(StringData name) {
    return globalCommandRegistry()->findCommand(name);
}

bool CommandHelpers::appendCommandStatus(BSONObjBuilder& result, const Status& status) {
    appendCommandStatus(result, status.isOK(), status.reason());
    BSONObj tmp = result.asTempObj();
    if (!status.isOK() && !tmp.hasField("code")) {
        result.append("code", status.code());
        result.append("codeName", ErrorCodes::errorString(status.code()));
    }
    if (auto extraInfo = status.extraInfo()) {
        extraInfo->serialize(&result);
    }
    return status.isOK();
}

void CommandHelpers::appendCommandStatus(BSONObjBuilder& result,
                                         bool ok,
                                         const std::string& errmsg) {
    BSONObj tmp = result.asTempObj();
    bool have_ok = tmp.hasField("ok");
    bool need_errmsg = !ok && !tmp.hasField("errmsg");

    if (!have_ok)
        result.append("ok", ok ? 1.0 : 0.0);

    if (need_errmsg) {
        result.append("errmsg", errmsg);
    }
}

bool CommandHelpers::extractOrAppendOk(BSONObjBuilder& reply) {
    if (auto okField = reply.asTempObj()["ok"]) {
        // If ok is present, use its truthiness.
        return okField.trueValue();
    }
    // Missing "ok" field is an implied success.
    CommandHelpers::appendCommandStatus(reply, true);
    return true;
}

void CommandHelpers::appendCommandWCStatus(BSONObjBuilder& result,
                                           const Status& awaitReplicationStatus,
                                           const WriteConcernResult& wcResult) {
    if (!awaitReplicationStatus.isOK() && !result.hasField("writeConcernError")) {
        WriteConcernErrorDetail wcError;
        wcError.setStatus(awaitReplicationStatus);
        if (wcResult.wTimedOut) {
            wcError.setErrInfo(BSON("wtimeout" << true));
        }
        result.append("writeConcernError", wcError.toBSON());
    }
}

BSONObj CommandHelpers::appendPassthroughFields(const BSONObj& cmdObjWithPassthroughFields,
                                                const BSONObj& request) {
    BSONObjBuilder b;
    b.appendElements(request);
    for (const auto& elem : filterCommandRequestForPassthrough(cmdObjWithPassthroughFields)) {
        const auto name = elem.fieldNameStringData();
        if (isGenericArgument(name) && !request.hasField(name)) {
            b.append(elem);
        }
    }
    return b.obj();
}

BSONObj CommandHelpers::appendMajorityWriteConcern(const BSONObj& cmdObj) {
    WriteConcernOptions newWC = kMajorityWriteConcern;

    if (cmdObj.hasField(kWriteConcernField)) {
        auto wc = cmdObj.getField(kWriteConcernField);
        // The command has a writeConcern field and it's majority, so we can
        // return it as-is.
        if (wc["w"].ok() && wc["w"].str() == "majority") {
            return cmdObj;
        }

        if (wc["wtimeout"].ok()) {
            // They set a timeout, but aren't using majority WC. We want to use their
            // timeout along with majority WC.
            newWC = WriteConcernOptions(WriteConcernOptions::kMajority,
                                        WriteConcernOptions::SyncMode::UNSET,
                                        wc["wtimeout"].Number());
        }
    }

    // Append all original fields except the writeConcern field to the new command.
    BSONObjBuilder cmdObjWithWriteConcern;
    for (const auto& elem : cmdObj) {
        const auto name = elem.fieldNameStringData();
        if (name != "writeConcern" && !cmdObjWithWriteConcern.hasField(name)) {
            cmdObjWithWriteConcern.append(elem);
        }
    }

    // Finally, add the new write concern.
    cmdObjWithWriteConcern.append(kWriteConcernField, newWC.toBSON());
    return cmdObjWithWriteConcern.obj();
}

BSONObj CommandHelpers::filterCommandRequestForPassthrough(const BSONObj& cmdObj) {
    BSONObjIterator cmdIter(cmdObj);
    BSONObjBuilder bob;
    filterCommandRequestForPassthrough(&cmdIter, &bob);
    return bob.obj();
}

void CommandHelpers::filterCommandRequestForPassthrough(BSONObjIterator* cmdIter,
                                                        BSONObjBuilder* requestBuilder) {
    while (cmdIter->more()) {
        auto elem = cmdIter->next();
        const auto name = elem.fieldNameStringData();
        if (name == "$readPreference") {
            BSONObjBuilder(requestBuilder->subobjStart("$queryOptions")).append(elem);
            continue;
        }
        if (isRequestStripArgument(name))
            continue;
        requestBuilder->append(elem);
    }
}

void CommandHelpers::filterCommandReplyForPassthrough(const BSONObj& cmdObj,
                                                      BSONObjBuilder* output) {
    for (auto elem : cmdObj) {
        const auto name = elem.fieldNameStringData();
        if (isReplyStripArgument(name))
            continue;
        output->append(elem);
    }
}

BSONObj CommandHelpers::filterCommandReplyForPassthrough(const BSONObj& cmdObj) {
    BSONObjBuilder bob;
    filterCommandReplyForPassthrough(cmdObj, &bob);
    return bob.obj();
}

bool CommandHelpers::isHelpRequest(const BSONElement& helpElem) {
    return !helpElem.eoo() && helpElem.trueValue();
}

constexpr StringData CommandHelpers::kHelpFieldName;

//////////////////////////////////////////////////////////////
// CommandReplyBuilder

CommandReplyBuilder::CommandReplyBuilder(BSONObjBuilder bodyObj)
    : _bodyBuf(&bodyObj.bb()), _bodyOffset(bodyObj.offset()) {
    // CommandReplyBuilder requires that bodyObj build into an externally-owned buffer.
    invariant(!bodyObj.owned());
    bodyObj.doneFast();
}

BSONObjBuilder CommandReplyBuilder::getBodyBuilder() const {
    return BSONObjBuilder(BSONObjBuilder::ResumeBuildingTag{}, *_bodyBuf, _bodyOffset);
}

void CommandReplyBuilder::reset() {
    getBodyBuilder().resetToEmpty();
}

void CommandReplyBuilder::fillFrom(const Status& status) {
    if (!status.isOK()) {
        reset();
    }
    auto bob = getBodyBuilder();
    CommandHelpers::appendCommandStatus(bob, status);
}

//////////////////////////////////////////////////////////////
// CommandInvocation

CommandInvocation::~CommandInvocation() = default;

void CommandInvocation::checkAuthorization(OperationContext* opCtx,
                                           const OpMsgRequest& request) const {
    const Command* c = definition();
    Status status = _checkAuthorizationImpl(opCtx, request);
    if (!status.isOK()) {
        log(LogComponent::kAccessControl) << status;
    }
    CommandHelpers::logAuthViolation(opCtx, c, request, status.code());
    uassertStatusOK(status);
}

//////////////////////////////////////////////////////////////
// Command

class BasicCommand::Invocation final : public CommandInvocation {
public:
    Invocation(OperationContext*, const OpMsgRequest& request, BasicCommand* command)
        : CommandInvocation(command),
          _command(command),
          _request(&request),
          _dbName(_request->getDatabase().toString()) {}

private:
    void run(OperationContext* opCtx, CommandReplyBuilder* result) override {
        try {
            BSONObjBuilder bob = result->getBodyBuilder();
            bool ok = _command->run(opCtx, _dbName, _request->body, bob);
            CommandHelpers::appendCommandStatus(bob, ok);
        } catch (const ExceptionFor<ErrorCodes::Unauthorized>& e) {
            CommandHelpers::logAuthViolation(opCtx, _command, *_request, e.code());
            throw;
        }
    }

    void explain(OperationContext* opCtx,
                 ExplainOptions::Verbosity verbosity,
                 BSONObjBuilder* result) override {
        uassertStatusOK(_command->explain(opCtx, *_request, verbosity, result));
    }

    NamespaceString ns() const override {
        return NamespaceString(_command->parseNs(_dbName, cmdObj()));
    }

    bool supportsWriteConcern() const override {
        return _command->supportsWriteConcern(cmdObj());
    }

    bool supportsReadConcern(repl::ReadConcernLevel level) const override {
        return _command->supportsReadConcern(_dbName, cmdObj(), level);
    }

    bool allowsAfterClusterTime() const override {
        return _command->allowsAfterClusterTime(cmdObj());
    }

    void doCheckAuthorization(OperationContext* opCtx) const override {
        uassertStatusOK(_command->checkAuthForOperation(
            opCtx, _request->getDatabase().toString(), _request->body));
    }

    const BSONObj& cmdObj() const {
        return _request->body;
    }

    BasicCommand* const _command;
    const OpMsgRequest* const _request;
    const std::string _dbName;
};

Command::~Command() = default;

std::unique_ptr<CommandInvocation> BasicCommand::parse(OperationContext* opCtx,
                                                       const OpMsgRequest& request) {
    CommandHelpers::uassertNoDocumentSequences(getName(), request);
    return stdx::make_unique<Invocation>(opCtx, request, this);
}

std::string Command::parseNs(const std::string& dbname, const BSONObj& cmdObj) const {
    BSONElement first = cmdObj.firstElement();
    if (first.type() != mongo::String)
        return dbname;

    return str::stream() << dbname << '.' << cmdObj.firstElement().valueStringData();
}

ResourcePattern Command::parseResourcePattern(const std::string& dbname,
                                              const BSONObj& cmdObj) const {
    const std::string ns = parseNs(dbname, cmdObj);
    if (!NamespaceString::validCollectionComponent(ns)) {
        return ResourcePattern::forDatabaseName(ns);
    }
    return ResourcePattern::forExactNamespace(NamespaceString(ns));
}

Command::Command(StringData name, StringData oldName)
    : _name(name.toString()),
      _commandsExecutedMetric("commands." + _name + ".total", &_commandsExecuted),
      _commandsFailedMetric("commands." + _name + ".failed", &_commandsFailed) {
    globalCommandRegistry()->registerCommand(this, name, oldName);
}

Status BasicCommand::explain(OperationContext* opCtx,
                             const OpMsgRequest& request,
                             ExplainOptions::Verbosity verbosity,
                             BSONObjBuilder* out) const {
    return {ErrorCodes::IllegalOperation, str::stream() << "Cannot explain cmd: " << getName()};
}

Status BasicCommand::checkAuthForOperation(OperationContext* opCtx,
                                           const std::string& dbname,
                                           const BSONObj& cmdObj) const {
    return checkAuthForCommand(opCtx->getClient(), dbname, cmdObj);
}

Status BasicCommand::checkAuthForCommand(Client* client,
                                         const std::string& dbname,
                                         const BSONObj& cmdObj) const {
    std::vector<Privilege> privileges;
    this->addRequiredPrivileges(dbname, cmdObj, &privileges);
    if (AuthorizationSession::get(client)->isAuthorizedForPrivileges(privileges))
        return Status::OK();
    return Status(ErrorCodes::Unauthorized, "unauthorized");
}

Status CommandInvocation::_checkAuthorizationImpl(OperationContext* opCtx,
                                                  const OpMsgRequest& request) const {
    namespace mmb = mutablebson;
    const Command* c = definition();
    auto client = opCtx->getClient();
    auto dbname = request.getDatabase();
    if (c->adminOnly() && dbname != "admin") {
        return Status(ErrorCodes::Unauthorized,
                      str::stream() << c->getName()
                                    << " may only be run against the admin database.");
    }
    if (AuthorizationSession::get(client)->getAuthorizationManager().isAuthEnabled()) {
        Status status = [&] {
            try {
                doCheckAuthorization(opCtx);
                return Status::OK();
            } catch (const DBException& e) {
                return e.toStatus();
            }
        }();
        if (status == ErrorCodes::Unauthorized) {
            mmb::Document cmdToLog(request.body, mmb::Document::kInPlaceDisabled);
            c->redactForLogging(&cmdToLog);
            return Status(ErrorCodes::Unauthorized,
                          str::stream() << "not authorized on " << dbname << " to execute command "
                                        << redact(cmdToLog.getObject()));
        }
        if (!status.isOK()) {
            return status;
        }
    } else if (c->adminOnly() && c->localHostOnlyIfNoAuth() &&
               !client->getIsLocalHostConnection()) {
        return Status(ErrorCodes::Unauthorized,
                      str::stream() << c->getName()
                                    << " must run from localhost when running db without auth");
    }
    return Status::OK();
}

void Command::generateHelpResponse(OperationContext* opCtx,
                                   rpc::ReplyBuilderInterface* replyBuilder,
                                   const Command& command) {
    BSONObjBuilder helpBuilder;
    helpBuilder.append("help",
                       str::stream() << "help for: " << command.getName() << " " << command.help());
    replyBuilder->setCommandReply(helpBuilder.obj());
    replyBuilder->setMetadata(rpc::makeEmptyMetadata());
}

bool ErrmsgCommandDeprecated::run(OperationContext* opCtx,
                                  const std::string& db,
                                  const BSONObj& cmdObj,
                                  BSONObjBuilder& result) {
    std::string errmsg;
    auto ok = errmsgRun(opCtx, db, cmdObj, errmsg, result);
    if (!errmsg.empty()) {
        CommandHelpers::appendCommandStatus(result, ok, errmsg);
    }
    return ok;
}

//////////////////////////////////////////////////////////////
// CommandRegistry

void CommandRegistry::registerCommand(Command* command, StringData name, StringData oldName) {
    for (StringData key : {name, oldName}) {
        if (key.empty()) {
            continue;
        }
        auto hashedKey = CommandMap::HashedKey(key);
        auto iter = _commands.find(hashedKey);
        invariant(iter == _commands.end(), str::stream() << "command name collision: " << key);
        _commands[hashedKey] = command;
    }
}

Command* CommandRegistry::findCommand(StringData name) const {
    auto it = _commands.find(name);
    if (it == _commands.end())
        return nullptr;
    return it->second;
}

CommandRegistry* globalCommandRegistry() {
    static auto reg = new CommandRegistry();
    return reg;
}

}  // namespace mongo
