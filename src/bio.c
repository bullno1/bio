#include "internal.h"

#define BIO_INITIAL_HANDLE_CAPACITY 4

bio_ctx_t bio_ctx = { 0 };

const bio_tag_t BIO_OS_ERROR = BIO_TAG_INIT("bio.error.os");
const bio_tag_t BIO_CORE_ERROR = BIO_TAG_INIT("bio.error.core");

static inline void
bio_grow_handle_table(int32_t old_capacity, int32_t new_capacity) {
	bio_ctx.handle_slots = bio_realloc(
		bio_ctx.handle_slots,
		sizeof(bio_handle_slot_t) * new_capacity
	);

	for (int32_t i = old_capacity; i < new_capacity; ++i) {
		bio_handle_slot_t* slot = &bio_ctx.handle_slots[i];
		slot->gen = 0;
		slot->obj = NULL;
		slot->tag = NULL;
		slot->next_handle_slot = i + 1;
	}
	bio_ctx.next_handle_slot = old_capacity;
	bio_ctx.handle_capacity = new_capacity;
}

void
bio_init(bio_options_t options) {
	bio_ctx.options = options;

	bio_ctx.num_handles = 0;
	bio_ctx.handle_slots = NULL;
	bio_grow_handle_table(0, BIO_INITIAL_HANDLE_CAPACITY);
}

void
bio_terminate(void) {
	bio_free(bio_ctx.handle_slots);
}

bio_handle_t
bio_make_handle(void* obj, bio_tag_t* tag) {
	if (bio_ctx.num_handles >= bio_ctx.handle_capacity) {
		bio_grow_handle_table(bio_ctx.handle_capacity, bio_ctx.handle_capacity * 2);
	}

	int next_handle_slot = bio_ctx.next_handle_slot;
	bio_handle_slot_t* slot = &bio_ctx.handle_slots[next_handle_slot];
	slot->obj = obj;
	slot->tag = tag;
	bio_ctx.next_handle_slot = slot->next_handle_slot;
	++bio_ctx.num_handles;

	return (bio_handle_t){
		.index = next_handle_slot,
		.gen = slot->gen,
	};
}

void*
bio_resolve_handle(bio_handle_t handle, bio_tag_t* tag) {
	if (BIO_LIKELY(handle.index < bio_ctx.handle_capacity)) {
		bio_handle_slot_t* slot = &bio_ctx.handle_slots[handle.index];
		if (BIO_LIKELY(slot->gen == handle.gen && slot->tag == tag)) {
			return slot->obj;
		}
	}

	return NULL;
}

void
bio_close_handle(bio_handle_t handle, bio_tag_t* tag) {
	if (BIO_LIKELY(handle.index < bio_ctx.handle_capacity)) {
		bio_handle_slot_t* slot = &bio_ctx.handle_slots[handle.index];
		if (BIO_LIKELY(slot->gen == handle.gen && slot->tag == tag)) {
			++slot->gen;
			slot->next_handle_slot = bio_ctx.next_handle_slot;
			bio_ctx.next_handle_slot = handle.index;
			--bio_ctx.num_handles;
		}
	}
}
