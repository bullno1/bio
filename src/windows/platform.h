#ifndef BIO_WINDOW_PLATFORM_H
#define BIO_WINDOW_PLATFORM_H

#include <stddef.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WS2tcpip.h>

/**
 * @defgroup windows Windows
 *
 * Windows implementation details
 *
 * @ingroup internal
 * @{
 */

/// Default number of items passed to GetQueuedCompletionStatusEx
#ifndef BIO_WINDOWS_DEFAULT_BATCH_SIZE
#	define BIO_WINDOWS_DEFAULT_BATCH_SIZE 4
#endif

/**@}*/

#ifndef DOXYGEN

typedef struct {
	HANDLE iocp;
	LARGE_INTEGER perf_counter_freq;
	LARGE_INTEGER start_time;

	OVERLAPPED_ENTRY* overlapped_entries;
	DWORD error_msg_buf_size;
	char* error_msg_buf;
	WSADATA wsadata;
} bio_platform_t;

#endif

#endif
