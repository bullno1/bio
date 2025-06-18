#ifndef BIO_LINUX_PLATFORM_H
#define BIO_LINUX_PLATFORM_H

#define _GNU_SOURCE
#include <liburing.h>
#include <threads.h>
#include <sys/signalfd.h>
#include <bio/bio.h>

/**
 * @defgroup linux Linux
 *
 * Linux implementation details.
 *
 * bio uses [iouring](https://man7.org/linux/man-pages/man7/io_uring.7.html) on Linux.
 *
 * For network, it detects whether `bind` or `listen` can be submitted through
 * iouring and fallback to the synchronous version instead as those calls
 * typically do not block.
 *
 * For @ref bio_platform_update, it uses a [futex](https://man7.org/linux/man-pages/man2/futex.2.html) if there is support.
 * Otherwise an [eventfd](https://man7.org/linux/man-pages/man2/eventfd.2.html) is used instead.
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
	bio_signal_t signal;
	int32_t res;
	uint32_t flags;
} bio_io_req_t;

typedef struct {
	struct io_uring ioring;

	// Signal handling
	struct signalfd_siginfo siginfo;
	sigset_t old_sigmask;
	int signalfd;
	bool signal_polled;

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

#endif
