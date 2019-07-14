
#include "sscsmfiledownloader.h"
#include "porting.h"
#include "util/serialize.h"
#include "filesys.h"
#include <fstream>
//~ #include "zlib.h"

/*
 * data in buffers:
 * for each file {
 *     u8 path_length
 *     chars path
 *     u32 file size
 *     chars file
 * }
 */


SSCSMFileDownloader::SSCSMFileDownloader(u32 bunches_count) :
	m_bunches(), m_bunches_count(bunches_count), m_next_bunch_index(0),
	m_current_file_path("")
{
	// todo: get m_remaining_disk_space from a setting
	m_remaining_disk_space = 1000000;
}

void SSCSMFileDownloader::addBunch(u32 i, u8 *buffer, u32 size)
{
	m_bunches.emplace(i, buffer, size);

	if (!m_bunches.empty() && m_bunches.top().i <= m_next_bunch_index) {
		readBunches();
	}

	if (m_next_bunch_index >= m_bunches_count) {
		// todo: should this happen somewhere else?
		// todo: verbosestream
		actionstream << "[Client] finished downloading sscsm files" << std::endl;
	}
}

void SSCSMFileDownloader::readBunches() // todo: decompress with zlib
{
	// Read a single bunch
	bunch b = m_bunches.top();
	m_bunches.pop();
	u32 buffer_read_offset = 0;

	while (b.size > buffer_read_offset) {
		// The buffer is not empty

		if (m_current_file_path.empty()) {
			// Read the file path
			u8 path_length = readU8(b.buffer + buffer_read_offset);
			buffer_read_offset++;

			// todo: path_length could be larger than b.size - buffer_read_offset
			if (path_length > b.size - buffer_read_offset)
				errorstream << "[Client] SSCSMFileDownloader: path cut off" << std::endl;

			m_current_file_path = std::string((char *)(b.buffer + buffer_read_offset),
					path_length);
			buffer_read_offset += path_length;

#ifdef _WIN32 // DIR_DELIM is not "/"
			m_current_file_path = str_replace(m_current_file_path, "/", DIR_DELIM);
#endif

			m_current_file_path = porting::path_cache + DIR_DELIM + "sscsm" +
					DIR_DELIM + m_current_file_path;

			// todo: check whether path is in path_cache/sscsm/* and stop connecting if it is not

			m_remaining_file_size = readU32(b.buffer + buffer_read_offset);
			buffer_read_offset += 4;

			// create directory to file if needed
			fs::CreateAllDirs(fs::RemoveLastPathComponent(m_current_file_path));
		}

		u32 actual_writing_length = MYMIN(m_remaining_file_size, b.size - buffer_read_offset);

		m_remaining_disk_space -= MYMIN(actual_writing_length, m_remaining_disk_space);

		if (m_remaining_disk_space == 0) {
			errorstream << "too much sscsm file data, it might be harmful" << std::endl;
			// todo: stop connecting to server
		}

		// append the contents to the file
		std::ofstream file(m_current_file_path, std::ios_base::app);
		file.write((char *)(b.buffer + buffer_read_offset), actual_writing_length);
		file.close();

		buffer_read_offset += actual_writing_length;
		m_remaining_file_size -= actual_writing_length;

		if (m_remaining_file_size == 0) {
			m_current_file_path = "";
		}
	}

	// Read the next bunch
	m_next_bunch_index++;
	if (!m_bunches.empty() && m_bunches.top().i <= m_next_bunch_index) {
		readBunches();
	}
}
