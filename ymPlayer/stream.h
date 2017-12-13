#pragma once
#include <stdint.h>
#include <intrin.h>
#include <string.h>

template <typename T>
inline T swap_endian(T val) {
	return val;
}

template <>
inline uint32_t swap_endian(uint32_t val) {
	return  _byteswap_ulong(val);
}

template <>
inline uint16_t swap_endian(uint16_t val) {
	return _byteswap_ushort(val);
}

template <>
inline uint64_t swap_endian(uint64_t val) {
	return _byteswap_uint64(val);
}

template <typename T>
inline T *offset_ptr(T *ptr, int size) {
	return reinterpret_cast<T*>(reinterpret_cast<char*>(ptr) + size);
}

template <typename T>
inline T read_type_endian_swap(char *ptr) {
	return swap_endian(*(T*)ptr);
}

template <typename T>
inline T read_type(char *ptr) {
	return *(T*)ptr;
}

class Stream
{
public:
	Stream() : _buffer(nullptr), _size(0), _ptr(0), _swap_endian(false) {}
	Stream(char *buffer, uint32_t buffer_size) : _buffer(buffer), _size(buffer_size), _ptr(buffer), _swap_endian(false) {}
	Stream(char *buffer, uint32_t buffer_size, uint32_t offset) : _buffer(buffer), _size(buffer_size), _ptr(buffer + offset), _swap_endian(false) {}

	void set_endian_swap(bool enable) { _swap_endian = enable; }

	template <typename T>
	T read_type() {
		T* ret = reinterpret_cast<T*>(_ptr);
		_ptr += sizeof(T);
		if(_swap_endian)
			return swap_endian(*ret);
		return *ret;
	}
	
	template <typename T>
	size_t write_type(T val) {
		if(_swap_endian)
			*reinterpret_cast<T*>(_ptr) = swap_endian(val);
		else
			*reinterpret_cast<T*>(_ptr) = val;
		_ptr += sizeof(T);
		return sizeof(T);
	}
	
	size_t read_bytes(char *buf, uint32_t size) {
		memcpy(buf, _ptr, size);
		_ptr += size;
		return size;
	}

	size_t write_bytes(char *buf, uint32_t size) {
		memcpy(_ptr, buf, size);
		_ptr += size;
		return size;
	}

	char *read_c_string() {
		char *ret = _ptr;
		_ptr += strlen(ret) + 1;
		return ret;
	}

	size_t write_c_string(char *str) {
		size_t len = strlen(str);
		memcpy(_ptr, str, len);
		_ptr += len;
		*_ptr = 0;
		_ptr++;
		return len+1;
	}

	char *ptr() {
		return (_ptr);
	}

	void skip(uint32_t size) {
		_ptr += size;
	}

private:
	char *_buffer;
	char *_ptr;
	uint32_t _offset;
	uint32_t _size;
	bool _swap_endian;
	
};
