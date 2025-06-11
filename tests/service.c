#include "common.h"
#include <bio/service.h>

typedef enum {
	INFO,
	ADD,
	DROP,
	DIE,
} service_msg_type_t;

typedef struct {
	BIO_SERVICE_MSG
	service_msg_type_t type;

	union {
		struct {
			int* start_arg;
		} info;

		struct {
			int lhs;
			int rhs;
			int* result;
		} add;
	};
} service_msg_t;

static btest_suite_t service_ = {
	.name = "service",
	.init_per_test = init_bio,
	.cleanup_per_test = cleanup_bio,
};

static void
service_entry(void* userdata) {
	int start_arg;
	BIO_MAILBOX(service_msg_t) mailbox;
	bio_get_service_info(userdata, &mailbox, &start_arg);

	bio_service_loop(msg, mailbox) {
		bio_yield();  // Simulate other activites that switch context
		switch (msg.type) {
			case INFO:
				bio_respond(msg) {
					*msg.info.start_arg = start_arg;
				}
				break;
			case ADD: {
				// Calculate first, which may cause context switch
				int result = msg.add.lhs + msg.add.rhs;
				// Then write result in the bio_respond block
				bio_respond(msg) {
					*msg.add.result = result;
				}
			} break;
			case DROP:
				break;
			case DIE:
				goto end;
				break;
		}
	}
end:;
}

BIO_TEST(service_, call) {
	BIO_SERVICE(service_msg_t) service;
	int start_arg = 42;
	bio_start_service(&service, service_entry, start_arg, 4);

	int saved_start_arg;
	service_msg_t info = {
		.type = INFO,
		.info.start_arg = &saved_start_arg,
	};
	bio_signal_t cancel_signal = { 0 };

	bio_call_status_t status = bio_call_service(service, info, cancel_signal);
	BTEST_EXPECT_EX(status == BIO_CALL_OK, "status = %d", status);
	BTEST_EXPECT(saved_start_arg == start_arg);

	int add_result;
	int lhs = 33;
	int rhs = 44;
	service_msg_t add = {
		.type = ADD,
		.add = {
			.lhs = lhs,
			.rhs = rhs,
			.result = &add_result,
		},
	};
	status = bio_call_service(service, add, cancel_signal);
	BTEST_EXPECT_EX(status == BIO_CALL_OK, "status = %d", status);
	BTEST_EXPECT(add_result == (lhs + rhs));

	bio_stop_service(service);
	BTEST_EXPECT(bio_coro_state(service.coro) == BIO_CORO_DEAD);
}

static void
canceller(void* userdata) {
	bio_signal_t signal = *(bio_signal_t*)userdata;
	bio_yield();
	bio_raise_signal(signal);
}

BIO_TEST(service_, cancel) {
	BIO_SERVICE(service_msg_t) service;
	int start_arg = 42;
	bio_start_service(&service, service_entry, start_arg, 4);

	int saved_start_arg = 0;
	service_msg_t info = {
		.type = INFO,
		.info.start_arg = &saved_start_arg,
	};
	bio_signal_t cancel_signal = bio_make_signal();
	bio_raise_signal(cancel_signal);
	bio_call_status_t status = bio_call_service(service, info, cancel_signal);
	BTEST_EXPECT(status == BIO_CALL_CANCELLED);
	BTEST_EXPECT(saved_start_arg == 0);

	// Call timeout
	service_msg_t drop = {  // This request will never be fulfilled
		.type = DROP,
	};
	cancel_signal = bio_make_signal();
	bio_raise_signal_after(cancel_signal, 1);
	status = bio_call_service(service, drop, cancel_signal);
	BTEST_EXPECT(status == BIO_CALL_CANCELLED);

	// Async cancel
	cancel_signal = bio_make_signal();
	bio_spawn(canceller, &cancel_signal);
	status = bio_call_service(service, info, cancel_signal);
	BTEST_EXPECT(status == BIO_CALL_CANCELLED);
	BTEST_EXPECT(saved_start_arg == 0);  // service must not write to var

	bio_stop_service(service);
	BTEST_EXPECT(saved_start_arg == 0);  // service must not write to var
}

BIO_TEST(service_, target_dies_during_call) {
	BIO_SERVICE(service_msg_t) service;
	int start_arg = 42;
	bio_start_service(&service, service_entry, start_arg, 4);

	service_msg_t die = {
		.type = DIE,
	};
	bio_signal_t cancel_signal = { 0 };
	bio_call_status_t status = bio_call_service(service, die, cancel_signal);
	BTEST_EXPECT(status == BIO_CALL_TARGET_DEAD);

	bio_stop_service(service);
}

BIO_TEST(service_, target_already_dead) {
	BIO_SERVICE(service_msg_t) service;
	int start_arg = 42;
	bio_start_service(&service, service_entry, start_arg, 4);
	bio_stop_service(service);

	service_msg_t die = {
		.type = INFO,
	};
	bio_signal_t cancel_signal = { 0 };
	bio_call_status_t status = bio_call_service(service, die, cancel_signal);
	BTEST_EXPECT(status == BIO_CALL_TARGET_DEAD);

	cancel_signal = bio_make_signal();
	status = bio_call_service(service, die, cancel_signal);
	BTEST_EXPECT(status == BIO_CALL_TARGET_DEAD);
}

static void
kill_service(void* userdata) {
	typedef BIO_SERVICE(service_msg_t) service_t;
	service_t service = *(service_t*)userdata;
	bio_stop_service(service);
}

BIO_TEST(service_, target_stops_during_call) {
	BIO_SERVICE(service_msg_t) service;
	int start_arg = 42;
	bio_start_service(&service, service_entry, start_arg, 4);

	service_msg_t die = {
		.type = DROP,
	};
	bio_signal_t cancel_signal = { 0 };
	bio_spawn(kill_service, &service);
	bio_call_status_t status = bio_call_service(service, die, cancel_signal);
	BTEST_EXPECT(status == BIO_CALL_TARGET_DEAD);
}

BIO_TEST(service_, notify) {
	BIO_SERVICE(service_msg_t) service;
	int start_arg = 42;
	bio_start_service(&service, service_entry, start_arg, 4);
	int result = 0;
	service_msg_t msg = {
		.type = INFO,
		.info.start_arg = &result,
	};
	bio_notify_service(service, msg, true);
	bio_yield();  // Let the service run
	bio_yield();
	BTEST_EXPECT(result == 0); // Service must not write to var
	bio_stop_service(service);

	// This should not block
	bio_notify_service(service, msg, true);
	bio_yield();

	BTEST_EXPECT(bio_service_state(service) == BIO_CORO_DEAD);
}

static void
self_stop_service(void* userdata) {
	int start_arg;
	BIO_MAILBOX(int) mailbox;
	bio_get_service_info(userdata, &mailbox, &start_arg);

	BIO_SERVICE(int) self = { .coro = bio_current_coro() };
	self.mailbox.bio__handle = mailbox.bio__handle;

	bio_foreach_message(msg, mailbox) {
		if (msg == start_arg) {
			bio_stop_service(self);
		}
	}
}

BIO_TEST(service_, self_stop) {
	BIO_SERVICE(int) service;
	int start_arg = 42;
	bio_start_service(&service, self_stop_service, start_arg, 4);
	bio_wait_and_send_message(true, service.mailbox, start_arg);
	bio_join(service.coro);
}
