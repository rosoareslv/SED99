#include <DB/Core/FieldVisitors.h>

#include <DB/IO/WriteHelpers.h>
#include <DB/IO/ReadHelpers.h>

#include <DB/Columns/ColumnConst.h>
#include <DB/Columns/ColumnAggregateFunction.h>

#include <DB/DataTypes/DataTypeAggregateFunction.h>


namespace DB
{

using Poco::SharedPtr;


std::string DataTypeAggregateFunction::getName() const
{
	std::stringstream stream;
	stream << "AggregateFunction(" << function->getName();

	if (!parameters.empty())
	{
		stream << "(";
		for (size_t i = 0; i < parameters.size(); ++i)
		{
			if (i)
				stream << ", ";
			stream << apply_visitor(DB::FieldVisitorToString(), parameters[i]);
		}
		stream << ")";
	}

	for (DataTypes::const_iterator it = argument_types.begin(); it != argument_types.end(); ++it)
		stream << ", " << (*it)->getName();

	stream << ")";
	return stream.str();
}

void DataTypeAggregateFunction::serializeBinary(const Field & field, WriteBuffer & ostr) const
{
	const String & s = get<const String &>(field);
	writeVarUInt(s.size(), ostr);
	writeString(s, ostr);
}

void DataTypeAggregateFunction::deserializeBinary(Field & field, ReadBuffer & istr) const
{
	UInt64 size;
	readVarUInt(size, istr);
	field = String();
	String & s = get<String &>(field);
	s.resize(size);
	istr.readStrict(&s[0], size);
}

void DataTypeAggregateFunction::serializeBinary(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	function.get()->serialize(static_cast<const ColumnAggregateFunction &>(column).getData()[row_num], ostr);
}

void DataTypeAggregateFunction::deserializeBinary(IColumn & column, ReadBuffer & istr) const
{
	ColumnAggregateFunction & column_concrete = static_cast<ColumnAggregateFunction &>(column);

	size_t size_of_state = function->sizeOfData();
	AggregateDataPtr place = column_concrete.createOrGetArena().alloc(size_of_state);

	function->create(place);
	try
	{
		function->deserialize(place, istr);
	}
	catch (...)
	{
		function->destroy(place);
		throw;
	}

	column_concrete.getData().push_back(place);
}

void DataTypeAggregateFunction::serializeBinary(const IColumn & column, WriteBuffer & ostr, size_t offset, size_t limit) const
{
	const ColumnAggregateFunction & real_column = typeid_cast<const ColumnAggregateFunction &>(column);
	const ColumnAggregateFunction::Container_t & vec = real_column.getData();

	ColumnAggregateFunction::Container_t::const_iterator it = vec.begin() + offset;
	ColumnAggregateFunction::Container_t::const_iterator end = limit ? it + limit : vec.end();

	if (end > vec.end())
		end = vec.end();

	for (; it != end; ++it)
		function->serialize(*it, ostr);
}

void DataTypeAggregateFunction::deserializeBinary(IColumn & column, ReadBuffer & istr, size_t limit, double avg_value_size_hint) const
{
	ColumnAggregateFunction & real_column = typeid_cast<ColumnAggregateFunction &>(column);
	ColumnAggregateFunction::Container_t & vec = real_column.getData();

	Arena & arena = real_column.createOrGetArena();
	real_column.set(function);
	vec.reserve(vec.size() + limit);

	size_t size_of_state = function->sizeOfData();

	for (size_t i = 0; i < limit; ++i)
	{
		if (istr.eof())
			break;

		AggregateDataPtr place = arena.alloc(size_of_state);

		function->create(place);

		try
		{
			function->deserialize(place, istr);
		}
		catch (...)
		{
			function->destroy(place);
			throw;
		}

		vec.push_back(place);
	}
}

static String serializeToString(const AggregateFunctionPtr & function, const IColumn & column, size_t row_num)
{
	String res;
	WriteBufferFromString buffer(res);
	function.get()->serialize(static_cast<const ColumnAggregateFunction &>(column).getData()[row_num], buffer);
	return res;
}

static void deserializeFromString(const AggregateFunctionPtr & function, IColumn & column, const String & s)
{
	ColumnAggregateFunction & column_concrete = static_cast<ColumnAggregateFunction &>(column);

	size_t size_of_state = function->sizeOfData();
	AggregateDataPtr place = column_concrete.createOrGetArena().alloc(size_of_state);

	function->create(place);

	try
	{
		ReadBufferFromString istr(s);
		function->deserialize(place, istr);
	}
	catch (...)
	{
		function->destroy(place);
		throw;
	}

	column_concrete.getData().push_back(place);
}

void DataTypeAggregateFunction::serializeText(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	writeString(serializeToString(function, column, row_num), ostr);
}


void DataTypeAggregateFunction::serializeTextEscaped(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	writeEscapedString(serializeToString(function, column, row_num), ostr);
}


void DataTypeAggregateFunction::deserializeTextEscaped(IColumn & column, ReadBuffer & istr) const
{
	String s;
	readEscapedString(s, istr);
	deserializeFromString(function, column, s);
}


void DataTypeAggregateFunction::serializeTextQuoted(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	writeQuotedString(serializeToString(function, column, row_num), ostr);
}


void DataTypeAggregateFunction::deserializeTextQuoted(IColumn & column, ReadBuffer & istr) const
{
	String s;
	readQuotedString(s, istr);
	deserializeFromString(function, column, s);
}


void DataTypeAggregateFunction::serializeTextJSON(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	writeJSONString(serializeToString(function, column, row_num), ostr);
}


void DataTypeAggregateFunction::deserializeTextJSON(IColumn & column, ReadBuffer & istr) const
{
	String s;
	readJSONString(s, istr);
	deserializeFromString(function, column, s);
}


void DataTypeAggregateFunction::serializeTextXML(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	writeXMLString(serializeToString(function, column, row_num), ostr);
}


void DataTypeAggregateFunction::serializeTextCSV(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
{
	writeCSV(serializeToString(function, column, row_num), ostr);
}


void DataTypeAggregateFunction::deserializeTextCSV(IColumn & column, ReadBuffer & istr, const char delimiter) const
{
	String s;
	readCSV(s, istr, delimiter);
	deserializeFromString(function, column, s);
}


ColumnPtr DataTypeAggregateFunction::createColumn() const
{
	return new ColumnAggregateFunction(function);
}

ColumnPtr DataTypeAggregateFunction::createConstColumn(size_t size, const Field & field) const
{
	throw Exception("Const column with aggregate function is not supported", ErrorCodes::NOT_IMPLEMENTED);
}


}

