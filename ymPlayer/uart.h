#pragma once
#include <stdint.h>

namespace uart
{

void *open(const char *port, uint32_t baud_rate);
void close(void *handle);
int send_byte(void *handle, uint8_t b);
int send_bytes(void *handle, uint8_t *buffer, uint32_t size);


}