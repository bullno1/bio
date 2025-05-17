#include "common.h"
#include <bio/service.h>
#include <string.h>

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

static btest_suite_t service = {
	.name = "service",
	.init_per_test = init_bio,
	.cleanup_per_test = cleanup_bio,
};

static void
service_entry(void* userdata) {
	int start_arg;
	BIO_MAILBOX(service_msg_t) mailbox;
	bio_get_service_info(userdata, &mailbox, &start_arg);

	bio_foreach_message(msg, mailbox) {
		if (bio_is_call_cancelled(msg)) { continue; }

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

static void
call(void* userdata) {
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
	BTEST_EXPECT(status == BIO_CALL_OK);
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
	BTEST_EXPECT(status == BIO_CALL_OK);
	BTEST_EXPECT(add_result == (lhs + rhs));

	bio_stop_service(service);
	BTEST_EXPECT(bio_coro_state(service.bio__coro) == BIO_CORO_DEAD);
}

BTEST(service, call) {
	bio_spawn(call, NULL);
	bio_loop();
}

static void
cancel(void* userdata) {
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

	service_msg_t drop = {  // This request will never be fulfilled
		.type = DROP,
	};
	cancel_signal = bio_make_signal();
	bio_raise_signal_after(cancel_signal, 1);
	status = bio_call_service(service, drop, cancel_signal);
	BTEST_EXPECT(status == BIO_CALL_CANCELLED);

	bio_stop_service(service);
}

BTEST(service, cancel) {
	bio_spawn(cancel, NULL);
	bio_loop();
}

static void
target_die_during_call(void* userdata) {
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

BTEST(service, target_die_during_call) {
	bio_spawn(target_die_during_call, NULL);
	bio_loop();
}

static void
target_already_dead(void* userdata) {
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

BTEST(service, target_already_dead) {
	bio_spawn(target_already_dead, NULL);
	bio_loop();
}

static void
kill_service(void* userdata) {
	typedef BIO_SERVICE(service_msg_t) service_t;
	service_t service = *(service_t*)userdata;
	bio_stop_service(service);
}

static void
target_stopped_during_call(void* userdata) {
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

BTEST(service, target_stopped_during_call) {
	bio_spawn(target_stopped_during_call, NULL);
	bio_loop();
}
