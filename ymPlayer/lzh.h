#pragma once
#include <stdint.h>

namespace lzh {

struct LZHeader
{
	uint8_t		header_size;
	uint8_t		header_checksum;
	char		method[5];
	uint32_t	compressed_size;
	uint32_t	decompressed_size;
	uint32_t	timestamp;
	uint8_t		file_attrib;
	uint8_t		level;
	char		*filename;
	uint16_t	crc;
	char		*compressed_data;
};

bool read_header(char* data, uint32_t size, LZHeader &header);
bool decompress(char *compressed, uint32_t compressed_size, char *decompressed, uint32_t decompressed_size);


}
