/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @author Christian <c@ethdev.com>
 * @author Gav Wood <g@ethdev.com>
 * @date 2014
 * Full-stack compiler that converts a source code string to bytecode.
 */

#include <libsolidity/interface/CompilerStack.h>

#include <libsolidity/interface/Version.h>
#include <libsolidity/analysis/SemVerHandler.h>
#include <libsolidity/ast/AST.h>
#include <libsolidity/parsing/Scanner.h>
#include <libsolidity/parsing/Parser.h>
#include <libsolidity/analysis/GlobalContext.h>
#include <libsolidity/analysis/NameAndTypeResolver.h>
#include <libsolidity/analysis/TypeChecker.h>
#include <libsolidity/analysis/DocStringAnalyser.h>
#include <libsolidity/analysis/StaticAnalyzer.h>
#include <libsolidity/analysis/SyntaxChecker.h>
#include <libsolidity/codegen/Compiler.h>
#include <libsolidity/interface/InterfaceHandler.h>
#include <libsolidity/formal/Why3Translator.h>

#include <libevmasm/Exceptions.h>

#include <libdevcore/SwarmHash.h>
#include <libdevcore/JSON.h>

#include <json/json.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>


using namespace std;
using namespace dev;
using namespace dev::solidity;

CompilerStack::CompilerStack(ReadFileCallback const& _readFile):
	m_readFile(_readFile), m_parseSuccessful(false) {}

void CompilerStack::setRemappings(vector<string> const& _remappings)
{
	vector<Remapping> remappings;
	for (auto const& remapping: _remappings)
	{
		auto eq = find(remapping.begin(), remapping.end(), '=');
		if (eq == remapping.end())
			continue; // ignore
		auto colon = find(remapping.begin(), eq, ':');
		Remapping r;
		r.context = colon == eq ? string() : string(remapping.begin(), colon);
		r.prefix = colon == eq ? string(remapping.begin(), eq) : string(colon + 1, eq);
		r.target = string(eq + 1, remapping.end());
		remappings.push_back(r);
	}
	swap(m_remappings, remappings);
}

void CompilerStack::reset(bool _keepSources)
{
	m_parseSuccessful = false;
	if (_keepSources)
		for (auto sourcePair: m_sources)
			sourcePair.second.reset();
	else
	{
		m_sources.clear();
	}
	m_optimize = false;
	m_optimizeRuns = 200;
	m_globalContext.reset();
	m_sourceOrder.clear();
	m_contracts.clear();
	m_errors.clear();
}

bool CompilerStack::addSource(string const& _name, string const& _content, bool _isLibrary)
{
	bool existed = m_sources.count(_name) != 0;
	reset(true);
	m_sources[_name].scanner = make_shared<Scanner>(CharStream(_content), _name);
	m_sources[_name].isLibrary = _isLibrary;
	return existed;
}

void CompilerStack::setSource(string const& _sourceCode)
{
	reset();
	addSource("", _sourceCode);
}

bool CompilerStack::parse()
{
	//reset
	m_errors.clear();
	m_parseSuccessful = false;

	if (SemVerVersion{string(VersionString)}.isPrerelease())
	{
		auto err = make_shared<Error>(Error::Type::Warning);
		*err << errinfo_comment("This is a pre-release compiler version, please do not use it in production.");
		m_errors.push_back(err);
	}

	vector<string> sourcesToParse;
	for (auto const& s: m_sources)
		sourcesToParse.push_back(s.first);
	map<string, SourceUnit const*> sourceUnitsByName;
	for (size_t i = 0; i < sourcesToParse.size(); ++i)
	{
		string const& path = sourcesToParse[i];
		Source& source = m_sources[path];
		source.scanner->reset();
		source.ast = Parser(m_errors).parse(source.scanner);
		sourceUnitsByName[path] = source.ast.get();
		if (!source.ast)
			solAssert(!Error::containsOnlyWarnings(m_errors), "Parser returned null but did not report error.");
		else
		{
			source.ast->annotation().path = path;
			for (auto const& newSource: loadMissingSources(*source.ast, path))
			{
				string const& newPath = newSource.first;
				string const& newContents = newSource.second;
				m_sources[newPath].scanner = make_shared<Scanner>(CharStream(newContents), newPath);
				sourcesToParse.push_back(newPath);
			}
		}
	}
	if (!Error::containsOnlyWarnings(m_errors))
		// errors while parsing. should stop before type checking
		return false;

	resolveImports();

	bool noErrors = true;
	SyntaxChecker syntaxChecker(m_errors);
	for (Source const* source: m_sourceOrder)
		if (!syntaxChecker.checkSyntax(*source->ast))
			noErrors = false;

	DocStringAnalyser docStringAnalyser(m_errors);
	for (Source const* source: m_sourceOrder)
		if (!docStringAnalyser.analyseDocStrings(*source->ast))
			noErrors = false;

	m_globalContext = make_shared<GlobalContext>();
	NameAndTypeResolver resolver(m_globalContext->declarations(), m_errors);
	for (Source const* source: m_sourceOrder)
		if (!resolver.registerDeclarations(*source->ast))
			return false;

	for (Source const* source: m_sourceOrder)
		if (!resolver.performImports(*source->ast, sourceUnitsByName))
			return false;

	for (Source const* source: m_sourceOrder)
		for (ASTPointer<ASTNode> const& node: source->ast->nodes())
			if (ContractDefinition* contract = dynamic_cast<ContractDefinition*>(node.get()))
			{
				m_globalContext->setCurrentContract(*contract);
				if (!resolver.updateDeclaration(*m_globalContext->currentThis())) return false;
				if (!resolver.updateDeclaration(*m_globalContext->currentSuper())) return false;
				if (!resolver.resolveNamesAndTypes(*contract)) return false;
				m_contracts[contract->name()].contract = contract;
			}

	if (!checkLibraryNameClashes())
		noErrors = false;

	for (Source const* source: m_sourceOrder)
		for (ASTPointer<ASTNode> const& node: source->ast->nodes())
			if (ContractDefinition* contract = dynamic_cast<ContractDefinition*>(node.get()))
			{
				m_globalContext->setCurrentContract(*contract);
				resolver.updateDeclaration(*m_globalContext->currentThis());
				TypeChecker typeChecker(m_errors);
				if (typeChecker.checkTypeRequirements(*contract))
				{
					contract->setDevDocumentation(InterfaceHandler::devDocumentation(*contract));
					contract->setUserDocumentation(InterfaceHandler::userDocumentation(*contract));
				}
				else
					noErrors = false;

				m_contracts[contract->name()].contract = contract;
			}

	if (noErrors)
	{
		StaticAnalyzer staticAnalyzer(m_errors);
		for (Source const* source: m_sourceOrder)
			if (!staticAnalyzer.analyze(*source->ast))
				noErrors = false;
	}

	m_parseSuccessful = noErrors;
	return m_parseSuccessful;
}

bool CompilerStack::parse(string const& _sourceCode)
{
	setSource(_sourceCode);
	return parse();
}

vector<string> CompilerStack::contractNames() const
{
	if (!m_parseSuccessful)
		BOOST_THROW_EXCEPTION(CompilerError() << errinfo_comment("Parsing was not successful."));
	vector<string> contractNames;
	for (auto const& contract: m_contracts)
		contractNames.push_back(contract.first);
	return contractNames;
}


bool CompilerStack::compile(bool _optimize, unsigned _runs, map<string, h160> const& _libraries)
{
	if (!m_parseSuccessful)
		if (!parse())
			return false;

	m_optimize = _optimize;
	m_optimizeRuns = _runs;
	m_libraries = _libraries;

	map<ContractDefinition const*, eth::Assembly const*> compiledContracts;
	for (Source const* source: m_sourceOrder)
		for (ASTPointer<ASTNode> const& node: source->ast->nodes())
			if (auto contract = dynamic_cast<ContractDefinition const*>(node.get()))
				compileContract(*contract, compiledContracts);
	this->link();
	return true;
}

bool CompilerStack::compile(string const& _sourceCode, bool _optimize, unsigned _runs)
{
	return parse(_sourceCode) && compile(_optimize, _runs);
}

void CompilerStack::link()
{
	for (auto& contract: m_contracts)
	{
		contract.second.object.link(m_libraries);
		contract.second.runtimeObject.link(m_libraries);
		contract.second.cloneObject.link(m_libraries);
	}
}

bool CompilerStack::prepareFormalAnalysis(ErrorList* _errors)
{
	if (!_errors)
		_errors = &m_errors;
	Why3Translator translator(*_errors);
	for (Source const* source: m_sourceOrder)
		if (!translator.process(*source->ast))
			return false;

	m_formalTranslation = translator.translation();

	return true;
}

eth::AssemblyItems const* CompilerStack::assemblyItems(string const& _contractName) const
{
	Contract const& currentContract = contract(_contractName);
	return currentContract.compiler ? &contract(_contractName).compiler->assemblyItems() : nullptr;
}

eth::AssemblyItems const* CompilerStack::runtimeAssemblyItems(string const& _contractName) const
{
	Contract const& currentContract = contract(_contractName);
	return currentContract.compiler ? &contract(_contractName).compiler->runtimeAssemblyItems() : nullptr;
}

string const* CompilerStack::sourceMapping(string const& _contractName) const
{
	Contract const& c = contract(_contractName);
	if (!c.sourceMapping)
	{
		if (auto items = assemblyItems(_contractName))
			c.sourceMapping.reset(new string(computeSourceMapping(*items)));
	}
	return c.sourceMapping.get();
}

string const* CompilerStack::runtimeSourceMapping(string const& _contractName) const
{
	Contract const& c = contract(_contractName);
	if (!c.runtimeSourceMapping)
	{
		if (auto items = runtimeAssemblyItems(_contractName))
			c.runtimeSourceMapping.reset(new string(computeSourceMapping(*items)));
	}
	return c.runtimeSourceMapping.get();
}

eth::LinkerObject const& CompilerStack::object(string const& _contractName) const
{
	return contract(_contractName).object;
}

eth::LinkerObject const& CompilerStack::runtimeObject(string const& _contractName) const
{
	return contract(_contractName).runtimeObject;
}

eth::LinkerObject const& CompilerStack::cloneObject(string const& _contractName) const
{
	return contract(_contractName).cloneObject;
}

dev::h256 CompilerStack::contractCodeHash(string const& _contractName) const
{
	auto const& obj = runtimeObject(_contractName);
	if (obj.bytecode.empty() || !obj.linkReferences.empty())
		return dev::h256();
	else
		return dev::keccak256(obj.bytecode);
}

Json::Value CompilerStack::streamAssembly(ostream& _outStream, string const& _contractName, StringMap _sourceCodes, bool _inJsonFormat) const
{
	Contract const& currentContract = contract(_contractName);
	if (currentContract.compiler)
		return currentContract.compiler->streamAssembly(_outStream, _sourceCodes, _inJsonFormat);
	else
	{
		_outStream << "Contract not fully implemented" << endl;
		return Json::Value();
	}
}

vector<string> CompilerStack::sourceNames() const
{
	vector<string> names;
	for (auto const& s: m_sources)
		names.push_back(s.first);
	return names;
}

map<string, unsigned> CompilerStack::sourceIndices() const
{
	map<string, unsigned> indices;
	for (auto const& s: m_sources)
		indices[s.first] = indices.size();
	return indices;
}

Json::Value const& CompilerStack::interface(string const& _contractName) const
{
	return metadata(_contractName, DocumentationType::ABIInterface);
}

Json::Value const& CompilerStack::metadata(string const& _contractName, DocumentationType _type) const
{
	if (!m_parseSuccessful)
		BOOST_THROW_EXCEPTION(CompilerError() << errinfo_comment("Parsing was not successful."));

	return metadata(contract(_contractName), _type);
}

Json::Value const& CompilerStack::metadata(Contract const& _contract, DocumentationType _type) const
{
	if (!m_parseSuccessful)
		BOOST_THROW_EXCEPTION(CompilerError() << errinfo_comment("Parsing was not successful."));

	solAssert(_contract.contract, "");
	std::unique_ptr<Json::Value const>* doc;

	// checks wheather we already have the documentation
	switch (_type)
	{
	case DocumentationType::NatspecUser:
		doc = &_contract.userDocumentation;
		break;
	case DocumentationType::NatspecDev:
		doc = &_contract.devDocumentation;
		break;
	case DocumentationType::ABIInterface:
		doc = &_contract.interface;
		break;
	default:
		BOOST_THROW_EXCEPTION(InternalCompilerError() << errinfo_comment("Illegal documentation type."));
	}

	// caches the result
	if (!*doc)
		doc->reset(new Json::Value(InterfaceHandler::documentation(*_contract.contract, _type)));

	return *(*doc);
}

string const& CompilerStack::onChainMetadata(string const& _contractName) const
{
	if (!m_parseSuccessful)
		BOOST_THROW_EXCEPTION(CompilerError() << errinfo_comment("Parsing was not successful."));

	return contract(_contractName).onChainMetadata;
}

Scanner const& CompilerStack::scanner(string const& _sourceName) const
{
	return *source(_sourceName).scanner;
}

SourceUnit const& CompilerStack::ast(string const& _sourceName) const
{
	return *source(_sourceName).ast;
}

ContractDefinition const& CompilerStack::contractDefinition(string const& _contractName) const
{
	return *contract(_contractName).contract;
}

size_t CompilerStack::functionEntryPoint(
	std::string const& _contractName,
	FunctionDefinition const& _function
) const
{
	shared_ptr<Compiler> const& compiler = contract(_contractName).compiler;
	if (!compiler)
		return 0;
	eth::AssemblyItem tag = compiler->functionEntryLabel(_function);
	if (tag.type() == eth::UndefinedItem)
		return 0;
	eth::AssemblyItems const& items = compiler->runtimeAssemblyItems();
	for (size_t i = 0; i < items.size(); ++i)
		if (items.at(i).type() == eth::Tag && items.at(i).data() == tag.data())
			return i;
	return 0;
}

tuple<int, int, int, int> CompilerStack::positionFromSourceLocation(SourceLocation const& _sourceLocation) const
{
	int startLine;
	int startColumn;
	int endLine;
	int endColumn;
	tie(startLine, startColumn) = scanner(*_sourceLocation.sourceName).translatePositionToLineColumn(_sourceLocation.start);
	tie(endLine, endColumn) = scanner(*_sourceLocation.sourceName).translatePositionToLineColumn(_sourceLocation.end);

	return make_tuple(++startLine, ++startColumn, ++endLine, ++endColumn);
}

StringMap CompilerStack::loadMissingSources(SourceUnit const& _ast, std::string const& _sourcePath)
{
	StringMap newSources;
	for (auto const& node: _ast.nodes())
		if (ImportDirective const* import = dynamic_cast<ImportDirective*>(node.get()))
		{
			string importPath = absolutePath(import->path(), _sourcePath);
			// The current value of `path` is the absolute path as seen from this source file.
			// We first have to apply remappings before we can store the actual absolute path
			// as seen globally.
			importPath = applyRemapping(importPath, _sourcePath);
			import->annotation().absolutePath = importPath;
			if (m_sources.count(importPath) || newSources.count(importPath))
				continue;

			ReadFileResult result{false, string("File not supplied initially.")};
			if (m_readFile)
				result = m_readFile(importPath);

			if (result.success)
				newSources[importPath] = result.contentsOrErrorMesage;
			else
			{
				auto err = make_shared<Error>(Error::Type::ParserError);
				*err <<
					errinfo_sourceLocation(import->location()) <<
					errinfo_comment("Source \"" + importPath + "\" not found: " + result.contentsOrErrorMesage);
				m_errors.push_back(std::move(err));
				continue;
			}
		}
	return newSources;
}

string CompilerStack::applyRemapping(string const& _path, string const& _context)
{
	// Try to find the longest prefix match in all remappings that are active in the current context.
	auto isPrefixOf = [](string const& _a, string const& _b)
	{
		if (_a.length() > _b.length())
			return false;
		return std::equal(_a.begin(), _a.end(), _b.begin());
	};

	size_t longestPrefix = 0;
	string longestPrefixTarget;
	for (auto const& redir: m_remappings)
	{
		// Skip if we already have a closer match.
		if (longestPrefix > 0 && redir.prefix.length() <= longestPrefix)
			continue;
		// Skip if redir.context is not a prefix of _context
		if (!isPrefixOf(redir.context, _context))
			continue;
		// Skip if the prefix does not match.
		if (!isPrefixOf(redir.prefix, _path))
			continue;

		longestPrefix = redir.prefix.length();
		longestPrefixTarget = redir.target;
	}
	string path = longestPrefixTarget;
	path.append(_path.begin() + longestPrefix, _path.end());
	return path;
}

void CompilerStack::resolveImports()
{
	// topological sorting (depth first search) of the import graph, cutting potential cycles
	vector<Source const*> sourceOrder;
	set<Source const*> sourcesSeen;

	function<void(Source const*)> toposort = [&](Source const* _source)
	{
		if (sourcesSeen.count(_source))
			return;
		sourcesSeen.insert(_source);
		for (ASTPointer<ASTNode> const& node: _source->ast->nodes())
			if (ImportDirective const* import = dynamic_cast<ImportDirective*>(node.get()))
			{
				string const& path = import->annotation().absolutePath;
				solAssert(!path.empty(), "");
				solAssert(m_sources.count(path), "");
				import->annotation().sourceUnit = m_sources[path].ast.get();
				toposort(&m_sources[path]);
			}
		sourceOrder.push_back(_source);
	};

	for (auto const& sourcePair: m_sources)
		if (!sourcePair.second.isLibrary)
			toposort(&sourcePair.second);

	swap(m_sourceOrder, sourceOrder);
}

bool CompilerStack::checkLibraryNameClashes()
{
	bool clashFound = false;
	map<string, SourceLocation> libraries;
	for (Source const* source: m_sourceOrder)
		for (ASTPointer<ASTNode> const& node: source->ast->nodes())
			if (ContractDefinition* contract = dynamic_cast<ContractDefinition*>(node.get()))
				if (contract->isLibrary())
				{
					if (libraries.count(contract->name()))
					{
						auto err = make_shared<Error>(Error::Type::DeclarationError);
						*err <<
							errinfo_sourceLocation(contract->location()) <<
							errinfo_comment(
								"Library \"" + contract->name() + "\" declared twice "
								"(will create ambiguities during linking)."
							) <<
							errinfo_secondarySourceLocation(SecondarySourceLocation().append(
									"The other declaration is here:", libraries[contract->name()]
							));

						m_errors.push_back(err);
						clashFound = true;
					}
					else
						libraries[contract->name()] = contract->location();
				}
	return !clashFound;
}

string CompilerStack::absolutePath(string const& _path, string const& _reference) const
{
	// Anything that does not start with `.` is an absolute path.
	if (_path.empty() || _path.front() != '.')
		return _path;
	using path = boost::filesystem::path;
	path p(_path);
	path result(_reference);
	result.remove_filename();
	for (path::iterator it = p.begin(); it != p.end(); ++it)
		if (*it == "..")
			result = result.parent_path();
		else if (*it != ".")
			result /= *it;
	return result.generic_string();
}

void CompilerStack::compileContract(
	ContractDefinition const& _contract,
	map<ContractDefinition const*, eth::Assembly const*>& _compiledContracts
)
{
	if (_compiledContracts.count(&_contract) || !_contract.annotation().isFullyImplemented)
		return;
	for (auto const* dependency: _contract.annotation().contractDependencies)
		compileContract(*dependency, _compiledContracts);

	shared_ptr<Compiler> compiler = make_shared<Compiler>(m_optimize, m_optimizeRuns);
	Contract& compiledContract = m_contracts.at(_contract.name());
	string onChainMetadata = createOnChainMetadata(compiledContract);
	bytes cborEncodedMetadata =
		// CBOR-encoding of {"bzzr0": dev::swarmHash(onChainMetadata)}
		bytes{0xa1, 0x65, 'b', 'z', 'z', 'r', '0', 0x58, 0x20} +
		dev::swarmHash(onChainMetadata).asBytes();
	solAssert(cborEncodedMetadata.size() <= 0xffff, "Metadata too large");
	// 16-bit big endian length
	cborEncodedMetadata += toCompactBigEndian(cborEncodedMetadata.size(), 2);
	compiler->compileContract(_contract, _compiledContracts, cborEncodedMetadata);
	compiledContract.compiler = compiler;
	compiledContract.object = compiler->assembledObject();
	compiledContract.runtimeObject = compiler->runtimeObject();
	compiledContract.onChainMetadata = onChainMetadata;
	_compiledContracts[compiledContract.contract] = &compiler->assembly();

	try
	{
		Compiler cloneCompiler(m_optimize, m_optimizeRuns);
		cloneCompiler.compileClone(_contract, _compiledContracts);
		compiledContract.cloneObject = cloneCompiler.assembledObject();
	}
	catch (eth::AssemblyException const&)
	{
		// In some cases (if the constructor requests a runtime function), it is not
		// possible to compile the clone.

		// TODO: Report error / warning
	}
}

std::string CompilerStack::defaultContractName() const
{
	return contract("").contract->name();
}

CompilerStack::Contract const& CompilerStack::contract(string const& _contractName) const
{
	if (m_contracts.empty())
		BOOST_THROW_EXCEPTION(CompilerError() << errinfo_comment("No compiled contracts found."));
	string contractName = _contractName;
	if (_contractName.empty())
		// try to find some user-supplied contract
		for (auto const& it: m_sources)
			for (ASTPointer<ASTNode> const& node: it.second.ast->nodes())
				if (auto contract = dynamic_cast<ContractDefinition const*>(node.get()))
					contractName = contract->name();
	auto it = m_contracts.find(contractName);
	if (it == m_contracts.end())
		BOOST_THROW_EXCEPTION(CompilerError() << errinfo_comment("Contract " + _contractName + " not found."));
	return it->second;
}

CompilerStack::Source const& CompilerStack::source(string const& _sourceName) const
{
	auto it = m_sources.find(_sourceName);
	if (it == m_sources.end())
		BOOST_THROW_EXCEPTION(CompilerError() << errinfo_comment("Given source file not found."));

	return it->second;
}

string CompilerStack::createOnChainMetadata(Contract const& _contract) const
{
	Json::Value meta;
	meta["version"] = 1;
	meta["language"] = "Solidity";
	meta["compiler"]["version"] = VersionString;

	meta["sources"] = Json::objectValue;
	for (auto const& s: m_sources)
	{
		solAssert(s.second.scanner, "Scanner not available");
		meta["sources"][s.first]["keccak256"] =
			"0x" + toHex(dev::keccak256(s.second.scanner->source()).asBytes());
		meta["sources"][s.first]["urls"] = Json::arrayValue;
		meta["sources"][s.first]["urls"].append(
			"bzzr://" + toHex(dev::swarmHash(s.second.scanner->source()).asBytes())
		);
	}
	meta["settings"]["optimizer"]["enabled"] = m_optimize;
	meta["settings"]["optimizer"]["runs"] = m_optimizeRuns;
	meta["settings"]["compilationTarget"][_contract.contract->sourceUnitName()] =
		_contract.contract->annotation().canonicalName;

	meta["settings"]["remappings"] = Json::arrayValue;
	set<string> remappings;
	for (auto const& r: m_remappings)
		remappings.insert(r.context + ":" + r.prefix + "=" + r.target);
	for (auto const& r: remappings)
		meta["settings"]["remappings"].append(r);

	meta["settings"]["libraries"] = Json::objectValue;
	for (auto const& library: m_libraries)
		meta["settings"]["libraries"][library.first] = "0x" + toHex(library.second.asBytes());

	meta["output"]["abi"] = metadata(_contract, DocumentationType::ABIInterface);
	meta["output"]["userdoc"] = metadata(_contract, DocumentationType::NatspecUser);
	meta["output"]["devdoc"] = metadata(_contract, DocumentationType::NatspecDev);

	return jsonCompactPrint(meta);
}

string CompilerStack::computeSourceMapping(eth::AssemblyItems const& _items) const
{
	string ret;
	map<string, unsigned> sourceIndicesMap = sourceIndices();
	int prevStart = -1;
	int prevLength = -1;
	int prevSourceIndex = -1;
	char prevJump = 0;
	for (auto const& item: _items)
	{
		if (!ret.empty())
			ret += ";";

		SourceLocation const& location = item.location();
		int length = location.start != -1 && location.end != -1 ? location.end - location.start : -1;
		int sourceIndex =
			location.sourceName && sourceIndicesMap.count(*location.sourceName) ?
			sourceIndicesMap.at(*location.sourceName) :
			-1;
		char jump = '-';
		if (item.getJumpType() == eth::AssemblyItem::JumpType::IntoFunction)
			jump = 'i';
		else if (item.getJumpType() == eth::AssemblyItem::JumpType::OutOfFunction)
			jump = 'o';

		unsigned components = 4;
		if (jump == prevJump)
		{
			components--;
			if (sourceIndex == prevSourceIndex)
			{
				components--;
				if (length == prevLength)
				{
					components--;
					if (location.start == prevStart)
						components--;
				}
			}
		}

		if (components-- > 0)
		{
			if (location.start != prevStart)
				ret += std::to_string(location.start);
			if (components-- > 0)
			{
				ret += ':';
				if (length != prevLength)
					ret += std::to_string(length);
				if (components-- > 0)
				{
					ret += ':';
					if (sourceIndex != prevSourceIndex)
						ret += std::to_string(sourceIndex);
					if (components-- > 0)
					{
						ret += ':';
						if (jump != prevJump)
							ret += jump;
					}
				}
			}
		}

		prevStart = location.start;
		prevLength = length;
		prevSourceIndex = sourceIndex;
		prevJump = jump;
	}
	return ret;
}
