#ifndef HEDITOR_THREAD_H
#define HEDITOR_THREAD_H

// Adapted from https://github.com/mattiasgustavsson/libs/blob/main/thread.h
// with C11 thread instead of platform API

#include <stdatomic.h>
#include <threads.h>
#include <stdbool.h>

typedef struct hed_signal_s {
	mtx_t mtx;
	cnd_t cnd;
	int value;
} hed_signal_t;

typedef struct hed_spsc_queue_s {
	hed_signal_t data_ready;
	hed_signal_t space_open;
	atomic_int count;
	atomic_int head;
	atomic_int tail;
	void** values;
	int size;
} hed_spsc_queue_t;

void
hed_signal_init(hed_signal_t* signal);

void
hed_signal_cleanup(hed_signal_t* signal);

void
hed_signal_raise(hed_signal_t* signal);

int
hed_signal_wait(hed_signal_t* signal, int timeout_ms);

void
hed_spsc_queue_init(hed_spsc_queue_t* queue, int size, void** values, int count);

void
hed_spsc_queue_cleanup(hed_spsc_queue_t* queue);

bool
hed_spsc_queue_produce(hed_spsc_queue_t* queue, void* value, int timeout_ms);

void*
hed_spsc_queue_consume(hed_spsc_queue_t* queue, int timeout_ms);

int
hed_spsc_queue_count(hed_spsc_queue_t* queue);

#endif
