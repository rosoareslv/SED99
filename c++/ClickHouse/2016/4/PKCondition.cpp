#include <DB/Storages/MergeTree/PKCondition.h>
#include <DB/DataTypes/DataTypesNumberFixed.h>
#include <DB/Interpreters/ExpressionAnalyzer.h>
#include <DB/Interpreters/ExpressionActions.h>
#include <DB/Columns/ColumnSet.h>
#include <DB/Columns/ColumnTuple.h>
#include <DB/Parsers/ASTSet.h>
#include <DB/Functions/FunctionFactory.h>


namespace DB
{


/// Пример: для строки Hello\_World%... возвращает Hello_World, а для строки %test% возвращает пустую строку.
static String extractFixedPrefixFromLikePattern(const String & like_pattern)
{
	String fixed_prefix;

	const char * pos = like_pattern.data();
	const char * end = pos + like_pattern.size();
	while (pos < end)
	{
		switch (*pos)
		{
			case '%':
			case '_':
				return fixed_prefix;

			case '\\':
				++pos;
				if (pos == end)
					break;
			default:
				fixed_prefix += *pos;
				break;
		}

		++pos;
	}

	return fixed_prefix;
}


/** Для заданной строки получить минимальную строку, которая строго больше всех строк с таким префиксом,
  *  или вернуть пустую строку, если таких строк не существует.
  */
static String firstStringThatIsGreaterThanAllStringsWithPrefix(const String & prefix)
{
	/** Увеличиваем последний байт префикса на единицу. Но если он равен 255, то убираем его и увеличиваем предыдущий.
	  * Пример (для удобства, представим, что максимальное значение байта равно z):
	  * abcx -> abcy
	  * abcz -> abd
	  * zzz -> пустая строка
	  * z -> пустая строка
	  */

	String res = prefix;

	while (!res.empty() && static_cast<UInt8>(res.back()) == 255)
		res.pop_back();

	if (res.empty())
		return res;

	res.back() = static_cast<char>(1 + static_cast<UInt8>(res.back()));
	return res;
}


const PKCondition::AtomMap PKCondition::atom_map{
	{
		"notEquals",
		[] (RPNElement & out, const Field & value, ASTPtr &)
		{
			out.function = RPNElement::FUNCTION_NOT_IN_RANGE;
			out.range = Range(value);
			return true;
		}
	},
	{
		"equals",
		[] (RPNElement & out, const Field & value, ASTPtr &)
		{
			out.function = RPNElement::FUNCTION_IN_RANGE;
			out.range = Range(value);
			return true;
		}
	},
	{
		"less",
		[] (RPNElement & out, const Field & value, ASTPtr &)
		{
			out.function = RPNElement::FUNCTION_IN_RANGE;
			out.range = Range::createRightBounded(value, false);
			return true;
		}
	},
	{
		"greater",
		[] (RPNElement & out, const Field & value, ASTPtr &)
		{
			out.function = RPNElement::FUNCTION_IN_RANGE;
			out.range = Range::createLeftBounded(value, false);
			return true;
		}
	},
	{
		"lessOrEquals",
		[] (RPNElement & out, const Field & value, ASTPtr &)
		{
			out.function = RPNElement::FUNCTION_IN_RANGE;
			out.range = Range::createRightBounded(value, true);
			return true;
		}
	},
	{
		"greaterOrEquals",
		[] (RPNElement & out, const Field & value, ASTPtr &)
		{
			out.function = RPNElement::FUNCTION_IN_RANGE;
			out.range = Range::createLeftBounded(value, true);
			return true;
		}
	},
	{
		"in",
		[] (RPNElement & out, const Field & value, ASTPtr & node)
		{
			out.function = RPNElement::FUNCTION_IN_SET;
			out.in_function = node;
			return true;
		}
	},
	{
		"notIn",
		[] (RPNElement & out, const Field & value, ASTPtr & node)
		{
			out.function = RPNElement::FUNCTION_NOT_IN_SET;
			out.in_function = node;
			return true;
		}
	},
	{
		"like",
		[] (RPNElement & out, const Field & value, ASTPtr & node)
		{
			if (value.getType() != Field::Types::String)
				return false;

			String prefix = extractFixedPrefixFromLikePattern(value.get<const String &>());
			if (prefix.empty())
				return false;

			String right_bound = firstStringThatIsGreaterThanAllStringsWithPrefix(prefix);

			out.function = RPNElement::FUNCTION_IN_RANGE;
			out.range = !right_bound.empty()
				? Range(prefix, true, right_bound, false)
				: Range::createLeftBounded(prefix, true);

			return true;
		}
	}
};


inline bool Range::equals(const Field & lhs, const Field & rhs) { return apply_visitor(FieldVisitorAccurateEquals(), lhs, rhs); }
inline bool Range::less(const Field & lhs, const Field & rhs) { return apply_visitor(FieldVisitorAccurateLess(), lhs, rhs); }


Block PKCondition::getBlockWithConstants(
	const ASTPtr & query, const Context & context, const NamesAndTypesList & all_columns)
{
	Block result
	{
		{ new ColumnConstUInt8{1, 0}, new DataTypeUInt8, "_dummy" }
	};

	const auto expr_for_constant_folding = ExpressionAnalyzer{query, context, nullptr, all_columns}
		.getConstActions();

	expr_for_constant_folding->execute(result);

	return result;
}


PKCondition::PKCondition(ASTPtr & query, const Context & context, const NamesAndTypesList & all_columns, const SortDescription & sort_descr_)
	: sort_descr(sort_descr_)
{
	for (size_t i = 0; i < sort_descr.size(); ++i)
	{
		std::string name = sort_descr[i].column_name;
		if (!pk_columns.count(name))
			pk_columns[name] = i;
	}

	/** Вычисление выражений, зависящих только от констант.
	  * Чтобы индекс мог использоваться, если написано, например WHERE Date = toDate(now()).
	  */
	Block block_with_constants = getBlockWithConstants(query, context, all_columns);

	/// Преобразуем секцию WHERE в обратную польскую строку.
	ASTSelectQuery & select = typeid_cast<ASTSelectQuery &>(*query);
	if (select.where_expression)
	{
		traverseAST(select.where_expression, context, block_with_constants);

		if (select.prewhere_expression)
		{
			traverseAST(select.prewhere_expression, context, block_with_constants);
			rpn.emplace_back(RPNElement::FUNCTION_AND);
		}
	}
	else if (select.prewhere_expression)
	{
		traverseAST(select.prewhere_expression, context, block_with_constants);
	}
	else
	{
		rpn.emplace_back(RPNElement::FUNCTION_UNKNOWN);
	}
}

bool PKCondition::addCondition(const String & column, const Range & range)
{
	if (!pk_columns.count(column))
		return false;
	rpn.emplace_back(RPNElement::FUNCTION_IN_RANGE, pk_columns[column], range);
	rpn.emplace_back(RPNElement::FUNCTION_AND);
	return true;
}

/** Получить значение константного выражения.
  * Вернуть false, если выражение не константно.
  */
static bool getConstant(const ASTPtr & expr, Block & block_with_constants, Field & value)
{
	String column_name = expr->getColumnName();

	if (const ASTLiteral * lit = typeid_cast<const ASTLiteral *>(&*expr))
	{
		/// литерал
		value = lit->value;
		return true;
	}
	else if (block_with_constants.has(column_name) && block_with_constants.getByName(column_name).column->isConst())
	{
		/// выражение, вычислившееся в константу
		value = (*block_with_constants.getByName(column_name).column)[0];
		return true;
	}
	else
		return false;
}

void PKCondition::traverseAST(ASTPtr & node, const Context & context, Block & block_with_constants)
{
	RPNElement element;

	if (ASTFunction * func = typeid_cast<ASTFunction *>(&*node))
	{
		if (operatorFromAST(func, element))
		{
			auto & args = typeid_cast<ASTExpressionList &>(*func->arguments).children;
			for (size_t i = 0, size = args.size(); i < size; ++i)
			{
				traverseAST(args[i], context, block_with_constants);

				/** Первая часть условия - для корректной поддержки функций and и or произвольной арности
				  * - в этом случае добавляется n - 1 элементов (где n - количество аргументов).
				  */
				if (i != 0 || element.function == RPNElement::FUNCTION_NOT)
					rpn.push_back(element);
			}

			return;
		}
	}

	if (!atomFromAST(node, context, block_with_constants, element))
	{
		element.function = RPNElement::FUNCTION_UNKNOWN;
	}

	rpn.push_back(element);
}


bool PKCondition::isPrimaryKeyPossiblyWrappedByMonotonicFunctions(
	const ASTPtr & node,
	const Context & context,
	size_t & out_primary_key_column_num,
	RPNElement::MonotonicFunctionsChain & out_functions_chain)
{
	std::vector<const ASTFunction *> chain_not_tested_for_monotonicity;

	if (!isPrimaryKeyPossiblyWrappedByMonotonicFunctionsImpl(node, out_primary_key_column_num, chain_not_tested_for_monotonicity))
		return false;

	for (auto it = chain_not_tested_for_monotonicity.rbegin(); it != chain_not_tested_for_monotonicity.rend(); ++it)
	{
		FunctionPtr func = FunctionFactory::instance().tryGet((*it)->name, context);
		if (!func || !func->hasInformationAboutMonotonicity())
			return false;

		out_functions_chain.push_back(func);
	}

	return true;
}


bool PKCondition::isPrimaryKeyPossiblyWrappedByMonotonicFunctionsImpl(
	const ASTPtr & node,
	size_t & out_primary_key_column_num,
	std::vector<const ASTFunction *> & out_functions_chain)
{
	/** Сам по себе, столбец первичного ключа может быть функциональным выражением. Например, intHash32(UserID).
	  * Поэтому, используем полное имя выражения для поиска.
	  */
	String name = node->getColumnName();

	auto it = pk_columns.find(name);
	if (pk_columns.end() != it)
	{
		out_primary_key_column_num = it->second;
		return true;
	}

	if (const ASTFunction * func = typeid_cast<const ASTFunction *>(node.get()))
	{
		const auto & args = func->arguments->children;
		if (args.size() != 1)
			return false;

		out_functions_chain.push_back(func);

		if (!isPrimaryKeyPossiblyWrappedByMonotonicFunctionsImpl(args[0], out_primary_key_column_num, out_functions_chain))
			return false;

		return true;
	}

	return false;
}


bool PKCondition::atomFromAST(ASTPtr & node, const Context & context, Block & block_with_constants, RPNElement & out)
{
	/** Функции < > = != <= >= in notIn, у которых один агрумент константа, другой - один из столбцов первичного ключа,
	  *  либо он же, завёрнутый в цепочку возможно-монотонных функций,
	  *  либо константное выражение - число.
	  */
	Field value;
	if (const ASTFunction * func = typeid_cast<const ASTFunction *>(&*node))
	{
		const ASTs & args = typeid_cast<const ASTExpressionList &>(*func->arguments).children;

		if (args.size() != 2)
			return false;

		/// Если true, слева константа.
		bool inverted;
		size_t column;
		RPNElement::MonotonicFunctionsChain chain;

		if (getConstant(args[1], block_with_constants, value)
			&& isPrimaryKeyPossiblyWrappedByMonotonicFunctions(args[0], context, column, chain))
		{
			inverted = false;
		}
		else if (getConstant(args[0], block_with_constants, value)
			&& isPrimaryKeyPossiblyWrappedByMonotonicFunctions(args[1], context, column, chain))
		{
			inverted = true;
		}
		else if (typeid_cast<const ASTSet *>(args[1].get())
			&& isPrimaryKeyPossiblyWrappedByMonotonicFunctions(args[0], context, column, chain))
		{
			inverted = false;
		}
		else
			return false;

		std::string func_name = func->name;

		/// Заменим <const> <sign> <column> на <column> <-sign> <const>
		if (inverted)
		{
			if (func_name == "less")
				func_name = "greater";
			else if (func_name == "greater")
				func_name = "less";
			else if (func_name == "greaterOrEquals")
				func_name = "lessOrEquals";
			else if (func_name == "lessOrEquals")
				func_name = "greaterOrEquals";
			else if (func_name == "in" || func_name == "notIn" || func_name == "like")
			{
				/// const IN x не имеет смысла (в отличие от x IN const).
				return false;
			}
		}

		out.key_column = column;
		out.monotonic_functions_chain = std::move(chain);

		const auto atom_it = atom_map.find(func_name);
		if (atom_it == std::end(atom_map))
			return false;

		return atom_it->second(out, value, node);
	}
	else if (getConstant(node, block_with_constants, value))	/// Для случаев, когда написано, например, WHERE 0 AND something
	{
		if (value.getType() == Field::Types::UInt64
			|| value.getType() == Field::Types::Int64
			|| value.getType() == Field::Types::Float64)
		{
			/// Ноль во всех типах представлен в памяти так же, как в UInt64.
			out.function = value.get<UInt64>()
				? RPNElement::ALWAYS_TRUE
				: RPNElement::ALWAYS_FALSE;

			return true;
		}
	}

	return false;
}

bool PKCondition::operatorFromAST(const ASTFunction * func, RPNElement & out)
{
	/// Функции AND, OR, NOT.
	/** Также особая функция indexHint - работает так, как будто вместо вызова функции стоят просто скобки
	  * (или, то же самое - вызов функции and из одного аргумента).
	  */
	const ASTs & args = typeid_cast<const ASTExpressionList &>(*func->arguments).children;

	if (func->name == "not")
	{
		if (args.size() != 1)
			return false;

		out.function = RPNElement::FUNCTION_NOT;
	}
	else
	{
		if (func->name == "and" || func->name == "indexHint")
			out.function = RPNElement::FUNCTION_AND;
		else if (func->name == "or")
			out.function = RPNElement::FUNCTION_OR;
		else
			return false;
	}

	return true;
}

String PKCondition::toString() const
{
	String res;
	for (size_t i = 0; i < rpn.size(); ++i)
	{
		if (i)
			res += ", ";
		res += rpn[i].toString();
	}
	return res;
}


static void applyFunction(
	FunctionPtr & func,
	const DataTypePtr & arg_type, const Field & arg_value,
	DataTypePtr & res_type, Field & res_value)
{
	res_type = func->getReturnType({arg_type});

	Block block
	{
		{ arg_type->createConstColumn(1, arg_value), arg_type, "x" },
		{ nullptr, res_type, "y" }
	};

	func->execute(block, {0}, 1);

	block.getByPosition(1).column->get(0, res_value);
}


/** Индекс представляет собой значение первичного ключа каждые index_granularity строк.
  * Такое значение называется "засечкой" (mark). То есть, индекс состоит из засечек.
  *
  * Первичный ключ - это кортеж.
  * Данные отсортированы по первичному ключу в смысле лексикографического порядка над кортежами.
  *
  * Пара засечек задаёт отрезок в отношении порядка над кортежами.
  * Обозначим его так: [ x1 y1 z1 .. x2 y2 z2 ],
  *  где x1 y1 z1 - кортеж - значение первичного ключа в левой границе отрезка;
  *      x2 y2 z2 - кортеж - значение первичного ключа в правой границе отрезка.
  * В этом отрезке лежат данные, находящиеся между этими засечками.
  *
  * Или, последняя засечка задаёт открытый справа диапазон: [ a b c .. +inf )
  *
  * Множество всех возможных кортежей можно рассматривать как n-мерное пространство, где n - размер кортежа.
  * Диапазон кортежей задаёт какое-то подмножество этого пространства.
  *
  * Паралелограммами (также можно встретить термин "брус")
  *  будем называть поднможества n-мерного пространства, являющиеся прямым произведением одномерных диапазонов.
  * При этом, одномерным диапазоном может быть: точка, отрезок, интервал, полуинтервал, неограниченный слева, неограниченный справа...
  *
  * Диапазон кортежей всегда можно представить в виде объединения параллелограммов.
  * Например, диапазон [ x1 y1 .. x2 y2 ] при x1 != x2 равен объединению следующих трёх параллелограммов:
  * [x1]       x [y1 .. +inf)
  * (x1 .. x2) x (-inf .. +inf)
  * [x2]       x (-inf .. y2]
  *
  * Или, например, диапазон [ x1 y1 .. +inf ] равен объединению следующих двух параллелограммов:
  * [x1]         x [y1 .. +inf)
  * (x1 .. +inf) x (-inf .. +inf)
  * Легко заметить, что это является частным случаем варианта выше.
  *
  * Это важно, потому что нам легко проверять выполнимость условия над параллелограммом,
  *  и поэтому, выполнимость условия над диапазоном кортежей будем проверять через выполнимость условия
  *  над хотя бы одним параллелограммом, из которого этот диапазон состоит.
  */

template <typename F>
static bool forAnyParallelogram(
	size_t key_size,
	const Field * key_left,
	const Field * key_right,
	bool left_bounded,
	bool right_bounded,
	std::vector<Range> & parallelogram,
	size_t prefix_size,
	F && callback)
{
	if (!left_bounded && !right_bounded)
		return callback(parallelogram);

	if (left_bounded && right_bounded)
	{
		/// Пройдём по совпадающим элементам ключа.
		while (prefix_size < key_size)
		{
			if (key_left[prefix_size] == key_right[prefix_size])
			{
				/// Точечные диапазоны.
				parallelogram[prefix_size] = Range(key_left[prefix_size]);
				++prefix_size;
			}
			else
				break;
		}
	}

	if (prefix_size == key_size)
		return callback(parallelogram);

	if (prefix_size + 1 == key_size)
	{
		if (left_bounded && right_bounded)
			parallelogram[prefix_size] = Range(key_left[prefix_size], true, key_right[prefix_size], true);
		else if (left_bounded)
			parallelogram[prefix_size] = Range::createLeftBounded(key_left[prefix_size], true);
		else if (right_bounded)
			parallelogram[prefix_size] = Range::createRightBounded(key_right[prefix_size], true);

		return callback(parallelogram);
	}

	/// (x1 .. x2) x (-inf .. +inf)

	if (left_bounded && right_bounded)
		parallelogram[prefix_size] = Range(key_left[prefix_size], false, key_right[prefix_size], false);
	else if (left_bounded)
		parallelogram[prefix_size] = Range::createLeftBounded(key_left[prefix_size], false);
	else if (right_bounded)
		parallelogram[prefix_size] = Range::createRightBounded(key_right[prefix_size], false);

	for (size_t i = prefix_size + 1; i < key_size; ++i)
		parallelogram[i] = Range();

	if (callback(parallelogram))
		return true;

	/// [x1]       x [y1 .. +inf)

	if (left_bounded)
	{
		parallelogram[prefix_size] = Range(key_left[prefix_size]);
		if (forAnyParallelogram(key_size, key_left, key_right, true, false, parallelogram, prefix_size + 1, callback))
			return true;
	}

	/// [x2]       x (-inf .. y2]

	if (right_bounded)
	{
		parallelogram[prefix_size] = Range(key_right[prefix_size]);
		if (forAnyParallelogram(key_size, key_left, key_right, false, true, parallelogram, prefix_size + 1, callback))
			return true;
	}

	return false;
}


bool PKCondition::mayBeTrueInRange(
	size_t used_key_size,
	const Field * left_pk,
	const Field * right_pk,
	const DataTypes & data_types,
	bool right_bounded) const
{
	std::vector<Range> key_ranges(used_key_size, Range());

/*	std::cerr << "Checking for: [";
	for (size_t i = 0; i != used_key_size; ++i)
		std::cerr << (i != 0 ? ", " : "") << apply_visitor(FieldVisitorToString(), left_pk[i]);
	std::cerr << " ... ";

	if (right_bounded)
	{
		for (size_t i = 0; i != used_key_size; ++i)
			std::cerr << (i != 0 ? ", " : "") << apply_visitor(FieldVisitorToString(), right_pk[i]);
		std::cerr << "]\n";
	}
	else
		std::cerr << "+inf)\n";*/

	return forAnyParallelogram(used_key_size, left_pk, right_pk, true, right_bounded, key_ranges, 0,
		[&] (const std::vector<Range> & key_ranges)
	{
		auto res = mayBeTrueInRangeImpl(key_ranges, data_types);

/*		std::cerr << "Parallelogram: ";
		for (size_t i = 0, size = key_ranges.size(); i != size; ++i)
			std::cerr << (i != 0 ? " x " : "") << key_ranges[i].toString();
		std::cerr << ": " << res << "\n";*/

		return res;
	});
}


bool PKCondition::mayBeTrueInRangeImpl(const std::vector<Range> & key_ranges, const DataTypes & data_types) const
{
	std::vector<BoolMask> rpn_stack;
	for (size_t i = 0; i < rpn.size(); ++i)
	{
		const auto & element = rpn[i];
		if (element.function == RPNElement::FUNCTION_UNKNOWN)
		{
			rpn_stack.emplace_back(true, true);
		}
		else if (element.function == RPNElement::FUNCTION_IN_RANGE
			|| element.function == RPNElement::FUNCTION_NOT_IN_RANGE
			|| element.function == RPNElement::FUNCTION_IN_SET
			|| element.function == RPNElement::FUNCTION_NOT_IN_SET)
		{
			const Range * key_range = &key_ranges[element.key_column];

			/// Случай, когда столбец обёрнут в цепочку возможно-монотонных функций.
			Range key_range_transformed;
			bool evaluation_is_not_possible = false;
			if (!element.monotonic_functions_chain.empty())
			{
				key_range_transformed = *key_range;
				DataTypePtr current_type = data_types[element.key_column];
				for (auto & func : element.monotonic_functions_chain)
				{
					/// Проверяем монотонность каждой функции на конкретном диапазоне.
					IFunction::Monotonicity monotonicity = func->getMonotonicityForRange(
						*current_type.get(), key_range_transformed.left, key_range_transformed.right);

				/*	std::cerr << "Function " << func->getName() << " is " << (monotonicity.is_monotonic ? "" : "not ")
						<< "monotonic " << (monotonicity.is_monotonic ? (monotonicity.is_positive ? "(positive) " : "(negative) ") : "")
						<< "in range "
						<< "[" << apply_visitor(FieldVisitorToString(), key_range_transformed.left)
						<< ", " << apply_visitor(FieldVisitorToString(), key_range_transformed.right) << "]\n";*/

					if (!monotonicity.is_monotonic)
					{
						evaluation_is_not_possible = true;
						break;
					}

					/// Вычисляем функцию.
					DataTypePtr new_type;
					if (!key_range_transformed.left.isNull())
						applyFunction(func, current_type, key_range_transformed.left, new_type, key_range_transformed.left);
					if (!key_range_transformed.right.isNull())
						applyFunction(func, current_type, key_range_transformed.right, new_type, key_range_transformed.right);

					if (!new_type)
					{
						evaluation_is_not_possible = true;
						break;
					}

					current_type.swap(new_type);

					if (!monotonicity.is_positive)
						key_range_transformed.swapLeftAndRight();
				}

				if (evaluation_is_not_possible)
				{
					rpn_stack.emplace_back(true, true);
					continue;
				}

				key_range = &key_range_transformed;
			}

			if (element.function == RPNElement::FUNCTION_IN_RANGE
				|| element.function == RPNElement::FUNCTION_NOT_IN_RANGE)
			{
				bool intersects = element.range.intersectsRange(*key_range);
				bool contains = element.range.containsRange(*key_range);

				rpn_stack.emplace_back(intersects, !contains);
				if (element.function == RPNElement::FUNCTION_NOT_IN_RANGE)
					rpn_stack.back() = !rpn_stack.back();
			}
			else	/// Set
			{
				auto in_func = typeid_cast<const ASTFunction *>(element.in_function.get());
				const ASTs & args = typeid_cast<const ASTExpressionList &>(*in_func->arguments).children;
				auto ast_set = typeid_cast<const ASTSet *>(args[1].get());
				if (in_func && ast_set)
				{
					rpn_stack.push_back(ast_set->set->mayBeTrueInRange(*key_range));
					if (element.function == RPNElement::FUNCTION_NOT_IN_SET)
						rpn_stack.back() = !rpn_stack.back();
				}
				else
				{
					throw DB::Exception("Set for IN is not created yet!", ErrorCodes::LOGICAL_ERROR);
				}
			}
		}
		else if (element.function == RPNElement::FUNCTION_NOT)
		{
			rpn_stack.back() = !rpn_stack.back();
		}
		else if (element.function == RPNElement::FUNCTION_AND)
		{
			auto arg1 = rpn_stack.back();
			rpn_stack.pop_back();
			auto arg2 = rpn_stack.back();
			rpn_stack.back() = arg1 & arg2;
		}
		else if (element.function == RPNElement::FUNCTION_OR)
		{
			auto arg1 = rpn_stack.back();
			rpn_stack.pop_back();
			auto arg2 = rpn_stack.back();
			rpn_stack.back() = arg1 | arg2;
		}
		else if (element.function == RPNElement::ALWAYS_FALSE)
		{
			rpn_stack.emplace_back(false, true);
		}
		else if (element.function == RPNElement::ALWAYS_TRUE)
		{
			rpn_stack.emplace_back(true, false);
		}
		else
			throw Exception("Unexpected function type in PKCondition::RPNElement", ErrorCodes::LOGICAL_ERROR);
	}

	if (rpn_stack.size() != 1)
		throw Exception("Unexpected stack size in PkCondition::mayBeTrueInRange", ErrorCodes::LOGICAL_ERROR);

	return rpn_stack[0].can_be_true;
}


bool PKCondition::mayBeTrueInRange(
	size_t used_key_size, const Field * left_pk, const Field * right_pk, const DataTypes & data_types) const
{
	return mayBeTrueInRange(used_key_size, left_pk, right_pk, data_types, true);
}

bool PKCondition::mayBeTrueAfter(
	size_t used_key_size, const Field * left_pk, const DataTypes & data_types) const
{
	return mayBeTrueInRange(used_key_size, left_pk, nullptr, data_types, false);
}


static const ASTSet & inFunctionToSet(const ASTPtr & in_function)
{
	const auto & in_func = typeid_cast<const ASTFunction &>(*in_function);
	const auto & args = typeid_cast<const ASTExpressionList &>(*in_func.arguments).children;
	const auto & ast_set = typeid_cast<const ASTSet &>(*args[1]);
	return ast_set;
}

String PKCondition::RPNElement::toString() const
{
	auto print_wrapped_column = [this](std::ostringstream & ss)
	{
		for (auto it = monotonic_functions_chain.rbegin(); it != monotonic_functions_chain.rend(); ++it)
			ss << (*it)->getName() << "(";

		ss << "column " << key_column;

		for (auto it = monotonic_functions_chain.rbegin(); it != monotonic_functions_chain.rend(); ++it)
			ss << ")";
	};

	std::ostringstream ss;
	switch (function)
	{
		case FUNCTION_AND:
			return "and";
		case FUNCTION_OR:
			return "or";
		case FUNCTION_NOT:
			return "not";
		case FUNCTION_UNKNOWN:
			return "unknown";
		case FUNCTION_NOT_IN_SET:
		case FUNCTION_IN_SET:
		{
			ss << "(";
			print_wrapped_column(ss);
			ss << (function == FUNCTION_IN_SET ? " in " : " notIn ") << inFunctionToSet(in_function).set->describe();
			ss << ")";
			return ss.str();
		}
		case FUNCTION_IN_RANGE:
		case FUNCTION_NOT_IN_RANGE:
		{
			ss << "(";
			print_wrapped_column(ss);
			ss << (function == FUNCTION_NOT_IN_RANGE ? " not" : "") << " in " << range.toString();
			ss << ")";
			return ss.str();
		}
		case ALWAYS_FALSE:
			return "false";
		case ALWAYS_TRUE:
			return "true";
		default:
			throw Exception("Unknown function in RPNElement", ErrorCodes::LOGICAL_ERROR);
	}
}


bool PKCondition::alwaysUnknownOrTrue() const
{
	std::vector<UInt8> rpn_stack;

	for (const auto & element : rpn)
	{
		if (element.function == RPNElement::FUNCTION_UNKNOWN
			|| element.function == RPNElement::ALWAYS_TRUE)
		{
			rpn_stack.push_back(true);
		}
		else if (element.function == RPNElement::FUNCTION_NOT_IN_RANGE
			|| element.function == RPNElement::FUNCTION_IN_RANGE
			|| element.function == RPNElement::FUNCTION_IN_SET
			|| element.function == RPNElement::FUNCTION_NOT_IN_SET
			|| element.function == RPNElement::ALWAYS_FALSE)
		{
			rpn_stack.push_back(false);
		}
		else if (element.function == RPNElement::FUNCTION_NOT)
		{
		}
		else if (element.function == RPNElement::FUNCTION_AND)
		{
			auto arg1 = rpn_stack.back();
			rpn_stack.pop_back();
			auto arg2 = rpn_stack.back();
			rpn_stack.back() = arg1 & arg2;
		}
		else if (element.function == RPNElement::FUNCTION_OR)
		{
			auto arg1 = rpn_stack.back();
			rpn_stack.pop_back();
			auto arg2 = rpn_stack.back();
			rpn_stack.back() = arg1 | arg2;
		}
		else
			throw Exception("Unexpected function type in PKCondition::RPNElement", ErrorCodes::LOGICAL_ERROR);
	}

	return rpn_stack[0];
}


size_t PKCondition::getMaxKeyColumn() const
{
	size_t res = 0;
	for (const auto & element : rpn)
	{
		if (element.function == RPNElement::FUNCTION_NOT_IN_RANGE
			|| element.function == RPNElement::FUNCTION_IN_RANGE
			|| element.function == RPNElement::FUNCTION_IN_SET
			|| element.function == RPNElement::FUNCTION_NOT_IN_SET)
		{
			if (element.key_column > res)
				res = element.key_column;
		}
	}
	return res;
}


}
