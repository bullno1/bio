#ifndef BIO_FREEBSD_PLATFORM_H
#define BIO_FREEBSD_PLATFORM_H

#include "../array.h"
#include <signal.h>

/// Default number of events passed to kevent
#ifndef BIO_FREEBSD_DEFAULT_BATCH_SIZE
#	define BIO_FREEBSD_DEFAULT_BATCH_SIZE 4
#endif

struct kevent;

typedef struct {
	int kqueue;
	BIO_ARRAY(struct kevent) in_events;
	struct kevent* out_events;
	sigset_t old_sigmask;
} bio_platform_t;

#endif
