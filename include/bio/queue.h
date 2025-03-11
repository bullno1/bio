#ifndef BIO_QUEUE_H
#define BIO_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

#define BIO_QUEUE(T) T*

#define bio_make_queue(TYPE, CAPACITY) \
	(TYPE*)bio__do_make_queue(sizeof(TYPE), CAPACITY)

#define bio_enqueue(queue, item) \
	queue[bio__prepare_enqueue(queue)] = item

#define bio_dequeue(queue) \
	queue[bio__prepare_dequeue(queue)]

void
bio_destroy_queue(void* queue);

bool
bio_can_enqueue(void* queue);

bool
bio_can_dequeue(void* queue);

uint32_t
bio__prepare_enqueue(void* queue);

uint32_t
bio__prepare_dequeue(void* queue);

#endif
