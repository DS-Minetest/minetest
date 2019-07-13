#include "sscsmfilegrabber.h"
#include "porting.h"
#include "util/serialize.h"
#include "log.h"
#include <fstream>
#include "filesys.h"
#include "string.h"
//~ #include "zlib.h"

SSCSMFileGrabber::SSCSMFileGrabber(std::vector<std::string> *mods,
	std::vector<std::pair<u8 *, u16>> *sscsm_files,
	const std::unique_ptr<ServerModManager> &modmgr) :
	m_mods(mods), m_sscsm_files(sscsm_files), m_modmgr(modmgr)
{
}

void SSCSMFileGrabber::parseMods() // todo: use zlib to compress
{
	//~ addDummyFile();

	// create first buffer
	m_buffer_offset = 0;
	//~ m_buffer = new u8[LONG_STRING_MAX_LEN];
	m_buffer = new u8[m_buffer_size]; // todo: what size should buffers have? (currently this is low for testing)

	std::string builtin_path = porting::path_share + DIR_DELIM + "builtin";
	addDir(builtin_path + DIR_DELIM + "sscsm", "*builtin*/sscsm");
	addDir(builtin_path + DIR_DELIM + "common", "*builtin*/common");

	std::vector<std::string> modnames;
	m_modmgr->getModNames(modnames);

	for (const std::string &modname : modnames) {
		errorstream << "checking mod for sscsm: " << modname << std::endl;
		std::string path = m_modmgr->getModSpec(modname)->path;
		path = path + DIR_DELIM + "client";
		if (!fs::IsDir(path))
			// mod does not have a sscsm
			continue;
		m_mods->emplace_back(modname);
		addDir(path, modname);
	}

	// add last buffer
	//~ writeU8(m_buffer + m_buffer_offset, 0); // This hopefully fixes a bug
	m_sscsm_files->emplace_back(m_buffer, m_buffer_offset);
	// How can I deallocate the unused end of m_buffer?
}

void SSCSMFileGrabber::addDir(const std::string &server_path,
	const std::string &client_path)
{
	//~ errorstream << "add dir: " << server_path << std::endl;

	std::vector<std::string> subpaths;
	fs::GetRecursiveSubPaths(server_path, subpaths, true, {'.'});

	for (const std::string &path : subpaths) {
		errorstream << "[addDir] subpath: " << path << std::endl;
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
	errorstream << "add file: " << server_path << " to: " << client_path << std::endl;

	// Add client_path to buffer
	u8 path_length = client_path.length();
	writeU8(m_buffer + m_buffer_offset, path_length);
	m_buffer_offset++;
	const char *client_path_c = client_path.c_str();
	for (u8 i = 0; i < path_length; i++)
		m_buffer[m_buffer_offset + i] = client_path_c[i];
	m_buffer_offset += path_length;

	// Prepare file length writing
	// file_length will be written to file_length_buffer when it is finished
	u8 *file_length_buffer = m_buffer + m_buffer_offset;
	m_buffer_offset += 2;
	u16 file_length = 0;

	// Read the file
	std::ifstream file(server_path);
	while (true) {
		file.read((char *)(m_buffer + m_buffer_offset), m_buffer_size - m_buffer_offset);
		u16 read = file.gcount();
		m_buffer_offset += read;
		file_length += read;
		if (m_buffer_offset == m_buffer_size) {
			errorstream << "buffer run full" << std::endl;
			// buffer is full, file probably not empty
			// add buffer to m_sscsm_files
			m_sscsm_files->emplace_back(m_buffer, m_buffer_size);
			// make new buffer
			m_buffer_offset = 0;
			m_buffer = new u8[m_buffer_size];
			continue;
		}
		// buffer is not full, but file is empty
		// save file length
		writeU16(file_length_buffer, file_length);
		break;
	}
	file.close();
}

void SSCSMFileGrabber::addDummyFile()
{
	// make a dummy for testing
	std::string path = "test.txt";
	std::string filetext = "if foo then\n\tprint(\"bar\")\nend\n";

	u8 path_length = path.size();
	u16 file_length = filetext.size();

	u8 *data = new u8[1 + path_length + 2 + file_length];

	u8 *path_c = (u8 *)path.c_str();
	u8 *filetext_c = (u8 *)filetext.c_str();

	writeU8(data, path_length);

	for (u8 i = 0; i < path_length; i++)
		data[i + 1] = path_c[i];

	writeU16(data + 1 + path_length, file_length);

	for (u16 i = 0; i < file_length; i++)
		data[i + 1 + path_length + 2] = filetext_c[i];

	m_sscsm_files->emplace_back(data, 1 + path_length + 2 + file_length);
}
