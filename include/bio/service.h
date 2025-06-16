#ifndef BIO_SERVICE_H
#define BIO_SERVICE_H

#include <bio/bio.h>
#include <bio/mailbox.h>

/**
 * @defgroup service Service
 *
 * Idiomatic coroutine interaction.
 *
 * This is inspired by Erlang's [gen_server](https://www.erlang.org/doc/apps/stdlib/gen_server.html) and [proc_lib](https://www.erlang.org/doc/apps/stdlib/proc_lib.html).
 *
 * bio already provides @ref coro and @ref mailbox.
 * However, there are several higher-level concerns that it does not address:
 *
 * * How to handle request/response type interaction between coroutines.
 * * Lifetime of data passed between coroutines.
 *   What would happen if either the sender or the receiver has terminated?
 *
 * This module offers a simple way to deal with the above concerns.
 *
 * Example:
 *
 * @snippet samples/service.c Service
 *
 * In the above example, the service consumer makes use of several stack-allocated
 * values.
 * This is typical for C code.
 * They would be invalid to access if it has terminated.
 * However, the service modules provide several guarantee so that it is safe to
 * do so.
 * These will be explained in this document.
 *
 * @{
 */

/// The status of a call using @ref bio_call_service
typedef enum {
	/// The call was handled by the service
	BIO_CALL_OK,
	///  The call was cancelled by the caller
	BIO_CALL_CANCELLED,
	/// The service has terminated during or before the call
	BIO_CALL_TARGET_DEAD,
} bio_call_status_t;

/**
 * The base for a message type of a service.
 *
 * This must be included in the message struct if the service wants to provide
 * a @ref bio_call_service interface.
 */
typedef struct {
	bio_signal_t bio__ack_signal;
	bio_signal_t bio__cancel_signal;
} bio_service_msg_base_t;

/**
 * A service reference struct.
 *
 * This is a combination of a @ref bio_coro_t handle and a @ref BIO_MAILBOX handle
 */
#define BIO_SERVICE(T) \
	struct { \
		bio_coro_t coro; \
		BIO_MAILBOX(T) mailbox; \
	}

/// Helper to include @ref bio_service_msg_base_t in a message type.
#define BIO_SERVICE_MSG bio_service_msg_base_t bio__service_msg_base;

/**
 * Start a service coroutine
 *
 * The caller will suspend until the service has called @ref bio_get_service_info.
 * This ensures that the stack allocated @p args has been copied from the caller's
 * stack into the service's stack.
 *
 * @param ptr Pointer to a variable of type @ref BIO_SERVICE
 * @param entry Entrypoint to the service
 * @param arg Start argument to be passed to the service
 * @param mailbox_capacity Mailbox capacity of the service
 *
 * @see bio_stop_service
 * @see bio_call_service
 */
#define bio_start_service(ptr, entry, args, mailbox_capacity) \
	bio__service_start(\
		&(ptr)->coro, \
		&(ptr)->mailbox.bio__handle, \
		sizeof(*((ptr)->mailbox.bio__message)), \
		mailbox_capacity, \
		entry, \
		&args, \
		sizeof(args) \
	)

/**
 * Stop a service
 *
 * The caller will suspends until the targeted service has terminated.
 * This is to ensure that the service can no longer refer to any shared resources that
 * was passed through @ref bio_start_service.
 * The caller may safely release them after this returns.
 *
 * As with all bio's resources, stopping an already stopped service is not an error.
 */
#define bio_stop_service(service) \
	bio__service_stop((service).coro, (service).mailbox.bio__handle)

/**
 * The service entrypoint must call this as soon as possible.
 *
 * @param userdata The userdata from the coroutine entrypoint.
 * @param mailbox_ptr Pointer to a mailbox variable of the appropriate message type.
 * @param args_ptr Pointer to a variable of the same type as @p args that was
 *   passed to @ref bio_start_service
 */
#define bio_get_service_info(userdata, mailbox_ptr, args_ptr) \
	bio__service_get_info(userdata, &(mailbox_ptr)->bio__handle, args_ptr)

/**
 * Make a synchronous service call
 *
 * The calling coroutine will be suspended until one of the following happens:
 *
 * * The service responds to the call.
 * * The service terminates before or during the call.
 * * The call is cancelled.
 *
 * The message may contain pointers to stack-allocated variables or resources
 * that will be freed when the calling coroutine terminates as long as both
 * the caller and the callee follow the guidelines in this document.
 *
 * To implement service call with timeout, something like this can be used:
 *
 * @snippet samples/service.c Call with timeout
 *
 * @param service The service handle
 * @param message The message for this call
 * @param cancel_signal Cancel signal for this call.
 *   If an invalid handle is passed, this call will not be cancellable.
 * @return A value of type @ref bio_call_status_t
 *
 * @see bio_start_service
 */
#define bio_call_service(service, message, cancel_signal) \
	( \
		BIO__TYPECHECK_EXP((message), *((service).mailbox.bio__message)), \
		bio__service_call( \
			(service).coro, \
			(service).mailbox.bio__handle, \
			&((message).bio__service_msg_base), \
			&(message), \
			sizeof(message), \
			cancel_signal \
		) \
	)

/**
 * Check a service's state
 *
 * @param service A variable of type @ref BIO_SERVICE
 * @return A value of type @ref bio_coro_state_t
 */
#define bio_service_state(service) \
	bio_coro_state((service).coro)

/**
 * Send a notification to a service
 *
 * This is similar to @ref bio_call_service but a respond is not expected.
 * The calling coroutine might suspend until the message has been delivered or
 * the service has terminated prematurely.
 *
 * @param service A variable of type @ref BIO_SERVICE
 * @param message A message for the service
 * @param retry_condition A boolean expression
 *
 * @see bio_send_message
 */
#define bio_notify_service(service, message, retry_condition) \
	bio_wait_and_send_message( \
		((bio_service_state(service) != BIO_CORO_DEAD) && (retry_condition)), \
		(service).mailbox, \
		(message) \
	)

/**
 * Check whether a call was cancelled.
 *
 * The service handler would typically call this after it has done something
 * that involves context-switching (e.g: querying a database).
 *
 * If this returns true, the service can stop its handler early.
 * In addition, if the respond involves allocating some resources, they should
 * be freed as the caller is no longer interested in the result.
 *
 * @param msg The message from a caller.
 * @return Whether the call was cancelled.
 */
#define bio_is_call_cancelled(msg) \
	bio__service_is_call_cancelled(&(msg).bio__service_msg_base)

/**
 * Message loop helper for the service
 *
 * This should be used instead of manually receiving message.
 * Refer to the example at the beginning of this section.
 *
 * This will automatically do the following:
 *
 * * Dropping cancelled messages
 * * Break out if @ref bio_stop_service is called
 */
#define bio_service_loop(msg, mailbox) \
	bio_foreach_message(msg, mailbox) \
		for ( \
			int bio__svc_loop = 0; \
			(bio__svc_loop < 1) && !bio_is_call_cancelled(msg); \
			++bio__svc_loop \
		)

/**
 * Helper for cancellable service call
 *
 * Instead of writing the result directly to the request message, the service
 * should do its computation using local variables first.
 * Only at the final step, it would write the result in a `bio_respond` block:
 *
 * @code{.c}
 * bio_respond(msg) {
 *     *msg.result = result;
 * }
 * @endcode
 *
 * This is to ensure that if the call has been cancelled and the caller has
 * terminated, the write would not be executed.
 *
 * If the result involves resource allocation, the service should check whether
 * that is needed before responding:
 *
 * @code{.c}
 * if (!bio_is_call_cancelled(msg)) {
 *     bio_respond(msg) {
 *         *msg.result = malloc(...);
 *     }
 * }
 * @endcode
 *
 * Alternatively, the service might have allocated resources before hand and they
 * should be freed upon cancellation:
 *
 * @code{.c}
 * // Allocate resource for computation
 * resource_t* resource = alloc_resource();
 * // Do computation
 * // ...
 * // The call might be cancelled by this point
 * if (!bio_is_call_cancelled(msg)) {
 *     bio_respond(msg) {
 *         *msg.result = resource;
 *     }
 * } else {
 *     // There is no one to receive this
 *     free_resource(resource);
 * }
 * @endcode
 *
 * The response writing code block will also be skipped if the caller uses
 * @ref bio_notify_service to send the request message.
 * Thus, it is safe, albeit potentially inefficient to use mismatched service
 * call types: Sending a notification using a message type with call semantics.
 *
 * This must be called to resume the caller's execution, otherwise the caller
 * might be suspended indefinitely.
 * In a `void` but synchrnous service call, use an empty block:
 *
 * @code{.c}
 * bio_respond(msg) {}
 * @endcode
 */
#define bio_respond(msg) \
	for ( \
		bool bio__should_respond = bio_begin_response(msg); \
		bio__should_respond; \
		bio__should_respond = false, bio_end_response(msg) \
	)

#ifndef DOXYGEN

#define bio_begin_response(msg) \
	(bio__service_begin_response(&(msg).bio__service_msg_base))

#define bio_end_response(msg) \
	bio_raise_signal((msg).bio__service_msg_base.bio__ack_signal)

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
bio__service_is_call_cancelled(const bio_service_msg_base_t* msg_base);

bool
bio__service_begin_response(const bio_service_msg_base_t* msg_base);

#endif

/**@}*/

#endif
