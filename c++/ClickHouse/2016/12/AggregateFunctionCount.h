#pragma once

#include <DB/IO/VarInt.h>

#include <DB/DataTypes/DataTypesNumberFixed.h>

#include <DB/AggregateFunctions/INullaryAggregateFunction.h>


namespace DB
{

struct AggregateFunctionCountData
{
	UInt64 count;

	AggregateFunctionCountData() : count(0) {}
};


/// Просто считает, сколько раз её вызвали
class AggregateFunctionCount final : public INullaryAggregateFunction<AggregateFunctionCountData, AggregateFunctionCount>
{
public:
	String getName() const override { return "count"; }

	DataTypePtr getReturnType() const override
	{
		return std::make_shared<DataTypeUInt64>();
	}


	void addImpl(AggregateDataPtr place) const
	{
		++data(place).count;
	}

	void merge(AggregateDataPtr place, ConstAggregateDataPtr rhs, Arena * arena) const override
	{
		data(place).count += data(rhs).count;
	}

	void serialize(ConstAggregateDataPtr place, WriteBuffer & buf) const override
	{
		writeVarUInt(data(place).count, buf);
	}

	void deserialize(AggregateDataPtr place, ReadBuffer & buf, Arena *) const override
	{
		readVarUInt(data(place).count, buf);
	}

	void insertResultInto(ConstAggregateDataPtr place, IColumn & to) const override
	{
		static_cast<ColumnUInt64 &>(to).getData().push_back(data(place).count);
	}

	/// Для оптимизации
	void addDelta(AggregateDataPtr place, UInt64 x) const
	{
		data(place).count += x;
	}
};

}
