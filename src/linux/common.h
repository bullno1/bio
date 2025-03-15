#ifndef BIO_LINUX_COMMON_H
#define BIO_LINUX_COMMON_H

#include "../internal.h"

struct io_uring_sqe*
bio_acquire_io_req(void);

int
bio_submit_io_req(struct io_uring_sqe* sqe, uint32_t* flags);

void
bio_set_errno(bio_error_t* error, int code, const char* file, int line);

#define bio_set_errno(error, code) bio_set_errno(error, code, __FILE__, __LINE__)

#define BIO_CHECK_VARIABLE(VAR) (sizeof(&VAR)) // This will fail if VAR is a function call

// This must be a macro so that we get the correct error location
// But as a result, bio__result must be a variable and not an expression
#define bio_result_to_size(bio__result, bio__error) \
	(bio__result >= 0 \
	 ? (size_t)(BIO_CHECK_VARIABLE(bio__result), bio__result) \
	 : (bio_set_errno((bio__error), -(bio__result)), (size_t)0))

#define bio_result_to_bool(bio__result, bio__error) \
	((bio__result) >= 0 \
	 ? (size_t)(BIO_CHECK_VARIABLE(bio__result), true) \
	 : (bio_set_errno((bio__error), -(bio__result)), false))

int
bio_io_close(int fd);

#endif
