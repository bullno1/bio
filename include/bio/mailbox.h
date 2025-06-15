#ifndef BIO_MAILBOX_H
#define BIO_MAILBOX_H

#include <bio/bio.h>

/**
 * @defgroup mailbox Mailbox
 *
 * Message passing between coroutines
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

#define BIO_MAILBOX(T) union { bio_handle_t bio__handle; T* bio__message; }

#define bio_open_mailbox(ptr, capacity) \
	bio__mailbox_open(&(ptr)->bio__handle, sizeof(*(ptr)->bio__message), capacity)

#define bio_close_mailbox(mailbox) \
	bio__mailbox_close((mailbox).bio__handle)

#define bio_is_mailbox_open(mailbox) \
	bio__mailbox_is_open((mailbox).bio__handle)

#define bio_send_message(mailbox, message) \
	( \
		BIO__TYPECHECK_EXP(message, *mailbox.bio__message), \
		bio__mailbox_send(mailbox.bio__handle, &message, sizeof(message)) \
	)

#define bio_wait_and_send_message(condition, mailbox, message) \
	do { \
		while ((bio_is_mailbox_open(mailbox)) && (condition)) { \
			if (bio_send_message((mailbox), (message))) { break; } \
			bio_yield(); \
		} \
	} while (0)

#define bio_recv_message(mailbox, message) \
	( \
		BIO__TYPECHECK_EXP(*(message), *(mailbox.bio__message)), \
		bio__mailbox_recv(mailbox.bio__handle, (message), sizeof(*(message))) \
	)

#define bio_can_recv_message(mailbox) \
	bio__mailbox_can_recv((mailbox).bio__handle)

#define bio_can_send_message(mailbox) \
	bio__mailbox_can_send((mailbox).bio__handle)

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
