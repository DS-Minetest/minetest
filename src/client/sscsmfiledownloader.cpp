
#include "sscsmfiledownloader.h"
#include "porting.h"
#include "util/serialize.h"
#include "filesys.h"
#include <fstream>
#include "settings.h"

/*
 * data in decompressed buffers:
 * for each file {
 *     u8 path_length
 *     chars path
 *     u32 file size
 *     chars file
 * }
 */


SSCSMFileDownloader::SSCSMFileDownloader(u32 bunches_count) :
	m_bunches(), m_bunches_count(bunches_count), m_next_bunch_index(0),
	m_current_file_path(""), m_read_length(0)
{
	m_remaining_disk_space = g_settings->getU64("sscsm_file_size_limit");

	m_buffer = new u8[m_buffer_size];

	// initaialize the z_stream
	m_zstream.zalloc = Z_NULL;
	m_zstream.zfree = Z_NULL;
	m_zstream.opaque = Z_NULL;

	int ret = inflateInit(&m_zstream);
	if (ret != Z_OK) {
		// todo: throw an error
		errorstream << "SSCSMFileDownloader: inflateInit failed" << std::endl;
	}

	m_zstream.avail_in = 0;

	fs::CreateAllDirs(porting::path_cache + DIR_DELIM + "sscsm");
}

SSCSMFileDownloader::~SSCSMFileDownloader()
{
	delete[] m_buffer;
}

void SSCSMFileDownloader::addBunch(u32 i, u8 *buffer, u32 size)
{
	// emplace into priority queue to ensure that the bunches are read in the
	// correct order
	m_bunches.emplace(i, buffer, size);

	if (!m_bunches.empty() && m_bunches.top().i <= m_next_bunch_index) {
		readBunches();
	}

	if (m_next_bunch_index < m_bunches_count)
		return;

	// all bunches are added

	// todo: should this message been written somewhere else?
	// todo: verbosestream
	actionstream << "[Client] finished downloading sscsm files" << std::endl;

	// todo: get last buffer (no, it will be empty)

	// end z_stream
	inflateEnd(&m_zstream);
}

void SSCSMFileDownloader::readBunches()
{
	// Read a single bunch
	const bunch &b = m_bunches.top();

	int ret = 0; // todo: use this?

	m_zstream.next_in = b.buffer;
	m_zstream.avail_in = b.size;

	while (m_zstream.avail_in > 0) {
		// the if cases (steps) are ordered in the temporal order in which they happen

		if (m_read_length == 0 && m_current_file_path.empty()) { // step 1
			// decompress the path length into buffer
			// it is just one byte, hence we can set next_out and avail_out every time
			m_zstream.next_out = m_buffer;
			m_zstream.avail_out = 1;
			ret = inflate(&m_zstream, Z_SYNC_FLUSH);

			if (m_zstream.avail_out == 0) {
				// a byte was read
				m_read_length = readU8(m_buffer);

				// prepare step 2
				m_zstream.next_out = m_buffer;
				m_zstream.avail_out = m_read_length;
			}

			continue;

		} else if (m_current_file_path.empty()) { // step 2
			// decompress the path into buffer
			ret = inflate(&m_zstream, Z_SYNC_FLUSH);

			if (m_zstream.avail_out == 0) {
				// the whole path is read into buffer
				m_current_file_path = std::string((char *)m_buffer, m_read_length);

				// check whether path is in path_cache/sscsm/* (this might be not good enough)
				if (m_current_file_path.find("..") != std::string::npos) {
					errorstream << "SSCSMFileDownloader::readBunches: the server is evil" << std::endl;
					// todo: stop connecting
				}

#ifdef _WIN32 // DIR_DELIM is not "/"
				m_current_file_path = str_replace(m_current_file_path, "/", DIR_DELIM);
#endif

				m_current_file_path = porting::path_cache + DIR_DELIM + "sscsm" +
						DIR_DELIM + m_current_file_path;

				// todo: check whether path is in path_cache/sscsm/* and todo: stop connecting if it is not
				// maybe that up there is enough

				// create directory to file if needed
				fs::CreateAllDirs(fs::RemoveLastPathComponent(m_current_file_path));

				m_read_length = 0;
				// prepare step 3
				m_zstream.next_out = m_buffer;
				m_zstream.avail_out = 4; // u32
			}

			continue;

		} else if (m_read_length == 0) { // step 3
			// decompress the file length into buffer
			ret = inflate(&m_zstream, Z_SYNC_FLUSH);

			if (m_zstream.avail_out == 0) {
				// the file length was read
				m_read_length = readU32(m_buffer);
				if (m_read_length == 0) {
					// empty file
					// create empty file
					std::ofstream file(m_current_file_path, std::ios_base::app);
					file.close();
					// prepare step 1
					m_current_file_path = "";
					continue;
				}
				// prepare step 4
				m_zstream.next_out = m_buffer;
				m_zstream.avail_out = MYMIN(m_read_length, m_buffer_size);
			}

			continue;

		} else { // step 4
			// read the file and write it
			ret = inflate(&m_zstream, Z_SYNC_FLUSH);

			if (m_zstream.avail_out == 0) {
				// append to the file
				u32 readc = MYMIN(m_read_length, m_buffer_size);
				if (m_remaining_disk_space < readc) {
					errorstream << "too much sscsm file data, it might be harmful"
						<< std::endl;
					// todo: stop connecting to server
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
			}

			continue;
		}
	}

	// remove unused variable compiler warning (todo: remove this)
	verbosestream << "ret: " << ret << std::endl;

	m_bunches.pop();
	// todo: delete &b; ?

	// Read the next bunch
	m_next_bunch_index++;
	if (!m_bunches.empty() && m_bunches.top().i <= m_next_bunch_index) {
		readBunches();
	}
}
