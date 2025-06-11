#ifndef BIO_WINDOW_PLATFORM_H
#define BIO_WINDOW_PLATFORM_H

#include <stddef.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

typedef struct {
	HANDLE iocp;
	LARGE_INTEGER perf_counter_freq;
	LARGE_INTEGER start_time;

	OVERLAPPED_ENTRY* overlapped_entries;
	DWORD error_msg_buf_size;
	char* error_msg_buf;
} bio_platform_t;

#endif
