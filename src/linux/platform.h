#ifndef BIO_LINUX_PLATFORM_H
#define BIO_LINUX_PLATFORM_H

#define _GNU_SOURCE
#include <liburing.h>

typedef struct {
	struct io_uring ioring;

	int eventfd;

	// Compatibility
	bool has_op_bind;
	bool has_op_listen;
} bio_platform_t;

#endif
