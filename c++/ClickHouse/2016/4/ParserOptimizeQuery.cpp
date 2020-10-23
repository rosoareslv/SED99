#include <DB/Parsers/ASTIdentifier.h>
#include <DB/Parsers/ASTOptimizeQuery.h>

#include <DB/Parsers/CommonParsers.h>
#include <DB/Parsers/ParserOptimizeQuery.h>


namespace DB
{


bool ParserOptimizeQuery::parseImpl(Pos & pos, Pos end, ASTPtr & node, Pos & max_parsed_pos, Expected & expected)
{
	Pos begin = pos;

	ParserWhiteSpaceOrComments ws;
	ParserString s_optimize("OPTIMIZE", true, true);
	ParserString s_table("TABLE", true, true);
	ParserString s_dot(".");
	ParserIdentifier name_p;

	ASTPtr database;
	ASTPtr table;

	ws.ignore(pos, end);

	if (!s_optimize.ignore(pos, end, max_parsed_pos, expected))
		return false;

	ws.ignore(pos, end);

	if (!s_table.ignore(pos, end, max_parsed_pos, expected))
		return false;

	ws.ignore(pos, end);

	if (!name_p.parse(pos, end, table, max_parsed_pos, expected))
		return false;

	ws.ignore(pos, end);

	if (s_dot.ignore(pos, end, max_parsed_pos, expected))
	{
		database = table;
		if (!name_p.parse(pos, end, table, max_parsed_pos, expected))
			return false;

		ws.ignore(pos, end);
	}

	ws.ignore(pos, end);

	ASTOptimizeQuery * query = new ASTOptimizeQuery(StringRange(begin, pos));
	node = query;

	if (database)
		query->database = typeid_cast<ASTIdentifier &>(*database).name;
	if (table)
		query->table = typeid_cast<ASTIdentifier &>(*table).name;

	return true;
}


}
