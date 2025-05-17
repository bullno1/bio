#include "internal.h"

#define BIO_INITIAL_HANDLE_CAPACITY 4

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
bio_handle_table_init(void) {
	bio_ctx.num_handles = 0;
	bio_ctx.handle_slots = NULL;
	bio_grow_handle_table(0, BIO_INITIAL_HANDLE_CAPACITY);
}

void
bio_handle_table_cleanup(void) {
	bio_free(bio_ctx.handle_slots);
}

bio_handle_t
bio_make_handle(void* obj, const bio_tag_t* tag) {
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
		.index = next_handle_slot + 1,
		.gen = slot->gen,
	};
}

void*
bio_resolve_handle(bio_handle_t handle, const bio_tag_t* tag) {
	int32_t index = handle.index - 1;
	if (BIO_LIKELY(0 <= index && index < bio_ctx.handle_capacity)) {
		bio_handle_slot_t* slot = &bio_ctx.handle_slots[index];
		if (BIO_LIKELY(slot->gen == handle.gen && slot->tag == tag)) {
			return slot->obj;
		}
	}

	return NULL;
}

const bio_tag_t*
bio_handle_info(bio_handle_t handle) {
	int32_t index = handle.index - 1;
	if (BIO_LIKELY(0 <= index && index < bio_ctx.handle_capacity)) {
		bio_handle_slot_t* slot = &bio_ctx.handle_slots[index];
		if (BIO_LIKELY(slot->gen == handle.gen)) {
			return slot->tag;
		}
	}

	return NULL;
}

void*
bio_close_handle(bio_handle_t handle, const bio_tag_t* tag) {
	int32_t index = handle.index - 1;
	if (BIO_LIKELY(0 <= index && index < bio_ctx.handle_capacity)) {
		bio_handle_slot_t* slot = &bio_ctx.handle_slots[index];
		if (BIO_LIKELY(slot->gen == handle.gen && slot->tag == tag)) {
			++slot->gen;
			slot->next_handle_slot = bio_ctx.next_handle_slot;
			bio_ctx.next_handle_slot = index;
			--bio_ctx.num_handles;

			return slot->obj;
		}
	}

	return NULL;
}
