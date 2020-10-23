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
 * Specific AST walker that collects all defined names.
 */

#include <libjulia/optimiser/NameCollector.h>

#include <libsolidity/inlineasm/AsmData.h>

using namespace std;
using namespace dev;
using namespace dev::julia;

void NameCollector::operator()(VariableDeclaration const& _varDecl)
{
	for (auto const& var: _varDecl.variables)
		m_names.insert(var.name);
}

void NameCollector::operator ()(FunctionDefinition const& _funDef)
{
	m_names.insert(_funDef.name);
	m_functions[_funDef.name] = &_funDef;
	for (auto const arg: _funDef.parameters)
		m_names.insert(arg.name);
	for (auto const ret: _funDef.returnVariables)
		m_names.insert(ret.name);
	ASTWalker::operator ()(_funDef);
}
