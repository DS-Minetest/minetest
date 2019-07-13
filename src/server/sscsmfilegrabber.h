
#pragma once

#include <string>
#include <vector>
#include <utility>
#include "util/numeric.h"
#include "server/mods.h"

class SSCSMFileGrabber
{
public:
	SSCSMFileGrabber(std::vector<std::string> *mods,
		std::vector<std::pair<u8 *, u16>> *sscsm_files,
		const std::unique_ptr<ServerModManager> &modmgr);

	/*
	 * parse all mods for sscsm and remove them from the mods list if they do not
	 * have a sscsm
	 */
	void parseMods();

private:
	/*
	 * add all files in a dir
	 */
	void addDir(const std::string &server_path, const std::string &client_path);

	/*
	 * server_path is the real absolute path of the file on the server
	 * client_path is the path that will be sent. The client will use it relative
	 * to its cache path
	 */
	void addFile(const std::string &server_path, const std::string &client_path);

	/*
	 * used for testing and will be removed
	 */
	void addDummyFile();

	std::vector<std::string> *m_mods;
	std::vector<std::pair<u8 *, u16>> *m_sscsm_files;
	const std::unique_ptr<ServerModManager> &m_modmgr;

	u8 *m_buffer;
	u16 m_buffer_offset;
	const u16 m_buffer_size = 32 * 1024; // uh, it should probably be an u32
};
