#include "common.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/eventfd.h>

typedef struct {
	bio_signal_t signal;
	int32_t res;
	uint32_t flags;
} bio_io_req_t;

void
bio_platform_init(void) {
	unsigned int queue_size = bio_ctx.options.linux.io_uring.queue_size;
	if (queue_size == 0) { queue_size = 64; }
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

	struct io_uring_probe* probe = io_uring_get_probe_ring(&bio_ctx.platform.ioring);
	bio_ctx.platform.has_op_bind = io_uring_opcode_supported(probe, IORING_OP_BIND);
	bio_ctx.platform.has_op_listen = io_uring_opcode_supported(probe, IORING_OP_LISTEN);
	free(probe);

	bio_ctx.platform.eventfd = eventfd(0, EFD_CLOEXEC);
	if (bio_ctx.platform.eventfd < 0) {
		fprintf(stderr, "Could not create eventfd: %s\n", strerror(errno));
		abort();
	}
}

void
bio_platform_cleanup(void) {
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

void
bio_platform_update(bio_platform_update_type_t type) {
	switch (type) {
		case BIO_PLATFORM_UPDATE_NO_WAIT:
			io_uring_submit(&bio_ctx.platform.ioring);
			bio_drain_io_completions();
			break;
		case BIO_PLATFORM_UPDATE_WAIT_INDEFINITELY:
			io_uring_submit_and_wait(&bio_ctx.platform.ioring, 1);
			bio_drain_io_completions();
			break;
		case BIO_PLATFORM_UPDATE_WAIT_NOTIFIABLE:
			{
				// Read from the eventfd so async threads can notify
				struct io_uring_sqe* sqe = bio_acquire_io_req();
				uint64_t counter;
				io_uring_prep_read(sqe, bio_ctx.platform.eventfd, &counter, sizeof(counter), 0);
				io_uring_sqe_set_data(sqe, NULL);

				io_uring_submit_and_wait(&bio_ctx.platform.ioring, 1);
				bio_drain_io_completions();
			}
			break;
	}
}

void
bio_platform_notify(void) {
	uint64_t counter = 1;
	write(bio_ctx.platform.eventfd, &counter, sizeof(counter));
}

struct io_uring_sqe*
bio_acquire_io_req(void) {
	struct io_uring_sqe* sqe;

	while ((sqe = io_uring_get_sqe(&bio_ctx.platform.ioring)) == NULL) {
		bio_drain_io_completions();
		io_uring_submit_and_wait(&bio_ctx.platform.ioring, 1);
	}

	return sqe;
}

int
bio_submit_io_req(struct io_uring_sqe* sqe, uint32_t* flags) {
	bio_io_req_t req = {
		.signal = bio_make_signal(),
	};
	io_uring_sqe_set_data(sqe, &req);
	bio_wait_for_signals(&req.signal, 1, true);

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
