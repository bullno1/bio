#include <bio/bio.h>
#include <bio/service.h>

//! [Service]
typedef struct {
	BIO_SERVICE_MSG  // Required header
	int lhs;
	int rhs;
	int* result;
} msg_t;

void service_entry(void* userdata) {
	int start_arg;
	BIO_MAILBOX(msg_t) mailbox;
	bio_get_service_info(userdata, &mailbox, &start_arg);

	bio_service_loop(msg, mailbox) {
		// Calculate first, which may cause context switch
		int result = msg.lhs + msg.rhs;
		// Then write result in the bio_respond block
		bio_respond(msg) {
			*msg.result = result;
		}
	}

	// The mailbox will be closed by the service module
}

void use_service(void) {
	BIO_SERVICE(msg_t) adder;
	// Start argument is stack-allocated
	int start_arg = 42;
	bio_start_service(&adder, service_entry, start_arg);

	bio_signal_t no_cancel = { 0 };
	int result;
	msg_t msg = {
		.lhs = 1,
		.rhs = 2,
		// Write result to a stack-allocated variable
		.result = &result,
	};
	bio_call_status_t status = bio_call_service(adder, msg, no_cancel);
	if (status == BIO_CALL_OK) {
		BIO_INFO("The result is: %d", result);
	}

	bio_stop_service(adder);
}
//! [Service]

//! [Call with timeout]
void call_with_timeout(void) {
	BIO_SERVICE(msg_t) adder;  // Assume we retrieved this somehow
	bio_signal_t cancel = bio_make_signal();
	bio_raise_signal_after(cancel, 2000);  // Allow 2s before cancelling
	int result;
	msg_t msg = {
		.lhs = 1,
		.rhs = 2,
		// Write result to a stack-allocated variable
		.result = &result,
	};
	bio_call_status_t status = bio_call_service(adder, msg, cancel);
	// status might be BIO_CALL_CANCELLED
}
//! [Call with timeout]
