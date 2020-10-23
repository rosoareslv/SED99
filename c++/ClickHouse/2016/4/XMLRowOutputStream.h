#pragma once

#include <DB/Core/Block.h>
#include <DB/IO/WriteBuffer.h>
#include <DB/DataStreams/IRowOutputStream.h>


namespace DB
{

/** Поток для вывода данных в формате XML.
  */
class XMLRowOutputStream : public IRowOutputStream
{
public:
	XMLRowOutputStream(WriteBuffer & ostr_, const Block & sample_);

	void writeField(const IColumn & column, const IDataType & type, size_t row_num) override;
	void writeRowStartDelimiter() override;
	void writeRowEndDelimiter() override;
	void writePrefix() override;
	void writeSuffix() override;

	void flush() override
	{
		ostr->next();

		if (validating_ostr)
			dst_ostr.next();
	}

	void setRowsBeforeLimit(size_t rows_before_limit_) override
	{
		applied_limit = true;
		rows_before_limit = rows_before_limit_;
	}

	void setTotals(const Block & totals_) override { totals = totals_; }
	void setExtremes(const Block & extremes_) override { extremes = extremes_; }

	String getContentType() const override { return "application/xml; charset=UTF-8"; }

protected:

	void writeRowsBeforeLimitAtLeast();
	virtual void writeTotals();
	virtual void writeExtremes();

	WriteBuffer & dst_ostr;
	std::unique_ptr<WriteBuffer> validating_ostr;	/// Валидирует UTF-8 последовательности.
	WriteBuffer * ostr;

	size_t field_number = 0;
	size_t row_count = 0;
	bool applied_limit = false;
	size_t rows_before_limit = 0;
	NamesAndTypes fields;
	Names field_tag_names;
	Block totals;
	Block extremes;
};

}

