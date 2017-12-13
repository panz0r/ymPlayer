#include "ym.h"
#include "stream.h"

static const uint32_t YM3 = ('Y' << 24) | ('M' << 16) | ('3' << 8) | ('!');
static const uint32_t YM4 = ('Y' << 24) | ('M' << 16) | ('4' << 8) | ('!');
static const uint32_t YM5 = ('Y' << 24) | ('M' << 16) | ('5' << 8) | ('!');
static const uint32_t YM6 = ('Y' << 24) | ('M' << 16) | ('6' << 8) | ('!');
static const uint32_t END = ('E' << 24) | ('n' << 16) | ('d' << 8) | ('!');

static const unsigned char reg_masks[] =
{
	0xff,	// fine tone A
	0x0f,	// coarse tone A
	0xff,	// fine tone B
	0x0f,	// coarse tone B
	0xff,	// fine tone C
	0x0f,	// coarse tone C
	0x1f,	// freq noise
	0xff,	// mixer, also fill with 0xc0 to prevent the IO bits to be clear
	0x1f,	// level chan A
	0x1f,	// level chan B
	0x1f,	// level chan C
	0xff,	// fine freq envelope
	0xff,	// coarse freq envelope
	0x0f,	// envelope shape
	0x00,	// IO A
	0x00,	// IO B
};

static const unsigned char reg_fill_bits[] = {
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0xc0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
	0x0,
};


bool is_ym_file(char *buffer)
{
	bool is_ym = false;
	Stream stream(buffer, 4);
	stream.set_endian_swap(true);
	uint32_t id = stream.read_type<uint32_t>();
	is_ym |= id == YM3;
	is_ym |= id == YM4;
	is_ym |= id == YM5;
	is_ym |= id == YM6;
	return is_ym;
}

bool load_ym5(YMTune &tune, Stream &input)
{
	const char *version = "YM5";
	strcpy(tune.version, version);

	YMHeader &header = tune.header;
	YMSongInfo &song_info = tune.song_info;
	YMData &data = tune.data;

	input.read_bytes(header.leonardo, 8);
	header.frame_count = input.read_type<uint32_t>();
	header.attributes = input.read_type<uint32_t>();
	header.digidrum_count = input.read_type<uint16_t>();
	header.clock = input.read_type<uint32_t>();
	header.frame_rate = input.read_type<uint16_t>();
	header.loop_frame = input.read_type<uint32_t>();
	header.reserved = input.read_type<uint16_t>();

	// Todo, read digidrum samples
	if (header.digidrum_count > 0) {
		uint32_t size = input.read_type<uint32_t>();
		input.skip(size);
	}

	char *name = input.read_c_string();
	song_info.name = new char[strlen(name) + 1];
	strcpy(song_info.name, name);

	char *author = input.read_c_string();
	song_info.author = new char[strlen(author) + 1];
	strcpy(song_info.author, author);

	char *description = input.read_c_string();
	song_info.description = new char[strlen(description) + 1];
	strcpy(song_info.description, description);

	data.register_stride = 16;
	data.unprocessed_regs = input.ptr();

	input.skip(header.frame_count * tune.data.register_stride);

	uint32_t end_marker = input.read_type<uint32_t>();
	return end_marker == END;
}

bool load_ym6(YMTune &tune, Stream &input)
{
	if (load_ym5(tune, input)) {
		const char *version = "YM6";
		strcpy(tune.version, version);
		return true;
	}
	return false;
}



void process_registers(YMData &data, uint32_t frame_count, bool deinterleave)
{
	data.registers = new char[frame_count * data.register_stride];
	data.special_registers = new char[frame_count * data.register_stride];

	uint32_t fc = frame_count;
	uint32_t stride = data.register_stride;
	char *src_regs = data.unprocessed_regs;
	char *dst_regs = data.registers;
	char *dst_special = data.special_registers;

	if (deinterleave) {
		// deinterleave frames
		for (uint32_t i = 0; i < fc; ++i) {
			for (uint32_t j = 0; j < stride; ++j) {
				*dst_special = src_regs[j * fc + i] & ~reg_masks[j];
				*dst_regs = src_regs[j * fc + i] & reg_masks[j] | reg_fill_bits[j];
				dst_special++;
				dst_regs++;
			}
		}
	}
	else {
		for (uint32_t i = 0; i < fc * stride; ++i) {
			*dst_special = src_regs[i] & ~reg_masks[i % stride];
			*dst_regs = src_regs[i] & reg_masks[i % stride] | reg_fill_bits[i % stride];
			dst_special++;
			dst_regs++;
		}
	}
}

YMTune create_ym_tune(char *buffer, uint32_t size)
{
	Stream input(buffer, size);
	input.set_endian_swap(true);

	YMTune tune = {};
	
	// read header
	tune.header.id = input.read_type<uint32_t>();

	switch (tune.header.id) {
		case YM3:
			break;
		case YM4:
			break;
		case YM5:
			load_ym5(tune, input);
			break;
		case YM6:
			load_ym6(tune, input);
			break;
	}
	
	process_registers(tune.data, tune.header.frame_count, tune.header.attributes & 0x1);
	return tune;
}

void destroy_ym_tune(YMTune &tune)
{
	delete [] tune.data.registers;
	delete [] tune.data.special_registers;
	delete [] tune.song_info.author;
	delete [] tune.song_info.name;
	delete [] tune.song_info.description;
}
