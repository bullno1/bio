#include "internal.h"

#define BIO_INITIAL_TIMER_CAPACITY 4

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
