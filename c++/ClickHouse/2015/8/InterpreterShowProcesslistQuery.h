#pragma once

#include <DB/IO/ReadBufferFromString.h>

#include <DB/Interpreters/executeQuery.h>
#include <DB/Interpreters/IInterpreter.h>

#include <DB/Parsers/ASTQueryWithOutput.h>
#include <DB/Parsers/ASTIdentifier.h>


namespace DB
{


/** Вернуть список запросов, исполняющихся прямо сейчас.
  */
class InterpreterShowProcesslistQuery : public IInterpreter
{
public:
	InterpreterShowProcesslistQuery(ASTPtr query_ptr_, Context & context_)
		: query_ptr(query_ptr_), context(context_) {}

	BlockIO execute() override
	{
		return executeQuery(getRewrittenQuery(), context, true);
	}

private:
	ASTPtr query_ptr;
	Context context;

	String getRewrittenQuery()
	{
		const ASTQueryWithOutput & query = dynamic_cast<const ASTQueryWithOutput &>(*query_ptr);

		std::stringstream rewritten_query;
		rewritten_query << "SELECT * FROM system.processes";

		if (query.format)
			rewritten_query << " FORMAT " << typeid_cast<const ASTIdentifier &>(*query.format).name;

		return rewritten_query.str();
	}
};


}
