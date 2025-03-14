#ifndef BIO_LINUX_COMMON_H
#define BIO_LINUX_COMMON_H

#include "../internal.h"

struct io_uring_sqe*
bio_acquire_io_req(void);

int
bio_submit_io_req(struct io_uring_sqe* sqe, uint32_t* flags);

void
bio_set_errno(bio_error_t* error, int code);

int
bio_io_close(int fd);

static inline size_t
bio_result_to_size(int result, bio_error_t* error) {
	if (result >= 0) {
		return (size_t)result;
	} else {
		bio_set_errno(error, -result);
		return 0;
	}
}

static inline bool
bio_result_to_bool(int result, bio_error_t* error) {
	if (result >= 0) {
		return true;
	} else {
		bio_set_errno(error, -result);
		return false;
	}
}

#endif
