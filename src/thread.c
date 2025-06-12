#include "internal.h"
#include <threads.h>
#include <stdatomic.h>
#include <limits.h>

typedef struct {
	mtx_t mtx;
	cnd_t cnd;
} bio_thread_signal_t;

typedef struct {
	bio_thread_signal_t can_produce;
	bio_thread_signal_t can_consume;
	atomic_int count;
	atomic_int head;
	atomic_int tail;
	void** values;
	int size;
} bio_spscq_t;

struct bio_worker_thread_s {
	thrd_t thread;
	bio_spscq_t request_queue;
	bio_spscq_t response_queue;
	// We need a separate counter since the length of the queue is not reliable.
	// If a worker has dequeued a job and is working on it, the queue would
	// omit that count.
	// There is no way to tell an idle worker from a busy one when both can
	// have an empty queue.
	unsigned int load;
};

typedef enum {
	BIO_WORKER_MSG_NOOP,
	BIO_WORKER_MSG_TERMINATE,
	BIO_WORKER_MSG_RUN,
} bio_worker_msg_type_t;

typedef struct {
	bio_worker_msg_type_t type;
	bool need_notification;

	struct {
		bio_entrypoint_t fn;
		void* userdata;
		bio_signal_t signal;
	} run;
} bio_worker_msg_t;


static void
bio_thread_signal_init(bio_thread_signal_t* signal) {
	mtx_init(&signal->mtx, mtx_plain);
	cnd_init(&signal->cnd);
}

static void
bio_thread_signal_cleanup(bio_thread_signal_t* signal) {
	cnd_destroy(&signal->cnd);
	mtx_destroy(&signal->mtx);
}

static void
bio_thread_signal_raise(bio_thread_signal_t* signal) {
	mtx_lock(&signal->mtx);
	cnd_signal(&signal->cnd);
	mtx_unlock(&signal->mtx);
}

static void
bio_spscq_init(bio_spscq_t* queue, unsigned int size) {
	bio_thread_signal_init(&queue->can_produce);
	bio_thread_signal_init(&queue->can_consume);
	queue->values = bio_malloc(sizeof(void*) * size);
	atomic_store(&queue->head, 0);
	atomic_store(&queue->tail, 0);
	atomic_store(&queue->count, 0);
	queue->size = size;
}

static void
bio_spscq_cleanup(bio_spscq_t* queue) {
	bio_free(queue->values);
	bio_thread_signal_cleanup(&queue->can_consume);
	bio_thread_signal_cleanup(&queue->can_produce);
}

static bool
bio_spscq_produce(bio_spscq_t* queue, void* item, bool wait) {
	if (atomic_load(&queue->count) == queue->size) {
		if (!wait) { return false; }

		mtx_lock(&queue->can_produce.mtx);
		while (queue->count == queue->size) {
			cnd_wait(&queue->can_produce.cnd, &queue->can_produce.mtx);
		}
		mtx_unlock(&queue->can_produce.mtx);
	}

	unsigned int tail = atomic_fetch_add(&queue->tail, 1);
	queue->values[tail & (queue->size - 1)] = item;
	if (atomic_fetch_add(&queue->count, 1) == 0) {
		bio_thread_signal_raise(&queue->can_consume);
	}

	return true;
}

static void*
bio_spscq_consume(bio_spscq_t* queue, bool wait) {
	if (atomic_load(&queue->count) == 0) {
		if (!wait) { return NULL; }

		mtx_lock(&queue->can_consume.mtx);
		while (queue->count == 0) {
			cnd_wait(&queue->can_consume.cnd, &queue->can_consume.mtx);
		}
		mtx_unlock(&queue->can_consume.mtx);
	}

	unsigned int head = atomic_fetch_add(&queue->head, 1);
	void* item = queue->values[head & (queue->size - 1)];
	if (atomic_fetch_add(&queue->count, -1) == queue->size) {
		bio_thread_signal_raise(&queue->can_produce);
	}

	return item;
}

static int
bio_async_worker(void* userdata) {
	bio_worker_thread_t* self = userdata;

	bool running = true;
	while (running) {
		bio_worker_msg_t* msg = bio_spscq_consume(&self->request_queue, true);

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
		// The condition hopefully introduces a dependency and ensure that
		// notification is only sent after the queue message is published.

		// Store the notification flag locally before sending the message.
		// This is because the scheduler will free the message so accessing
		// msg->need_notification after bio_spscq_produce is unsafe.
		bool need_notification = msg->need_notification;
		if (
			bio_spscq_produce(&self->response_queue, msg, true)
			&& need_notification
		) {
			bio_platform_notify();
		}
	}

	return 0;
}

void
bio_thread_init(void) {
	int num_threads = bio_ctx.options.thread_pool.num_threads;
	if (num_threads <= 0) { num_threads = 2; }

	int queue_size = bio_ctx.options.thread_pool.queue_size;
	if (queue_size <= 0) { queue_size = 2; }
	queue_size = bio_next_pow2(queue_size);

	bio_ctx.options.thread_pool.num_threads = num_threads;
	bio_ctx.options.thread_pool.queue_size = queue_size;

	bio_worker_thread_t* workers = bio_malloc(num_threads * sizeof(bio_worker_thread_t));
	bio_platform_begin_create_thread_pool();
	for (int i = 0; i < num_threads; ++i) {
		bio_worker_thread_t* worker = &workers[i];
		bio_spscq_init(&worker->request_queue, queue_size);
		bio_spscq_init(&worker->response_queue, queue_size * 2);
		thrd_create(&worker->thread, bio_async_worker, worker);
	}
	bio_platform_end_create_thread_pool();
	bio_ctx.thread_pool = workers;

	bio_ctx.num_running_async_jobs = 0;
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
		while (!bio_spscq_produce(&worker->request_queue, &terminate, false)) {
			// Either the response queue is full or the worker is busy so we
			// wait for its first response
			msg = bio_spscq_consume(&worker->response_queue, true);
			while (msg != NULL) {
				if (msg != &terminate) { bio_free(msg); }

				// Drain as much as possible before retrying
				msg = bio_spscq_consume(&worker->response_queue, false);
			}
		}

		// Drain and free messages from response queue
		while ((msg = bio_spscq_consume(&worker->response_queue, false)) != NULL) {
			if (msg != &terminate) { bio_free(msg); }
		}

		thrd_join(worker->thread, NULL);
		bio_spscq_cleanup(&worker->response_queue);
		bio_spscq_cleanup(&worker->request_queue);
	}

	bio_free(bio_ctx.thread_pool);
}

static void
bio_thread_drain_responses(bio_worker_thread_t* worker) {
	bio_worker_msg_t* msg;
	while ((msg = bio_spscq_consume(&worker->response_queue, false)) != NULL) {
		if (msg->type == BIO_WORKER_MSG_RUN) {
			bio_raise_signal(msg->run.signal);
			--bio_ctx.num_running_async_jobs;
			--worker->load;
		}

		bio_free(msg);
	}
}

void
bio_thread_update(void) {
	int num_threads = bio_ctx.options.thread_pool.num_threads;
	bio_worker_thread_t* workers = bio_ctx.thread_pool;
	for (int i = 0; i < num_threads; ++i) {
		bio_thread_drain_responses(&workers[i]);
	}
}

void
bio_run_async(bio_entrypoint_t task, void* userdata, bio_signal_t signal) {
	bio_worker_msg_t* msg = bio_malloc(sizeof(bio_worker_msg_t));
	*msg = (bio_worker_msg_t){
		.type = BIO_WORKER_MSG_RUN,
		.need_notification = true,
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
		// Try to send to the least busy worker
		int chosen_worker_index = -1;
		unsigned int min_load = UINT_MAX;
		for (int i = 0; i < num_threads; ++i) {
			bio_worker_thread_t* worker = &workers[i];

			// Drain before submit to prevent queue from filling up
			bio_thread_drain_responses(worker);

			unsigned int load = worker->load;
			if (load < min_load) {
				min_load = load;
				chosen_worker_index = i;
			}
		}

		bio_worker_thread_t* worker = &workers[chosen_worker_index];
		if (bio_spscq_produce(&worker->request_queue, msg, false)) {
			message_sent = true;
			++bio_ctx.num_running_async_jobs;
			++worker->load;
			break;
		}

		// Let other threads run
		if (!message_sent) { bio_yield(); }
	} while (!message_sent);
}

int32_t
bio_num_running_async_jobs(void) {
	return bio_ctx.num_running_async_jobs;
}
