#ifndef BIO_MAILBOX_H
#define BIO_MAILBOX_H

#include <bio/bio.h>

/**
 * @defgroup mailbox Mailbox
 *
 * Message passing between coroutines.
 *
 * Inspired by [Erlang](https://www.erlang.org/), bio provides a way for coroutines
 * to message one another.
 *
 * This module also makes use of macro to enforce compile-time type checking.
 *
 * Example:
 *
 * @code
 * // A mailbox can receive different types of messages
 * typedef enum {
 *     TYPE_A,
 *     TYPE_B,
 * } msg_type_t;
 *
 * // Tagged union for message type
 * typedef struct {
 *     msg_type_t type;
 *     union {
 *         struct {
 *             int foo;
 *         } a;
 *         struct {
 *             int bar;
 *         } b;
 *     };
 * } msg_t;
 *
 * // Define the mailbox type
 * typedef BIO_MAILBOX(msg_t) mailbox_t;
 *
 * void entry(void* userdata) {
 *     mailbox_t mailbox;
 *     bio_open_mailbox(mailbox, 16);  // 16 messages in queue
 *
 *     // Pass this to other coroutines
 *     bio_coro_t child_coro = bio_spawn(child_entry, &mailbox);
 *
 *     // Message loop
 *     msg_t message;
 *     while (bio_recv_message(mailbox, &message)) {
 *         // Process message
 *     }
 *
 *     bio_close_mailbox(mailbox);
 *     bio_join(child_coro);
 * }
 * @endcode
 *
 * @see service
 *
 * @{
 */

#ifndef DOXYGEN

#if __STDC_VERSION__ >= 202311L
#	define BIO__TYPEOF(EXP) typeof(EXP)
#elif defined(__clang__) || defined(__GNUC__) || defined(_MSC_VER)
#	define BIO__TYPEOF(EXP) __typeof__(EXP)
#endif

#ifdef BIO__TYPEOF
// _Static_assert is actually quite hard to use in an expression.
// Jamming it into a struct: `sizeof(struct{_Static_assert(...);})` doesn't work
// quite well.
// Clang requires the RHS of BIO__TYPECHECK_EXP to be a constant expression
// which is not always the case in typical user code.
// The good old negative size array works in all compilers but the error message
// is somewhat cryptic.
#	define BIO__TYPECHECK_EXP(LHS, RHS) \
	(void)sizeof(char[_Generic(RHS, BIO__TYPEOF(LHS): 1, default: -1)]) /* If you get an error here, you have the wrong type */
#else
// Check both size and assignability as integer types of different sizes are
// assignable (with warnings).
// We can't count on user having warnings enabled.
#	define BIO__TYPECHECK_EXP(LHS, RHS) \
	((void)sizeof(LHS = RHS), (void)sizeof(char[sizeof(LHS) == sizeof(RHS) ? 1 : -1]))  /* If you get an error here, you have the wrong type */
#endif

#endif

/**
 * Define a mailbox type
 *
 * @param T the type of message for this mailbox
 */
#define BIO_MAILBOX(T) union { bio_handle_t bio__handle; T* bio__message; }

/**
 * Create a new mailbox
 *
 * A mailbox holds all of its messages by copy.
 * When not needed, it should be closed with @ref bio_close_mailbox.
 *
 * @param ptr Pointer to a mailbox handle
 * @param capacity How many messages can this mailbox hold before senders are blocked
 */
#define bio_open_mailbox(ptr, capacity) \
	bio__mailbox_open(&(ptr)->bio__handle, sizeof(*(ptr)->bio__message), capacity)

/**
 * Close a mailbox
 *
 * As with all resources in bio, closing an already closed mailbox is not an error.
 * In fact, it is idiomatic to close a mailbox that another coroutine is waiting
 * on to tell it to terminate.
 *
 * @see handle
 */
#define bio_close_mailbox(mailbox) \
	bio__mailbox_close((mailbox).bio__handle)

/// Check whether a mailbox is open
#define bio_is_mailbox_open(mailbox) \
	bio__mailbox_is_open((mailbox).bio__handle)

/**
 * Attempt to send a message to a mailbox
 *
 * The message will be copied into the mailbox.
 * Upon return, it is safe to invalidate/reuse/free the @p message.
 *
 * A mailbox has limited capacity so when it is full, this will return `false`
 * and the message was not actually sent.
 *
 * A mailbox can also already be closed.
 * In that case, this will also return `false`.
 *
 * @param mailbox The mailbox to send to
 * @param message The message, must be of the same accepted type as the declared mailbox.
 *   This will be checked at compile-time.
 * @return Whether the message was actually sent.
 *
 * @see BIO_MAILBOX
 * @see bio_wait_and_send_message
 */
#define bio_send_message(mailbox, message) \
	( \
		BIO__TYPECHECK_EXP(message, *mailbox.bio__message), \
		bio__mailbox_send(mailbox.bio__handle, &message, sizeof(message)) \
	)

/**
 * A more reliable way to send message
 *
 * This will attempt to send to a mailbox and busy wait if the mailbox is full.
 * If the mailbox is already closed, it will stop trying.
 *
 * @code{.c}
 * // Main loop
 * while (!should_terminate) {
 *     // ...
 *
 *     bio_wait_and_send_message(
 *         !should_terminate,  // Do not retry if we received a termination signal
 *         mailbox,
 *         message
 *     );
 * }
 * @endcode
 *
 * @param condition A boolean expression
 * @param mailbox The mailbox to send to
 * @param message The message
 */
#define bio_wait_and_send_message(condition, mailbox, message) \
	do { \
		while ((bio_is_mailbox_open(mailbox)) && (condition)) { \
			if (bio_send_message((mailbox), (message))) { break; } \
			bio_yield(); \
		} \
	} while (0)

/**
 * Attempt to receive from a mailbox
 *
 * This will dequeue a message from the mailbox.
 *
 * If the mailbox is empty, the calling coroutine will be suspended.
 * It will be resumed once a message is sent to the mailbox.
 * Otherwise, this would return immediately without context switching.
 *
 * If the mailbox is already closed, this will immediately return `false`.
 *
 * If a mailbox is closed while a coroutine is waiting on it, the coroutine
 * will be resumed with this returning `false`.
 * Closing a mailbox is an idiomatic way to ask a coroutine to terminate.
 *
 * @param mailbox The mailbox to receive from
 * @param message Pointer to a message
 * @return Whether a message was received
 */
#define bio_recv_message(mailbox, message) \
	( \
		BIO__TYPECHECK_EXP(*(message), *(mailbox.bio__message)), \
		bio__mailbox_recv(mailbox.bio__handle, (message), sizeof(*(message))) \
	)

/**
 * Check whether a mailbox can be immediately received from
 *
 * Return whether all of the following are true:
 *
 * * The mailbox is not closed
 * * The mailbox is not empty
 */
#define bio_can_recv_message(mailbox) \
	bio__mailbox_can_recv((mailbox).bio__handle)

/**
 * Check whether a mailbox can be immediately sent to
 *
 * Return whether all of the following are true:
 *
 * * The mailbox is not closed
 * * The mailbox is not full
 */
#define bio_can_send_message(mailbox) \
	bio__mailbox_can_send((mailbox).bio__handle)

/**
 * Convenient message loop macro
 *
 * @code
 * mailbox_t mailbox;
 * bio_open_mailbox(mailbox, 16);
 *
 * bio_foreach_message(message, mailbox) {  // message is declared in this scope
 *     if (message.type == MSG_QUIT) { break; }
 * }
 *
 * bio_close_mailbox(mailbox);
 * @endcode
 */
#define bio_foreach_message(msg, mailbox) \
	for ( \
		BIO__TYPEOF(*(mailbox).bio__message) msg; \
		bio_recv_message(mailbox, &msg); \
	)

#ifndef DOXYGEN

void
bio__mailbox_open(bio_handle_t* handle, size_t item_size, uint32_t capacity);

void
bio__mailbox_close(bio_handle_t handle);

bool
bio__mailbox_is_open(bio_handle_t handle);

bool
bio__mailbox_send(bio_handle_t handle, const void* data, size_t message_size);

bool
bio__mailbox_recv(bio_handle_t handle, void* data, size_t message_size);

bool
bio__mailbox_can_recv(bio_handle_t handle);

bool
bio__mailbox_can_send(bio_handle_t handle);

#endif

/**@}*/

#endif
