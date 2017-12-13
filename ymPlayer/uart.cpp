#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "uart.h"

namespace uart
{

void *open(const char *port, uint32_t baud_rate)
{
	HANDLE serial_comm = CreateFileA(port, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
	
	DCB dcb;
	dcb.DCBlength = sizeof(dcb);
	GetCommState(serial_comm, &dcb);
	dcb.BaudRate = baud_rate;
	SetCommState(serial_comm, &dcb);
	return serial_comm;
}

void close(void *handle) {
	CloseHandle(handle);
}

int send_byte(void *handle, uint8_t b)
{
	DWORD written;
	WriteFile(handle, &b, 1, &written, nullptr);
	return written;
}

int send_bytes(void *handle, uint8_t *buffer, uint32_t size)
{
	DWORD written;
	WriteFile(handle, buffer, size, &written, nullptr);
	return written;
}

}