#include "internal.h"
#include <bio/service.h>
#include <string.h>

typedef struct {
	bio_handle_t mailbox;
	bio_signal_t ready_signal;
	bio_entrypoint_t entry;
	const void* args;
	size_t args_size;
} bio_service_data_t;

static void
bio_service_wrapper(void* userdata) {
	bio_service_data_t* service_data = (bio_service_data_t*)userdata;
	// mailbox handle must stay valid even after parent terminates
	bio_handle_t mailbox = service_data->mailbox;
	// Let parent resume *after* this coro yields
	bio_raise_signal(service_data->ready_signal);
	// Enter actual entry
	service_data->entry(service_data);
	// Cleanup
	bio__mailbox_close(mailbox);
}

void
bio__service_start(
	bio_coro_t* coro_ptr,
	bio_handle_t* mailbox_ptr,
	size_t mailbox_msg_size,
	uint32_t mailbox_capacity,
	bio_entrypoint_t entry,
	const void* args,
	size_t args_size
) {
	bio__mailbox_open(mailbox_ptr, mailbox_msg_size, mailbox_capacity);
	bio_signal_t ready_signal = bio_make_signal();
	bio_service_data_t service_data = {
		.mailbox = *mailbox_ptr,
		.ready_signal = ready_signal,
		.entry = entry,
		.args = args,
		.args_size = args_size,
	};
	*coro_ptr = bio_spawn(bio_service_wrapper, &service_data);
	bio_wait_for_signals(&ready_signal, 1, true);
}

void
bio__service_get_info(void* userdata, bio_handle_t* mailbox_ptr, void* args) {
	bio_service_data_t* service_data = userdata;
	*mailbox_ptr = service_data->mailbox;

	if (args != NULL) {
		memcpy(args, service_data->args, service_data->args_size);
	}
}

void
bio__service_stop(bio_coro_t coro, bio_handle_t mailbox) {
	bio__mailbox_close(mailbox);
	if (bio_coro_state(coro) != BIO_CORO_DEAD) {
		bio_signal_t term_signal = bio_make_signal();
		bio_monitor(coro, term_signal);
		bio_wait_for_signals(&term_signal, 1, true);
	}
}

bio_call_status_t
bio__service_call(
	bio_coro_t coro,
	bio_handle_t mailbox,
	bio_service_msg_base_t* service_msg_base,
	const void* msg,
	size_t msg_size,
	bio_signal_t cancel_signal
) {
	if (bio_coro_state(coro) == BIO_CORO_DEAD) {
		return BIO_CALL_TARGET_DEAD;
	}

	bool cancellable = bio_handle_compare(cancel_signal.handle, BIO_INVALID_HANDLE) != 0;

	bio_signal_t ack_signal = bio_make_signal();
	service_msg_base->bio__ack_signal = ack_signal;
	service_msg_base->bio__cancel_signal = cancel_signal;

	bio_signal_t monitor_signal = bio_make_signal();
	bio_monitor_t monitor = bio_monitor(coro, monitor_signal);

	while (
		!(cancellable && bio_check_signal(cancel_signal))  // Call not cancelled
		&& !bio_check_signal(monitor_signal)  // Target not dead
		&& !bio__mailbox_send(mailbox, msg, msg_size)  // Cannot send
	) {
		bio_yield();  // Wait for target to be free
	}

	bio_signal_t signals[3] = {
		ack_signal,
		monitor_signal,
		cancel_signal,
	};
	bio_wait_for_signals(signals, sizeof(signals) / sizeof(signals[0]), false);

	bio_call_status_t status;
	if (bio_check_signal(ack_signal)) {
		status = BIO_CALL_OK;
	} else if (bio_check_signal(cancel_signal)) {
		status = BIO_CALL_CANCELLED;
	} else {
		status = BIO_CALL_TARGET_DEAD;
	}

	bio_unmonitor(monitor);
	bio_raise_signal(monitor_signal);
	bio_raise_signal(ack_signal);

	return status;
}

bool
bio__service_is_call_cancelled(bio_signal_t cancel_signal) {
	bool cancellable = bio_handle_compare(cancel_signal.handle, BIO_INVALID_HANDLE) != 0;
	return cancellable && bio_check_signal(cancel_signal);
}
