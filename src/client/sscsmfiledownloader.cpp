
#include "sscsmfiledownloader.h"
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
	if (ret < 0)
		throw SerializationError("SSCSMFileDownloader: inflateInit failed");

	m_zstream.avail_in = 0;

	fs::CreateAllDirs(porting::path_cache + DIR_DELIM + "sscsm");
}

SSCSMFileDownloader::~SSCSMFileDownloader()
{
	delete[] m_buffer;
}

bool SSCSMFileDownloader::addBunch(u32 i, u8 *buffer, u32 size)
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

	return true;
}

void SSCSMFileDownloader::readBunches()
{
	// Read a single bunch
	const bunch &b = m_bunches.top();

	int ret = 0;

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
			if (ret < 0)
				throw SerializationError("SSCSMFileDownloader: inflate failed (step 1)");

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
			if (ret < 0)
				throw SerializationError("SSCSMFileDownloader: inflate failed (step 2)");

			if (m_zstream.avail_out == 0) {
				// the whole path is read into buffer
				m_current_file_path = std::string((char *)m_buffer, m_read_length);

				// check whether path is in path_cache/sscsm/* (this might be not good enough)
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

				m_read_length = 0;
				// prepare step 3
				m_zstream.next_out = m_buffer;
				m_zstream.avail_out = 4; // u32
			}

			continue;

		} else if (m_read_length == 0) { // step 3
			// decompress the file length into buffer
			ret = inflate(&m_zstream, Z_SYNC_FLUSH);
			if (ret < 0)
				throw SerializationError("SSCSMFileDownloader: inflate failed (step 3)");

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
			if (ret < 0)
				throw SerializationError("SSCSMFileDownloader: inflate failed (step 4)");

			if (m_zstream.avail_out == 0) {
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
