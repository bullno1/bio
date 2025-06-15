#ifndef BIO_LINUX_PLATFORM_H
#define BIO_LINUX_PLATFORM_H

#define _GNU_SOURCE
#include <liburing.h>
#include <threads.h>
#include <signal.h>

/**
 * @defgroup linux Linux
 *
 * Linux implementation details
 *
 * @ingroup internal
 * @{
 */

/// Default queue size for the io_uring
#ifndef BIO_LINUX_DEFAULT_QUEUE_SIZE
#	define BIO_LINUX_DEFAULT_QUEUE_SIZE 64
#endif

/**@}*/

#ifndef DOXYGEN

typedef struct {
	struct io_uring ioring;

	// Notification from thread pool
	int eventfd;
	atomic_uint notification_counter;
	unsigned int ack_counter;
	sigset_t old_sigmask;

	// Compatibility
	bool has_op_bind;
	bool has_op_listen;
	bool has_op_futex_wait;
} bio_platform_t;

#endif

#endif
