/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#include "mongo/platform/basic.h"

#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/pipeline/expression_javascript.h"
#include "mongo/db/pipeline/make_js_function.h"
#include "mongo/db/query/query_knobs_gen.h"

namespace mongo {

REGISTER_EXPRESSION(_internalJsEmit, ExpressionInternalJsEmit::parse);

REGISTER_EXPRESSION(_internalJs, ExpressionInternalJs::parse);
namespace {

/**
 * This function is called from the JavaScript function provided to the expression.
 */
BSONObj emitFromJS(const BSONObj& args, void* data) {
    uassert(31220, "emit takes 2 args", args.nFields() == 2);
    auto emitState = static_cast<ExpressionInternalJsEmit::EmitState*>(data);
    if (args.firstElement().type() == Undefined) {
        emitState->emit(DOC("k" << BSONNULL << "v" << args["1"]));
    } else {
        emitState->emit(DOC("k" << args["0"] << "v" << args["1"]));
    }
    return BSONObj();
}
}  // namespace

ExpressionInternalJsEmit::ExpressionInternalJsEmit(
    const boost::intrusive_ptr<ExpressionContext>& expCtx,
    boost::intrusive_ptr<Expression> thisRef,
    std::string funcSource)
    : Expression(expCtx, {std::move(thisRef)}),
      _emitState{{}, internalQueryMaxJsEmitBytes.load(), 0},
      _thisRef(_children[0]),
      _funcSource(std::move(funcSource)) {}

void ExpressionInternalJsEmit::_doAddDependencies(mongo::DepsTracker* deps) const {
    _children[0]->addDependencies(deps);
}

boost::intrusive_ptr<Expression> ExpressionInternalJsEmit::parse(
    const boost::intrusive_ptr<ExpressionContext>& expCtx,
    BSONElement expr,
    const VariablesParseState& vps) {

    uassert(31221,
            str::stream() << kExpressionName
                          << " requires an object as an argument, found: " << typeName(expr.type()),
            expr.type() == BSONType::Object);

    BSONElement evalField = expr["eval"];

    uassert(31222, str::stream() << "The map function must be specified.", evalField);
    uassert(31224,
            "The map function must be of type string or code",
            evalField.type() == BSONType::String || evalField.type() == BSONType::Code);

    std::string funcSourceString = evalField._asCode();
    BSONElement thisField = expr["this"];
    uassert(
        31223, str::stream() << kExpressionName << " requires 'this' to be specified", thisField);
    boost::intrusive_ptr<Expression> thisRef = parseOperand(expCtx, thisField, vps);

    return new ExpressionInternalJsEmit(expCtx, std::move(thisRef), std::move(funcSourceString));
}

Value ExpressionInternalJsEmit::serialize(bool explain) const {
    return Value(
        Document{{kExpressionName,
                  Document{{"eval", _funcSource}, {"this", _thisRef->serialize(explain)}}}});
}

Value ExpressionInternalJsEmit::evaluate(const Document& root, Variables* variables) const {
    Value thisVal = _thisRef->evaluate(root, variables);
    uassert(31225, "'this' must be an object.", thisVal.getType() == BSONType::Object);

    // If the scope does not exist and is created by the following call, then make sure to
    // re-bind emit() and the given function to the new scope.
    auto expCtx = getExpressionContext();

    auto jsExec = expCtx->getJsExecWithScope();
    // Inject the native "emit" function to be called from the user-defined map function. This
    // particular Expression/ExpressionContext may be reattached to a new OperationContext (and thus
    // a new JS Scope) when used across getMore operations, so this method will handle that case for
    // us by only injecting if we haven't already.
    jsExec->injectEmitIfNecessary(emitFromJS, &_emitState);

    // Although inefficient to "create" a new function every time we evaluate, this will usually end
    // up being a simple cache lookup. This is needed because the JS Scope may have been recreated
    // on a new thread if the expression is evaluated across getMores.
    auto func = makeJsFunc(expCtx, _funcSource.c_str());

    BSONObj thisBSON = thisVal.getDocument().toBson();
    BSONObj params;
    jsExec->callFunctionWithoutReturn(func, params, thisBSON);

    auto returnValue = Value(std::move(_emitState.emittedObjects));
    _emitState.reset();
    return returnValue;
}

ExpressionInternalJs::ExpressionInternalJs(const boost::intrusive_ptr<ExpressionContext>& expCtx,
                                           boost::intrusive_ptr<Expression> passedArgs,
                                           std::string funcSource)
    : Expression(expCtx, {std::move(passedArgs)}),
      _passedArgs(_children[0]),
      _funcSource(std::move(funcSource)) {}

Value ExpressionInternalJs::serialize(bool explain) const {
    return Value(
        Document{{kExpressionName,
                  Document{{"eval", _funcSource}, {"args", _passedArgs->serialize(explain)}}}});
}

void ExpressionInternalJs::_doAddDependencies(mongo::DepsTracker* deps) const {
    _children[0]->addDependencies(deps);
}

boost::intrusive_ptr<Expression> ExpressionInternalJs::parse(
    const boost::intrusive_ptr<ExpressionContext>& expCtx,
    BSONElement expr,
    const VariablesParseState& vps) {

    uassert(31260,
            str::stream() << kExpressionName
                          << " requires an object as an argument, found: " << expr.type(),
            expr.type() == BSONType::Object);

    BSONElement evalField = expr["eval"];

    uassert(31261, "The eval function must be specified.", evalField);
    uassert(31262,
            "The eval function must be of type string or code",
            evalField.type() == BSONType::String || evalField.type() == BSONType::Code);

    BSONElement argsField = expr["args"];
    uassert(31263, "The args field must be specified.", argsField);
    boost::intrusive_ptr<Expression> argsExpr = parseOperand(expCtx, argsField, vps);

    return new ExpressionInternalJs(expCtx, argsExpr, evalField._asCode());
}

Value ExpressionInternalJs::evaluate(const Document& root, Variables* variables) const {
    auto& expCtx = getExpressionContext();

    auto jsExec = expCtx->getJsExecWithScope();

    ScriptingFunction func = jsExec->getScope()->createFunction(_funcSource.c_str());
    uassert(31265, "The eval function did not evaluate", func);

    auto argExpressions = _passedArgs->evaluate(root, variables);
    uassert(
        31266, "The args field must be of type array", argExpressions.getType() == BSONType::Array);

    int argNum = 0;
    BSONObjBuilder bob;
    for (const auto& arg : argExpressions.getArray()) {
        arg.addToBsonObj(&bob, "arg" + std::to_string(argNum++));
    }
    return jsExec->callFunction(func, bob.done(), {});
}
}  // namespace mongo
