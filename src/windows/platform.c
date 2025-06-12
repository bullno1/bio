#include "common.h"

static const char BIO_WINDOWS_NOTIFY_KEY = 0;

void
bio_platform_init(void) {
	QueryPerformanceFrequency(&bio_ctx.platform.perf_counter_freq);
	QueryPerformanceCounter(&bio_ctx.platform.start_time);

	bio_ctx.platform.iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);

	unsigned int batch_size = bio_ctx.options.windows.iocp.batch_size;
	if (batch_size == 0) { batch_size = 4; }
	bio_ctx.platform.overlapped_entries = bio_malloc(
		sizeof(OVERLAPPED_ENTRY) * batch_size
	);
}

void
bio_platform_cleanup(void) {
	bio_free(bio_ctx.platform.error_msg_buf);
	bio_ctx.platform.error_msg_buf = NULL;
	bio_ctx.platform.error_msg_buf_size = 0;
	bio_free(bio_ctx.platform.overlapped_entries);
	CloseHandle(bio_ctx.platform.iocp);
}

bio_time_t
bio_platform_current_time_ms(void) {
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	LONGLONG diff = now.QuadPart - bio_ctx.platform.start_time.QuadPart;
	int64_t denom = bio_ctx.platform.perf_counter_freq.QuadPart;
	int64_t numer = 1000;
	int64_t q = diff / denom;
	int64_t r = diff % denom;
	return q * numer + r * numer / denom;
}

void
bio_platform_update(bio_time_t wait_timeout_ms, bool notifiable) {
	unsigned int batch_size = bio_ctx.options.windows.iocp.batch_size;
	if (batch_size == 0) { batch_size = 4; }

	if (wait_timeout_ms >= INFINITE) { wait_timeout_ms = INFINITE - 1;  }
	if (wait_timeout_ms < 0) { wait_timeout_ms = INFINITE;  }

	ULONG num_entries = 0;
	GetQueuedCompletionStatusEx(
		bio_ctx.platform.iocp,
		bio_ctx.platform.overlapped_entries,
		batch_size,
		&num_entries,
		(DWORD)wait_timeout_ms,
		TRUE
	);

	for (ULONG entry_index = 0; entry_index < num_entries; ++entry_index) {
		OVERLAPPED_ENTRY* entry = &bio_ctx.platform.overlapped_entries[entry_index];
		if (entry->lpCompletionKey != (uintptr_t)&BIO_WINDOWS_NOTIFY_KEY) {
			bio_io_req_t* req = BIO_CONTAINER_OF(entry->lpOverlapped, bio_io_req_t, overlapped);
			bio_raise_signal(req->signal);
		}
	}
}

void
bio_platform_notify(void) {
	PostQueuedCompletionStatus(bio_ctx.platform.iocp, 0, (uintptr_t)&BIO_WINDOWS_NOTIFY_KEY, NULL);
}

bio_io_req_t
bio_prepare_io_req(void) {
	return (bio_io_req_t){ .signal = bio_make_signal() };
}

void
bio_wait_for_io(bio_io_req_t* req) {
	bio_wait_for_one_signal(req->signal);
}

void
bio_maybe_wait_after_success(bio_io_req_t* req, bio_completion_mode_t completion_mode) {
	if (completion_mode == BIO_COMPLETION_MODE_SKIP_ON_SUCCESS) {
		bio_raise_signal(req->signal);
	} else {
		bio_wait_for_one_signal(req->signal);
	}
}

void
bio_platform_begin_create_thread_pool(void) {
}

void
bio_platform_end_create_thread_pool(void) {
}

static
const char* bio_format_error(int code) {
	DWORD msg_len = FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		(DWORD)code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		bio_ctx.platform.error_msg_buf,
		bio_ctx.platform.error_msg_buf_size,
		NULL
	);
	if (msg_len >= bio_ctx.platform.error_msg_buf_size) {
		bio_free(bio_ctx.platform.error_msg_buf);
		bio_ctx.platform.error_msg_buf = bio_malloc(msg_len + 1);
		bio_ctx.platform.error_msg_buf_size = msg_len + 1;
		FormatMessageA(
			FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			(DWORD)code,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			bio_ctx.platform.error_msg_buf,
			bio_ctx.platform.error_msg_buf_size,
			NULL
		);
	}
	return bio_ctx.platform.error_msg_buf;
}

void
(bio_set_error)(bio_error_t* error, DWORD code, const char* file, int line) {
	if (BIO_LIKELY(error != NULL)) {
		error->tag = &BIO_PLATFORM_ERROR;
		error->code = (int)code;
		error->strerror = bio_format_error;
		error->file = file;
		error->line = line;
	}
}
