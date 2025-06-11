#include "common.h"
#include <bio/file.h>

#define BIO_MODE_READ   (1 << 0)
#define BIO_MODE_WRITE  (1 << 1)
#define BIO_MODE_APPEND (1 << 2)

static const bio_tag_t BIO_FILE_HANDLE = BIO_TAG_INIT("bio.handle.file");

typedef struct {
	HANDLE handle;
	uint64_t offset;
} bio_file_impl_t;

bio_file_t BIO_STDIN;
bio_file_t BIO_STDOUT;
bio_file_t BIO_STDERR;

typedef struct {
	HANDLE handle;
	OVERLAPPED* overlapped;
	void* buf;
	DWORD bytes_transferred;
	DWORD error;
	DWORD buf_size;
} bio_fs_rw_args_t;

typedef struct {
	HANDLE handle;
	const char* filename;
	DWORD error;
	DWORD desired_access;
	DWORD creation_disposition;
} bio_fs_open_args_t;

typedef struct {
	HANDLE handle;
	DWORD error;
} bio_fs_simple_args_t;

typedef struct {
	HANDLE handle;
	DWORD error;
	bio_stat_t* stat;
} bio_fs_stat_args_t;

static void
bio_fs_open_file(void* userdata) {
	bio_fs_open_args_t* args = userdata;
	args->handle = CreateFileA(
		args->filename,
		args->desired_access,
		FILE_SHARE_READ,
		NULL,
		args->creation_disposition,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
		NULL
	);
	if (args->handle != INVALID_HANDLE_VALUE) {
		SetFileCompletionNotificationModes(args->handle, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
		args->error = ERROR_SUCCESS;
	} else {
		args->error = GetLastError();
	}
}

static void
bio_fs_read_file(void* userdata) {
	bio_fs_rw_args_t* args = userdata;
	if (ReadFile(
		args->handle,
		args->buf, args->buf_size, &args->bytes_transferred,
		args->overlapped
	)) {
		args->error = ERROR_SUCCESS;
	} else {
		args->error = GetLastError();
	}
}

static void
bio_fs_write_file(void* userdata) {
	bio_fs_rw_args_t* args = userdata;
	if (WriteFile(
		args->handle,
		args->buf, args->buf_size, &args->bytes_transferred,
		args->overlapped
	)) {
		args->error = ERROR_SUCCESS;
	} else {
		args->error = GetLastError();
	}
}

static void
bio_fs_close_file(void* userdata) {
	bio_fs_simple_args_t* args = userdata;
	CancelIo(args->handle);
	if (CloseHandle(args->handle)) {
		args->error = ERROR_SUCCESS;
	} else {
		args->error = GetLastError();
	}
}

static void
bio_fs_flush_file(void* userdata) {
	bio_fs_simple_args_t* args = userdata;
	if (FlushFileBuffers(args->handle)) {
		args->error = ERROR_SUCCESS;
	} else {
		args->error = GetLastError();
	}
}

static void
bio_fs_stat_file(void* userdata) {
	bio_fs_stat_args_t* args = userdata;
	BY_HANDLE_FILE_INFORMATION file_info;
	if (GetFileInformationByHandle(args->handle, &file_info)) {
		args->stat->size = (uint64_t)file_info.nFileSizeHigh << 32 | (uint64_t)file_info.nFileSizeLow;
		args->error = ERROR_SUCCESS;
	} else {
		args->error = GetLastError();
	}
}

void
bio_fs_init(void) {
	bio_fdopen(&BIO_STDIN, GetStdHandle(STD_INPUT_HANDLE), NULL);
	bio_fdopen(&BIO_STDOUT, GetStdHandle(STD_OUTPUT_HANDLE), NULL);
	bio_fdopen(&BIO_STDERR, GetStdHandle(STD_ERROR_HANDLE), NULL);
}

void
bio_fs_cleanup(void) {
	bio_free(bio_close_handle(BIO_STDIN.handle, &BIO_FILE_HANDLE));
	bio_free(bio_close_handle(BIO_STDOUT.handle, &BIO_FILE_HANDLE));
	bio_free(bio_close_handle(BIO_STDERR.handle, &BIO_FILE_HANDLE));
}

bool
bio_fopen(
	bio_file_t* file_ptr,
	const char* restrict filename,
	const char* restrict mode,
	bio_error_t* error
) {
    DWORD desired_access = 0;
    DWORD creation_disposition = 0;

    int modeFlags = 0;
    const char* p = mode;

    // Parse primary mode
	switch (*p) {
		case 'r':
			modeFlags |= BIO_MODE_READ;
			desired_access = GENERIC_READ;
			creation_disposition = OPEN_EXISTING;
			break;
		case 'w':
			modeFlags |= BIO_MODE_WRITE;
			desired_access = GENERIC_WRITE;
			creation_disposition = CREATE_ALWAYS;
			break;
		case 'a':
			modeFlags |= BIO_MODE_WRITE | BIO_MODE_APPEND;
			desired_access = GENERIC_WRITE;
			creation_disposition = OPEN_ALWAYS;
			break;
	}
    ++p;

    // Parse additional flags
    while (*p) {
        switch (*p) {
			case '+':
				desired_access = GENERIC_READ | GENERIC_WRITE;
				if (modeFlags & BIO_MODE_READ) {
					creation_disposition = OPEN_EXISTING;
				}
				else if (modeFlags & BIO_MODE_WRITE && !(modeFlags & BIO_MODE_APPEND)) {
					creation_disposition = CREATE_ALWAYS;
				}
				else if (modeFlags & BIO_MODE_APPEND) {
					creation_disposition = OPEN_ALWAYS;
				}
				break;
			case 'b':
				break;
			case 't':
				break;
        }
        p++;
    }

	bio_fs_open_args_t args = {
		.desired_access = desired_access,
		.creation_disposition = creation_disposition,
		.filename = filename,
	};
	bio_run_async_and_wait(bio_fs_open_file, &args);
    if (args.error != ERROR_SUCCESS) {
        bio_set_error(error, args.error);
        return false;
    }

	return bio_fdopen(file_ptr, args.handle, error);
}

bool
bio_fdopen(bio_file_t* file_ptr, void* fd, bio_error_t* error) {
	if (CreateIoCompletionPort(fd, bio_ctx.platform.iocp, 0, 0) == NULL) {
		bio_set_last_error(error);
		return false;
	}

	bio_file_impl_t* impl = bio_malloc(sizeof(bio_file_impl_t));
	*impl = (bio_file_impl_t){ .handle = fd };
	*file_ptr = (bio_file_t){ .handle = bio_make_handle(impl, &BIO_FILE_HANDLE) };
	return true;
}

size_t
bio_fwrite(
	bio_file_t file,
	const void* buf,
	size_t size,
	bio_error_t* error
) {
	bio_file_impl_t* impl = bio_resolve_handle(file.handle, &BIO_FILE_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		if (size > MAXDWORD) { size = MAXDWORD; }

		bio_io_req_t req = bio_prepare_io_req();
		req.overlapped.Offset = impl->offset & 0xffffffff;
		req.overlapped.OffsetHigh = (impl->offset >> 32) & 0xffffffff;

		bio_fs_rw_args_t args = {
			.buf = (void*)buf,
			.buf_size = (DWORD)size,
			.handle = impl->handle,
			.overlapped = &req.overlapped,
		};
		bio_run_async_and_wait(bio_fs_write_file, &args);
		if (args.error == ERROR_SUCCESS) {
			bio_raise_signal(req.signal);
			impl->offset += args.bytes_transferred;
			return args.bytes_transferred;
		} else if (args.error == ERROR_IO_PENDING) {
			bio_wait_for_io(&req);
			if (!GetOverlappedResult(impl->handle, &req.overlapped, &args.bytes_transferred, FALSE)) {
				bio_set_last_error(error);
				return 0;
			}
			impl->offset += args.bytes_transferred;
			return args.bytes_transferred;
		} else {
			bio_set_error(error, args.error);
			return 0;
		}
	} else {
		bio_set_error(error, ERROR_INVALID_HANDLE);
		return 0;
	}
}

size_t
bio_fread(
	bio_file_t file,
	void* buf,
	size_t size,
	bio_error_t* error
) {
	bio_file_impl_t* impl = bio_resolve_handle(file.handle, &BIO_FILE_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		if (size > MAXDWORD) { size = MAXDWORD; }

		bio_io_req_t req = bio_prepare_io_req();
		req.overlapped.Offset = impl->offset & 0xffffffff;
		req.overlapped.OffsetHigh = (impl->offset >> 32) & 0xffffffff;

		bio_fs_rw_args_t args = {
			.buf = (void*)buf,
			.buf_size = (DWORD)size,
			.handle = impl->handle,
			.overlapped = &req.overlapped,
		};
		bio_run_async_and_wait(bio_fs_read_file, &args);
		if (args.error == ERROR_SUCCESS) {
			bio_raise_signal(req.signal);
			impl->offset += args.bytes_transferred;
			return args.bytes_transferred;
		} else if (args.error == ERROR_IO_PENDING) {
			bio_wait_for_io(&req);
			if (!GetOverlappedResult(impl->handle, &req.overlapped, &args.bytes_transferred, FALSE)) {
				bio_set_last_error(error);
				return 0;
			}
			impl->offset += args.bytes_transferred;
			return args.bytes_transferred;
		} else {
			bio_set_error(error, args.error);
			return 0;
		}
	} else {
		bio_set_error(error, ERROR_INVALID_HANDLE);
		return 0;
	}
}

bool
bio_fseek(
	bio_file_t file,
	int64_t offset,
	int origin,
	bio_error_t* error
) {
	bio_file_impl_t* impl = bio_resolve_handle(file.handle, &BIO_FILE_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		switch (origin) {
			case SEEK_CUR:
				impl->offset = (uint64_t)((int64_t)impl->offset + offset);
				return true;
			case SEEK_SET:
				impl->offset = (uint64_t)offset;
				return true;
			case SEEK_END:
				bio_set_error(error, ERROR_NOT_SUPPORTED);
				return false;
			default:
				bio_set_error(error, ERROR_INVALID_PARAMETER);
				return false;
		}
	} else {
		bio_set_error(error, ERROR_INVALID_HANDLE);
		return false;
	}
}

int64_t
bio_ftell(
	bio_file_t file,
	bio_error_t* error
) {
	bio_file_impl_t* impl = bio_resolve_handle(file.handle, &BIO_FILE_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		return impl->offset;
	} else {
		bio_set_error(error, ERROR_INVALID_HANDLE);
		return 0;
	}
}

bool
bio_fflush(bio_file_t file, bio_error_t* error) {
	bio_file_impl_t* impl = bio_resolve_handle(file.handle, &BIO_FILE_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		bio_fs_simple_args_t args = { .handle = impl->handle };
		bio_run_async_and_wait(bio_fs_flush_file, &args);
		if (args.error != ERROR_SUCCESS) {
			bio_set_error(error, args.error);
			return false;
		} else {
			return true;
		}
	} else {
		bio_set_error(error, ERROR_INVALID_HANDLE);
		return 0;
	}
}

bool
bio_fclose(bio_file_t file, bio_error_t* error) {
	bio_file_impl_t* impl = bio_close_handle(file.handle, &BIO_FILE_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		bio_fs_simple_args_t args = { .handle = impl->handle };
		bio_run_async_and_wait(bio_fs_close_file, &args);
		bio_free(impl);
		if (args.error != ERROR_SUCCESS) {
			bio_set_error(error, args.error);
			return false;
		} else {
			return true;
		}
	} else {
		bio_set_error(error, ERROR_INVALID_HANDLE);
		return false;
	}
}

bool
bio_fstat(bio_file_t file, bio_stat_t* stat, bio_error_t* error) {
	bio_file_impl_t* impl = bio_resolve_handle(file.handle, &BIO_FILE_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		bio_fs_stat_args_t args = { .handle = impl->handle, .stat = stat };
		bio_run_async_and_wait(bio_fs_stat_file, &args);
		return args.error == ERROR_SUCCESS;
	} else {
		bio_set_error(error, ERROR_INVALID_HANDLE);
		return 0;
	}
}
