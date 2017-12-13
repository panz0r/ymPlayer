#pragma once
#include <stdint.h>

struct YMHeader
{
	uint32_t id;
	char leonardo[8];
	uint32_t frame_count;
	uint32_t attributes;
	uint16_t digidrum_count;
	uint32_t clock;
	uint16_t frame_rate;
	uint32_t loop_frame;
	uint16_t reserved;
};

struct YMSongInfo
{
	char *name;
	char *author;
	char *description;
};

struct YMData
{
	char *unprocessed_regs;
	char *registers;
	char *special_registers;
	uint32_t register_stride;
};

struct YMTune
{
	char version[4];
	YMHeader header;
	YMSongInfo song_info;
	YMData data;
};

bool is_ym_file(char *buffer);
YMTune create_ym_tune(char *buffer, uint32_t size);
void destroy_ym_tune(YMTune &tune);
