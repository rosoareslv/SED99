#include <DB/DataStreams/BinaryRowInputStream.h>


namespace DB
{

BinaryRowInputStream::BinaryRowInputStream(ReadBuffer & istr_, const Block & sample_)
	: istr(istr_), sample(sample_)
{
	size_t columns = sample.columns();
	data_types.resize(columns);
	for (size_t i = 0; i < columns; ++i)
		data_types[i] = sample.getByPosition(i).type;
}


bool BinaryRowInputStream::read(Row & row)
{
	size_t size = data_types.size();
	row.resize(size);
		
	for (size_t i = 0; i < size; ++i)
	{
		if (i == 0 && istr.eof())
		{
			row.clear();
			return false;
		}

		data_types[i]->deserializeBinary(row[i], istr);
	}

	return true;
}

}
