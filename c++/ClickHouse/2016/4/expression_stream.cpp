#include <iostream>
#include <iomanip>

#include <Poco/SharedPtr.h>
#include <Poco/Stopwatch.h>

#include <DB/IO/WriteBufferFromOStream.h>

#include <DB/Storages/System/StorageSystemNumbers.h>

#include <DB/DataStreams/LimitBlockInputStream.h>
#include <DB/DataStreams/ExpressionBlockInputStream.h>
#include <DB/DataStreams/TabSeparatedRowOutputStream.h>
#include <DB/DataStreams/BlockOutputStreamFromRowOutputStream.h>
#include <DB/DataStreams/copyData.h>

#include <DB/DataTypes/DataTypesNumberFixed.h>

#include <DB/Parsers/ParserSelectQuery.h>
#include <DB/Parsers/parseQuery.h>

#include <DB/Interpreters/ExpressionAnalyzer.h>
#include <DB/Interpreters/ExpressionActions.h>


using Poco::SharedPtr;


int main(int argc, char ** argv)
{
	using namespace DB;

	try
	{
		size_t n = argc == 2 ? parse<UInt64>(argv[1]) : 10ULL;

		std::string input = "SELECT number, number / 3, number * number";

		ParserSelectQuery parser;
		ASTPtr ast = parseQuery(parser, input.data(), input.data() + input.size(), "");

		Context context;

		ExpressionAnalyzer analyzer(ast, context, {}, {NameAndTypePair("number", new DataTypeUInt64)});
		ExpressionActionsChain chain;
		analyzer.appendSelect(chain, false);
		analyzer.appendProjectResult(chain, false);
		chain.finalize();
		ExpressionActionsPtr expression = chain.getLastActions();

		StoragePtr table = StorageSystemNumbers::create("Numbers");

		Names column_names;
		column_names.push_back("number");

		QueryProcessingStage::Enum stage;

		Poco::SharedPtr<IBlockInputStream> in;
		in = table->read(column_names, 0, context, Settings(), stage)[0];
		in = new ExpressionBlockInputStream(in, expression);
		in = new LimitBlockInputStream(in, 10, std::max(static_cast<Int64>(0), static_cast<Int64>(n) - 10));

		WriteBufferFromOStream out1(std::cout);
		RowOutputStreamPtr out2 = new TabSeparatedRowOutputStream(out1, expression->getSampleBlock());
		BlockOutputStreamFromRowOutputStream out(out2);

		{
			Poco::Stopwatch stopwatch;
			stopwatch.start();

			copyData(*in, out);

			stopwatch.stop();
			std::cout << std::fixed << std::setprecision(2)
				<< "Elapsed " << stopwatch.elapsed() / 1000000.0 << " sec."
				<< ", " << n * 1000000 / stopwatch.elapsed() << " rows/sec."
				<< std::endl;
		}
	}
	catch (const Exception & e)
	{
		std::cerr << e.what() << ", " << e.displayText() << std::endl;
		return 1;
	}

	return 0;
}
