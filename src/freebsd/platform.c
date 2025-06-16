#include "common.h"
#include <sys/event.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

static const bio_tag_t BIO_PLATFORM_ERROR = BIO_TAG_INIT("bio.error.freebsd");

void
bio_platform_init(void) {
	bio_ctx.platform.kqueue = kqueuex(KQUEUE_CLOEXEC);
	if (bio_ctx.options.freebsd.kqueue.batch_size == 0) {
		bio_ctx.options.freebsd.kqueue.batch_size = BIO_FREEBSD_DEFAULT_BATCH_SIZE;
	}

	bio_ctx.platform.out_events = bio_malloc(
		sizeof(struct kevent) * bio_ctx.options.freebsd.kqueue.batch_size
	);
}

void
bio_platform_cleanup(void) {
	bio_free(bio_ctx.platform.out_events);
	bio_array_free(bio_ctx.platform.in_events);
	bio_ctx.platform.in_events = NULL;
	close(bio_ctx.platform.kqueue);
}

void
bio_platform_update(bio_time_t wait_timeout_ms, bool notifiable) {
	struct timespec timespec = {
		.tv_sec = wait_timeout_ms / 1000,
		.tv_nsec = (wait_timeout_ms % 1000) * 1000000,
	};

	// Filter out cancelled event
	size_t out_index = 0;
	size_t num_in_events = bio_array_len(bio_ctx.platform.in_events);
	for (size_t event_index = 0; event_index < num_in_events; ++event_index) {
		struct kevent event = bio_ctx.platform.in_events[event_index];
		const struct kevent* proxied_event = event.udata;
		if (proxied_event->udata != NULL) {
			bio_ctx.platform.in_events[out_index++] = event;
		}
	}

	if (notifiable) {
		struct kevent event = {
			.filter = EVFILT_USER,
			.flags = EV_ADD | EV_DISPATCH,
			.fflags = NOTE_FFNOP,
		};
		bio_array_push(bio_ctx.platform.in_events, event);
	}

	int num_events = kevent(
		bio_ctx.platform.kqueue,
		bio_ctx.platform.in_events, out_index,
		bio_ctx.platform.out_events, bio_ctx.options.freebsd.kqueue.batch_size,
		wait_timeout_ms >= 0 ? &timespec : NULL
	);
	bio_array_clear(bio_ctx.platform.in_events);
	for (int i = 0; i < num_events; ++i) {
		struct kevent* event = &bio_ctx.platform.out_events[i];
		if (event->udata != NULL) {
			struct kevent* proxied_event = event->udata;
			bio_signal_t* signal = proxied_event->udata;
			*proxied_event = *event;
			bio_raise_signal(*signal);
		}
	}
}

void
bio_platform_notify(void) {
	struct kevent event = {
		.filter = EVFILT_USER,
		.fflags = NOTE_FFNOP | NOTE_TRIGGER,
	};
	kevent(
		bio_ctx.platform.kqueue,
		&event, 1,
		NULL, 0,
		NULL
	);
}

bio_time_t
bio_platform_current_time_ms(void) {
	struct timespec timespec;
	clock_gettime(CLOCK_MONOTONIC, &timespec);
	return (timespec.tv_sec * 1000L) + (timespec.tv_nsec / 1000000L);
}

void
bio_platform_begin_create_thread_pool(void) {
	// block all signals before creating new threads so only the main thread
	// can receive signal
	sigset_t sigset;
	sigfillset(&sigset);
	pthread_sigmask(SIG_BLOCK, &sigset, &bio_ctx.platform.old_sigmask);
}

void
bio_platform_end_create_thread_pool(void) {
	pthread_sigmask(SIG_SETMASK, &bio_ctx.platform.old_sigmask, NULL);
}

void
bio_wait_for_event(struct kevent* event) {
	bio_signal_t signal = bio_make_signal();
	event->udata = &signal;

	struct kevent proxy_event = *event;
	proxy_event.udata = event;
	bio_array_push(bio_ctx.platform.in_events, proxy_event);

	bio_wait_for_one_signal(signal);
	event->udata = NULL;
}

void
bio_cancel_event(struct kevent* event) {
	bio_signal_t* signal = event->udata;
	event->udata = NULL;
	event->flags |= EV_ERROR;
	event->data = ECANCELED;
	bio_raise_signal(*signal);
}

static
const char* bio_format_errno(int code) {
	return strerror(code);
}

void
(bio_set_errno)(bio_error_t* error, int code, const char* file, int line) {
	if (BIO_LIKELY(error != NULL)) {
		error->tag = &BIO_PLATFORM_ERROR;
		error->code = code;
		error->strerror = bio_format_errno;
		error->file = file;
		error->line = line;
	}
}
