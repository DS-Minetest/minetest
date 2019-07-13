#include "sscsmfilegrabber.h"
#include "porting.h"
#include "util/serialize.h"
#include "log.h"

SSCSMFileGrabber::SSCSMFileGrabber(std::vector<std::string> *mods,
	std::vector<std::pair<u8 *, u16>> *sscsm_files) :
	m_mods(mods), m_sscsm_files(sscsm_files)
{
}

void SSCSMFileGrabber::parseMods() // todo: really parse mods // todo: use zlib to compress
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
