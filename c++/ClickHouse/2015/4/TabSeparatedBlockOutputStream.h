#pragma once

#include <DB/DataStreams/IBlockOutputStream.h>


namespace DB
{

/** Пишет данные в tab-separated файл, но по столбцам, блоками.
  * Блоки разделены двойным переводом строки.
  * На каждой строке блока - данные одного столбца.
  */
class TabSeparatedBlockOutputStream : public IBlockOutputStream
{
public:
	TabSeparatedBlockOutputStream(WriteBuffer & ostr_) : ostr(ostr_) {}

	void write(const Block & block) override;
	void flush() override { ostr.next(); }

private:
	WriteBuffer & ostr;
};

}
