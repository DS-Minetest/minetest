
#include "sscsmloader.h"
#include "porting.h"
#include "util/serialize.h"
#include "filesys.h"
#include <fstream>
#include "settings.h"
#include "exceptions.h"

/*
 * data in decompressed buffers:
 * for each file {
 *     u8 path_length
 *     chars path
 *     u32 file size
 *     chars file
 * }
 */


SSCSMLoader::SSCSMLoader(u32 bunches_count, std::vector<std::string> sscsms) :
	m_bunches(), m_bunches_count(bunches_count), m_next_bunch_index(0),
	m_sscsms(sscsms), m_current_buffer(nullptr), m_current_file_path(""),
	m_read_length(0)
{
	// initaialize the z_stream
	m_zstream.zalloc = Z_NULL;
	m_zstream.zfree = Z_NULL;
	m_zstream.opaque = Z_NULL;

	int ret = inflateInit(&m_zstream);
	if (ret < 0)
		throw SerializationError("SSCSMFileDownloader: inflateInit failed");

	m_zstream.avail_in = 0;

	//~ fs::CreateAllDirs(porting::path_cache + DIR_DELIM + "sscsm");
}

SSCSMLoader::~SSCSMLoader()
{
	//~ delete[] m_buffer;

	for (auto file_it = m_files.begin(); file_it != m_files.end(); ++file_it) {
		delete[] file_it->second.first;
	}
}

bool SSCSMLoader::addBunch(u32 i, u8 *buffer, u32 size)
{
	// emplace into priority queue to ensure that the bunches are read in the
	// correct order
	m_bunches.emplace(i, buffer, size);

	if (!m_bunches.empty() && m_bunches.top().i <= m_next_bunch_index) {
		readBunches();
	}

	if (m_next_bunch_index < m_bunches_count)
		return false;

	// all bunches are added

	// todo: verbosestream
	actionstream << "[Client] finished downloading sscsm files" << std::endl;

	// todo: get last buffer? (no, it will be empty)

	// end z_stream
	int ret = inflateEnd(&m_zstream);
	if (ret < 0) {
		throw SerializationError("SSCSMFileDownloader: inflateEnd failed");
	}

	loadMods();

	return true;
}

void SSCSMLoader::readBunches()
{
	// Read a single bunch
	const bunch &b = m_bunches.top();

	int ret = 0;

	m_zstream.next_in = b.buffer;
	m_zstream.avail_in = b.size;

	while (m_zstream.avail_in > 0) {
		// the if cases (steps) are ordered in the temporal order in which they happen

		if (m_read_length == 0 && m_current_file_path.empty()) { // step 1
			// decompress the path length
			// it is just one byte, hence we can set next_out and avail_out every time
			m_read_length = 0;
			m_zstream.next_out = (u8 *)&m_read_length; // write the first byte of the u32
			m_zstream.avail_out = 1;
			ret = inflate(&m_zstream, Z_SYNC_FLUSH);
			if (ret < 0)
				throw SerializationError("SSCSMFileDownloader: inflate failed (step 1)");

			if (m_zstream.avail_out == 0) {
				// prepare step 2
				m_read_length = (u32)readU8((u8 *)&m_read_length); // read the first byte
				m_current_buffer = new u8[m_read_length]; // buffer for path string
				m_zstream.next_out = m_current_buffer;
				m_zstream.avail_out = m_read_length;
			}

			continue;

		} else if (m_current_file_path.empty()) { // step 2
			// decompress the path into buffer
			ret = inflate(&m_zstream, Z_SYNC_FLUSH);
			if (ret < 0)
				throw SerializationError("SSCSMFileDownloader: inflate failed (step 2)");

			if (m_zstream.avail_out == 0) {
				// the whole path is read into buffer
				m_current_file_path = std::string((char *)m_current_buffer, m_read_length);
				delete[] m_current_buffer;

				/* code for file writing
				// check whether path is in path_cache/sscsm/ * (this might be not good enough)
				if (m_current_file_path.find("..") != std::string::npos) {
					// todo: translate?
					throw SerializationError("The server wanted to write somewhere into your filesystem, it is evil.");
				}

#ifdef _WIN32 // DIR_DELIM is not "/"
				m_current_file_path = str_replace(m_current_file_path, "/", DIR_DELIM);
#endif

				m_current_file_path = porting::path_cache + DIR_DELIM + "sscsm" +
						DIR_DELIM + m_current_file_path;

				// create directory to file if needed
				fs::CreateAllDirs(fs::RemoveLastPathComponent(m_current_file_path));
				*/

				// prepare step 3
				m_read_length = 0;
				m_current_buffer = new u8[4]; // u32 file_size
				m_zstream.next_out = m_current_buffer;
				m_zstream.avail_out = 4;
			}

			continue;

		} else if (m_read_length == 0) { // step 3
			// decompress the file length into buffer
			ret = inflate(&m_zstream, Z_SYNC_FLUSH);
			if (ret < 0)
				throw SerializationError("SSCSMFileDownloader: inflate failed (step 3)");

			if (m_zstream.avail_out == 0) {
				// the file length was read
				m_read_length = readU32(m_current_buffer);
				delete[] m_current_buffer;
				m_current_buffer = nullptr;
				if (m_read_length == 0) {
					// empty file
					// todo
					//~ // create empty file
					//~ std::ofstream file(m_current_file_path, std::ios_base::app);
					//~ file.close();
					// prepare step 1
					m_current_file_path = "";
					continue;
				}
				// create buffer for file
				m_current_buffer = new u8[m_read_length];
				m_files.emplace(m_current_file_path,
						std::pair<char *, u32>((char *)m_current_buffer, m_read_length));
				// prepare step 4
				m_zstream.next_out = m_current_buffer;
				m_zstream.avail_out = m_read_length;
			}

			continue;

		} else { // step 4
			// read the file and save it
			ret = inflate(&m_zstream, Z_SYNC_FLUSH);
			if (ret < 0)
				throw SerializationError("SSCSMFileDownloader: inflate failed (step 4)");

			if (m_zstream.avail_out == 0) {
				/*
				// append to the file
				u32 readc = MYMIN(m_read_length, m_buffer_size);
				if (m_remaining_disk_space < readc) {
					// todo: translate this?
					// todo: SerializationError is probably not correct
					// todo: give more information (newlines?)
					throw SerializationError("There was too much SSCSM file data.");
				}
				m_remaining_disk_space -= readc;
				std::ofstream file(m_current_file_path, std::ios_base::app);
				file.write((char *)(m_buffer), readc);
				file.close();
				m_read_length -= readc;

				if (m_read_length > 0) {
					// do step 4 again
					m_zstream.next_out = m_buffer;
					m_zstream.avail_out = MYMIN(m_read_length, m_buffer_size);
				} else {
					// prepare step 1
					m_current_file_path = "";
				}
				*/

				// file buffer is filled
				// prepare step 1
				m_current_file_path = "";
				m_read_length = 0;
				m_current_buffer = nullptr; // do not delete
			}

			continue;
		}
	}

	delete[] b.buffer;
	m_bunches.pop();

	// Read the next bunch
	m_next_bunch_index++;
	if (!m_bunches.empty() && m_bunches.top().i <= m_next_bunch_index) {
		readBunches();
	}
}

void SSCSMLoader::loadMods()
{
	// todo: load all m_sscsms from m_files

	warningstream << "SSCSMLoader::loadMods sscsms: " << std::endl;
	for (const std::string &n : m_sscsms)
		warningstream << "\t\"" << n << "\"" << std::endl;

	warningstream << "SSCSMLoader::loadMods file paths: " << std::endl;
	for (auto file_it = m_files.begin(); file_it != m_files.end(); ++file_it) {
		warningstream << "\t\"" << file_it->first << "\"" << std::endl;
	}

	//~ warningstream << "SSCSMLoader::loadMods files: " << std::endl;
	//~ for (auto file_it = m_files.begin(); file_it != m_files.end(); ++file_it) {
		//~ warningstream << "\t\"" << file_it->first << "\"" << std::endl;
		//~ warningstream << "\"" << std::string(file_it->second.first, 10) << "\"" << std::endl;
	//~ }
}
