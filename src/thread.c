#include "internal.h"
#include "hthread.h"
#include <stdio.h>

struct bio_worker_thread_s {
	thrd_t thread;
	hed_spsc_queue_t request_queue;
	hed_spsc_queue_t response_queue;

	int num_jobs;
};

typedef enum {
	BIO_WORKER_MSG_NOOP,
	BIO_WORKER_MSG_TERMINATE,
	BIO_WORKER_MSG_RUN,
} bio_worker_msg_type_t;

typedef struct {
	bio_worker_msg_type_t type;

	struct {
		bio_entrypoint_t fn;
		void* userdata;
		bio_signal_t signal;
	} run;
} bio_worker_msg_t;

static int
bio_async_worker(void* userdata) {
	bio_worker_thread_t* self = userdata;

	bool running = true;
	while (running) {
		bio_worker_msg_t* msg = hed_spsc_queue_consume(&self->request_queue, -1);

		switch (msg->type) {
			case BIO_WORKER_MSG_NOOP:
				break;
			case BIO_WORKER_MSG_TERMINATE:
				running = false;
				break;
			case BIO_WORKER_MSG_RUN:
				msg->run.fn(msg->run.userdata);
				break;
		}

		// If the scheduler is waiting for I/O, wake it up
		bio_platform_notify();
		hed_spsc_queue_produce(&self->response_queue, msg, -1);
	}

	return 0;
}

void
bio_thread_init(void) {
	int num_threads = bio_ctx.options.thread_pool.num_threads;
	if (num_threads <= 0) { num_threads = 1; }

	int queue_size = bio_ctx.options.thread_pool.queue_size;
	if (queue_size <= 0) { queue_size = 2; }

	bio_ctx.options.thread_pool.num_threads = num_threads;
	bio_ctx.options.thread_pool.queue_size = queue_size;

	bio_worker_thread_t* workers = bio_malloc(num_threads * sizeof(bio_worker_thread_t));
	for (int i = 0; i < num_threads; ++i) {
		bio_worker_thread_t* worker = &workers[i];
		hed_spsc_queue_init(
			&worker->request_queue,
			queue_size,
			bio_malloc(queue_size * sizeof(void*)),
			0
		);
		hed_spsc_queue_init(
			&worker->response_queue,
			queue_size,
			bio_malloc(queue_size * sizeof(void*)),
			0
		);
		thrd_create(&worker->thread, bio_async_worker, worker);
		worker->num_jobs = 0;
	}
	bio_ctx.thread_pool = workers;
}

void
bio_thread_cleanup(void) {
	bio_worker_msg_t terminate = { .type = BIO_WORKER_MSG_TERMINATE };

	int num_threads = bio_ctx.options.thread_pool.num_threads;
	bio_worker_thread_t* workers = bio_ctx.thread_pool;
	for (int i = 0; i < num_threads; ++i) {
		bio_worker_thread_t* worker = &workers[i];

		// Try to send the termination message
		bio_worker_msg_t* msg;
		while (!hed_spsc_queue_produce(&worker->request_queue, &terminate, 0)) {
			// Either the response queue is full or the worker is busy so we
			// wait for its first response
			msg = hed_spsc_queue_consume(&worker->response_queue, -1);
			while (msg != NULL) {
				if (msg != &terminate) { bio_free(msg); }

				// Drain as much as possible before retrying
				msg = hed_spsc_queue_consume(&worker->response_queue, 0);
			}
		}

		// Drain and free messages from response queue
		while ((msg = hed_spsc_queue_consume(&worker->response_queue, 0)) != NULL) {
			if (msg != &terminate) { bio_free(msg); }
		}

		thrd_join(worker->thread, NULL);
		bio_free(worker->request_queue.values);
		bio_free(worker->response_queue.values);
	}

	bio_free(bio_ctx.thread_pool);
}

int
bio_thread_update(void) {
	int num_running_jobs = 0;
	int num_threads = bio_ctx.options.thread_pool.num_threads;
	bio_worker_thread_t* workers = bio_ctx.thread_pool;
	for (int i = 0; i < num_threads; ++i) {
		bio_worker_thread_t* worker = &workers[i];

		bio_worker_msg_t* msg;
		while ((msg = hed_spsc_queue_consume(&worker->response_queue, 0)) != NULL) {
			if (msg->type == BIO_WORKER_MSG_RUN) {
				bio_raise_signal(msg->run.signal);
				--worker->num_jobs;
			}

			bio_free(msg);
		}

		num_running_jobs += worker->num_jobs;
	}

	return num_running_jobs;
}

void
bio_run_async(bio_entrypoint_t task, void* userdata, bio_signal_t signal) {
	bio_worker_msg_t* msg = bio_malloc(sizeof(bio_worker_msg_t));
	*msg = (bio_worker_msg_t){
		.type = BIO_WORKER_MSG_RUN,
		.run = {
			.fn = task,
			.userdata = userdata,
			.signal = signal,
		},
	};

	int num_threads = bio_ctx.options.thread_pool.num_threads;
	bio_worker_thread_t* workers = bio_ctx.thread_pool;
	bool message_sent = false;
	do {
		// Try to send to one worker
		for (int i = 0; i < num_threads; ++i) {
			bio_worker_thread_t* worker = &workers[i];
			if (hed_spsc_queue_produce(&worker->request_queue, msg, 0)) {
				message_sent = true;
				++worker->num_jobs;
				break;
			}
		}

		// Let other threads run
		if (!message_sent) { bio_yield(); }
	} while (!message_sent);
}
