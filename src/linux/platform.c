#include "common.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <linux/futex.h>
#include <sys/syscall.h>

static const bio_tag_t BIO_PLATFORM_ERROR = BIO_TAG_INIT("bio.error.linux");

typedef struct {
	bio_signal_t signal;
	int32_t res;
	uint32_t flags;
} bio_io_req_t;

static inline int
futex(
	uint32_t* uaddr, int futex_op, uint32_t val,
	const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3
) {
	return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

void
bio_platform_init(void) {
	unsigned int queue_size = bio_ctx.options.linux.io_uring.queue_size;
	if (queue_size == 0) { queue_size = BIO_LINUX_DEFAULT_QUEUE_SIZE; }
	queue_size = bio_next_pow2(queue_size);
	bio_ctx.options.linux.io_uring.queue_size = queue_size;

	int flags = 0
		| IORING_SETUP_SUBMIT_ALL
		| IORING_SETUP_COOP_TASKRUN
		| IORING_SETUP_SINGLE_ISSUER
		| IORING_SETUP_DEFER_TASKRUN;
	int result = io_uring_queue_init(queue_size, &bio_ctx.platform.ioring, flags);
	if (result < 0) {
		fprintf(stderr, "Could not create io_uring: %s\n", strerror(errno));
		abort();
	}
	io_uring_ring_dontfork(&bio_ctx.platform.ioring);

	struct io_uring_probe* probe = io_uring_get_probe_ring(&bio_ctx.platform.ioring);
	bio_ctx.platform.has_op_bind = io_uring_opcode_supported(probe, IORING_OP_BIND);
	bio_ctx.platform.has_op_listen = io_uring_opcode_supported(probe, IORING_OP_LISTEN);
	bio_ctx.platform.has_op_futex_wait = io_uring_opcode_supported(probe, IORING_OP_FUTEX_WAIT);
	io_uring_free_probe(probe);

	if (bio_ctx.platform.has_op_futex_wait) {
		bio_ctx.platform.notification_counter = 0;
		bio_ctx.platform.ack_counter = 0;
	} else {
		bio_ctx.platform.eventfd = eventfd(0, EFD_CLOEXEC);
	}

	if (bio_ctx.platform.eventfd < 0) {
		fprintf(stderr, "Could not create eventfd: %s\n", strerror(errno));
		abort();
	}
}

void
bio_platform_cleanup(void) {
	if (!bio_ctx.platform.has_op_futex_wait) {
		close(bio_ctx.platform.eventfd);
	}

	io_uring_queue_exit(&bio_ctx.platform.ioring);
}

static void
bio_drain_io_completions(void) {
	struct io_uring_cqe *cqe;
	unsigned head;
	unsigned i = 0;

	io_uring_for_each_cqe(&bio_ctx.platform.ioring, head, cqe) {
		bio_io_req_t* request = io_uring_cqe_get_data(cqe);
		if (BIO_LIKELY(request != NULL)) {
			request->res = cqe->res;
			request->flags = cqe->flags;
			if (BIO_LIKELY(bio_handle_compare(request->signal.handle, BIO_INVALID_HANDLE) != 0)) {
				bio_raise_signal(request->signal);
			}
		}

		++i;
	}

	io_uring_cq_advance(&bio_ctx.platform.ioring, i);
}

static void
bio_platform_update_wait(bio_time_t wait_timeout_ms, bool notifiable) {
	struct io_uring* ioring = &bio_ctx.platform.ioring;

	if (wait_timeout_ms > 0) {  // Wait with timeout
		struct io_uring_cqe* cqe = NULL;

		struct __kernel_timespec timespec = {
			.tv_sec = wait_timeout_ms / 1000,
			.tv_nsec = (wait_timeout_ms % 1000) * 1000000,
		};
		int num_submitted = io_uring_submit_and_wait_timeout(ioring, &cqe, 1, &timespec, NULL);

		if (cqe != NULL) {  // We got events
			bio_drain_io_completions();
		}

		if (num_submitted <= 0) {  // Nothing was submitted due to event or timeout
			io_uring_submit_and_get_events(ioring);
			bio_drain_io_completions();
		}
	} else {  // Wait indefinitely until there is an event
		io_uring_submit_and_wait(ioring, 1);
		bio_drain_io_completions();
	}
}

static void
bio_platform_update_no_wait(void) {
	struct io_uring* ioring = &bio_ctx.platform.ioring;
	io_uring_submit_and_get_events(ioring);
	bio_drain_io_completions();
}

void
bio_platform_update(bio_time_t wait_timeout_ms, bool notifiable) {
	if (wait_timeout_ms == 0) {  // No wait
		bio_platform_update_no_wait();
	} else {
		if (bio_ctx.platform.has_op_futex_wait) { // Using futex for notification
			unsigned int notification_counter = atomic_load(&bio_ctx.platform.notification_counter);
			if (bio_ctx.platform.ack_counter != notification_counter) {
				// Cancel any wait if we got notification
				bio_platform_update_no_wait();
				bio_ctx.platform.ack_counter = notification_counter;
			} else {
				if (notifiable) {
					struct io_uring_sqe* sqe = bio_acquire_io_req();
					io_uring_prep_futex_wait(
						sqe,
						(uint32_t*)&bio_ctx.platform.notification_counter,
						bio_ctx.platform.ack_counter,
						FUTEX_BITSET_MATCH_ANY,
						FUTEX2_SIZE_U32 | FUTEX2_PRIVATE,
						0
					);
					io_uring_sqe_set_data(sqe, NULL);
				}

				bio_platform_update_wait(wait_timeout_ms, notifiable);
				bio_ctx.platform.ack_counter = atomic_load(&bio_ctx.platform.notification_counter);
			}
		} else { // Using eventfd for notification
			// Read from the eventfd so async threads can notify by writing to it
			uint64_t counter;
			if (notifiable) {
				struct io_uring_sqe* sqe = bio_acquire_io_req();
				io_uring_prep_read(
					sqe,
					bio_ctx.platform.eventfd,
					&counter, sizeof(counter),
					0
				);
				io_uring_sqe_set_data(sqe, NULL);
			}

			bio_platform_update_wait(wait_timeout_ms, notifiable);
		}
	}
}

void
bio_platform_notify(void) {
	if (bio_ctx.platform.has_op_futex_wait) {
		atomic_fetch_add(&bio_ctx.platform.notification_counter, 1);
		futex(
			(uint32_t*)&bio_ctx.platform.notification_counter,
			FUTEX_WAKE_BITSET | (FUTEX2_SIZE_U32 | FUTEX2_PRIVATE),
			1,
			NULL,
			NULL,
			FUTEX_BITSET_MATCH_ANY
		);
	} else {
		uint64_t counter = 1;
		write(bio_ctx.platform.eventfd, &counter, sizeof(counter));
	}
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

struct io_uring_sqe*
bio_acquire_io_req(void) {
	struct io_uring_sqe* sqe;

	while ((sqe = io_uring_get_sqe(&bio_ctx.platform.ioring)) == NULL) {
		io_uring_submit_and_get_events(&bio_ctx.platform.ioring);
		bio_drain_io_completions();
	}

	return sqe;
}

int
bio_submit_io_req(struct io_uring_sqe* sqe, uint32_t* flags) {
	bio_io_req_t req = {
		.signal = bio_make_signal(),
	};
	io_uring_sqe_set_data(sqe, &req);
	bio_wait_for_one_signal(req.signal);

	if (flags != NULL) { *flags = req.flags; }
	return req.res;
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

int
bio_io_close(int fd) {
	struct io_uring_sqe* sqe = bio_acquire_io_req();
	io_uring_prep_close(sqe, fd);
	return bio_submit_io_req(sqe, NULL);
}
