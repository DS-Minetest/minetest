#include "sscsmfilegrabber.h"
#include "porting.h"
#include "util/serialize.h"
#include "log.h"
#include <fstream>
#include "filesys.h"
#include "string.h"
#include "server/mods.h"
#include "exceptions.h"

SSCSMFileGrabber::SSCSMFileGrabber(std::vector<std::string> *mods,
	std::vector<std::pair<u8 *, u32>> *sscsm_files,
	const std::unique_ptr<ServerModManager> &modmgr) :
	m_mods(mods), m_sscsm_files(sscsm_files), m_modmgr(modmgr), m_buffer_queue()
{
	m_out_buffer = new u8[m_buffer_size];
	m_zstream.zalloc = Z_NULL;
	m_zstream.zfree = Z_NULL;
	m_zstream.opaque = Z_NULL;
}

SSCSMFileGrabber::~SSCSMFileGrabber()
{
	delete[] m_out_buffer;
}

void SSCSMFileGrabber::parseMods()
{
	// initialize the z_stream
	int ret = deflateInit(&m_zstream, Z_DEFAULT_COMPRESSION);
	if (ret < 0)
		throw SerializationError("SSCSMFileGrabber: deflateInit failed");

	m_zstream.next_out = m_out_buffer;
	m_zstream.avail_out = m_buffer_size;

	// create first buffer
	m_buffer_offset = 0;
	m_buffer = new u8[m_buffer_size];

	// add builtin
	std::string builtin_path = porting::path_share + DIR_DELIM + "builtin" + DIR_DELIM;
	addDir(builtin_path + "sscsm", "*builtin*/sscsm");
	addDir(builtin_path + "common", "*builtin*/common");

	// parse all mods
	std::vector<std::string> modnames;
	m_modmgr->getModNames(modnames);

	for (const std::string &modname : modnames) {
		std::string path = m_modmgr->getModSpec(modname)->path;
		path = path + DIR_DELIM + "client";
		if (!fs::IsDir(path))
			// mod does not have a sscsm
			continue;
		verbosestream << "[Server] found sscsm for mod \"" << modname << "\"" << std::endl;
		m_mods->emplace_back(modname);
		addDir(path, modname);
	}

	// flush the z_stream (and add last buffer)
	m_zstream.next_in = m_buffer;
	m_zstream.avail_in = m_buffer_offset;
	do {
		ret = deflate(&m_zstream, Z_FINISH);
		if (ret < 0)
			throw SerializationError("SSCSMFileGrabber: deflate failed");

		if (m_zstream.avail_out == m_buffer_size)
			break;

		m_sscsm_files->emplace_back(m_out_buffer, m_buffer_size - m_zstream.avail_out);
		m_out_buffer = new u8[m_buffer_size];
		m_zstream.next_out = m_out_buffer;
		m_zstream.avail_out = m_buffer_size;

		if (ret == Z_STREAM_END)
			break;
	} while (true);

	// end the z_stream
	ret = deflateEnd(&m_zstream);
	if (ret < 0)
		throw SerializationError("SSCSMFileGrabber: deflateEnd failed");

	delete[] m_buffer;
}

void SSCSMFileGrabber::addDir(const std::string &server_path,
	const std::string &client_path)
{
	// get all subpaths (ignore if folder-/filename begins with '.')
	std::vector<std::string> subpaths;
	fs::GetRecursiveSubPaths(server_path, subpaths, true, {'.'});

	// add all files
	for (const std::string &path : subpaths) {
		if (fs::IsDir(path))
			continue;
#ifndef _WIN32 // DIR_DELIM is "/"
		addFile(path, client_path + path.substr(server_path.length()));
#else // DIR_DELIM is not "/"
		addFile(path, client_path + str_replace(path.substr(server_path.length()),
			DIR_DELIM, "/"));
#endif
	}
}

void SSCSMFileGrabber::addFile(const std::string &server_path,
	const std::string &client_path)
{
	verbosestream << "[Server] adding sscsm-file from " << server_path << " to " <<
			client_path << std::endl;

	// Add client_path to buffer
	// (the buffer always has enough space free, see below)
	u8 path_length = client_path.length();
	writeU8(m_buffer + m_buffer_offset, path_length);
	m_buffer_offset++;
	const char *client_path_c = client_path.c_str();
	for (u8 i = 0; i < path_length; i++)
		m_buffer[m_buffer_offset + i] = client_path_c[i];
	m_buffer_offset += path_length;

	// Prepare file length writing
	// (file_length will be written to file_length_buffer when it is finished)
	u8 *file_length_buffer = m_buffer + m_buffer_offset;
	m_buffer_offset += 4;
	u32 file_length = 0;

	// Read the file
	std::ifstream file(server_path);
	while (true) {
		file.read((char *)(m_buffer + m_buffer_offset), m_buffer_size - m_buffer_offset);
		u32 readl = file.gcount();
		m_buffer_offset += readl;
		file_length += readl;
		if (m_buffer_offset == m_buffer_size) {
			// buffer is full, file probably not empty
			// add buffer to m_buffer_queue
			m_buffer_queue.push(m_buffer);
			// make new buffer
			m_buffer_offset = 0;
			m_buffer = new u8[m_buffer_size];
			continue;
		}
		// buffer is not full, but file is empty
		// save file length
		writeU32(file_length_buffer, file_length);
		// compress buffers
		// and ensure that m_buffer has enough space free for the next path
		clearQueue(m_buffer_size < m_buffer_offset + 1 + 256);
		break;
	}
	file.close();
}

void SSCSMFileGrabber::clearQueue(bool also_clear_m_buffer)
{
	int ret = 0;

	while (!m_buffer_queue.empty()) {
		u8 *buffer = m_buffer_queue.front();

		m_zstream.next_in = buffer;
		m_zstream.avail_in = m_buffer_size;

		do {
			ret = deflate(&m_zstream, Z_NO_FLUSH);
			if (ret < 0)
				throw SerializationError("SSCSMFileGrabber: deflate failed");

			if (m_zstream.avail_out > 0)
				break;

			// m_out_buffer is full, save it and continue compressing
			m_sscsm_files->emplace_back(m_out_buffer, m_buffer_size);
			m_out_buffer = new u8[m_buffer_size];
			m_zstream.next_out = m_out_buffer;
			m_zstream.avail_out = m_buffer_size;

		} while (m_zstream.avail_in > 0);

		delete[] buffer;
		m_buffer_queue.pop();
	}

	if (!also_clear_m_buffer)
		return;

	m_zstream.next_in = m_buffer;
	m_zstream.avail_in = m_buffer_offset;

	do {
		ret = deflate(&m_zstream, Z_NO_FLUSH);
		if (ret < 0)
			throw SerializationError("SSCSMFileGrabber: deflate failed");

		if (m_zstream.avail_out > 0)
			break;

		// m_out_buffer is full, save it and continue compressing
		m_sscsm_files->emplace_back(m_out_buffer, m_buffer_size);
		m_out_buffer = new u8[m_buffer_size];
		m_zstream.next_out = m_out_buffer;
		m_zstream.avail_out = m_buffer_size;

	} while (m_zstream.avail_in > 0);
}
