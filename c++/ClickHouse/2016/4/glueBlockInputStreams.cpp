#include <set>

#include <DB/DataStreams/glueBlockInputStreams.h>


namespace DB
{


typedef std::map<String, BlockInputStreams> IDsMap;
typedef std::map<String, ForkPtr> ForksMap;


static void createIDsMap(BlockInputStreamPtr & node, IDsMap & ids_map)
{
	ids_map[node->getID()].push_back(node);

	BlockInputStreams & children = node->getChildren();
	for (size_t i = 0, size = children.size(); i < size; ++i)
		createIDsMap(children[i], ids_map);
}


static void glue(BlockInputStreamPtr & node, IDsMap & ids_map, ForksMap & forks_map)
{
	String id = node->getID();
	if (ids_map.end() != ids_map.find(id) && ids_map[id].size() > 1)
	{
		/// Вставить "вилку" или использовать уже готовую.
		if (forks_map.end() == forks_map.find(id))
		{
			forks_map[id] = new ForkBlockInputStreams(node);
			std::cerr << "Forking at " << id << std::endl;
		}

		std::cerr << "Replacing node with fork end" << std::endl;
		node = forks_map[id]->createInput();
	}
	else
	{
		BlockInputStreams & children = node->getChildren();
		for (size_t i = 0, size = children.size(); i < size; ++i)
			glue(children[i], ids_map, forks_map);
	}
}


void glueBlockInputStreams(BlockInputStreams & inputs, Forks & forks)
{
	IDsMap ids_map;
	for (size_t i = 0, size = inputs.size(); i < size; ++i)
		createIDsMap(inputs[i], ids_map);

	ForksMap forks_map;
	for (size_t i = 0, size = inputs.size(); i < size; ++i)
		glue(inputs[i], ids_map, forks_map);

	for (ForksMap::iterator it = forks_map.begin(); it != forks_map.end(); ++it)
		forks.push_back(it->second);
}


}
