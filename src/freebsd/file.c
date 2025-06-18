#include "common.h"
#include <bio/file.h>
#include <sys/stat.h>
#include <sys/event.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <aio.h>

static const bio_tag_t BIO_FILE_HANDLE = BIO_TAG_INIT("bio.handle.file");

typedef enum {
	BIO_AIO_SUPPORT_UNKNOWN = 0,
	BIO_AIO_SUPPORT_ENABLED,
	BIO_AIO_SUPPORT_DISABLED,
} bio_aio_support_t;

bio_file_t BIO_STDIN;
bio_file_t BIO_STDOUT;
bio_file_t BIO_STDERR;

typedef struct {
	int fd;
	int64_t offset;
	bool seekable;
	bio_aio_support_t aio_support;
	struct aiocb* aio;
} bio_file_impl_t;

static bio_file_t
bio_file_from_fd(int fd, int64_t offset, bool seekable) {
	bio_file_impl_t* file_impl = bio_malloc(sizeof(bio_file_impl_t));
	*file_impl = (bio_file_impl_t){
		.fd = fd,
		.offset = seekable ? offset : 0,
		.seekable = seekable,
	};
	return (bio_file_t){
		.handle = bio_make_handle(file_impl, &BIO_FILE_HANDLE),
	};
}

void
bio_fs_init(void) {
	BIO_STDIN = bio_file_from_fd(0, 0, false);
	BIO_STDOUT = bio_file_from_fd(1, 0, false);
	BIO_STDERR = bio_file_from_fd(2, 0, false);
}

void
bio_fs_cleanup(void) {
	bio_free(bio_close_handle(BIO_STDIN.handle, &BIO_FILE_HANDLE));
	bio_free(bio_close_handle(BIO_STDOUT.handle, &BIO_FILE_HANDLE));
	bio_free(bio_close_handle(BIO_STDERR.handle, &BIO_FILE_HANDLE));
}

bool
bio_fdopen(bio_file_t* file_ptr, uintptr_t fd, bio_error_t* error) {
	*file_ptr = bio_file_from_fd(fd, -1, false);
	return true;
}

uintptr_t
bio_funwrap(bio_file_t file) {
	bio_file_impl_t* impl = bio_resolve_handle(file.handle, &BIO_FILE_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		return (uintptr_t)(impl->fd);
	} else {
		return (uintptr_t)(-1);
	}
}

typedef struct {
	const char* filename;
	int flags;
	int result;
	int64_t offset;
} bio_fs_fopen_args_t;

static void
bio_fs_fopen(void* userdata) {
	bio_fs_fopen_args_t* args = userdata;
	int fd = open(args->filename, args->flags, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		args->result = -errno;
		return;
	}

	args->result = fd;
	if ((args->flags & O_APPEND) > 0) {
		args->offset = lseek(fd, 0, SEEK_END);
	} else {
		args->offset = lseek(fd, 0, SEEK_CUR);
	}
}


bool
bio_fopen(
	bio_file_t* file_ptr,
	const char* restrict filename,
	const char* restrict mode,
	bio_error_t* error
) {
	int flags;
	// Translate mode string to open() flags
	if (mode[0] == 'r') {
		flags = O_RDONLY;
		if (mode[1] == '+') {
			flags = O_RDWR;
		}
	} else if (mode[0] == 'w') {
		flags = O_WRONLY | O_CREAT | O_TRUNC;
		if (mode[1] == '+') {
			flags = O_RDWR | O_CREAT | O_TRUNC;
		}
	} else if (mode[0] == 'a') {
		flags = O_WRONLY | O_CREAT | O_APPEND;
		if (mode[1] == '+') {
			flags = O_RDWR | O_CREAT | O_APPEND;
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
		return false;
	}

	bio_fs_fopen_args_t args = {
		.filename = filename,
		.flags = flags | O_CLOEXEC | O_NONBLOCK,
	};
	bio_run_async_and_wait(bio_fs_fopen, &args);
	if (args.result >= 0) {
		*file_ptr = bio_file_from_fd(args.result, args.offset, args.offset >= 0);
		return true;
	} else {
		bio_set_errno(error, -args.result);
		return false;
	}
}

bool
bio_fclose(bio_file_t file, bio_error_t* error) {
	bio_file_impl_t* impl = bio_close_handle(file.handle, &BIO_FILE_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		int fd = impl->fd;
		if (
			impl->aio != NULL
			&& aio_cancel(fd, impl->aio) == AIO_NOTCANCELED
		) {
			bio_set_errno(error, EBUSY);
			return false;
		} else {
			bio_free(impl);
			int result = close(fd);
			if (result == 0) {
				return true;
			} else {
				bio_set_errno(error, errno);
				return false;
			}
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
		return false;
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
		if (BIO_LIKELY(impl->seekable)) {
			if (origin == SEEK_SET) {
				impl->offset = offset;
				return true;
			} else if (origin == SEEK_CUR) {
				impl->offset += offset;
				return true;
			} else {
				bio_set_errno(error, ENOTSUP);
				return false;
			}
		} else {
			bio_set_errno(error, ENOTSUP);
			return false;
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
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
		if (BIO_LIKELY(impl->seekable)) {
			return impl->offset;
		} else {
			bio_set_errno(error, ENOTSUP);
			return -1;
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
		return -1;
	}
}

typedef struct {
	int fd;
	void* buf;
	size_t size;
	ssize_t result;
} bio_fs_io_args_t;

static void
bio_fs_write(void* userdata) {
	bio_fs_io_args_t* args = userdata;
	ssize_t result = write(args->fd, args->buf, args->size);
	if (result >= 0) {
		args->result = result;
	} else {
		args->result = -errno;
	}
}

static void
bio_fs_read(void* userdata) {
	bio_fs_io_args_t* args = userdata;
	ssize_t result = read(args->fd, args->buf, args->size);
	if (result >= 0) {
		args->result = result;
	} else {
		args->result = -errno;
	}
}

static void
bio_fs_fsync(void* userdata) {
	bio_fs_io_args_t* args = userdata;
	ssize_t result = fsync(args->fd);
	if (result >= 0) {
		args->result = result;
	} else {
		args->result = -errno;
	}
}

static size_t
bio_fwrite_in_async_thread(
	bio_file_impl_t* impl,
	const void* buf,
	size_t size,
	bio_error_t* error
) {
	bio_fs_io_args_t args = {
		.fd = impl->fd,
		.buf = (void*)buf,
		.size = size,
	};
	bio_run_async_and_wait(bio_fs_write, &args);
	if (args.result >= 0) {
		return (size_t)args.result;
	} else {
		bio_set_errno(error, -args.result);
		return 0;
	}
}

static size_t
bio_fread_in_async_thread(
	bio_file_impl_t* impl,
	void* buf,
	size_t size,
	bio_error_t* error
) {
	bio_fs_io_args_t args = {
		.fd = impl->fd,
		.buf = buf,
		.size = size,
	};
	bio_run_async_and_wait(bio_fs_read, &args);
	if (args.result >= 0) {
		return (size_t)args.result;
	} else {
		bio_set_errno(error, -args.result);
		return 0;
	}
}

static bool
bio_fsync_in_async_thread(bio_file_impl_t* impl, bio_error_t* error) {
	bio_fs_io_args_t args = { .fd = impl->fd };
	bio_run_async_and_wait(bio_fs_fsync, &args);
	if (args.result >= 0) {
		return true;
	} else {
		bio_set_errno(error, -args.result);
		return false;
	}
}

static ssize_t
bio_fwrite_aio(
	bio_file_t file,
	bio_file_impl_t* impl,
	const void* buf,
	size_t size
) {
	if (impl->aio != NULL) {
		return -EBUSY;
	}

	bio_io_req_t req = bio_prepare_io_req(NULL);
	struct aiocb aio = {
		.aio_fildes = impl->fd,
		.aio_buf = (void*)buf,
		.aio_nbytes = size,
		.aio_offset = impl->offset,
		.aio_sigevent = {
			.sigev_notify = SIGEV_KEVENT,
			.sigev_notify_kqueue = bio_ctx.platform.kqueue,
			.sigev_notify_kevent_flags = EV_DISPATCH,
			.sigev_value.sival_ptr = &req,
		},
	};
	int submission_status = aio_write(&aio);
	if (submission_status != 0) {
		return -errno;
	}

	impl->aio = &aio;
	bio_wait_for_io(&req);
	impl = bio_resolve_handle(file.handle, &BIO_FILE_HANDLE);
	if (impl != NULL) { impl->aio = NULL; }
	if (req.cancelled) { return -ECANCELED; }

	int error_code = aio_error(&aio);
	if (error_code != 0) {
		return -error_code;
	}

	ssize_t result = aio_return(&aio);
	if (result < 0) {
		return -errno;
	}

	if (impl->seekable) {
		impl->offset += (size_t)result;
	}
	return result;
}

static ssize_t
bio_fread_aio(
	bio_file_t file,
	bio_file_impl_t* impl,
	void* buf,
	size_t size
) {
	if (impl->aio != NULL) {
		return -EBUSY;
	}

	bio_io_req_t req = bio_prepare_io_req(NULL);
	struct aiocb aio = {
		.aio_fildes = impl->fd,
		.aio_buf = (void*)buf,
		.aio_nbytes = size,
		.aio_offset = impl->offset,
		.aio_sigevent = {
			.sigev_notify = SIGEV_KEVENT,
			.sigev_notify_kqueue = bio_ctx.platform.kqueue,
			.sigev_notify_kevent_flags = EV_DISPATCH,
			.sigev_value.sival_ptr = &req,
		},
	};
	int submission_status = aio_read(&aio);
	if (submission_status != 0) {
		return -errno;
	}

	impl->aio = &aio;
	bio_wait_for_io(&req);
	impl = bio_resolve_handle(file.handle, &BIO_FILE_HANDLE);
	if (impl != NULL) { impl->aio = NULL; }
	if (req.cancelled) { return -ECANCELED; }

	int error_code = aio_error(&aio);
	if (error_code != 0) {
		return -error_code;
	}

	ssize_t result = aio_return(&aio);
	if (result < 0) {
		return -errno;
	}

	if (impl->seekable) {
		impl->offset += (size_t)result;
	}
	return result;
}

static ssize_t
bio_fsync_aio(bio_file_t file, bio_file_impl_t* impl) {
	if (impl->aio != NULL) {
		return -EBUSY;
	}

	bio_io_req_t req = bio_prepare_io_req(NULL);
	struct aiocb aio = {
		.aio_fildes = impl->fd,
		.aio_sigevent = {
			.sigev_notify = SIGEV_KEVENT,
			.sigev_notify_kqueue = bio_ctx.platform.kqueue,
			.sigev_notify_kevent_flags = EV_DISPATCH,
			.sigev_value.sival_ptr = &req,
		},
	};
	int submission_status = aio_fsync(O_SYNC, &aio);
	if (submission_status != 0) {
		return -errno;
	}

	impl->aio = &aio;
	bio_wait_for_io(&req);
	impl = bio_resolve_handle(file.handle, &BIO_FILE_HANDLE);
	if (impl != NULL) { impl->aio = NULL; }
	if (req.cancelled) { return -ECANCELED; }

	int error_code = aio_error(&aio);
	if (error_code != 0) {
		return -error_code;
	}

	ssize_t result = aio_return(&aio);
	if (result < 0) {
		return -errno;
	}

	return result;
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
		switch (impl->aio_support) {
			case BIO_AIO_SUPPORT_UNKNOWN: {
				ssize_t result = bio_fwrite_aio(file, impl, buf, size);
				if (result >= 0) {
					impl->aio_support = BIO_AIO_SUPPORT_ENABLED;
					return result;
				} else if (result == -EOPNOTSUPP) {
					impl->aio_support = BIO_AIO_SUPPORT_DISABLED;
					return bio_fwrite_in_async_thread(impl, buf, size, error);
				} else {
					bio_set_errno(error, -result);
					return 0;
				}
			} break;
			case BIO_AIO_SUPPORT_ENABLED: {
				ssize_t result = bio_fwrite_aio(file, impl, buf, size);
				if (result >= 0) {
					return result;
				} else {
					bio_set_errno(error, -result);
					return 0;
				}
			} break;
			case BIO_AIO_SUPPORT_DISABLED:
				return bio_fwrite_in_async_thread(impl, buf, size, error);
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
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
		switch (impl->aio_support) {
			case BIO_AIO_SUPPORT_UNKNOWN: {
				ssize_t result = bio_fread_aio(file, impl, buf, size);
				if (result >= 0) {
					impl->aio_support = BIO_AIO_SUPPORT_ENABLED;
					return result;
				} else if (result == -EOPNOTSUPP) {
					impl->aio_support = BIO_AIO_SUPPORT_DISABLED;
					return bio_fread_in_async_thread(impl, buf, size, error);
				} else {
					bio_set_errno(error, -result);
					return 0;
				}
			} break;
			case BIO_AIO_SUPPORT_ENABLED: {
				ssize_t result = bio_fread_aio(file, impl, buf, size);
				if (result >= 0) {
					return result;
				} else {
					bio_set_errno(error, -result);
					return 0;
				}
			} break;
			case BIO_AIO_SUPPORT_DISABLED:
				return bio_fread_in_async_thread(impl, buf, size, error);
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
		return 0;
	}
}

bool
bio_fflush(bio_file_t file, bio_error_t* error) {
	bio_file_impl_t* impl = bio_resolve_handle(file.handle, &BIO_FILE_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		switch (impl->aio_support) {
			case BIO_AIO_SUPPORT_UNKNOWN: {
				ssize_t result = bio_fsync_aio(file, impl);
				if (result >= 0) {
					impl->aio_support = BIO_AIO_SUPPORT_ENABLED;
					return true;
				} else if (result == -EOPNOTSUPP) {
					impl->aio_support = BIO_AIO_SUPPORT_DISABLED;
					return bio_fsync_in_async_thread(impl, error);
				} else {
					bio_set_errno(error, -result);
					return false;
				}
			} break;
			case BIO_AIO_SUPPORT_ENABLED: {
				ssize_t result = bio_fsync_aio(file, impl);
				if (result >= 0) {
					return true;
				} else {
					bio_set_errno(error, -result);
					return false;
				}
			} break;
			case BIO_AIO_SUPPORT_DISABLED:
				return bio_fsync_in_async_thread(impl, error);
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
		return false;
	}
}

typedef struct {
	int fd;
	int result;
	struct stat* stat;
} bio_fs_fstat_args_t;

static void
bio_fs_fstat(void* userdata) {
	bio_fs_fstat_args_t* args = userdata;
	if (fstat(args->fd, args->stat) == 0) {
		args->result = 0;
	} else {
		args->result = errno;
	}
}

bool
bio_fstat(bio_file_t file, bio_stat_t* stat, bio_error_t* error) {
	bio_file_impl_t* impl = bio_resolve_handle(file.handle, &BIO_FILE_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		struct stat fstat;
		bio_fs_fstat_args_t args = {
			.fd = impl->fd,
			.stat = &fstat,
		};
		bio_run_async_and_wait(bio_fs_fstat, &args);
		if (args.result == 0) {
			stat->size = (size_t)fstat.st_size;
			return true;
		} else {
			bio_set_errno(error, args.result);
			return false;
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
		return false;
	}
}
