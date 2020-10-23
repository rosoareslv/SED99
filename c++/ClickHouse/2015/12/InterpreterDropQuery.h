#pragma once

#include <DB/Storages/IStorage.h>
#include <DB/Interpreters/Context.h>
#include <DB/Interpreters/IInterpreter.h>


namespace DB
{


/** Позволяет удалить таблицу вместе со всеми данными (DROP), или удалить информацию о таблице из сервера (DETACH).
  */
class InterpreterDropQuery : public IInterpreter
{
public:
	InterpreterDropQuery(ASTPtr query_ptr_, Context & context_);

	/// Удаляет таблицу.
	BlockIO execute() override;

	/// Удаляет таблицу, уже отцепленную от контекста (Context::detach).
	static void dropDetachedTable(String database_name, StoragePtr table, Context & context);

private:
	ASTPtr query_ptr;
	Context context;
};


}
