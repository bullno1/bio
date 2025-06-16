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
 * bio uses uses [IOCP](https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports) on Windows.
 *
 * For network, it transparently handles the differences between named pipes and
 * regular sockets.
 *
 * For file, the @ref bio_run_async "async thread pool" is always used in tandem with IOCP.
 * This is because [not all file handles](https://stackoverflow.com/questions/55619555/win32-impossible-to-use-iocp-with-stdin-handle) support IOCP.
 * Moreover, async calls [may be converted](https://learn.microsoft.com/en-us/previous-versions/troubleshoot/windows/win32/asynchronous-disk-io-synchronous) to be synchronous.
 * For these reasons, bio will always send file I/O calls to the async thread pool even though it already uses IOCP with those calls.
 * File I/O heavy applications may need to increase the @ref bio_options_t::num_threads "thread pool size" from the default.
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
