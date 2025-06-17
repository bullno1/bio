#include "common.h"
#include <sys/event.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>

static const bio_tag_t BIO_PLATFORM_ERROR = BIO_TAG_INIT("bio.error.freebsd");

void
bio_platform_init(void) {
	bio_ctx.platform.kqueue = kqueuex(KQUEUE_CLOEXEC);
	struct kevent event = {
		.filter = EVFILT_USER,
		.flags = EV_ADD | EV_CLEAR,
		.fflags = NOTE_FFNOP,
	};
	kevent(bio_ctx.platform.kqueue, &event, 1, NULL, 0, NULL);

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

static void
bio_platform_dispatch_events(struct kevent* events, int num_events) {
	for (int i = 0; i < num_events; ++i) {
		const struct kevent* event = &events[i];
		bio_io_req_t* req = event->udata;
		if (req != NULL) {
			if (req->result != NULL) {
				*(req->result) = *event;
			}
			bio_raise_signal(req->signal);
		}
	}
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
		bio_io_req_t* req = event.udata;
		if (!req->cancelled) {
			bio_ctx.platform.in_events[out_index++] = event;
		}
	}

	int num_events = kevent(
		bio_ctx.platform.kqueue,
		bio_ctx.platform.in_events, (int)out_index,
		bio_ctx.platform.out_events, bio_ctx.options.freebsd.kqueue.batch_size,
		wait_timeout_ms >= 0 ? &timespec : NULL
	);
	bio_array_clear(bio_ctx.platform.in_events);
	bio_platform_dispatch_events(bio_ctx.platform.out_events, num_events);

	while (num_events == (int)bio_ctx.options.freebsd.kqueue.batch_size) {
		struct timespec no_wait = { 0 };
		num_events = kevent(
			bio_ctx.platform.kqueue,
			NULL, 0,
			bio_ctx.platform.out_events, bio_ctx.options.freebsd.kqueue.batch_size,
			&no_wait
		);
		bio_platform_dispatch_events(bio_ctx.platform.out_events, num_events);
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


bio_io_req_t
bio_prepare_io_req(struct kevent* result) {
	return (bio_io_req_t){
		.result = result,
		.signal = bio_make_signal(),
	};
}

void
bio_wait_for_io(bio_io_req_t* req) {
	bio_wait_for_one_signal(req->signal);
}

void
bio_cancel_io(bio_io_req_t* req) {
	req->cancelled = true;
	bio_raise_signal(req->signal);
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
