
#include "sscsmfiledownloader.h"
#include "porting.h"
#include "util/serialize.h"
#include "filesys.h"
#include <fstream>

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
	errorstream << "[Client] SSCSMFileDownloader: constructor start" << std::endl;

	// todo: get m_remaining_disk_space from a setting
	m_remaining_disk_space = 1000000;

	m_buffer = new u8[m_buffer_size];

	// initaialize the z_stream
	m_zstream.zalloc = Z_NULL;
	m_zstream.zfree = Z_NULL;
	m_zstream.opaque = Z_NULL;

	// zlib.h says, next_in and avail_in have to be initialized before, but
	// serialization.cpp does not do this either
	//~ m_zstream.next_in = ;
	//~ m_zstream.avail_in = ;

	int ret = inflateInit(&m_zstream);
	if (ret != Z_OK) {
		// todo: throw an error
		errorstream << "SSCSMFileDownloader: inflateInit failed" << std::endl;
	}

	m_zstream.avail_in = 0;

	fs::CreateAllDirs(porting::path_cache + DIR_DELIM + "sscsm");

	errorstream << "[Client] SSCSMFileDownloader: constructor end" << std::endl;
}

SSCSMFileDownloader::~SSCSMFileDownloader()
{
	delete[] m_buffer;
}

void SSCSMFileDownloader::addBunch(u32 i, u8 *buffer, u32 size)
{
	errorstream << "[Client] SSCSMFileDownloader::addBunch start" << std::endl;

	// emplace into priority queue to ensure that the bunches are read in the
	// correct order
	m_bunches.emplace(i, buffer, size); // todo: fix crash

	errorstream << "[Client] SSCSMFileDownloader::addBunch hm0" << std::endl;

	if (!m_bunches.empty() && m_bunches.top().i <= m_next_bunch_index) {
		readBunches();
	}

	errorstream << "[Client] SSCSMFileDownloader::addBunch hm1" << std::endl;

	if (m_next_bunch_index < m_bunches_count) {
		errorstream << "[Client] SSCSMFileDownloader::addBunch break" << std::endl;
		return;
	}

	// all bunches are added

	// todo: should this message been written somewhere else?
	// todo: verbosestream
	actionstream << "[Client] finished downloading sscsm files" << std::endl;

	// todo: get last buffer (no, it will be empty)

	// end z_stream
	inflateEnd(&m_zstream);

	errorstream << "[Client] SSCSMFileDownloader::addBunch end" << std::endl;
}

void SSCSMFileDownloader::readBunches()
{
	errorstream << "[client] SSCSMFileDownloader::readBunches start" << std::endl;

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

#ifdef _WIN32 // DIR_DELIM is not "/"
				m_current_file_path = str_replace(m_current_file_path, "/", DIR_DELIM);
#endif

				m_current_file_path = porting::path_cache + DIR_DELIM + "sscsm" +
						DIR_DELIM + m_current_file_path;

				// todo: check whether path is in path_cache/sscsm/* and todo: stop connecting if it is not
				/*
				// this does not work because fs::AbsolutePath only works for existing directories >_<

				if (!fs::PathStartsWith(fs::AbsolutePath(m_current_file_path),
						fs::AbsolutePath(porting::path_cache + DIR_DELIM + "sscsm"))) {
					errorstream << "evvilll: " << std::endl;
					errorstream << "path: " << m_current_file_path << std::endl;
					errorstream << "abspath: " << fs::AbsolutePath(m_current_file_path) << std::endl;
					errorstream << "dirpath: " << fs::RemoveLastPathComponent(m_current_file_path) << std::endl;
					errorstream << "absdirpath: " << fs::AbsolutePath(fs::RemoveLastPathComponent(m_current_file_path)) << std::endl;
					errorstream << "absdirpath2: " << fs::AbsolutePath(fs::RemoveLastPathComponent(fs::RemoveLastPathComponent(m_current_file_path))) << std::endl;
					errorstream << "hmpath: " << fs::AbsolutePath(fs::RemoveLastPathComponent(m_current_file_path) + DIR_DELIM + "/bla") << std::endl;
					errorstream << "startpath: " << porting::path_cache + DIR_DELIM + "sscsm" << std::endl;
					errorstream << "absstartpath: " << fs::AbsolutePath(porting::path_cache + DIR_DELIM + "sscsm") << std::endl;
					//~ return;
				}
				std::string path_start_ok = fs::AbsolutePath(porting::path_cache + DIR_DELIM + "sscsm");
				std::string path_abs = fs::AbsolutePath(m_current_file_path);
				bool path_is_ok = false;
				while (!path_abs.empty()) {
					errorstream << "path_abs: " << path_abs << std::endl;
					if (path_abs == path_start_ok) {
						path_is_ok = true;
						break;
					}
					path_abs = fs::RemoveLastPathComponent(path_abs);
				}
				if (!path_is_ok) {
					errorstream << "really evvilll: " << std::endl;
					// todo: stop connecting
					return;
				}
				*/

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

	errorstream << "[client] SSCSMFileDownloader::readBunches end" << std::endl;
}
