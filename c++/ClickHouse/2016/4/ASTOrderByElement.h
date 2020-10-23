#pragma once

#include <DB/Parsers/IAST.h>
#include <DB/Common/Collator.h>

namespace DB
{

using Poco::SharedPtr;


/** Элемент выражения, после которого стоит ASC или DESC
  */
class ASTOrderByElement : public IAST
{
public:
	int direction;	/// 1, если ASC, -1, если DESC
	
	/** Collator для locale-specific сортировки строк.
	 * Если nullptr, то производится сортировка по байтам.
	 */
	Poco::SharedPtr<Collator> collator;
	
	ASTOrderByElement() = default;
	ASTOrderByElement(const StringRange range_, const int direction_, const Poco::SharedPtr<Collator> & collator_ = nullptr)
		: IAST(range_), direction(direction_), collator(collator_) {}
	
	/** Получить текст, который идентифицирует этот элемент. */
	String getID() const override { return "OrderByElement"; }

	ASTPtr clone() const override { return new ASTOrderByElement(*this); }

protected:
	void formatImpl(const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override
	{
		children.front()->formatImpl(settings, state, frame);
		settings.ostr << (settings.hilite ? hilite_keyword : "") << (direction == -1 ? " DESC" : " ASC") << (settings.hilite ? hilite_none : "");
		if (!collator.isNull())
		{
			settings.ostr << (settings.hilite ? hilite_keyword : "") << " COLLATE " << (settings.hilite ? hilite_none : "")
				<< "'" << collator->getLocale() << "'";
		}
	}
};

}
