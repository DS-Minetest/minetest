
#pragma once

#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>
#include "util/numeric.h"
#include "zlib.h"

class ServerModManager;

class SSCSMFileGrabber
{
public:
	SSCSMFileGrabber(std::vector<std::string> *mods,
		std::vector<std::pair<u8 *, u32>> *sscsm_files,
		const std::unique_ptr<ServerModManager> &modmgr);

	~SSCSMFileGrabber();

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
	 * clears m_buffer_queue by compressing the buffers and putting them into m_sscsm_files
	 */
	void clearQueue(bool also_clear_m_buffer);

	std::vector<std::string> *m_mods;
	std::vector<std::pair<u8 *, u32>> *m_sscsm_files;
	const std::unique_ptr<ServerModManager> &m_modmgr;

	u8 *m_buffer;
	u32 m_buffer_offset;
	const u32 m_buffer_size = 1024; // todo: what size should buffers have?

	z_stream m_zstream;

	std::queue<u8 *> m_buffer_queue; // the size of the buffers in here is always m_buffer_size

	u8 *m_out_buffer;
};
