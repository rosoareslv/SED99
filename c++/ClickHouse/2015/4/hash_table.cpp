#include <iostream>
#include <iomanip>

#include <DB/Interpreters/AggregationCommon.h>

#include <DB/Common/HashTable/HashMap.h>
#include <DB/Common/HashTable/HashSet.h>



int main(int argc, char ** argv)
{
	{
		typedef HashSet<int, DefaultHash<int>, HashTableGrower<1> > Cont;
		Cont cont;

		cont.insert(1);
		cont.insert(2);

		Cont::iterator it;
		bool inserted;

		cont.emplace(3, it, inserted);
		std::cerr << inserted << ", " << *it << std::endl;

		cont.emplace(3, it, inserted);
		std::cerr << inserted << ", " << *it << std::endl;

		for (auto x : cont)
			std::cerr << x << std::endl;

		std::string dump;
		{
			DB::WriteBufferFromString wb(dump);
			cont.writeText(wb);
		}

		std::cerr << "dump: " << dump << std::endl;
	}

	{
		typedef HashMap<int, std::string, DefaultHash<int>, HashTableGrower<1> > Cont;
		Cont cont;

		cont.insert(Cont::value_type(1, "Hello, world!"));
		cont[1] = "Goodbye.";

		for (auto x : cont)
			std::cerr << x.first << " -> " << x.second << std::endl;

		std::string dump;
		{
			DB::WriteBufferFromString wb(dump);
			cont.writeText(wb);
		}

		std::cerr << "dump: " << dump << std::endl;
	}

	{
		typedef HashSet<
			DB::UInt128,
			DB::UInt128TrivialHash> Cont;
		Cont cont;

		std::string dump;
		{
			DB::WriteBufferFromString wb(dump);
			cont.write(wb);
		}

		std::cerr << "dump: " << dump << std::endl;
	}

	return 0;
}
