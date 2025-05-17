#include "common.h"
#include <bio/service.h>
#include <string.h>

typedef enum {
	INFO,
	ADD,
	DROP,
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

	while (true) {
		service_msg_t msg;
		if (!bio_recv_message(mailbox, &msg)) { break; }
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
		}
	}
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

	bio_stop_service(service);
}

BTEST(service, call) {
	bio_spawn(call, NULL);
	bio_loop();
}
