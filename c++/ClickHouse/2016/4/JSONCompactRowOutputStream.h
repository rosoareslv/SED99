#pragma once

#include <Poco/SharedPtr.h>

#include <DB/Core/Block.h>
#include <DB/IO/WriteBuffer.h>
#include <DB/IO/WriteBufferValidUTF8.h>
#include <DB/DataStreams/JSONRowOutputStream.h>


namespace DB
{

/** Поток для вывода данных в формате JSONCompact.
  */
class JSONCompactRowOutputStream : public JSONRowOutputStream
{
public:
	JSONCompactRowOutputStream(WriteBuffer & ostr_, const Block & sample_);

	void writeField(const IColumn & column, const IDataType & type, size_t row_num) override;
	void writeFieldDelimiter() override;
	void writeRowStartDelimiter() override;
	void writeRowEndDelimiter() override;

protected:
	void writeTotals() override;
	void writeExtremes() override;
};

}
