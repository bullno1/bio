#include "internal.h"
#include <minicoro.h>

static const bio_tag_t BIO_CORO_HANDLE = BIO_TAG_INIT("bio.handle.coro");
static const bio_tag_t BIO_SIGNAL_HANDLE = BIO_TAG_INIT("bio.handle.signal");

void
bio_scheduler_init(void) {
	bio_ctx.num_coros = 0;
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
		for (int i = 0; i < num_coros; ++i) {
			bio_coro_impl_t* coro = bio_ctx.current_ready_coros[i];
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
		bio_array_clear(bio_ctx.current_ready_coros);

		if (bio_ctx.num_coros == 0) { break; }

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

	bio_array_push(bio_ctx.next_ready_coros, coro);
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
