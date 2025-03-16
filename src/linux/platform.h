#ifndef BIO_LINUX_PLATFORM_H
#define BIO_LINUX_PLATFORM_H

#define _GNU_SOURCE
#include <liburing.h>
#include <threads.h>

typedef struct {
	struct io_uring ioring;

	// Notification from thread pool
	int eventfd;
	atomic_uint notification_counter;
	unsigned int ack_counter;

	// Compatibility
	bool has_op_bind;
	bool has_op_listen;
	bool has_op_futex_wait;
} bio_platform_t;

#endif
