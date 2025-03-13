#include <bio/mailbox.h>
#include <string.h>
#include "internal.h"

const bio_tag_t BIO_MAILBOX_HANDLE = BIO_TAG_INIT("bio.handle.mailbox");

typedef struct {
	uint32_t capacity;
	uint32_t read;
	uint32_t write;

	bio_signal_ref_t signal;

	_Alignas(max_align_t) char data[];
} bio_mailbox_t;

static inline uint32_t
bio_next_pow2(uint32_t v) {
    uint32_t next = v;
    next--;
    next |= next >> 1;
    next |= next >> 2;
    next |= next >> 4;
    next |= next >> 8;
    next |= next >> 16;
    next++;

    return next;
}

void
bio__mailbox_open(bio_handle_t* handle, size_t item_size, uint32_t capacity) {
	capacity = bio_next_pow2(capacity);
	bio_mailbox_t* mailbox = bio_malloc(sizeof(bio_mailbox_t) + item_size * capacity);
	*mailbox = (bio_mailbox_t){
		.capacity = capacity,
		.signal = { .handle = BIO_INVALID_HANDLE },
	};

	*handle = bio_make_handle(mailbox, &BIO_MAILBOX_HANDLE);
}

void
bio__mailbox_close(bio_handle_t handle) {
	bio_mailbox_t* mailbox = bio_close_handle(handle, &BIO_MAILBOX_HANDLE);
	if (BIO_LIKELY(mailbox != NULL)) {
		// Wake up the coro waiting on this
		if (bio_handle_compare(mailbox->signal.handle, BIO_INVALID_HANDLE) != 0) {
			bio_raise_signal(mailbox->signal);
		}
	}
}

static inline uint32_t
bio_queue_index(uint32_t vindex, uint32_t capacity) {
	return vindex & (capacity - 1);
}

bool
bio__mailbox_send(bio_handle_t handle, const void* data, size_t item_size) {
	bio_mailbox_t* mailbox = bio_resolve_handle(handle, &BIO_MAILBOX_HANDLE);
	if (BIO_LIKELY(mailbox != NULL)) {
		uint32_t size = mailbox->write - mailbox->read;
		if (BIO_LIKELY(size < mailbox->capacity)) {
			uint32_t index = bio_queue_index(mailbox->write++, mailbox->capacity);
			memcpy(mailbox->data + index * item_size, data, item_size);
			// Wake up the waiting coro
			if (bio_handle_compare(mailbox->signal.handle, BIO_INVALID_HANDLE) != 0) {
				bio_raise_signal(mailbox->signal);
				mailbox->signal.handle = BIO_INVALID_HANDLE;
			}
			return true;
		}
	}

	return false;
}

bool
bio__mailbox_recv(bio_handle_t handle, void* data, size_t item_size) {
	bio_mailbox_t* mailbox = bio_resolve_handle(handle, &BIO_MAILBOX_HANDLE);
	if (BIO_LIKELY(mailbox != NULL)) {
		// Mailbox is empty
		if (mailbox->read == mailbox->write) {
			if (BIO_LIKELY(bio_handle_compare(mailbox->signal.handle, BIO_INVALID_HANDLE) == 0)) {
				bio_signal_ref_t signal = mailbox->signal = bio_make_signal();
				bio_wait_for_signals(&signal, 1, true);
			} else {
				return false;
			}
		}

		// The mailbox might be closed so we resolve again
		mailbox = bio_resolve_handle(handle, &BIO_MAILBOX_HANDLE);
		if (BIO_LIKELY(mailbox != NULL && mailbox->read != mailbox->write)) {
			uint32_t index = bio_queue_index(mailbox->read++, mailbox->capacity);
			memcpy(data, mailbox->data + index * item_size, item_size);
			return true;
		}
	}

	return false;
}

bool
bio__mailbox_can_recv(bio_handle_t handle) {
	bio_mailbox_t* mailbox = bio_resolve_handle(handle, &BIO_MAILBOX_HANDLE);
	if (BIO_LIKELY(mailbox != NULL)) {
		return mailbox->read != mailbox->write;
	}

	return false;
}

bool
bio__mailbox_can_send(bio_handle_t handle) {
	bio_mailbox_t* mailbox = bio_resolve_handle(handle, &BIO_MAILBOX_HANDLE);
	if (BIO_LIKELY(mailbox != NULL)) {
		return mailbox->write - mailbox->read < mailbox->capacity;
	}

	return false;
}
