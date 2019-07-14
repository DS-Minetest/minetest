
#pragma once

#include <vector>
#include <queue>
#include "util/string.h"
#include "util/numeric.h"

class SSCSMFileDownloader
{
public:
	SSCSMFileDownloader(u32 bunches_count);

	void addBunch(u32 i, u8 *buffer, u32 size);

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

	std::priority_queue<bunch, std::vector<bunch>, comp> m_bunches;

	u32 m_bunches_count;

	u32 m_next_bunch_index;

	std::string m_current_file_path;
	u32 m_remaining_file_size;

	u64 m_remaining_disk_space;
};
