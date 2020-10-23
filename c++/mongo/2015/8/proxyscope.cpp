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

#include "mongo/scripting/mozjs/proxyscope.h"

#include "mongo/db/client.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/platform/decimal128.h"
#include "mongo/scripting/mozjs/implscope.h"
#include "mongo/util/quick_exit.h"

namespace mongo {
namespace mozjs {

MozJSProxyScope::MozJSProxyScope(MozJSScriptEngine* engine)
    : _engine(engine),
      _implScope(nullptr),
      _mutex(),
      _state(State::Idle),
      _status(Status::OK()),
      _condvar(),
      _thread(&MozJSProxyScope::implThread, this) {
    // Test the child on startup to make sure it's awake and that the
    // implementation scope sucessfully constructed.
    try {
        runOnImplThread([] {});
    } catch (...) {
        shutdownThread();
        throw;
    }
}

MozJSProxyScope::~MozJSProxyScope() {
    DESTRUCTOR_GUARD(kill(); shutdownThread(););
}

void MozJSProxyScope::init(const BSONObj* data) {
    runOnImplThread([&] { _implScope->init(data); });
}

void MozJSProxyScope::reset() {
    runOnImplThread([&] { _implScope->reset(); });
}

bool MozJSProxyScope::isKillPending() const {
    return _implScope->isKillPending();
}

void MozJSProxyScope::registerOperation(OperationContext* txn) {
    runOnImplThread([&] { _implScope->registerOperation(txn); });
}

void MozJSProxyScope::unregisterOperation() {
    runOnImplThread([&] { _implScope->unregisterOperation(); });
}

void MozJSProxyScope::localConnectForDbEval(OperationContext* txn, const char* dbName) {
    runOnImplThread([&] { _implScope->localConnectForDbEval(txn, dbName); });
}

void MozJSProxyScope::externalSetup() {
    runOnImplThread([&] { _implScope->externalSetup(); });
}

std::string MozJSProxyScope::getError() {
    std::string out;
    runOnImplThread([&] { out = _implScope->getError(); });
    return out;
}

/**
 * This is an artifact of how out of memory errors were communicated in V8.  We
 * just throw out of memory errors from spidermonkey when we get them, rather
 * than setting a flag and having to pick them up here.
 */
bool MozJSProxyScope::hasOutOfMemoryException() {
    return false;
}

void MozJSProxyScope::gc() {
    _implScope->gc();
}

double MozJSProxyScope::getNumber(const char* field) {
    double out;
    runOnImplThread([&] { out = _implScope->getNumber(field); });
    return out;
}

int MozJSProxyScope::getNumberInt(const char* field) {
    int out;
    runOnImplThread([&] { out = _implScope->getNumberInt(field); });
    return out;
}

long long MozJSProxyScope::getNumberLongLong(const char* field) {
    long long out;
    runOnImplThread([&] { out = _implScope->getNumberLongLong(field); });
    return out;
}

Decimal128 MozJSProxyScope::getNumberDecimal(const char* field) {
    Decimal128 out;
    runOnImplThread([&] { out = _implScope->getNumberDecimal(field); });
    return out;
}

std::string MozJSProxyScope::getString(const char* field) {
    std::string out;
    runOnImplThread([&] { out = _implScope->getString(field); });
    return out;
}

bool MozJSProxyScope::getBoolean(const char* field) {
    bool out;
    runOnImplThread([&] { out = _implScope->getBoolean(field); });
    return out;
}

BSONObj MozJSProxyScope::getObject(const char* field) {
    BSONObj out;
    runOnImplThread([&] { out = _implScope->getObject(field); });
    return out;
}

void MozJSProxyScope::setNumber(const char* field, double val) {
    runOnImplThread([&] { _implScope->setNumber(field, val); });
}

void MozJSProxyScope::setString(const char* field, StringData val) {
    runOnImplThread([&] { _implScope->setString(field, val); });
}

void MozJSProxyScope::setBoolean(const char* field, bool val) {
    runOnImplThread([&] { _implScope->setBoolean(field, val); });
}

void MozJSProxyScope::setElement(const char* field, const BSONElement& e) {
    runOnImplThread([&] { _implScope->setElement(field, e); });
}

void MozJSProxyScope::setObject(const char* field, const BSONObj& obj, bool readOnly) {
    runOnImplThread([&] { _implScope->setObject(field, obj, readOnly); });
}

void MozJSProxyScope::setFunction(const char* field, const char* code) {
    runOnImplThread([&] { _implScope->setFunction(field, code); });
}

int MozJSProxyScope::type(const char* field) {
    int out;
    runOnImplThread([&] { out = _implScope->type(field); });
    return out;
}

void MozJSProxyScope::rename(const char* from, const char* to) {
    runOnImplThread([&] { _implScope->rename(from, to); });
}

int MozJSProxyScope::invoke(ScriptingFunction func,
                            const BSONObj* argsObject,
                            const BSONObj* recv,
                            int timeoutMs,
                            bool ignoreReturn,
                            bool readOnlyArgs,
                            bool readOnlyRecv) {
    int out;
    runOnImplThread([&] {
        out = _implScope->invoke(
            func, argsObject, recv, timeoutMs, ignoreReturn, readOnlyArgs, readOnlyRecv);
    });

    return out;
}

bool MozJSProxyScope::exec(StringData code,
                           const std::string& name,
                           bool printResult,
                           bool reportError,
                           bool assertOnError,
                           int timeoutMs) {
    bool out;
    runOnImplThread([&] {
        out = _implScope->exec(code, name, printResult, reportError, assertOnError, timeoutMs);
    });
    return out;
}

void MozJSProxyScope::injectNative(const char* field, NativeFunction func, void* data) {
    runOnImplThread([&] { _implScope->injectNative(field, func, data); });
}

ScriptingFunction MozJSProxyScope::_createFunction(const char* raw,
                                                   ScriptingFunction functionNumber) {
    ScriptingFunction out;
    runOnImplThread([&] { out = _implScope->_createFunction(raw, functionNumber); });
    return out;
}

OperationContext* MozJSProxyScope::getOpContext() const {
    return _implScope->getOpContext();
}

void MozJSProxyScope::kill() {
    _implScope->kill();
}

/**
 * Invokes a function on the implementation thread
 *
 * It does this by serializing the invocation through a stdx::function and
 * capturing any exceptions through _status.
 *
 * We transition:
 *
 * Idle -> ProxyRequest -> ImplResponse -> Idle
 */
void MozJSProxyScope::runOnImplThread(std::function<void()> f) {
    // We can end up calling functions on the proxy scope from the impl thread
    // when callbacks from javascript have a handle to the proxy scope and call
    // methods on it from there. If we're on the same thread, it's safe to
    // simply call back in, so let's do that.

    if (_thread.get_id() == std::this_thread::get_id()) {
        return f();
    }

    stdx::unique_lock<stdx::mutex> lk(_mutex);
    _function = std::move(f);

    invariant(_state == State::Idle);
    _state = State::ProxyRequest;

    _condvar.notify_one();

    _condvar.wait(lk, [this] { return _state == State::ImplResponse; });

    _state = State::Idle;

    // Clear the _status state and throw it if necessary
    auto status = std::move(_status);
    uassertStatusOK(status);
}

void MozJSProxyScope::shutdownThread() {
    {
        stdx::lock_guard<stdx::mutex> lk(_mutex);

        invariant(_state == State::Idle);

        _state = State::Shutdown;
    }

    _condvar.notify_one();

    _thread.join();
}

/**
 * The main loop for the implementation thread
 *
 * This owns the actual implementation scope (which needs to be created on this
 * child thread) and has essentially two transition paths:
 *
 * Standard: ProxyRequest -> ImplResponse
 *   Invoke _function. Serialize exceptions to _status.
 *
 * Shutdown: Shutdown -> _
 *   break out of the loop and return.
 */
void MozJSProxyScope::implThread() {
    if (hasGlobalServiceContext())
        Client::initThread("js");

    std::unique_ptr<MozJSImplScope> scope;

    // This will leave _status set for the first noop runOnImplThread(), which
    // captures the startup exception that way
    try {
        scope.reset(new MozJSImplScope(_engine));
        _implScope = scope.get();
    } catch (...) {
        _status = exceptionToStatus();
    }

    while (true) {
        stdx::unique_lock<stdx::mutex> lk(_mutex);
        _condvar.wait(
            lk, [this] { return _state == State::ProxyRequest || _state == State::Shutdown; });

        if (_state == State::Shutdown)
            break;

        try {
            _function();
        } catch (...) {
            _status = exceptionToStatus();
        }

        int exitCode;
        if (_implScope && _implScope->getQuickExit(&exitCode)) {
            scope.reset();
            quickExit(exitCode);
        }

        _state = State::ImplResponse;

        _condvar.notify_one();
    }
}

}  // namespace mozjs
}  // namespace mongo
