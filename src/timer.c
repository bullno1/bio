#include "internal.h"
#include <bio/timer.h>

#define BIO_INITIAL_TIMER_CAPACITY 4

static const bio_tag_t BIO_TIMER_HANDLE = BIO_TAG_INIT("bio.handle.timer");

typedef struct {
	bio_timer_type_t type;
	bio_time_t timeout_ms;
	bio_entrypoint_t fn;
	void* userdata;

	bio_signal_t wake_signal;
	bio_time_t due_time_ms;
	bool cancelled;
	bio_handle_t handle;
} bio_timer_impl_t;

void
bio_timer_init(void) {
	bio_ctx.timer_entries = bio_malloc(BIO_INITIAL_TIMER_CAPACITY * sizeof(bio_timer_entry_t));
	bio_ctx.num_timers = 0;
	bio_ctx.timer_capacity = BIO_INITIAL_TIMER_CAPACITY;
	bio_ctx.current_time_ms = bio_platform_current_time_ms();
}

void
bio_timer_cleanup(void) {
	bio_free(bio_ctx.timer_entries);
}

static inline int32_t
bio_heap_parent(int32_t index) {
    return (index - 1) / 2;
}

static void
bio_heap_swap(bio_timer_entry_t* entries, int32_t index1, int32_t index2) {
	bio_timer_entry_t temp = entries[index1];
	entries[index1] = entries[index2];
	entries[index2] = temp;
}

void
bio_raise_signal_after(bio_signal_t signal, bio_time_t time_ms) {
	if (BIO_LIKELY(time_ms > 0)) {
		if (bio_ctx.num_timers == bio_ctx.timer_capacity) {
			int32_t new_capacity = bio_ctx.timer_capacity * 2;
			bio_ctx.timer_entries = bio_realloc(
				bio_ctx.timer_entries,
				new_capacity * sizeof(bio_timer_entry_t)
			);
			bio_ctx.timer_capacity = new_capacity;
		}

		// Heap insert the new timer
		bio_time_t current_time = bio_ctx.current_time_ms;
		int32_t current = bio_ctx.num_timers;
		bio_ctx.timer_entries[current].due_time_ms = current_time + time_ms;
		bio_ctx.timer_entries[current].signal = signal;
		++bio_ctx.num_timers;

		while (
			current > 0
			&& bio_ctx.timer_entries[bio_heap_parent(current)].due_time_ms > bio_ctx.timer_entries[current].due_time_ms
		) {
			int32_t parent = bio_heap_parent(current);
			bio_heap_swap(bio_ctx.timer_entries, parent, current);
			current = parent;
		}
	} else {
		bio_raise_signal(signal);
	}
}

void
bio_timer_update(void) {
	bio_time_t current_time = bio_ctx.current_time_ms = bio_platform_current_time_ms();

	bio_timer_entry_t* entries = bio_ctx.timer_entries;
	while (
		bio_ctx.num_timers > 0
		&& bio_ctx.timer_entries[0].due_time_ms <= current_time
	) {
		bio_timer_entry_t* entry = &entries[0];
		bio_raise_signal(entry->signal);

		// Delete the timer
		int32_t num_entries = --bio_ctx.num_timers;
		*entry = entries[num_entries];
		int32_t index = 0;
		while (true) {
			int32_t left = 2 * index + 1;
			int32_t right = 2 * index + 2;
			int32_t smallest = index;

			if (left < num_entries && entries[left].due_time_ms < entries[smallest].due_time_ms) {
				smallest = left;
			}
			if (right < num_entries && entries[right].due_time_ms < entries[smallest].due_time_ms) {
				smallest = right;
			}

			if (smallest == index) {
				break;
			}

			bio_heap_swap(entries, index, smallest);

			index = smallest;
		}
	}
}

bio_time_t
bio_time_until_next_timer(void) {
	if (bio_ctx.num_timers > 0) {
		bio_time_t current_time = bio_platform_current_time_ms();
		bio_time_t due_time = bio_ctx.timer_entries[0].due_time_ms;

		if (due_time < current_time) {
			return 0;
		} else {
			return due_time - current_time;
		}
	} else {
		return -1;
	}
}

static void
bio_timer_entry(void* userdata) {
	bio_timer_impl_t* impl = userdata;
	while (true) {
		impl->wake_signal = bio_make_signal();
		bio_raise_signal_after(
			impl->wake_signal,
			impl->due_time_ms - bio_current_time_ms()
		);
		bio_wait_for_one_signal(impl->wake_signal);

		bio_time_t current_time_ms = bio_current_time_ms();
		if (impl->cancelled) {
			break;
		} else if (current_time_ms >= impl->due_time_ms) {
			impl->fn(impl->userdata);
			if (impl->type == BIO_TIMER_ONESHOT) {
				break;
			} else {
				impl->due_time_ms = current_time_ms + impl->timeout_ms;
			}
		}
	}

	bio_close_handle(impl->handle, &BIO_TIMER_HANDLE);
	bio_free(impl);
}

bio_timer_t
bio_create_timer(
	bio_timer_type_t type, bio_time_t timeout_ms,
	bio_entrypoint_t fn, void* userdata
) {
	bio_timer_impl_t* impl = bio_malloc(sizeof(bio_timer_impl_t));
	*impl = (bio_timer_impl_t){
		.type = type,
		.timeout_ms = timeout_ms,
		.fn = fn,
		.userdata = userdata,

		.due_time_ms = bio_current_time_ms() + timeout_ms,
	};
	bio_handle_t handle = impl->handle = bio_make_handle(impl, &BIO_TIMER_HANDLE);
	bio_spawn(bio_timer_entry, impl);

	return (bio_timer_t){ .handle = handle };
}

bool
bio_is_timer_pending(bio_timer_t timer) {
	return bio_resolve_handle(timer.handle, &BIO_TIMER_HANDLE) != NULL;
}

void
bio_reset_timer(bio_timer_t timer, bio_time_t timeout_ms) {
	bio_timer_impl_t* impl = bio_resolve_handle(timer.handle, &BIO_TIMER_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		impl->timeout_ms = timeout_ms;
		impl->due_time_ms = bio_current_time_ms() + timeout_ms;
	}
}

void
bio_cancel_timer(bio_timer_t timer) {
	bio_timer_impl_t* impl = bio_resolve_handle(timer.handle, &BIO_TIMER_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		impl->cancelled = true;
		bio_raise_signal(impl->wake_signal);
		bio_close_handle(impl->handle, &BIO_TIMER_HANDLE);
	}
}
