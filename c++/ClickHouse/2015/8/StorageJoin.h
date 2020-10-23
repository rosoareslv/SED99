#pragma once

#include <DB/Storages/StorageSet.h>
#include <DB/Interpreters/Join.h>


namespace DB
{

/** Позволяет сохранить состояние для последующего использования в правой части JOIN.
  * При вставке в таблицу, данные будут вставлены в состояние,
  *  а также записаны в файл-бэкап, для восстановления после перезапуска.
  * Чтение из таблицы напрямую невозможно - возможно лишь указание в правой части JOIN.
  *
  * При использовании, JOIN должен быть соответствующего типа (ANY|ALL LEFT|INNER ...).
  */
class StorageJoin : public StorageSetOrJoinBase
{
public:
	static StoragePtr create(
		const String & path_,
		const String & name_,
		const Names & key_names_,
		ASTJoin::Kind kind_, ASTJoin::Strictness strictness_,
		NamesAndTypesListPtr columns_,
		const NamesAndTypesList & materialized_columns_,
		const NamesAndTypesList & alias_columns_,
		const ColumnDefaults & column_defaults_)
	{
		return (new StorageJoin{
			path_, name_,
			key_names_, kind_, strictness_,
			columns_,
			materialized_columns_, alias_columns_, column_defaults_})->thisPtr();
	}

	String getName() const override { return "Join"; }

	/// Получить доступ к внутренностям.
	JoinPtr & getJoin() { return join; }

	/// Убедиться, что структура данных подходит для осуществления JOIN такого типа.
	void assertCompatible(ASTJoin::Kind kind_, ASTJoin::Strictness strictness_) const;

private:
	const Names & key_names;
	ASTJoin::Kind kind;					/// LEFT | INNER ...
	ASTJoin::Strictness strictness;		/// ANY | ALL

	JoinPtr join;

	StorageJoin(
		const String & path_,
		const String & name_,
		const Names & key_names_,
		ASTJoin::Kind kind_, ASTJoin::Strictness strictness_,
		NamesAndTypesListPtr columns_,
		const NamesAndTypesList & materialized_columns_,
		const NamesAndTypesList & alias_columns_,
		const ColumnDefaults & column_defaults_);

	void insertBlock(const Block & block) override { join->insertFromBlock(block); }
	size_t getSize() const override { return join->getTotalRowCount(); };
};

}
