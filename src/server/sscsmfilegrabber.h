
#pragma once

#include <string>
#include <vector>
#include <utility>
#include "util/numeric.h"

class SSCSMFileGrabber // todo: finish this
{
public:
	SSCSMFileGrabber(std::vector<std::string> *mods,
		std::vector<std::pair<u8 *, u16>> *sscsm_files);

	/*
	 * parse all mods for sscsm and remove them from the mods list if they do not
	 * have a sscsm
	 */
	void parseMods();

private:
	std::vector<std::string> *m_mods;

	std::vector<std::pair<u8 *, u16>> *m_sscsm_files;
};
