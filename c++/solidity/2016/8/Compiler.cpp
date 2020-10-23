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
 * Solidity compiler.
 */

#include <libsolidity/codegen/Compiler.h>
#include <libevmasm/Assembly.h>
#include <libsolidity/codegen/ContractCompiler.h>

using namespace std;
using namespace dev;
using namespace dev::solidity;

void Compiler::compileContract(
	ContractDefinition const& _contract,
	std::map<const ContractDefinition*, eth::Assembly const*> const& _contracts
)
{
	ContractCompiler runtimeCompiler(m_runtimeContext, m_optimize);
	runtimeCompiler.compileContract(_contract, _contracts);

	ContractCompiler creationCompiler(m_context, m_optimize);
	m_runtimeSub = creationCompiler.compileConstructor(m_runtimeContext, _contract, _contracts);

	if (m_optimize)
		m_context.optimise(m_optimizeRuns);

	if (_contract.isLibrary())
	{
		solAssert(m_runtimeSub != size_t(-1), "");
		m_context.injectVersionStampIntoSub(m_runtimeSub);
	}
}

void Compiler::compileClone(
	ContractDefinition const& _contract,
	map<ContractDefinition const*, eth::Assembly const*> const& _contracts
)
{
	ContractCompiler cloneCompiler(m_context, m_optimize);
	m_runtimeSub = cloneCompiler.compileClone(_contract, _contracts);

	if (m_optimize)
		m_context.optimise(m_optimizeRuns);
}

eth::AssemblyItem Compiler::functionEntryLabel(FunctionDefinition const& _function) const
{
	return m_runtimeContext.functionEntryLabelIfExists(_function);
}
