#include "internal.h"
#include <minicoro.h>

#define BIO_INITIAL_HANDLE_CAPACITY 4

bio_ctx_t bio_ctx = { 0 };

const bio_tag_t BIO_PLATFORM_ERROR = BIO_TAG_INIT("bio.error.platform");

static const bio_tag_t BIO_CORO_HANDLE = BIO_TAG_INIT("bio.handle.coro");
static const bio_tag_t BIO_SIGNAL_HANDLE = BIO_TAG_INIT("bio.handle.signal");

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

	// Handle
	bio_ctx.num_handles = 0;
	bio_ctx.handle_slots = NULL;
	bio_grow_handle_table(0, BIO_INITIAL_HANDLE_CAPACITY);

	// Coro
	BIO_LIST_INIT(&bio_ctx.ready_coros_a);
	BIO_LIST_INIT(&bio_ctx.ready_coros_b);
	bio_ctx.current_ready_coros = &bio_ctx.ready_coros_a;
	bio_ctx.next_ready_coros = &bio_ctx.ready_coros_b;

	bio_platform_init();
}

void
bio_terminate(void) {
	bio_platform_cleanup();
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
		.index = next_handle_slot,
		.gen = slot->gen,
	};
}

void*
bio_resolve_handle(bio_handle_t handle, const bio_tag_t* tag) {
	if (BIO_LIKELY(0 <= handle.index && handle.index < bio_ctx.handle_capacity)) {
		bio_handle_slot_t* slot = &bio_ctx.handle_slots[handle.index];
		if (BIO_LIKELY(slot->gen == handle.gen && slot->tag == tag)) {
			return slot->obj;
		}
	}

	return NULL;
}

void*
bio_close_handle(bio_handle_t handle, const bio_tag_t* tag) {
	if (BIO_LIKELY(0 <= handle.index && handle.index < bio_ctx.handle_capacity)) {
		bio_handle_slot_t* slot = &bio_ctx.handle_slots[handle.index];
		if (BIO_LIKELY(slot->gen == handle.gen && slot->tag == tag)) {
			++slot->gen;
			slot->next_handle_slot = bio_ctx.next_handle_slot;
			bio_ctx.next_handle_slot = handle.index;
			--bio_ctx.num_handles;

			return slot->obj;
		}
	}

	return NULL;
}

void
bio_loop(void) {
	while (true) {
		// Pop and run coros off the current list until it is empty
		while (!BIO_LIST_IS_EMPTY(bio_ctx.current_ready_coros)) {
			bio_coro_link_t* coro_link = bio_ctx.current_ready_coros->next;
			BIO_LIST_REMOVE(coro_link);

			bio_coro_impl_t* coro = BIO_CONTAINER_OF(coro_link, bio_coro_impl_t, link);
			coro->state = BIO_CORO_RUNNING;
			coro->num_blocking_signals = 0;
			mco_resume(coro->impl);
			if (mco_status(coro->impl) != MCO_DEAD) {
				bool waiting = coro->num_blocking_signals > 0;
				coro->state = waiting ? BIO_CORO_WAITING : BIO_CORO_READY;
				if (!waiting) {
					BIO_LIST_APPEND(bio_ctx.next_ready_coros, &coro->link);
				}
			} else {
				// TODO: Wait until all pending iops finished or cancel them

				// Destroy all signals
				for (
					bio_signal_link_t* itr = coro->pending_signals.next;
					itr != &coro->pending_signals;
				) {
					bio_signal_link_t* next = itr->next;

					bio_signal_impl_t* signal = BIO_CONTAINER_OF(itr, bio_signal_impl_t, link);
					bio_close_handle(signal->handle, &BIO_SIGNAL_HANDLE);
					bio_free(signal);

					itr = next;
				}

				// Destroy the coroutine
				mco_destroy(coro->impl);
				bio_close_handle(coro->handle, &BIO_CORO_HANDLE);
				bio_free(coro);
				--bio_ctx.num_coros;
			}
		}

		if (bio_ctx.num_coros == 0) { break; }

		// Perform I/O
		bio_platform_update(BIO_LIST_IS_EMPTY(bio_ctx.next_ready_coros));

		// Swap the coro lists
		bio_coro_link_t* tmp = bio_ctx.next_ready_coros;
		bio_ctx.next_ready_coros = bio_ctx.current_ready_coros;
		bio_ctx.current_ready_coros = tmp;
	}
}

static void
bio_coro_entry_wrapper(mco_coro* co) {
	bio_coro_impl_t* args = co->user_data;
	args->entrypoint(args->userdata);
}

bio_coro_t
bio_spawn(bio_entrypoint_t entrypoint, void* userdata) {
	bio_coro_impl_t* coro = bio_malloc(sizeof(bio_coro_impl_t));
	*coro = (bio_coro_impl_t){
		.entrypoint = entrypoint,
		.userdata = userdata,
		.state = BIO_CORO_READY,
	};
	BIO_LIST_INIT(&coro->pending_signals);

	mco_desc desc = mco_desc_init(bio_coro_entry_wrapper, 0);
	desc.user_data = coro;
	mco_create(&coro->impl, &desc);

	coro->handle = bio_make_handle(coro, &BIO_CORO_HANDLE);

	BIO_LIST_APPEND(bio_ctx.next_ready_coros, &coro->link);
	++bio_ctx.num_coros;

	return (bio_coro_t){ .handle = coro->handle };
}

bio_coro_state_t
bio_coro_state(bio_coro_t ref) {
	bio_coro_impl_t* coro = bio_resolve_handle(ref.handle, &BIO_CORO_HANDLE);
	if (BIO_LIKELY(coro != NULL)) {
		return coro->state;
	} else {
		return BIO_CORO_DEAD;
	}
}

void
bio_yield(void) {
	mco_yield(mco_running());
}

bio_coro_t
bio_current_coro(void) {
	mco_coro* impl = mco_running();
	if (BIO_LIKELY(impl)) {
		bio_coro_impl_t* coro = impl->user_data;
		return (bio_coro_t){ .handle = coro->handle };
	} else {
		return (bio_coro_t){ .handle = BIO_INVALID_HANDLE };
	}
}

bio_signal_t
bio_make_signal(void) {
	mco_coro* coro_impl = mco_running();
	if (BIO_LIKELY(coro_impl != NULL)) {
		bio_coro_impl_t* coro = coro_impl->user_data;

		bio_signal_impl_t* signal = bio_malloc(sizeof(bio_signal_impl_t));
		*signal = (bio_signal_impl_t){
			.owner = coro,
			.wait_counter = coro->wait_counter - 1,
		};
		signal->handle = bio_make_handle(signal, &BIO_SIGNAL_HANDLE);
		BIO_LIST_APPEND(&coro->pending_signals, &signal->link);

		return (bio_signal_t){ .handle = signal->handle };
	} else {
		return (bio_signal_t){ .handle = BIO_INVALID_HANDLE };
	}
}

void
bio_raise_signal(bio_signal_t ref) {
	bio_signal_impl_t* signal = bio_close_handle(ref.handle, &BIO_SIGNAL_HANDLE);
	if (BIO_LIKELY(signal != NULL)) {
		bio_coro_impl_t* owner = signal->owner;

		if (
			owner->state == BIO_CORO_WAITING
			&& signal->wait_counter == owner->wait_counter
		) {
			if (--owner->num_blocking_signals == 0) {  // Schedule to run once
				BIO_LIST_APPEND(bio_ctx.next_ready_coros, &owner->link);
				owner->state = BIO_CORO_READY;
			}
		}

		BIO_LIST_REMOVE(&signal->link);
		bio_free(signal);
	}
}

bool
bio_check_signal(bio_signal_t ref) {
	bio_signal_t* signal = bio_resolve_handle(ref.handle, &BIO_SIGNAL_HANDLE);
	return signal == NULL;
}

void
bio_wait_for_signals(
	bio_signal_t* signals,
	int num_signals,
	bool wait_all
) {
	mco_coro* impl = mco_running();
	if (BIO_LIKELY(impl != NULL)) {
		bio_coro_impl_t* coro = impl->user_data;

		// Ensure that only the relevant signals are checked
		int wait_counter = ++coro->wait_counter;
		int num_blocking_signals = 0;
		for (int i = 0; i < num_signals; ++i) {
			bio_signal_impl_t* signal = bio_resolve_handle(signals[i].handle, &BIO_SIGNAL_HANDLE);

			if (BIO_LIKELY(signal != NULL && signal->owner == coro)) {
				signal->wait_counter = wait_counter;
				++num_blocking_signals;
			}
		}

		if (BIO_LIKELY(num_blocking_signals > 0)) {
			coro->num_blocking_signals = wait_all ? num_blocking_signals : 1;
			mco_yield(impl);
		}
	}
}
