#ifndef BIO_SERVICE_H
#define BIO_SERVICE_H

#include <bio/bio.h>
#include <bio/mailbox.h>

typedef enum {
	BIO_CALL_OK,
	BIO_CALL_CANCELLED,
	BIO_CALL_TARGET_DEAD,
} bio_call_status_t;

typedef struct {
	bio_signal_t bio__ack_signal;
	bio_signal_t bio__cancel_signal;
} bio_service_msg_base_t;

#define BIO_SERVICE(T) \
	struct { \
		bio_coro_t bio__coro; \
		BIO_MAILBOX(T) bio__mailbox; \
	}

#define BIO_SERVICE_MSG bio_service_msg_base_t bio__service_msg_base;

#define bio_start_service(ptr, entry, args, mailbox_capacity) \
	bio__service_start(\
		&(ptr)->bio__coro, \
		&(ptr)->bio__mailbox.bio__handle, \
		sizeof(*((ptr)->bio__mailbox.bio__message)), \
		mailbox_capacity, \
		entry, \
		&args, \
		sizeof(args) \
	)

#define bio_stop_service(service) \
	bio__service_stop((service).bio__coro, (service).bio__mailbox.bio__handle)

#define bio_get_service_info(userdata, mailbox_ptr, args_ptr) \
	bio__service_get_info(userdata, &(mailbox_ptr)->bio__handle, args_ptr)

#define bio_call_service(service, message, cancel_signal) \
	( \
		BIO__TYPECHECK_EXP((message), *(service.bio__mailbox.bio__message)), \
		bio__service_call( \
			(service).bio__coro, \
			(service).bio__mailbox.bio__handle, \
			&((message).bio__service_msg_base), \
			&(message), \
			sizeof(message), \
			cancel_signal \
		) \
	)

#define bio_is_call_cancelled(msg) \
	bio__service_is_call_cancelled((msg).bio__service_msg_base.bio__cancel_signal)

#define bio_begin_response(msg) \
	(!(bio_is_call_cancelled(msg)))

#define bio_end_response(msg) \
	bio_raise_signal((msg).bio__service_msg_base.bio__ack_signal)

#define bio_respond(msg) \
	for ( \
		bool bio__can_respond = bio_begin_response(msg); \
		bio__can_respond; \
		bio__can_respond = false, bio_end_response(msg) \
	)

void
bio__service_start(
	bio_coro_t* coro_ptr,
	bio_handle_t* mailbox_ptr,
	size_t mailbox_msg_size,
	uint32_t mailbox_capacity,
	bio_entrypoint_t entry,
	const void* args,
	size_t args_size
);

void
bio__service_get_info(void* userdata, bio_handle_t* mailbox_ptr, void* args);

void
bio__service_stop(bio_coro_t coro, bio_handle_t mailbox);

bio_call_status_t
bio__service_call(
	bio_coro_t coro,
	bio_handle_t mailbox,
	bio_service_msg_base_t* service_msg_base,
	const void* msg,
	size_t msg_size,
	bio_signal_t cancel_signal
);

bool
bio__service_is_call_cancelled(bio_signal_t cancel_signal);

#endif
