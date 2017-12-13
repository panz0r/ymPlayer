#include "lzh.h"
#include "stream.h"

namespace lzh {

const char *lzh_method = "-lh5-";

bool read_header(char* data, uint32_t size, LZHeader &header) {
	
	Stream input(data, size);
	memset(&header, 0, sizeof(LZHeader));

	header.header_size = input.read_type<uint8_t>();
	header.header_checksum = input.read_type<uint8_t>();
	input.read_bytes(header.method, sizeof(header.method));
	header.compressed_size = input.read_type<uint32_t>();
	header.decompressed_size = input.read_type<uint32_t>();
	header.timestamp = input.read_type<uint32_t>();
	header.file_attrib = input.read_type<uint8_t>();
	header.level = input.read_type<uint8_t>();
	uint8_t filename_length = input.read_type<uint8_t>();
	header.filename = new char[filename_length + 1];
	memset(header.filename, 0, filename_length+1);
	input.read_bytes(header.filename, filename_length);
	header.crc = input.read_type<uint16_t>();
	header.compressed_data = new char[header.compressed_size];
	input.read_bytes(header.compressed_data, header.compressed_size);

	if (header.header_size == 0 || header.level != 0 || strcmp(header.method, lzh_method) != 0) {
		return false;
	}

	return true;
}




#define DICBIT    13                              /* 12(-lh4-) or 13(-lh5-) */
#define DICSIZ (1U << DICBIT)
#define THRESHOLD 3

#define UCHAR_MAX 255
#define MAXMATCH (UCHAR_MAX + 1)
#define NC (UCHAR_MAX + MAXMATCH + 2 - THRESHOLD)

#define BUFFER_SIZE 4096
#define BITBUFSIZE (uint32_t)(8U * sizeof(uint16_t))
#define WINDOW_SIZE (1U<<13)

#define CBIT 9                                    /* $\lfloor \log_2 NC \rfloor + 1$ */
#define CODE_BIT  16                              /* codeword length */

#define NP (DICBIT + 1)
#define NT (CODE_BIT + 3)
#define PBIT 4      /* smallest integer such that (1U << PBIT) > NP */
#define TBIT 5      /* smallest integer such that (1U << TBIT) > NT */
#if NT > NP
#define NPT NT
#else
#define NPT NP
#endif

uint8_t buffer[WINDOW_SIZE];

struct LZHIO
{
	char *ptr;
	uint32_t offset;
	uint32_t size;
};

struct LZHContext
{
	LZHIO input;
	LZHIO output;

	char *source_data;
	uint32_t source_size;


	uint8_t buf[BUFFER_SIZE];

	uint8_t decompress_buffer[DICSIZ];


	uint16_t bitbuf;
	
	uint32_t subbitbuf; // ? type
	int bitcount; // ? type
	int fillbufsize; // ? type
	uint32_t fillbuf_i; // ?

	uint32_t decode_i;
	int		decode_j;


	uint8_t		pt_len[NPT];
	uint8_t		c_len[NC];
	uint32_t	blocksize;
	uint16_t	pt_table[256];
	uint16_t	c_table[4096];

	uint16_t	left[2 * NC - 1];
	uint16_t	right[2 * NC - 1];



};


#define min(a,b) ((a)<(b)?(a):(b))


int32_t make_table(LZHContext &context, int32_t nchar, uint8_t *bitlen,
	int32_t tablebits, uint16_t *table)
{
	uint16_t count[17], weight[17], start[18], *p;
	uint32_t jutbits, avail, mask;
	int32_t i, ch, len, nextcode;

	uint16_t *left = context.left;
	uint16_t *right = context.right;

	for (i = 1; i <= 16; i++)
		count[i] = 0;
	for (i = 0; i < nchar; i++)
		count[bitlen[i]]++;

	start[1] = 0;
	for (i = 1; i <= 16; i++)
		start[i + 1] = start[i] + (count[i] << (16 - i));
	if (start[17] != (uint16_t)(1U << 16))
		return (1); /* error: bad table */

	jutbits = 16 - tablebits;
	for (i = 1; i <= tablebits; i++)
	{
		start[i] >>= jutbits;
		weight[i] = 1U << (tablebits - i);
	}
	while (i <= 16)
	{
		weight[i] = 1U << (16 - i);
		i++;
	}

	i = start[tablebits + 1] >> jutbits;
	if (i != (uint16_t)(1U << 16))
	{
		int k = 1U << tablebits;
		while (i != k)
			table[i++] = 0;
	}

	avail = nchar;
	mask = 1U << (15 - tablebits);
	for (ch = 0; ch < nchar; ch++)
	{
		if ((len = bitlen[ch]) == 0)
			continue;
		nextcode = start[len] + weight[len];
		if (len <= tablebits)
		{
			for (i = start[len]; i < nextcode; i++)
				table[i] = ch;
		}
		else
		{
			uint16_t k = start[len];
			p = &table[k >> jutbits];
			i = len - tablebits;
			while (i != 0)
			{
				if (*p == 0)
				{
					right[avail] = left[avail] = 0;
					*p = avail++;
				}
				if (k & mask)
					p = &right[*p];
				else
					p = &left[*p];
				k <<= 1;
				i--;
			}
			*p = ch;
		}
		start[len] = nextcode;
	}
	return (0);
}



uint32_t read_bytes(LZHIO &input, uint8_t *dest, uint32_t size) {

	uint32_t to_read = min(size, input.size - input.offset);
	if (to_read > 0) {
		memcpy(dest, input.ptr + input.offset, to_read);
	}
	input.offset += to_read;
	return to_read;
}

uint32_t write_bytes(LZHIO &output, uint8_t *src, uint32_t size) {
	uint32_t to_write = min(size, output.size - output.offset);
	if (to_write > 0) {
		memcpy(output.ptr + output.offset, src, to_write);
	}
	output.offset += to_write;
	return to_write;
}


void fill_buffer(LZHContext &c, uint32_t n)
{
	c.bitbuf = (c.bitbuf << n) & 0xffff;
	while (n > c.bitcount) {
		c.bitbuf |= c.subbitbuf << (n -= c.bitcount);
		if (c.fillbufsize == 0) {
			c.fillbuf_i = 0;
			c.fillbufsize = read_bytes(c.input, c.buf, BUFFER_SIZE - 32);
		}

		if (c.fillbufsize > 0) {
			c.fillbufsize--;
			c.subbitbuf = c.buf[c.fillbuf_i++];
		} 
		else
		{
			c.subbitbuf = 0;
		}
		c.bitcount = 8;
	}
	c.bitbuf |= c.subbitbuf >> (c.bitcount -= n);
}

uint16_t getbits(LZHContext &context, int32_t n)
{
	uint16_t x = context.bitbuf >> (BITBUFSIZE - n);
	fill_buffer(context, n);
	return x;
}


void read_pt_len(LZHContext &context, int nn, int nbit, int i_special)
{
	int i, n;
	short c;
	uint16_t mask;

	n = getbits(context, nbit);
	if (n == 0)
	{
		c = getbits(context, nbit);
		for (i = 0; i < nn; i++)
			context.pt_len[i] = 0;
		for (i = 0; i < 256; i++)
			context.pt_table[i] = c;
	}
	else
	{
		i = 0;
		while (i < n)
		{
			c = context.bitbuf >> (BITBUFSIZE - 3);
			if (c == 7)
			{
				mask = 1U << (BITBUFSIZE - 1 - 3);
				while (mask & context.bitbuf)
				{
					mask >>= 1;
					c++;
				}
			}
			fill_buffer(context, (c < 7) ? 3 : c - 3);
			context.pt_len[i++] = uint8_t(c);
			if (i == i_special)
			{
				c = getbits(context, 2);
				while (--c >= 0)
					context.pt_len[i++] = 0;
			}
		}
		while (i < nn)
			context.pt_len[i++] = 0;
		make_table(context, nn, context.pt_len, 8, context.pt_table);
	}
}

void read_c_len(LZHContext &context)
{
	short i, c, n;
	uint16_t mask;

	n = getbits(context, CBIT);
	if (n == 0)
	{
		c = getbits(context, CBIT);
		for (i = 0; i < NC; i++)
			context.c_len[i] = 0;
		for (i = 0; i < 4096; i++)
			context.c_table[i] = c;
	}
	else
	{
		i = 0;
		while (i < n)
		{
			c = context.pt_table[context.bitbuf >> (BITBUFSIZE - 8)];
			if (c >= NT)
			{
				mask = 1U << (BITBUFSIZE - 1 - 8);
				do
				{
					if (context.bitbuf & mask)
						c = context.right[c];
					else
						c = context.left[c];
					mask >>= 1;
				} while (c >= NT);
			}
			fill_buffer(context, context.pt_len[c]);
			if (c <= 2)
			{
				if (c == 0)
					c = 1;
				else if (c == 1)
					c = getbits(context, 4) + 3;
				else
					c = getbits(context, CBIT) + 20;
				while (--c >= 0)
					context.c_len[i++] = 0;
			}
			else
				context.c_len[i++] = c - 2;
		}
		while (i < NC)
			context.c_len[i++] = 0;
		make_table(context, NC, context.c_len, 12, context.c_table);
	}
}


uint16_t decode_c(LZHContext &context)
{
	uint16_t j, mask;

	if (context.blocksize == 0)
	{
		context.blocksize = getbits(context, 16);
		read_pt_len(context, NT, TBIT, 3);
		read_c_len(context);
		read_pt_len(context, NP, PBIT, -1);
	}
	context.blocksize--;
	j = context.c_table[context.bitbuf >> (BITBUFSIZE - 12)];
	if (j >= NC)
	{
		mask = 1U << (BITBUFSIZE - 1 - 12);
		do
		{
			if (context.bitbuf & mask)
				j = context.right[j];
			else
				j = context.left[j];
			mask >>= 1;
		} while (j >= NC);
	}
	fill_buffer(context, context.c_len[j]);
	return j;
}

uint16_t decode_p(LZHContext &context)
{
	uint16_t j, mask;

	j = context.pt_table[context.bitbuf >> (BITBUFSIZE - 8)];
	if (j >= NP)
	{
		mask = 1U << (BITBUFSIZE - 1 - 8);
		do
		{
			if (context.bitbuf & mask)
				j = context.right[j];
			else
				j = context.left[j];
			mask >>= 1;
		} while (j >= NP);
	}
	fill_buffer(context, context.pt_len[j]);
	if (j != 0)
		j = (1U << (j - 1)) + getbits(context, j - 1);
	return j;
}


void initialize(LZHContext &context)
{
	fill_buffer(context, BITBUFSIZE);
}

void decode(LZHContext &context, uint32_t size, uint8_t *buffer) {
	
	uint32_t r = 0, c = 0;

	while (--context.decode_j >= 0) {
		buffer[r] = buffer[context.decode_i];
		context.decode_i = (context.decode_i) & (DICSIZ - 1);
		if(++r == size)
			return;
	}

	for (;;) {
		c = decode_c(context);
		if(c <= 255) {
			buffer[r] = c;
			if(++r == size)
				return;
		}
		else {
			context.decode_j = c - (256 - THRESHOLD);
			context.decode_i = (r - decode_p(context) - 1) & (DICSIZ - 1);

			while (--context.decode_j >= 0) {
				buffer[r] = buffer[context.decode_i];
				context.decode_i = (context.decode_i + 1) & (DICSIZ - 1);
				if(++r == size)
					return;
			}
		}
	}

}

bool decompress(char *compressed, uint32_t compressed_size, char *decompressed, uint32_t decompressed_size)
{
	LZHContext context;
	memset(&context, 0, sizeof(LZHContext));

	context.input.ptr = compressed;
	context.input.size = compressed_size;
	context.output.ptr = decompressed;
	context.output.size = decompressed_size;

	initialize(context);


	uint32_t remaining = decompressed_size;
	while (remaining != 0) {
		
		uint32_t n = (remaining > WINDOW_SIZE) ? WINDOW_SIZE : remaining;
	
		decode(context, n, context.decompress_buffer);
	
		uint32_t bytes_written = write_bytes(context.output, context.decompress_buffer, n);
		remaining -= bytes_written;
	}

	return true;
}


}