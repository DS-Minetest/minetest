
#pragma once

#include <vector>
#include <queue>
#include <map>
#include <utility> //pair
#include "util/string.h"
#include "util/numeric.h"
#include "zlib.h"

class SSCSMLoader
{
public:
	SSCSMLoader(u32 bunches_count, std::vector<std::string> sscsms);

	~SSCSMLoader();

	/*
	 * Returns true when finished
	 */
	bool addBunch(u32 i, u8 *buffer, u32 size);

private:
	struct bunch
	{
		bunch(u32 ai, u8 *abuffer, u32 asize) :
			i(ai), buffer(abuffer), size(asize)
		{
		}

		u32 i;
		u8 *buffer;
		u32 size;
	};

	class comp
	{
	public:
		bool operator()(bunch const &a, bunch const &b) {
			return a.i > b.i;
		}
	};

	void readBunches();

	void loadMods();

	std::priority_queue<bunch, std::vector<bunch>, comp> m_bunches;
	u32 m_bunches_count;
	u32 m_next_bunch_index;

	std::vector<std::string> m_sscsms;

	z_stream m_zstream;

	u8 *m_current_buffer;

	std::string m_current_file_path;
	u32 m_read_length;

	std::map<std::string, std::pair<char *, u32>> m_files;
};
