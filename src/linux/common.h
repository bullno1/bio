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

#endif
