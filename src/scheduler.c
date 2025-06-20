#include "internal.h"
#include <minicoro.h>
#include <string.h>

static const bio_tag_t BIO_CORO_HANDLE = BIO_TAG_INIT("bio.handle.coro");
static const bio_tag_t BIO_SIGNAL_HANDLE = BIO_TAG_INIT("bio.handle.signal");
static const bio_tag_t BIO_MONITOR_HANDLE = BIO_TAG_INIT("bio.handle.monitor");

void
bio_scheduler_init(void) {
	bio_ctx.num_coros = 0;
	bio_ctx.num_daemons = 0;
	if (bio_ctx.options.num_cls_buckets == 0) {
		bio_ctx.options.num_cls_buckets = BIO_DEFAULT_NUM_CLS_BUCKETS;
	} else {
		bio_ctx.options.num_cls_buckets = bio_next_pow2(bio_ctx.options.num_cls_buckets);
	}
}

void
bio_scheduler_cleanup(void) {
	bio_array_free(bio_ctx.next_ready_coros);
	bio_array_free(bio_ctx.current_ready_coros);
	bio_ctx.next_ready_coros = NULL;
	bio_ctx.current_ready_coros = NULL;
}

void
bio_loop(void) {
	while (true) {
		// Pop and run coros off the current list until it is empty
		int num_coros = (int)bio_array_len(bio_ctx.current_ready_coros);
		for (int coro_index = 0; coro_index < num_coros; ++coro_index) {
			bio_coro_impl_t* coro = bio_ctx.current_ready_coros[coro_index];
			coro->state = BIO_CORO_RUNNING;
			coro->num_blocking_signals = 0;
			mco_resume(coro->impl);
			if (mco_status(coro->impl) != MCO_DEAD) {
				bool waiting = coro->num_blocking_signals > 0;
				coro->state = waiting ? BIO_CORO_WAITING : BIO_CORO_READY;
				if (!waiting) {
					bio_array_push(bio_ctx.next_ready_coros, coro);
				}
			} else {
				// Cleanup all CLS
				bio_cls_link_t* buckets = coro->cls_buckets;
				int num_buckets = bio_ctx.options.num_cls_buckets;
				for (int bucket_index = 0; bucket_index < num_buckets && buckets != NULL; ++bucket_index) {
					bio_cls_link_t* bucket = &buckets[bucket_index];
					for (
						bio_cls_link_t* itr = bucket->next;
						itr != bucket;
					) {
						bio_cls_link_t* next = itr->next;
						bio_cls_entry_t* entry = BIO_CONTAINER_OF(itr, bio_cls_entry_t, link);

						const bio_cls_t* cls = entry->spec;
						if (cls->cleanup != NULL) { cls->cleanup(entry->data); }
						bio_free(entry);

						itr = next;
					}
				}
				bio_free(buckets);

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

				// Trigger all monitors
				for (
					bio_monitor_link_t* itr = coro->monitors.next;
					itr != &coro->monitors;
				) {
					bio_monitor_link_t* next = itr->next;

					bio_monitor_impl_t* monitor = BIO_CONTAINER_OF(itr, bio_monitor_impl_t, link);
					bio_raise_signal(monitor->signal);
					bio_close_handle(monitor->handle, &BIO_MONITOR_HANDLE);
					bio_free(monitor);

					itr = next;
				}

				// Destroy the coroutine
				mco_destroy(coro->impl);
				bio_close_handle(coro->handle, &BIO_CORO_HANDLE);
				--bio_ctx.num_coros;
				if (coro->daemon) {
					--bio_ctx.num_daemons;
				}
				bio_free(coro);
			}
		}
		bio_array_clear(bio_ctx.current_ready_coros);

		bool should_terminate = bio_ctx.is_terminating
			// During termination, run until there is no coroutines
			? bio_ctx.num_coros == 0
			// During normal operation, run until there is no non-daemon coroutines
			: bio_ctx.num_coros == bio_ctx.num_daemons;
		if (should_terminate) { break; }

		// Poll async jobs
		bio_thread_update();

		// Expire timers
		bio_timer_update();

		// Perform I/O, wait if there is no ready coros
		bool should_wait_for_io = bio_array_len(bio_ctx.next_ready_coros) == 0;
		bio_platform_update(
			should_wait_for_io ? bio_time_until_next_timer() : 0,
			bio_num_running_async_jobs() > 0
		);

		// Some async tasks or timer might have completed during the long wait
		if (should_wait_for_io) {
			bio_thread_update();
			bio_timer_update();
		}

		// Swap the coro lists
		BIO_ARRAY(bio_coro_impl_t*) tmp = bio_ctx.next_ready_coros;
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
bio_spawn_ex(
	bio_entrypoint_t entrypoint,
	void* userdata,
	const bio_coro_options_t* options
) {
	if (options == NULL) {
		options = &(bio_coro_options_t){ 0 };
	}

	bio_coro_impl_t* coro = bio_malloc(sizeof(bio_coro_impl_t));
	*coro = (bio_coro_impl_t){
		.entrypoint = entrypoint,
		.userdata = userdata,
		.state = BIO_CORO_READY,
		.daemon = options->daemon,
	};
	BIO_LIST_INIT(&coro->pending_signals);
	BIO_LIST_INIT(&coro->monitors);

	mco_desc desc = mco_desc_init(bio_coro_entry_wrapper, options->stack_size);
	desc.user_data = coro;
	mco_create(&coro->impl, &desc);

	coro->handle = bio_make_handle(coro, &BIO_CORO_HANDLE);

	bio_array_push(bio_ctx.next_ready_coros, coro);
	if (++bio_ctx.num_coros == 1) {
		bio_timer_update();
	}
	if (options->daemon) {
		++bio_ctx.num_daemons;
	}

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

bool
bio_raise_signal(bio_signal_t ref) {
	bool owner_waken_up = false;

	bio_signal_impl_t* signal = bio_close_handle(ref.handle, &BIO_SIGNAL_HANDLE);
	if (BIO_LIKELY(signal != NULL)) {
		bio_coro_impl_t* owner = signal->owner;

		if (
			owner->state == BIO_CORO_WAITING
			&& signal->wait_counter == owner->wait_counter
		) {
			if (--owner->num_blocking_signals == 0) {  // Schedule to run once
				bio_array_push(bio_ctx.next_ready_coros, owner);
				owner->state = BIO_CORO_READY;
				owner_waken_up = true;
			}
		}

		BIO_LIST_REMOVE(&signal->link);
		bio_free(signal);
	}

	return owner_waken_up;
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
		bool signal_raised = false;
		for (int i = 0; i < num_signals; ++i) {
			bio_handle_t handle = signals[i].handle;
			bio_signal_impl_t* signal = bio_resolve_handle(handle, &BIO_SIGNAL_HANDLE);

			if (BIO_LIKELY(signal != NULL)) {
				if (signal->owner == coro) {
					signal->wait_counter = wait_counter;
					++num_blocking_signals;
				}
			} else {
				// Only non-zero signals are considered raised
				signal_raised |= bio_handle_compare(handle, BIO_INVALID_HANDLE) != 0;
			}
		}

		if (BIO_LIKELY((num_blocking_signals > 0) && (wait_all || !signal_raised))) {
			coro->num_blocking_signals = wait_all ? num_blocking_signals : 1;
			mco_yield(impl);
		}
	}
}

bio_monitor_t
bio_monitor(bio_coro_t coro, bio_signal_t signal) {
	bio_coro_impl_t* coro_impl = bio_resolve_handle(coro.handle, &BIO_CORO_HANDLE);
	bio_signal_impl_t* signal_impl = bio_resolve_handle(signal.handle, &BIO_SIGNAL_HANDLE);
	if (
		BIO_LIKELY(
			coro_impl != NULL
			&& signal_impl != NULL
			&& signal_impl->owner != coro_impl
		)
	) {
		bio_monitor_impl_t* monitor = bio_malloc(sizeof(bio_monitor_impl_t));
		*monitor = (bio_monitor_impl_t){ .signal = signal };
		monitor->handle = bio_make_handle(monitor, &BIO_MONITOR_HANDLE);
		BIO_LIST_APPEND(&coro_impl->monitors, &monitor->link);
		return (bio_monitor_t){ .handle = monitor->handle };
	} else {
		bio_raise_signal(signal);
		return (bio_monitor_t){ .handle = BIO_INVALID_HANDLE };
	}
}

void
bio_unmonitor(bio_monitor_t ref) {
	bio_monitor_impl_t* monitor = bio_close_handle(ref.handle, &BIO_MONITOR_HANDLE);
	if (BIO_LIKELY(monitor != NULL)) {
		BIO_LIST_REMOVE(&monitor->link);
		bio_free(monitor);
	}
}

void
bio_set_coro_data(void* data, const bio_tag_t* tag) {
	mco_coro* impl = mco_running();
	if (BIO_LIKELY(impl)) {
		bio_coro_impl_t* coro = impl->user_data;
		coro->extra_data = data;
		coro->extra_data_tag = tag;
	}
}

void*
bio_get_coro_data(bio_coro_t coro, const bio_tag_t* tag) {
	bio_coro_impl_t* coro_impl = bio_resolve_handle(coro.handle, &BIO_CORO_HANDLE);
	if (BIO_LIKELY(coro_impl != NULL && coro_impl->extra_data_tag == tag)) {
		return coro_impl->extra_data;
	} else {
		return NULL;
	}
}

void
bio_set_coro_name(const char* name) {
	mco_coro* impl = mco_running();
	if (BIO_LIKELY(impl)) {
		bio_coro_impl_t* coro = impl->user_data;
		coro->name = name;
	}
}

const char*
bio_get_coro_name(bio_coro_t coro) {
	bio_coro_impl_t* coro_impl = bio_resolve_handle(coro.handle, &BIO_CORO_HANDLE);
	if (BIO_LIKELY(coro_impl != NULL)) {
		return coro_impl->name;
	} else {
		return NULL;
	}
}

// https://nullprogram.com/blog/2018/07/31/
static uint64_t
bio_splittable64(uint64_t x) {
	x ^= x >> 30;
	x *= 0xbf58476d1ce4e5b9U;
	x ^= x >> 27;
	x *= 0x94d049bb133111ebU;
	x ^= x >> 31;
	return x;
}

void*
bio_get_cls(const bio_cls_t* cls) {
	mco_coro* impl = mco_running();
	if (BIO_LIKELY(impl)) {
		bio_coro_impl_t* coro = impl->user_data;

		int num_buckets = bio_ctx.options.num_cls_buckets;
		bio_cls_link_t* buckets = coro->cls_buckets;
		if (buckets == NULL) {
			coro->cls_buckets = buckets = bio_malloc(sizeof(bio_cls_link_t) * num_buckets);
			for (int i = 0; i < num_buckets; ++i) {
				BIO_LIST_INIT(&buckets[i]);
			}
		}

		uint64_t hash = bio_splittable64((uint64_t)(uintptr_t)cls);
		int bucket_index = (int)(hash & ((uint64_t)num_buckets - 1));
		bio_cls_link_t* bucket = &buckets[bucket_index];

		for (bio_cls_link_t* itr = bucket->next; itr != bucket; itr = itr->next) {
			bio_cls_entry_t* entry = BIO_CONTAINER_OF(itr, bio_cls_entry_t, link);
			if (entry->spec == cls) {
				return entry->data;
			}
		}

		bio_cls_entry_t* new_entry = bio_malloc(sizeof(bio_cls_entry_t) + cls->size);
		new_entry->spec = cls;
		if (cls->init != NULL) { cls->init(new_entry->data); }
		BIO_LIST_APPEND(bucket, &new_entry->link);
		return new_entry->data;
	} else {
		return NULL;
	}
}
