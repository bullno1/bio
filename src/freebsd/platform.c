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
}

void
bio_platform_notify(void) {

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
