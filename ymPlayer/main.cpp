#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <chrono>
#include <conio.h>
#include <string>

#include "ym.h"
#include "stream.h"
#include "lzh.h"
#include "uart.h"


void output(const char *format, ...)
{
	char buffer[512];

	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	va_end(args);

	if(IsDebuggerPresent())
		OutputDebugStringA(buffer);
	printf(buffer);
}

bool load_ym(const char * filename, YMTune &tune)
{
	FILE *file = fopen(filename, "rb");
	if(!file)
		return false;

	fseek(file, 0, SEEK_END);
	DWORD size = ftell(file);
	fseek(file, 0, SEEK_SET);
	char *data = new char[size];
	fread(data, 1, size, file);
	fclose(file);

	printf("\n");

	if (!is_ym_file(data)) {
		output("compressed file, decompressing...");
		lzh::LZHeader header;
		if (!lzh::read_header(data, size, header)) {
			output("Invalid lzh header\n");
			return false;
		}

		char *decompressed_data = new char[header.decompressed_size];
		if (lzh::decompress(header.compressed_data, header.compressed_size, decompressed_data, header.decompressed_size)) {
			delete [] data;
			data = decompressed_data;
			size = header.decompressed_size;
			output("done\n");
		}
		else {
			output("Error while decompressing\n");
			return false;
		}
	}

	if (!is_ym_file(data)) {
		output("Not a valid YM format!\n");
		return false;
	}

	tune = create_ym_tune(data, size);

	output("File version: %s\n", tune.version);
	output("Name: %s\n", tune.song_info.name);
	output("Author: %s\n", tune.song_info.author);
	output("Description: %s\n", tune.song_info.description);
	output("---------------------------\n");
	output("Length: %ds\n", tune.header.frame_count / tune.header.frame_rate);
	output("Frame rate: %d Hz\n", tune.header.frame_rate);
	output("Clock: %d Hz\n", tune.header.clock);
	output("Interleaved: %s\n", tune.header.attributes & 0x1 ? "true" : "false");

	return true;
}


std::string get_dropped_filename()
{
	std::string filename;

	if (_kbhit()) {
		char ch = _getch();
		if (ch == '\"') {
			while ((ch = _getch()) != '\"') {
				filename += ch;
			}
			return filename;
		}
		else {
			filename += ch;
			while (_kbhit()) {
				filename += _getch();
			}
		}
	}
	return filename;
}


struct SongState
{
	std::chrono::time_point<std::chrono::steady_clock> song_start;
	uint32_t current_frame;
	uint32_t frame_time_us;
	bool is_playing;
	YMTune tune;
};


SongState play_song(YMTune &tune) {
	SongState state;
	state.song_start = std::chrono::steady_clock::now();
	state.current_frame = 0;
	state.frame_time_us = 1000000U / tune.header.frame_rate;
	state.is_playing = true;
	state.tune = tune;
	return state;
}

int main(int argc, char **argv)
{
	YMTune tune;
	bool have_tune = false;

	if (argc > 1) {
		have_tune = load_ym(argv[1], tune);
	}

	void *comm_handle = uart::open("com3", 57600);
	if (comm_handle == (void*)-1) {
		output("couldn't open com port for serial communincation\n");
		return 0;
	}

	int last_esc_state = GetKeyState(VK_ESCAPE);
	HANDLE out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO screen_buffer_info;
	GetConsoleScreenBufferInfo(out_handle, &screen_buffer_info);
	COORD cursor_coords = screen_buffer_info.dwCursorPosition;

	SongState current_song = {};
	if (have_tune) {
		current_song = play_song(tune);
	}

	// clear registers
	uint8_t stop_byte = 0;
	for (int j = 0; j < 16; ++j)
		uart::send_byte(comm_handle, stop_byte);


	static char print_buffer[255];
	int bytes_sent = 0;
	bool quit = false;
	do 
	{
		auto frame_start = std::chrono::steady_clock::now();
		
		// exit on double esc
		int esc_state = GetKeyState(VK_ESCAPE);
		if (esc_state == 0 && esc_state != last_esc_state)
			quit = true;
		last_esc_state = esc_state;


		std::string new_song_filename = get_dropped_filename();
		if (!new_song_filename.empty()) {
			YMTune new_tune;
			if (load_ym(new_song_filename.c_str(), new_tune)) {
				current_song = play_song(new_tune);

				printf("\n");

				CONSOLE_SCREEN_BUFFER_INFO screen_buffer_info;
				GetConsoleScreenBufferInfo(out_handle, &screen_buffer_info);
				cursor_coords = screen_buffer_info.dwCursorPosition;
				bytes_sent = 0;

				// clear registers
				for (int j = 0; j < 16; ++j)
					uart::send_byte(comm_handle, stop_byte);

			}
		}

		
		if (current_song.is_playing) {
			int bytes = uart::send_bytes(comm_handle, (uint8_t*)&current_song.tune.data.registers[current_song.current_frame * 16], 16);
			bytes_sent += bytes;
			auto song_time = std::chrono::steady_clock::now() - current_song.song_start;
			int32_t minutes = std::chrono::duration_cast<std::chrono::minutes>(song_time).count();
			int32_t seconds = std::chrono::duration_cast<std::chrono::seconds>(song_time).count() % 60;
			sprintf_s(print_buffer, "Playing: %02d:%02d - frame: %d/%d - bytes sent: %d", minutes, seconds, (int)current_song.current_frame, (int)current_song.tune.header.frame_count, bytes_sent);

			DWORD str_len = strlen(print_buffer);
			DWORD output_written;
			WriteConsoleOutputCharacterA(out_handle, print_buffer, str_len, cursor_coords, &output_written);

			current_song.current_frame++;
			if(current_song.current_frame > current_song.tune.header.frame_count)
				current_song.current_frame = current_song.tune.header.loop_frame;

			// wait 1000000 / frame_rate us to send next frame.
			while (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - frame_start).count() < current_song.frame_time_us) {};
		}

	} while(!quit);


	// clear registers
	for (int j = 0; j < 16; ++j)
		uart::send_byte(comm_handle, stop_byte);

	uart::close(comm_handle);

	return 0;
}