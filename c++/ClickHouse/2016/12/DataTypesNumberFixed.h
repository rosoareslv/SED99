#pragma once

#include <DB/Columns/ColumnsNumber.h>
#include <DB/DataTypes/IDataTypeNumberFixed.h>
#include <DB/DataTypes/DataTypeNull.h>


namespace DB
{

/** Типы столбцов для чисел фиксированной ширины. */

template <typename T>
struct DataTypeFromFieldType;

#define DEFINE_DATA_TYPE_NUMBER_FIXED(TYPE) 										\
	class DataType ## TYPE final : public IDataTypeNumberFixed<TYPE, Column ## TYPE> \
	{																				\
	public:																			\
		std::string getName() const override { return #TYPE; }						\
		DataTypePtr clone() const override { return std::make_shared<DataType ## TYPE>(); } \
	};																				\
																					\
	template <> struct DataTypeFromFieldType<TYPE>									\
	{																				\
		using Type = DataType ## TYPE;												\
	};

DEFINE_DATA_TYPE_NUMBER_FIXED(UInt8);
DEFINE_DATA_TYPE_NUMBER_FIXED(UInt16);
DEFINE_DATA_TYPE_NUMBER_FIXED(UInt32);
DEFINE_DATA_TYPE_NUMBER_FIXED(UInt64);

DEFINE_DATA_TYPE_NUMBER_FIXED(Int8);
DEFINE_DATA_TYPE_NUMBER_FIXED(Int16);
DEFINE_DATA_TYPE_NUMBER_FIXED(Int32);
DEFINE_DATA_TYPE_NUMBER_FIXED(Int64);

DEFINE_DATA_TYPE_NUMBER_FIXED(Float32);
DEFINE_DATA_TYPE_NUMBER_FIXED(Float64);

/// The following type is not a real column data type. It is used in the multiIf
/// function implementation for argument type checking.
class DataTypeVoid : public IDataTypeNumberFixed<void, void>
{
public:
	DataTypeVoid() = default;
	std::string getName() const override { return "void"; }
	DataTypePtr clone() const override { return std::make_shared<DataTypeVoid>(); }
};

template <> struct DataTypeFromFieldType<void>
{
	using Type = DataTypeVoid;
};

template <> struct DataTypeFromFieldType<Null>
{
	using Type = DataTypeNull;
};

}
