
#include <libsolidity/interface/InterfaceHandler.h>
#include <boost/range/irange.hpp>
#include <libsolidity/ast/AST.h>
#include <libsolidity/interface/CompilerStack.h>

using namespace std;
using namespace dev;
using namespace dev::solidity;

string InterfaceHandler::documentation(
	ContractDefinition const& _contractDef,
	DocumentationType _type
)
{
	switch(_type)
	{
	case DocumentationType::NatspecUser:
		return userDocumentation(_contractDef);
	case DocumentationType::NatspecDev:
		return devDocumentation(_contractDef);
	case DocumentationType::ABIInterface:
		return abiInterface(_contractDef);
	case DocumentationType::ABISolidityInterface:
		return ABISolidityInterface(_contractDef);
	}

	BOOST_THROW_EXCEPTION(InternalCompilerError() << errinfo_comment("Unknown documentation type"));
	return "";
}

string InterfaceHandler::abiInterface(ContractDefinition const& _contractDef)
{
	Json::Value abi(Json::arrayValue);

	auto populateParameters = [](vector<string> const& _paramNames, vector<string> const& _paramTypes)
	{
		Json::Value params(Json::arrayValue);
		solAssert(_paramNames.size() == _paramTypes.size(), "Names and types vector size does not match");
		for (unsigned i = 0; i < _paramNames.size(); ++i)
		{
			Json::Value param;
			param["name"] = _paramNames[i];
			param["type"] = _paramTypes[i];
			params.append(param);
		}
		return params;
	};

	for (auto it: _contractDef.interfaceFunctions())
	{
		auto externalFunctionType = it.second->interfaceFunctionType();
		Json::Value method;
		method["type"] = "function";
		method["name"] = it.second->declaration().name();
		method["constant"] = it.second->isConstant();
		method["inputs"] = populateParameters(
			externalFunctionType->parameterNames(),
			externalFunctionType->parameterTypeNames(_contractDef.isLibrary())
		);
		method["outputs"] = populateParameters(
			externalFunctionType->returnParameterNames(),
			externalFunctionType->returnParameterTypeNames(_contractDef.isLibrary())
		);
		abi.append(method);
	}
	if (_contractDef.constructor())
	{
		Json::Value method;
		method["type"] = "constructor";
		auto externalFunction = FunctionType(*_contractDef.constructor()).interfaceFunctionType();
		solAssert(!!externalFunction, "");
		method["inputs"] = populateParameters(
			externalFunction->parameterNames(),
			externalFunction->parameterTypeNames(_contractDef.isLibrary())
		);
		abi.append(method);
	}

	for (auto const& it: _contractDef.interfaceEvents())
	{
		Json::Value event;
		event["type"] = "event";
		event["name"] = it->name();
		event["anonymous"] = it->isAnonymous();
		Json::Value params(Json::arrayValue);
		for (auto const& p: it->parameters())
		{
			Json::Value input;
			input["name"] = p->name();
			input["type"] = p->annotation().type->canonicalName(false);
			input["indexed"] = p->isIndexed();
			params.append(input);
		}
		event["inputs"] = params;
		abi.append(event);
	}
	return Json::FastWriter().write(abi);
}

string InterfaceHandler::ABISolidityInterface(ContractDefinition const& _contractDef)
{
	string ret = (_contractDef.isLibrary() ? "library " : "contract ") + _contractDef.name() + "{";

	auto populateParameters = [](vector<string> const& _paramNames, vector<string> const& _paramTypes)
	{
		string ret = "(";
		for (size_t i = 0; i < _paramNames.size(); ++i)
			ret += _paramTypes[i] + " " + _paramNames[i] + ",";
		if (ret.size() != 1)
			ret.pop_back();
		return ret + ")";
	};
	// If this is a library, include all its enum and struct types. Should be more intelligent
	// in the future and check what is actually used (it might even use types from other libraries
	// or contracts or in the global scope).
	if (_contractDef.isLibrary())
	{
		for (auto const& stru: _contractDef.definedStructs())
		{
			ret += "struct " + stru->name() + "{";
			for (ASTPointer<VariableDeclaration> const& _member: stru->members())
				ret += _member->type()->canonicalName(false) + " " + _member->name() + ";";
			ret += "}";
		}
		for (auto const& enu: _contractDef.definedEnums())
		{
			ret += "enum " + enu->name() + "{";
			for (ASTPointer<EnumValue> const& val: enu->members())
				ret += val->name() + ",";
			if (ret.back() == ',')
				ret.pop_back();
			ret += "}";
		}
	}
	if (_contractDef.constructor())
	{
		auto externalFunction = FunctionType(*_contractDef.constructor()).interfaceFunctionType();
		solAssert(!!externalFunction, "");
		ret +=
			"function " +
			_contractDef.name() +
			populateParameters(
				externalFunction->parameterNames(),
				externalFunction->parameterTypeNames(_contractDef.isLibrary())
			) +
			";";
	}
	for (auto const& it: _contractDef.interfaceFunctions())
	{
		ret += "function " + it.second->declaration().name() +
			populateParameters(
				it.second->parameterNames(),
				it.second->parameterTypeNames(_contractDef.isLibrary())
			) + (it.second->isConstant() ? "constant " : "");
		if (it.second->returnParameterTypes().size())
			ret += "returns" + populateParameters(
				it.second->returnParameterNames(),
				it.second->returnParameterTypeNames(_contractDef.isLibrary())
			);
		else if (ret.back() == ' ')
			ret.pop_back();
		ret += ";";
	}

	return ret + "}";
}

string InterfaceHandler::userDocumentation(ContractDefinition const& _contractDef)
{
	Json::Value doc;
	Json::Value methods(Json::objectValue);

	for (auto const& it: _contractDef.interfaceFunctions())
		if (it.second->hasDeclaration())
			if (auto const* f = dynamic_cast<FunctionDefinition const*>(&it.second->declaration()))
			{
				string value = extractDoc(f->annotation().docTags, "notice");
				if (!value.empty())
				{
					Json::Value user;
					// since @notice is the only user tag if missing function should not appear
					user["notice"] = Json::Value(value);
					methods[it.second->externalSignature()] = user;
				}
			}
	doc["methods"] = methods;

	return Json::StyledWriter().write(doc);
}

string InterfaceHandler::devDocumentation(ContractDefinition const& _contractDef)
{
	Json::Value doc;
	Json::Value methods(Json::objectValue);

	auto author = extractDoc(_contractDef.annotation().docTags, "author");
	if (!author.empty())
		doc["author"] = author;
	auto title = extractDoc(_contractDef.annotation().docTags, "title");
	if (!title.empty())
		doc["title"] = title;

	for (auto const& it: _contractDef.interfaceFunctions())
	{
		if (!it.second->hasDeclaration())
			continue;
		Json::Value method;
		if (auto fun = dynamic_cast<FunctionDefinition const*>(&it.second->declaration()))
		{
			auto dev = extractDoc(fun->annotation().docTags, "dev");
			if (!dev.empty())
				method["details"] = Json::Value(dev);

			auto author = extractDoc(fun->annotation().docTags, "author");
			if (!author.empty())
				method["author"] = author;

			auto ret = extractDoc(fun->annotation().docTags, "return");
			if (!ret.empty())
				method["return"] = ret;

			Json::Value params(Json::objectValue);
			auto paramRange = fun->annotation().docTags.equal_range("param");
			for (auto i = paramRange.first; i != paramRange.second; ++i)
				params[i->second.paramName] = Json::Value(i->second.content);

			if (!params.empty())
				method["params"] = params;

			if (!method.empty())
				// add the function, only if we have any documentation to add
				methods[it.second->externalSignature()] = method;
		}
	}
	doc["methods"] = methods;

	return Json::StyledWriter().write(doc);
}

string InterfaceHandler::extractDoc(multimap<string, DocTag> const& _tags, string const& _name)
{
	string value;
	auto range = _tags.equal_range(_name);
	for (auto i = range.first; i != range.second; i++)
		value += i->second.content;
	return value;
}
