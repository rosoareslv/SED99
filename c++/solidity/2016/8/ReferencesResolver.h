/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @author Christian <c@ethdev.com>
 * @date 2014
 * Component that resolves type names to types and annotates the AST accordingly.
 */

#pragma once

#include <map>
#include <list>
#include <boost/noncopyable.hpp>
#include <libsolidity/ast/ASTVisitor.h>
#include <libsolidity/ast/ASTAnnotations.h>

namespace dev
{
namespace solidity
{

class NameAndTypeResolver;

/**
 * Resolves references to declarations (of variables and types) and also establishes the link
 * between a return statement and the return parameter list.
 */
class ReferencesResolver: private ASTConstVisitor
{
public:
	ReferencesResolver(
		ErrorList& _errors,
		NameAndTypeResolver& _resolver,
		ParameterList const* _returnParameters,
		bool _resolveInsideCode = false
	):
		m_errors(_errors),
		m_resolver(_resolver),
		m_returnParameters(_returnParameters),
		m_resolveInsideCode(_resolveInsideCode)
	{}

	/// @returns true if no errors during resolving
	bool resolve(ASTNode const& _root);

private:
	virtual bool visit(Block const&) override { return m_resolveInsideCode; }
	virtual bool visit(Identifier const& _identifier) override;
	virtual bool visit(ElementaryTypeName const& _typeName) override;
	virtual void endVisit(UserDefinedTypeName const& _typeName) override;
	virtual void endVisit(Mapping const& _typeName) override;
	virtual void endVisit(ArrayTypeName const& _typeName) override;
	virtual bool visit(InlineAssembly const& _inlineAssembly) override;
	virtual bool visit(Return const& _return) override;
	virtual void endVisit(VariableDeclaration const& _variable) override;

	/// Adds a new error to the list of errors.
	void typeError(SourceLocation const& _location, std::string const& _description);

	/// Adds a new error to the list of errors and throws to abort type checking.
	void fatalTypeError(SourceLocation const& _location, std::string const& _description);

	/// Adds a new error to the list of errors.
	void declarationError(const SourceLocation& _location, std::string const& _description);

	/// Adds a new error to the list of errors and throws to abort type checking.
	void fatalDeclarationError(const SourceLocation& _location, std::string const& _description);

	ErrorList& m_errors;
	NameAndTypeResolver& m_resolver;
	ParameterList const* m_returnParameters;
	bool const m_resolveInsideCode;
	bool m_errorOccurred = false;
};

}
}
