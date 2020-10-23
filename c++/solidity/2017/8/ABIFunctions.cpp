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
 * @author Christian <chris@ethereum.org>
 * @date 2017
 * Routines that generate JULIA code related to ABI encoding, decoding and type conversions.
 */

#include <libsolidity/codegen/ABIFunctions.h>

#include <libdevcore/Whiskers.h>

#include <libsolidity/ast/AST.h>

using namespace std;
using namespace dev;
using namespace dev::solidity;

ABIFunctions::~ABIFunctions()
{
	// This throws an exception and thus might cause immediate termination, but hey,
	// it's a failed assertion anyway :-)
	solAssert(m_requestedFunctions.empty(), "Forgot to call ``requestedFunctions()``.");
}

string ABIFunctions::tupleEncoder(
	TypePointers const& _givenTypes,
	TypePointers const& _targetTypes,
	bool _encodeAsLibraryTypes
)
{
	// stack: <$value0> <$value1> ... <$value(n-1)> <$headStart>

	solAssert(!_givenTypes.empty(), "");
	size_t const headSize_ = headSize(_targetTypes);

	Whiskers encoder(R"(
		{
			let tail := add($headStart, <headSize>)
			<encodeElements>
			<deepestStackElement> := tail
		}
	)");
	encoder("headSize", to_string(headSize_));
	string encodeElements;
	size_t headPos = 0;
	size_t stackPos = 0;
	for (size_t i = 0; i < _givenTypes.size(); ++i)
	{
		solAssert(_givenTypes[i], "");
		solAssert(_targetTypes[i], "");
		size_t sizeOnStack = _givenTypes[i]->sizeOnStack();
		string valueNames = "";
		for (size_t j = 0; j < sizeOnStack; j++)
			valueNames += "$value" + to_string(stackPos++) + ", ";
		bool dynamic = _targetTypes[i]->isDynamicallyEncoded();
		Whiskers elementTempl(
			dynamic ?
			string(R"(
				mstore(add($headStart, <pos>), sub(tail, $headStart))
				tail := <abiEncode>(<values> tail)
			)") :
			string(R"(
				<abiEncode>(<values> add($headStart, <pos>))
			)")
		);
		elementTempl("values", valueNames);
		elementTempl("pos", to_string(headPos));
		elementTempl("abiEncode", abiEncodingFunction(*_givenTypes[i], *_targetTypes[i], _encodeAsLibraryTypes, false));
		encodeElements += elementTempl.render();
		headPos += dynamic ? 0x20 : _targetTypes[i]->calldataEncodedSize();
	}
	solAssert(headPos == headSize_, "");
	encoder("encodeElements", encodeElements);
	encoder("deepestStackElement", stackPos > 0 ? "$value0" : "$headStart");

	return encoder.render();
}

string ABIFunctions::requestedFunctions()
{
	string result;
	for (auto const& f: m_requestedFunctions)
		result += f.second;
	m_requestedFunctions.clear();
	return result;
}

string ABIFunctions::cleanupFunction(Type const& _type, bool _revertOnFailure)
{
	string functionName = string("cleanup_") + (_revertOnFailure ? "revert_" : "assert_") + _type.identifier();
	return createFunction(functionName, [&]() {
		Whiskers templ(R"(
			function <functionName>(value) -> cleaned {
				<body>
			}
		)");
		templ("functionName", functionName);
		switch (_type.category())
		{
		case Type::Category::Integer:
		{
			IntegerType const& type = dynamic_cast<IntegerType const&>(_type);
			if (type.numBits() == 256)
				templ("body", "cleaned := value");
			else if (type.isSigned())
				templ("body", "cleaned := signextend(" + to_string(type.numBits() / 8 - 1) + ", value)");
			else
				templ("body", "cleaned := and(value, " + toCompactHexWithPrefix((u256(1) << type.numBits()) - 1) + ")");
			break;
		}
		case Type::Category::RationalNumber:
			templ("body", "cleaned := value");
			break;
		case Type::Category::Bool:
			templ("body", "cleaned := iszero(iszero(value))");
			break;
		case Type::Category::FixedPoint:
			solUnimplemented("Fixed point types not implemented.");
			break;
		case Type::Category::Array:
			solAssert(false, "Array cleanup requested.");
			break;
		case Type::Category::Struct:
			solAssert(false, "Struct cleanup requested.");
			break;
		case Type::Category::FixedBytes:
		{
			FixedBytesType const& type = dynamic_cast<FixedBytesType const&>(_type);
			if (type.numBytes() == 32)
				templ("body", "cleaned := value");
			else if (type.numBytes() == 0)
				templ("body", "cleaned := 0");
			else
			{
				size_t numBits = type.numBytes() * 8;
				u256 mask = ((u256(1) << numBits) - 1) << (256 - numBits);
				templ("body", "cleaned := and(value, " + toCompactHexWithPrefix(mask) + ")");
			}
			break;
		}
		case Type::Category::Contract:
			templ("body", "cleaned := " + cleanupFunction(IntegerType(0, IntegerType::Modifier::Address)) + "(value)");
			break;
		case Type::Category::Enum:
		{
			size_t members = dynamic_cast<EnumType const&>(_type).numberOfMembers();
			solAssert(members > 0, "empty enum should have caused a parser error.");
			Whiskers w("switch lt(value, <members>) case 0 { <failure> } cleaned := value");
			w("members", to_string(members));
			if (_revertOnFailure)
				w("failure", "revert(0, 0)");
			else
				w("failure", "invalid()");
			templ("body", w.render());
			break;
		}
		default:
			solAssert(false, "Cleanup of type " + _type.identifier() + " requested.");
		}

		return templ.render();
	});
}

string ABIFunctions::conversionFunction(Type const& _from, Type const& _to)
{
	string functionName =
		"convert_" +
		_from.identifier() +
		"_to_" +
		_to.identifier();
	return createFunction(functionName, [&]() {
		Whiskers templ(R"(
			function <functionName>(value) -> converted {
				<body>
			}
		)");
		templ("functionName", functionName);
		string body;
		auto toCategory = _to.category();
		auto fromCategory = _from.category();
		switch (fromCategory)
		{
		case Type::Category::Integer:
		case Type::Category::RationalNumber:
		case Type::Category::Contract:
		{
			if (RationalNumberType const* rational = dynamic_cast<RationalNumberType const*>(&_from))
				solUnimplementedAssert(!rational->isFractional(), "Not yet implemented - FixedPointType.");
			if (toCategory == Type::Category::FixedBytes)
			{
				solAssert(
					fromCategory == Type::Category::Integer || fromCategory == Type::Category::RationalNumber,
					"Invalid conversion to FixedBytesType requested."
				);
				FixedBytesType const& toBytesType = dynamic_cast<FixedBytesType const&>(_to);
				body =
					Whiskers("converted := <shiftLeft>(<clean>(value))")
						("shiftLeft", shiftLeftFunction(256 - toBytesType.numBytes() * 8))
						("clean", cleanupFunction(_from))
						.render();
			}
			else if (toCategory == Type::Category::Enum)
			{
				solAssert(_from.mobileType(), "");
				body =
					Whiskers("converted := <cleanEnum>(<cleanInt>(value))")
					("cleanEnum", cleanupFunction(_to, false))
					// "mobileType()" returns integer type for rational
					("cleanInt", cleanupFunction(*_from.mobileType()))
					.render();
			}
			else if (toCategory == Type::Category::FixedPoint)
			{
				solUnimplemented("Not yet implemented - FixedPointType.");
			}
			else
			{
				solAssert(
					toCategory == Type::Category::Integer ||
					toCategory == Type::Category::Contract,
				"");
				IntegerType const addressType(0, IntegerType::Modifier::Address);
				IntegerType const& to =
					toCategory == Type::Category::Integer ?
					dynamic_cast<IntegerType const&>(_to) :
					addressType;

				// Clean according to the "to" type, except if this is
				// a widening conversion.
				IntegerType const* cleanupType = &to;
				if (fromCategory != Type::Category::RationalNumber)
				{
					IntegerType const& from =
						fromCategory == Type::Category::Integer ?
						dynamic_cast<IntegerType const&>(_from) :
						addressType;
					if (to.numBits() > from.numBits())
						cleanupType = &from;
				}
				body =
					Whiskers("converted := <cleanInt>(value)")
					("cleanInt", cleanupFunction(*cleanupType))
					.render();
			}
			break;
		}
		case Type::Category::Bool:
		{
			solAssert(_from == _to, "Invalid conversion for bool.");
			body =
				Whiskers("converted := <clean>(value)")
				("clean", cleanupFunction(_from))
				.render();
			break;
		}
		case Type::Category::FixedPoint:
			solUnimplemented("Fixed point types not implemented.");
			break;
		case Type::Category::Array:
			solUnimplementedAssert(false, "Array conversion not implemented.");
			break;
		case Type::Category::Struct:
			solUnimplementedAssert(false, "Struct conversion not implemented.");
			break;
		case Type::Category::FixedBytes:
		{
			FixedBytesType const& from = dynamic_cast<FixedBytesType const&>(_from);
			if (toCategory == Type::Category::Integer)
				body =
					Whiskers("converted := <convert>(<shift>(value))")
					("shift", shiftRightFunction(256 - from.numBytes() * 8, false))
					("convert", conversionFunction(IntegerType(from.numBytes() * 8), _to))
					.render();
			else
			{
				// clear for conversion to longer bytes
				solAssert(toCategory == Type::Category::FixedBytes, "Invalid type conversion requested.");
				body =
					Whiskers("converted := <clean>(value)")
					("clean", cleanupFunction(from))
					.render();
			}
			break;
		}
		case Type::Category::Function:
		{
			solAssert(false, "Conversion should not be called for function types.");
			break;
		}
		case Type::Category::Enum:
		{
			solAssert(toCategory == Type::Category::Integer || _from == _to, "");
			EnumType const& enumType = dynamic_cast<decltype(enumType)>(_from);
			body =
				Whiskers("converted := <clean>(value)")
				("clean", cleanupFunction(enumType))
				.render();
			break;
		}
		case Type::Category::Tuple:
		{
			solUnimplementedAssert(false, "Tuple conversion not implemented.");
			break;
		}
		default:
			solAssert(false, "");
		}

		solAssert(!body.empty(), "");
		templ("body", body);
		return templ.render();
	});
}

string ABIFunctions::cleanupCombinedExternalFunctionIdFunction()
{
	string functionName = "cleanup_combined_external_function_id";
	return createFunction(functionName, [&]() {
		return Whiskers(R"(
			function <functionName>(addr_and_selector) -> cleaned {
				cleaned := <clean>(addr_and_selector)
			}
		)")
		("functionName", functionName)
		("clean", cleanupFunction(FixedBytesType(24)))
		.render();
	});
}

string ABIFunctions::combineExternalFunctionIdFunction()
{
	string functionName = "combine_external_function_id";
	return createFunction(functionName, [&]() {
		return Whiskers(R"(
			function <functionName>(addr, selector) -> combined {
				combined := <shl64>(or(<shl32>(addr), and(selector, 0xffffffff)))
			}
		)")
		("functionName", functionName)
		("shl32", shiftLeftFunction(32))
		("shl64", shiftLeftFunction(64))
		.render();
	});
}

string ABIFunctions::abiEncodingFunction(
	Type const& _from,
	Type const& _to,
	bool _encodeAsLibraryTypes,
	bool _compacted
)
{
	solUnimplementedAssert(
		_to.mobileType() &&
		_to.mobileType()->interfaceType(_encodeAsLibraryTypes) &&
		_to.mobileType()->interfaceType(_encodeAsLibraryTypes)->encodingType(),
		"Encoding type \"" + _to.toString() + "\" not yet implemented."
	);
	TypePointer toInterface = _to.mobileType()->interfaceType(_encodeAsLibraryTypes)->encodingType();
	Type const& to = *toInterface;

	if (_from.category() == Type::Category::StringLiteral)
		return abiEncodingFunctionStringLiteral(_from, to, _encodeAsLibraryTypes);
	else if (auto toArray = dynamic_cast<ArrayType const*>(&to))
	{
		solAssert(_from.category() == Type::Category::Array, "");
		solAssert(to.dataStoredIn(DataLocation::Memory), "");
		ArrayType const& fromArray = dynamic_cast<ArrayType const&>(_from);
		if (fromArray.location() == DataLocation::CallData)
			return abiEncodingFunctionCalldataArray(fromArray, *toArray, _encodeAsLibraryTypes);
		else if (!fromArray.isByteArray() && (
				fromArray.location() == DataLocation::Memory ||
				fromArray.baseType()->storageBytes() > 16
		))
			return abiEncodingFunctionSimpleArray(fromArray, *toArray, _encodeAsLibraryTypes);
		else if (fromArray.location() == DataLocation::Memory)
			return abiEncodingFunctionMemoryByteArray(fromArray, *toArray, _encodeAsLibraryTypes);
		else if (fromArray.location() == DataLocation::Storage)
			return abiEncodingFunctionCompactStorageArray(fromArray, *toArray, _encodeAsLibraryTypes);
		else
			solAssert(false, "");
	}
	else if (dynamic_cast<StructType const*>(&to))
	{
		solUnimplementedAssert(false, "Structs not yet implemented.");
	}
	else if (_from.category() == Type::Category::Function)
		return abiEncodingFunctionFunctionType(
			dynamic_cast<FunctionType const&>(_from),
			to,
			_encodeAsLibraryTypes,
			_compacted
		);

	solAssert(_from.sizeOnStack() == 1, "");
	solAssert(to.isValueType(), "");
	solAssert(to.calldataEncodedSize() == 32, "");
	string functionName =
		"abi_encode_" +
		_from.identifier() +
		"_to_" +
		to.identifier() +
		(_encodeAsLibraryTypes ? "_library" : "");
	return createFunction(functionName, [&]() {
		solAssert(!to.isDynamicallyEncoded(), "");

		Whiskers templ(R"(
			function <functionName>(value, pos) {
				mstore(pos, <cleanupConvert>)
			}
		)");
		templ("functionName", functionName);

		if (_from.dataStoredIn(DataLocation::Storage) && to.isValueType())
		{
			// special case: convert storage reference type to value type - this is only
			// possible for library calls where we just forward the storage reference
			solAssert(_encodeAsLibraryTypes, "");
			solAssert(to == IntegerType(256), "");
			templ("cleanupConvert", "value");
		}
		else
		{
			if (_from == to)
				templ("cleanupConvert", cleanupFunction(_from) + "(value)");
			else
				templ("cleanupConvert", conversionFunction(_from, to) + "(value)");
		}
		return templ.render();
	});
}

string ABIFunctions::abiEncodingFunctionCalldataArray(
	Type const& _from,
	Type const& _to,
	bool _encodeAsLibraryTypes
)
{
	solAssert(_to.isDynamicallySized(), "");
	solAssert(_from.category() == Type::Category::Array, "Unknown dynamic type.");
	solAssert(_to.category() == Type::Category::Array, "Unknown dynamic type.");
	auto const& fromArrayType = dynamic_cast<ArrayType const&>(_from);
	auto const& toArrayType = dynamic_cast<ArrayType const&>(_to);

	solAssert(fromArrayType.location() == DataLocation::CallData, "");

	solAssert(
		*fromArrayType.copyForLocation(DataLocation::Memory, true) ==
		*toArrayType.copyForLocation(DataLocation::Memory, true),
		""
	);

	string functionName =
		"abi_encode_" +
		_from.identifier() +
		"_to_" +
		_to.identifier() +
		(_encodeAsLibraryTypes ? "_library" : "");
	return createFunction(functionName, [&]() {
		solUnimplementedAssert(fromArrayType.isByteArray(), "");
		// TODO if this is not a byte array, we might just copy byte-by-byte anyway,
		// because the encoding is position-independent, but we have to check that.
		Whiskers templ(R"(
			function <functionName>(start, length, pos) -> end {
				<storeLength> // might update pos
				<copyFun>(start, pos, length)
				end := add(pos, <roundUpFun>(length))
			}
		)");
		templ("storeLength", _to.isDynamicallySized() ? "mstore(pos, length) pos := add(pos, 0x20)" : "");
		templ("functionName", functionName);
		templ("copyFun", copyToMemoryFunction(true));
		templ("roundUpFun", roundUpFunction());
		return templ.render();
	});
}

string ABIFunctions::abiEncodingFunctionSimpleArray(
	ArrayType const& _from,
	ArrayType const& _to,
	bool _encodeAsLibraryTypes
)
{
	string functionName =
		"abi_encode_" +
		_from.identifier() +
		"_to_" +
		_to.identifier() +
		(_encodeAsLibraryTypes ? "_library" : "");

	solAssert(_from.isDynamicallySized() == _to.isDynamicallySized(), "");
	solAssert(_from.length() == _to.length(), "");
	solAssert(_from.dataStoredIn(DataLocation::Memory) || _from.dataStoredIn(DataLocation::Storage), "");
	solAssert(!_from.isByteArray(), "");
	solAssert(_from.dataStoredIn(DataLocation::Memory) || _from.baseType()->storageBytes() > 16, "");

	return createFunction(functionName, [&]() {
		bool dynamic = _to.isDynamicallyEncoded();
		bool dynamicBase = _to.baseType()->isDynamicallyEncoded();
		bool inMemory = _from.dataStoredIn(DataLocation::Memory);
		Whiskers templ(
			dynamicBase ?
			R"(
				function <functionName>(value, pos) <return> {
					let length := <lengthFun>(value)
					<storeLength> // might update pos
					let headStart := pos
					let tail := add(pos, mul(length, 0x20))
					let srcPtr := <dataAreaFun>(value)
					for { let i := 0 } lt(i, length) { i := add(i, 1) }
					{
						mstore(pos, sub(tail, headStart))
						tail := <encodeToMemoryFun>(<arrayElementAccess>(srcPtr), tail)
						srcPtr := <nextArrayElement>(srcPtr)
						pos := add(pos, <elementEncodedSize>)
					}
					pos := tail
					<assignEnd>
				}
			)" :
			R"(
				function <functionName>(value, pos) <return> {
					let length := <lengthFun>(value)
					<storeLength> // might update pos
					let srcPtr := <dataAreaFun>(value)
					for { let i := 0 } lt(i, length) { i := add(i, 1) }
					{
						<encodeToMemoryFun>(<arrayElementAccess>(srcPtr), pos)
						srcPtr := <nextArrayElement>(srcPtr)
						pos := add(pos, <elementEncodedSize>)
					}
					<assignEnd>
				}
			)"
		);
		templ("functionName", functionName);
		templ("return", dynamic ? " -> end " : "");
		templ("assignEnd", dynamic ? "end := pos" : "");
		templ("lengthFun", arrayLengthFunction(_from));
		if (_to.isDynamicallySized())
			templ("storeLength", "mstore(pos, length) pos := add(pos, 0x20)");
		else
			templ("storeLength", "");
		templ("dataAreaFun", arrayDataAreaFunction(_from));
		templ("elementEncodedSize", toCompactHexWithPrefix(_to.baseType()->calldataEncodedSize()));
		templ("encodeToMemoryFun", abiEncodingFunction(
			*_from.baseType(),
			*_to.baseType(),
			_encodeAsLibraryTypes,
			true
		));
		templ("arrayElementAccess", inMemory ? "mload" : "sload");
		templ("nextArrayElement", nextArrayElementFunction(_from));
		return templ.render();
	});
}

string ABIFunctions::abiEncodingFunctionMemoryByteArray(
	ArrayType const& _from,
	ArrayType const& _to,
	bool _encodeAsLibraryTypes
)
{
	string functionName =
		"abi_encode_" +
		_from.identifier() +
		"_to_" +
		_to.identifier() +
		(_encodeAsLibraryTypes ? "_library" : "");

	solAssert(_from.isDynamicallySized() == _to.isDynamicallySized(), "");
	solAssert(_from.length() == _to.length(), "");
	solAssert(_from.dataStoredIn(DataLocation::Memory), "");
	solAssert(_from.isByteArray(), "");

	return createFunction(functionName, [&]() {
		solAssert(_to.isByteArray(), "");
		Whiskers templ(R"(
			function <functionName>(value, pos) -> end {
				let length := <lengthFun>(value)
				mstore(pos, length)
				<copyFun>(add(value, 0x20), add(pos, 0x20), length)
				end := add(add(pos, 0x20), <roundUpFun>(length))
			}
		)");
		templ("functionName", functionName);
		templ("lengthFun", arrayLengthFunction(_from));
		templ("copyFun", copyToMemoryFunction(false));
		templ("roundUpFun", roundUpFunction());
		return templ.render();
	});
}

string ABIFunctions::abiEncodingFunctionCompactStorageArray(
	ArrayType const& _from,
	ArrayType const& _to,
	bool _encodeAsLibraryTypes
)
{
	string functionName =
		"abi_encode_" +
		_from.identifier() +
		"_to_" +
		_to.identifier() +
		(_encodeAsLibraryTypes ? "_library" : "");

	solAssert(_from.isDynamicallySized() == _to.isDynamicallySized(), "");
	solAssert(_from.length() == _to.length(), "");
	solAssert(_from.dataStoredIn(DataLocation::Storage), "");

	return createFunction(functionName, [&]() {
		if (_from.isByteArray())
		{
			solAssert(_to.isByteArray(), "");
			Whiskers templ(R"(
				function <functionName>(value, pos) -> ret {
					let slotValue := sload(value)
					switch and(slotValue, 1)
					case 0 {
						// short byte array
						let length := and(div(slotValue, 2), 0x7f)
						mstore(pos, length)
						mstore(add(pos, 0x20), and(slotValue, not(0xff)))
						ret := add(pos, 0x40)
					}
					case 1 {
						// long byte array
						let length := div(slotValue, 2)
						mstore(pos, length)
						pos := add(pos, 0x20)
						let dataPos := <arrayDataSlot>(value)
						let i := 0
						for { } lt(i, length) { i := add(i, 0x20) } {
							mstore(add(pos, i), sload(dataPos))
							dataPos := add(dataPos, 1)
						}
						ret := add(pos, i)
					}
				}
			)");
			templ("functionName", functionName);
			templ("arrayDataSlot", arrayDataAreaFunction(_from));
			return templ.render();
		}
		else
		{
			// Multiple items per slot
			solAssert(_from.baseType()->storageBytes() <= 16, "");
			solAssert(!_from.baseType()->isDynamicallyEncoded(), "");
			solAssert(_from.baseType()->isValueType(), "");
			bool dynamic = _to.isDynamicallyEncoded();
			size_t storageBytes = _from.baseType()->storageBytes();
			size_t itemsPerSlot = 32 / storageBytes;
			// This always writes full slot contents to memory, which might be
			// more than desired, i.e. it writes beyond the end of memory.
			Whiskers templ(
				R"(
					function <functionName>(value, pos) <return> {
						let length := <lengthFun>(value)
						<storeLength> // might update pos
						let originalPos := pos
						let srcPtr := <dataArea>(value)
						for { let i := 0 } lt(i, length) { i := add(i, <itemsPerSlot>) }
						{
							let data := sload(srcPtr)
							<#items>
								<encodeToMemoryFun>(<shiftRightFun>(data), pos)
								pos := add(pos, <elementEncodedSize>)
							</items>
							srcPtr := add(srcPtr, 1)
						}
						pos := add(originalPos, mul(length, <elementEncodedSize>))
						<assignEnd>
					}
				)"
			);
			templ("functionName", functionName);
			templ("return", dynamic ? " -> end " : "");
			templ("assignEnd", dynamic ? "end := pos" : "");
			templ("lengthFun", arrayLengthFunction(_from));
			if (_to.isDynamicallySized())
				templ("storeLength", "mstore(pos, length) pos := add(pos, 0x20)");
			else
				templ("storeLength", "");
			templ("dataArea", arrayDataAreaFunction(_from));
			templ("itemsPerSlot", to_string(itemsPerSlot));
			string elementEncodedSize = toCompactHexWithPrefix(_to.baseType()->calldataEncodedSize());
			templ("elementEncodedSize", elementEncodedSize);
			string encodeToMemoryFun = abiEncodingFunction(
				*_from.baseType(),
				*_to.baseType(),
				_encodeAsLibraryTypes,
				true
			);
			templ("encodeToMemoryFun", encodeToMemoryFun);
			std::vector<std::map<std::string, std::string>> items(itemsPerSlot);
			for (size_t i = 0; i < itemsPerSlot; ++i)
				items[i]["shiftRightFun"] = shiftRightFunction(i * storageBytes * 8, false);
			templ("items", items);
			return templ.render();
		}
	});
}

string ABIFunctions::abiEncodingFunctionStringLiteral(
	Type const& _from,
	Type const& _to,
	bool _encodeAsLibraryTypes
)
{
	solAssert(_from.category() == Type::Category::StringLiteral, "");

	string functionName =
		"abi_encode_" +
		_from.identifier() +
		"_to_" +
		_to.identifier() +
		(_encodeAsLibraryTypes ? "_library" : "");
	return createFunction(functionName, [&]() {
		auto const& strType = dynamic_cast<StringLiteralType const&>(_from);
		string const& value = strType.value();
		solAssert(_from.sizeOnStack() == 0, "");

		if (_to.isDynamicallySized())
		{
			Whiskers templ(R"(
				function <functionName>(pos) -> end {
					mstore(pos, <length>)
					<#word>
						mstore(add(pos, <offset>), <wordValue>)
					</word>
					end := add(pos, <overallSize>)
				}
			)");
			templ("functionName", functionName);

			// TODO this can make use of CODECOPY for large strings once we have that in JULIA
			size_t words = (value.size() + 31) / 32;
			templ("overallSize", to_string(32 + words * 32));
			templ("length", to_string(value.size()));
			vector<map<string, string>> wordParams(words);
			for (size_t i = 0; i < words; ++i)
			{
				wordParams[i]["offset"] = to_string(32 + i * 32);
				wordParams[i]["wordValue"] = "0x" + h256(value.substr(32 * i, 32), h256::AlignLeft).hex();
			}
			templ("word", wordParams);
			return templ.render();
		}
		else
		{
			solAssert(_to.category() == Type::Category::FixedBytes, "");
			solAssert(value.size() <= 32, "");
			Whiskers templ(R"(
				function <functionName>(pos) {
					mstore(pos, <wordValue>)
				}
			)");
			templ("functionName", functionName);
			templ("wordValue", "0x" + h256(value, h256::AlignLeft).hex());
			return templ.render();
		}
	});
}

string ABIFunctions::abiEncodingFunctionFunctionType(
	FunctionType const& _from,
	Type const& _to,
	bool _encodeAsLibraryTypes,
	bool _compacted
)
{
	solAssert(_from.kind() == FunctionType::Kind::External, "");
	solAssert(_from == _to, "");

	string functionName =
		"abi_encode_" +
		_from.identifier() +
		"_to_" +
		_to.identifier() +
		(_compacted ? "_compacted" : "") +
		(_encodeAsLibraryTypes ? "_library" : "");

	if (_compacted)
	{
		return createFunction(functionName, [&]() {
			return Whiskers(R"(
				function <functionName>(addr_and_function_id, pos) {
					mstore(pos, <cleanExtFun>(addr_and_function_id))
				}
			)")
			("functionName", functionName)
			("cleanExtFun", cleanupCombinedExternalFunctionIdFunction())
			.render();
		});
	}
	else
	{
		return createFunction(functionName, [&]() {
			return Whiskers(R"(
				function <functionName>(addr, function_id, pos) {
					mstore(pos, <combineExtFun>(addr, function_id))
				}
			)")
			("functionName", functionName)
			("combineExtFun", combineExternalFunctionIdFunction())
			.render();
		});
	}
}

string ABIFunctions::copyToMemoryFunction(bool _fromCalldata)
{
	string functionName = "copy_" + string(_fromCalldata ? "calldata" : "memory") + "_to_memory";
	return createFunction(functionName, [&]() {
		if (_fromCalldata)
		{
			return Whiskers(R"(
				function <functionName>(src, dst, length) {
					calldatacopy(dst, src, length)
					// clear end
					mstore(add(dst, length), 0)
				}
			)")
			("functionName", functionName)
			.render();
		}
		else
		{
			return Whiskers(R"(
				function <functionName>(src, dst, length) {
					let i := 0
					for { } lt(i, length) { i := add(i, 32) }
					{
						mstore(add(dst, i), mload(add(src, i)))
					}
					switch eq(i, length)
					case 0 {
						// clear end
						mstore(add(dst, length), 0)
					}
				}
			)")
			("functionName", functionName)
			.render();
		}
	});
}

string ABIFunctions::shiftLeftFunction(size_t _numBits)
{
	string functionName = "shift_left_" + to_string(_numBits);
	return createFunction(functionName, [&]() {
		solAssert(_numBits < 256, "");
		return
			Whiskers(R"(function <functionName>(value) -> newValue {
				newValue := mul(value, <multiplier>)
			})")
			("functionName", functionName)
			("multiplier", toCompactHexWithPrefix(u256(1) << _numBits))
			.render();
	});
}

string ABIFunctions::shiftRightFunction(size_t _numBits, bool _signed)
{
	string functionName = "shift_right_" + to_string(_numBits) + (_signed ? "_signed" : "_unsigned");
	return createFunction(functionName, [&]() {
		solAssert(_numBits < 256, "");
		return
			Whiskers(R"(function <functionName>(value) -> newValue {
				newValue := <div>(value, <multiplier>)
			})")
			("functionName", functionName)
			("div", _signed ? "sdiv" : "div")
			("multiplier", toCompactHexWithPrefix(u256(1) << _numBits))
			.render();
	});
}

string ABIFunctions::roundUpFunction()
{
	string functionName = "round_up_to_mul_of_32";
	return createFunction(functionName, [&]() {
		return
			Whiskers(R"(function <functionName>(value) -> result {
				result := and(add(value, 31), not(31))
			})")
			("functionName", functionName)
			.render();
	});
}

string ABIFunctions::arrayLengthFunction(ArrayType const& _type)
{
	string functionName = "array_length_" + _type.identifier();
	return createFunction(functionName, [&]() {
		Whiskers w(R"(
			function <functionName>(value) -> length {
				<body>
			}
		)");
		w("functionName", functionName);
		string body;
		if (!_type.isDynamicallySized())
			body = "length := " + toCompactHexWithPrefix(_type.length());
		else
		{
			switch (_type.location())
			{
			case DataLocation::CallData:
				solAssert(false, "called regular array length function on calldata array");
				break;
			case DataLocation::Memory:
				body = "length := mload(value)";
				break;
			case DataLocation::Storage:
				if (_type.isByteArray())
				{
					// Retrieve length both for in-place strings and off-place strings:
					// Computes (x & (0x100 * (ISZERO (x & 1)) - 1)) / 2
					// i.e. for short strings (x & 1 == 0) it does (x & 0xff) / 2 and for long strings it
					// computes (x & (-1)) / 2, which is equivalent to just x / 2.
					body = R"(
						length := sload(value)
						let mask := sub(mul(0x100, iszero(and(length, 1))), 1)
						length := div(and(length, mask), 2)
					)";
				}
				else
					body = "length := sload(value)";
				break;
			}
		}
		solAssert(!body.empty(), "");
		w("body", body);
		return w.render();
	});
}

string ABIFunctions::arrayDataAreaFunction(ArrayType const& _type)
{
	string functionName = "array_dataslot_" + _type.identifier();
	return createFunction(functionName, [&]() {
		if (_type.dataStoredIn(DataLocation::Memory))
		{
			if (_type.isDynamicallySized())
				return Whiskers(R"(
					function <functionName>(memPtr) -> dataPtr {
						dataPtr := add(memPtr, 0x20)
					}
				)")
				("functionName", functionName)
				.render();
			else
				return Whiskers(R"(
					function <functionName>(memPtr) -> dataPtr {
						dataPtr := memPtr
					}
				)")
				("functionName", functionName)
				.render();
		}
		else if (_type.dataStoredIn(DataLocation::Storage))
		{
			if (_type.isDynamicallySized())
			{
				Whiskers w(R"(
					function <functionName>(slot) -> dataSlot {
						mstore(0, slot)
						dataSlot := keccak256(0, 0x20)
					}
				)");
				w("functionName", functionName);
				return w.render();
			}
			else
			{
				Whiskers w(R"(
					function <functionName>(slot) -> dataSlot {
						dataSlot := slot
					}
				)");
				w("functionName", functionName);
				return w.render();
			}
		}
		else
		{
			// Not used for calldata
			solAssert(false, "");
		}
	});
}

string ABIFunctions::nextArrayElementFunction(ArrayType const& _type)
{
	solAssert(!_type.isByteArray(), "");
	solAssert(
		_type.location() == DataLocation::Memory ||
		_type.location() == DataLocation::Storage,
		""
	);
	solAssert(
		_type.location() == DataLocation::Memory ||
		_type.baseType()->storageBytes() > 16,
		""
	);
	string functionName = "array_nextElement_" + _type.identifier();
	return createFunction(functionName, [&]() {
		if (_type.location() == DataLocation::Memory)
			return Whiskers(R"(
				function <functionName>(memPtr) -> nextPtr {
					nextPtr := add(memPtr, 0x20)
				}
			)")
			("functionName", functionName)
			.render();
		else if (_type.location() == DataLocation::Storage)
			return Whiskers(R"(
				function <functionName>(slot) -> nextSlot {
					nextSlot := add(slot, 1)
				}
			)")
			("functionName", functionName)
			.render();
		else
			solAssert(false, "");
	});
}

string ABIFunctions::createFunction(string const& _name, function<string ()> const& _creator)
{
	if (!m_requestedFunctions.count(_name))
	{
		auto fun = _creator();
		solAssert(!fun.empty(), "");
		m_requestedFunctions[_name] = fun;
	}
	return _name;
}

size_t ABIFunctions::headSize(TypePointers const& _targetTypes)
{
	size_t headSize = 0;
	for (auto const& t: _targetTypes)
	{
		if (t->isDynamicallyEncoded())
			headSize += 0x20;
		else
		{
			solAssert(t->calldataEncodedSize() > 0, "");
			headSize += t->calldataEncodedSize();
		}
	}

	return headSize;
}
