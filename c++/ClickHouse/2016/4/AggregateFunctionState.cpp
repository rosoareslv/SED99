#include <DB/AggregateFunctions/AggregateFunctionState.h>

namespace DB
{

AggregateFunctionPtr createAggregateFunctionState(AggregateFunctionPtr & nested)
{
	return new AggregateFunctionState(nested);
}

}
