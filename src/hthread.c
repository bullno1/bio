#include "hthread.h"
#include <threads.h>
#include <time.h>

void
hed_signal_init(hed_signal_t* signal) {
	mtx_init(&signal->mtx, mtx_plain);
	cnd_init(&signal->cnd);
	signal->value = 0;
}

void
hed_signal_cleanup(hed_signal_t* signal) {
	cnd_destroy(&signal->cnd);
	mtx_destroy(&signal->mtx);
}

void
hed_signal_raise(hed_signal_t* signal) {
	mtx_lock(&signal->mtx);
	signal->value = 1;
	cnd_signal(&signal->cnd);
	mtx_unlock(&signal->mtx);
}

int
hed_signal_wait(hed_signal_t* signal, int timeout_ms) {
	struct timespec ts = { 0 };
	if (timeout_ms >= 0) {
		timespec_get(&ts, TIME_UTC);
		long ms = timeout_ms;
		ts.tv_sec += ms / 1000;
		ts.tv_nsec += (ms % 1000) * 1000000;
		if (ts.tv_nsec >= 1000000000) {
			ts.tv_nsec -= 1000000000;
			ts.tv_sec++;
		}
	}

	int result = thrd_success;
	mtx_lock(&signal->mtx);
	while (signal->value == 0) {
		if (timeout_ms < 0) {
			cnd_wait(&signal->cnd, &signal->mtx);
		} else if (cnd_timedwait(&signal->cnd, &signal->mtx, &ts) == thrd_timedout) {
			result = thrd_timedout;
			break;
		}
	}
	if (result != thrd_timedout) { signal->value = 0; }
	mtx_unlock(&signal->mtx);

	return result;
}

void
hed_spsc_queue_init(hed_spsc_queue_t* queue, int size, void** values, int count) {
	queue->values = values;
	hed_signal_init(&queue->data_ready);
	hed_signal_init(&queue->space_open);
	atomic_store(&queue->head, 0);
    atomic_store(&queue->tail, count > size ? size : count );
    atomic_store(&queue->count, count > size ? size : count );
    queue->size = size;
}

void
hed_spsc_queue_cleanup(hed_spsc_queue_t* queue) {
	hed_signal_cleanup(&queue->space_open);
	hed_signal_cleanup(&queue->data_ready);
}

bool
hed_spsc_queue_produce(hed_spsc_queue_t* queue, void* value, int timeout_ms) {
    if (atomic_load(&queue->count) == queue->size) {
        if (timeout_ms == 0) { return false; }

        if (hed_signal_wait(&queue->space_open, timeout_ms) == thrd_timedout) {
			return false;
		}
    }

    int tail = atomic_fetch_add(&queue->tail, 1);
    queue->values[tail % queue->size] = value;
    if (atomic_fetch_add(&queue->count, 1) == 0) {
        hed_signal_raise(&queue->data_ready);
	}

    return true;
}

void*
hed_spsc_queue_consume(hed_spsc_queue_t* queue, int timeout_ms) {
    if (atomic_load(&queue->count) == 0) {
        if (timeout_ms == 0) { return NULL; }

        if (hed_signal_wait(&queue->data_ready, timeout_ms) == thrd_timedout) {
            return NULL;
        }
	}

    int head = atomic_fetch_add(&queue->head, 1);
    void* retval = queue->values[head % queue->size];
    if (atomic_fetch_add(&queue->count, -1) == queue->size) {
        hed_signal_raise(&queue->space_open);
	}

    return retval;
}

int
hed_spsc_queue_count(hed_spsc_queue_t* queue) {
	return atomic_load(&queue->count);
}
