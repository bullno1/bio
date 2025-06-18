#ifndef BIO_FREEBSD_PLATFORM_H
#define BIO_FREEBSD_PLATFORM_H

#include "../array.h"
#include <sys/signal.h>
#include <bio/bio.h>

/**
 * @defgroup freebsd FreeBSD
 *
 * FreeBSD implementation details
 *
 * bio uses uses [kqueue](https://man.freebsd.org/cgi/man.cgi?query=kqueue&apropos=0&sektion=2&manpath=FreeBSD+15.0-CURRENT&arch=default&format=html) on FreeBSD.
 *
 * Abstract socket is not supported.
 * Using it will result in `ENOTSUP`.
 *
 * For file, [aio](https://man.freebsd.org/cgi/man.cgi?query=aio&sektion=4&apropos=0&manpath=FreeBSD+14.3-RELEASE+and+Ports) is used.
 * However, it is possible that unsafe operations on special files might not be enabled with `vfs.aio.enable_unsafe=1`.
 * For each file, bio will detect `EOPNOTSUPP` on the initial aio call and fallback to the @ref bio_run_async "async thread pool".
 *
 * @ingroup internal
 * @{
 */

/// Default number of output events passed to kevent
#ifndef BIO_FREEBSD_DEFAULT_BATCH_SIZE
#	define BIO_FREEBSD_DEFAULT_BATCH_SIZE 4
#endif

/**@}*/

struct kevent;

typedef struct {
	bio_signal_t signal;
	struct kevent* result;
	bool cancelled;
} bio_io_req_t;

typedef enum {
	BIO_SIGNAL_UNBLOCKED,
	BIO_SIGNAL_BLOCKED,
	BIO_SIGNAL_WAITED,
} bio_signal_state_t;

typedef struct {
	int kqueue;
	BIO_ARRAY(struct kevent) in_events;
	struct kevent* out_events;

	sigset_t old_sigmask;
	bio_signal_state_t signal_state;
	bio_io_req_t signal_req;
} bio_platform_t;

#endif
