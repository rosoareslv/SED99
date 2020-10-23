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
 * @author Lefteris <lefteris@ethdev.com>
 * @date 2014
 * Takes the parsed AST and produces the Natspec
 * documentation and the ABI interface
 * https://github.com/ethereum/wiki/wiki/Ethereum-Natural-Specification-Format
 *
 * Can generally deal with JSON files
 */

#pragma once

#include <string>
#include <memory>
#include <json/json.h>

namespace dev
{
namespace solidity
{

// Forward declarations
class ContractDefinition;
struct DocTag;
enum class DocumentationType: uint8_t;

enum class DocTagType: uint8_t
{
	None = 0,
	Dev,
	Notice,
	Param,
	Return,
	Author,
	Title
};

enum class CommentOwner
{
	Contract,
	Function
};

class InterfaceHandler
{
public:
	/// Get the given type of documentation
	/// @param _contractDef The contract definition
	/// @param _type        The type of the documentation. Can be one of the
	///                     types provided by @c DocumentationType
	/// @return             A string with the json representation of provided type
	static std::string documentation(
		ContractDefinition const& _contractDef,
		DocumentationType _type
	);
	/// Get the ABI Interface of the contract
	/// @param _contractDef The contract definition
	/// @return             A string with the json representation of the contract's ABI Interface
	static std::string abiInterface(ContractDefinition const& _contractDef);
	/// Get the User documentation of the contract
	/// @param _contractDef The contract definition
	/// @return             A string with the json representation of the contract's user documentation
	static std::string userDocumentation(ContractDefinition const& _contractDef);
	/// Genereates the Developer's documentation of the contract
	/// @param _contractDef The contract definition
	/// @return             A string with the json representation
	///                     of the contract's developer documentation
	static std::string devDocumentation(ContractDefinition const& _contractDef);

private:
	/// @returns concatenation of all content under the given tag name.
	static std::string extractDoc(std::multimap<std::string, DocTag> const& _tags, std::string const& _name);
};

} //solidity NS
} // dev NS
