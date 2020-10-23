#pragma once

#include <DB/DataTypes/IDataTypeDummy.h>


namespace DB
{

/** Тип данных, соответствующий множеству значений в секции IN.
  * Используется только как промежуточный вариант при вычислении выражений.
  */
class DataTypeSet final : public IDataTypeDummy
{
public:
	std::string getName() const override { return "Set"; }
	SharedPtr<IDataType> clone() const override { return new DataTypeSet(); }
};

}

