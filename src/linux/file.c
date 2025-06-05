#define _GNU_SOURCE
#include "common.h"
#include <bio/file.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

static const bio_tag_t BIO_FILE_HANDLE = BIO_TAG_INIT("bio.handle.file");

bio_file_t BIO_STDIN;
bio_file_t BIO_STDOUT;
bio_file_t BIO_STDERR;

typedef struct {
	int fd;
	int64_t offset;
	bool seekable;
} bio_file_impl_t;

static bio_file_t
bio_file_from_fd(int fd, int64_t offset, bool seekable) {
	bio_file_impl_t* file_impl = bio_malloc(sizeof(bio_file_impl_t));
	*file_impl = (bio_file_impl_t){
		.fd = fd,
		.offset = seekable ? offset : -1,
		.seekable = seekable,
	};
	return (bio_file_t){
		.handle = bio_make_handle(file_impl, &BIO_FILE_HANDLE),
	};
}

void
bio_fs_init(void) {
	BIO_STDIN = bio_file_from_fd(0, -1, false);
	BIO_STDOUT = bio_file_from_fd(1, -1, false);
	BIO_STDERR = bio_file_from_fd(2, -1, false);
}

void
bio_fs_cleanup(void) {
	bio_free(bio_close_handle(BIO_STDIN.handle, &BIO_FILE_HANDLE));
	bio_free(bio_close_handle(BIO_STDOUT.handle, &BIO_FILE_HANDLE));
	bio_free(bio_close_handle(BIO_STDERR.handle, &BIO_FILE_HANDLE));
}

typedef struct {
	off64_t result;
	int fd;
	int whence;
} bio_fs_test_lseek_args_t;

static void
bio_fs_test_lseek(void* userdata) {
	bio_fs_test_lseek_args_t* args = userdata;
	args->result = lseek(args->fd, 0, args->whence);
}

bio_file_t
bio_fdopen(int fd) {
	return bio_file_from_fd(fd, -1, false);
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
		bio_set_errno(error, EINVAL);
		return false;
	}

	struct io_uring_sqe* sqe = bio_acquire_io_req();
	io_uring_prep_open(sqe, filename, flags | O_CLOEXEC, S_IRUSR | S_IWUSR);
	int fd = bio_submit_io_req(sqe, NULL);
	if (fd >= 0) {
		bio_fs_test_lseek_args_t args = {
			.fd = fd,
			.whence = (flags & O_APPEND) > 0 ? SEEK_END : SEEK_CUR,
		};
		bio_run_async_and_wait(bio_fs_test_lseek, &args);
		*file_ptr = bio_file_from_fd(fd, args.result, args.result >= 0);
		return true;
	} else {
		bio_set_errno(error, -fd);
		return false;
	}
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
		struct io_uring_sqe* sqe = bio_acquire_io_req();
		size = size < (size_t)INT32_MAX ? size : (size_t)INT32_MAX;
		io_uring_prep_write(sqe, impl->fd, buf, size, impl->offset);
		int result = bio_submit_io_req(sqe, NULL);
		size_t bytes_written = bio_result_to_size(result, error);
		if (impl->seekable) {
			impl->offset += bytes_written;
		}
		return bytes_written;
	} else {
		bio_set_errno(error, EINVAL);
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
		struct io_uring_sqe* sqe = bio_acquire_io_req();
		size = size < (size_t)INT32_MAX ? size : (size_t)INT32_MAX;
		io_uring_prep_read(sqe, impl->fd, buf, size, impl->offset);
		int result = bio_submit_io_req(sqe, NULL);
		size_t bytes_read = bio_result_to_size(result, error);
		if (impl->seekable) {
			impl->offset += bytes_read;
		}
		return bytes_read;
	} else {
		bio_set_errno(error, EINVAL);
		return 0;
	}
}

bool
bio_fflush(bio_file_t file, bio_error_t* error) {
	bio_file_impl_t* impl = bio_resolve_handle(file.handle, &BIO_FILE_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		struct io_uring_sqe* sqe = bio_acquire_io_req();
		io_uring_prep_fsync(sqe, impl->fd, 0);
		int result = bio_submit_io_req(sqe, NULL);
		return bio_result_to_bool(result, error);
	} else {
		bio_set_errno(error, EINVAL);
		return false;
	}
}

bool
bio_fclose(bio_file_t file, bio_error_t* error) {
	bio_file_impl_t* impl = bio_close_handle(file.handle, &BIO_FILE_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		int fd = impl->fd;
		bio_free(impl);
		int result = bio_io_close(fd);
		return bio_result_to_bool(result, error);
	} else {
		bio_set_errno(error, EINVAL);
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
		bio_set_errno(error, EINVAL);
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
		bio_set_errno(error, EINVAL);
		return -1;
	}
}

bool
bio_fstat(bio_file_t file, bio_stat_t* stat, bio_error_t* error) {
	bio_file_impl_t* impl = bio_resolve_handle(file.handle, &BIO_FILE_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		struct io_uring_sqe* sqe = bio_acquire_io_req();
		struct statx statx;
		io_uring_prep_statx(sqe, impl->fd, "", AT_EMPTY_PATH, STATX_SIZE, &statx);
		int result = bio_submit_io_req(sqe, NULL);
		if (result == 0) {
			stat->size = statx.stx_size;
			return true;
		} else {
			return bio_result_to_bool(result, error);
		}
	} else {
		bio_set_errno(error, EINVAL);
		return -1;
	}
}
