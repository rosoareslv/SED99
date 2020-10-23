#include <cstdlib>
#include <DB/DataStreams/ColumnGathererStream.h>
using DB::RowSourcePart;

static void check(const RowSourcePart & s, size_t num, bool flag)
{
	if ((s.getSourceNum() != num || s.getSkipFlag() != flag) || (!flag && s.getData() != num))
	{
		printf("FAIL");
		std::exit(-1);
	}
}

int main(int, char **)
{
	check(RowSourcePart(0, false), 0, false);
	check(RowSourcePart(0, true), 0, true);
	check(RowSourcePart(1, false), 1, false);
	check(RowSourcePart(1, true), 1, true);
	check(RowSourcePart(RowSourcePart::MAX_PARTS, false), RowSourcePart::MAX_PARTS, false);
	check(RowSourcePart(RowSourcePart::MAX_PARTS, true), RowSourcePart::MAX_PARTS, true);

	RowSourcePart p{80, false};
	check(p, 80, false);
	p.setSkipFlag(true);
	check(p, 80, true);
	p.setSkipFlag(false);
	check(p, 80, false);
	p.setSourceNum(RowSourcePart::MAX_PARTS);
	check(p, RowSourcePart::MAX_PARTS, false);

	printf("PASSED");
	return 0;
}
