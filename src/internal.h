#ifndef BIO_INTERNAL_H
#define BIO_INTERNAL_H

/**
 * @defgroup internal Internal details
 *
 * For porting to a new platform
 *
 * For each platform, bio makes use of its specific async I/O API.
 * To port to a new platform, all of the functions mentioned here must be
 * implemented in addition to all @ref file and @ref net functions.
 *
 * The @ref bio_loop "event loop" and the @ref bio_run_async "async thread pool"
 * are largely platform-agnostic.
 * All the platform-dependent details are abstracted into @ref bio_platform_update
 * and @ref bio_platform_notify.
 *
 * The I/O functions are usually implemented like this:
 *
 * * Create a new @ref bio_make_signal "signal"
 * * Submit an async I/O request using the platform's API, along with the signal
 *   as "userdata".
 * * Call @ref bio_wait_for_one_signal
 *
 * This would suspend the calling coroutine.
 *
 * Later on, when all existing coroutines have yielded or suspended, bio will
 * call @ref bio_platform_update.
 * The implementation should check for completed I/O requests there.
 * Through the "userdata" feature often found in async I/O APIs, it will call
 * @ref bio_raise_signal to resume coroutines whose I/O requests have completed.
 *
 * The async thread pool is handled through @ref bio_platform_notify.
 * Whenever a task has finished execution, bio will call @ref bio_platform_notify
 * from the **worker thread**, not the main thread.
 * The implementation must have a way to make @ref bio_platform_update return
 * when signalled from a different thread regardless of whether an I/O event
 * has occured.
 * Async I/O APIs often have some sort of "user event" that can be
 * posted from another thread for this purpose.
 *
 * @{
 */

/// The default number of CLS hash buckets
#ifndef BIO_DEFAULT_NUM_CLS_BUCKETS
#	define BIO_DEFAULT_NUM_CLS_BUCKETS 4
#endif

/// The default number of threads in the async thread pool
#ifndef BIO_DEFAULT_THREAD_POOL_SIZE
#	define BIO_DEFAULT_THREAD_POOL_SIZE 2
#endif

/// The default queue size for the async thread pool
#ifndef BIO_DEFAULT_THREAD_POOL_QUEUE_SIZE
#	define BIO_DEFAULT_THREAD_POOL_QUEUE_SIZE 2
#endif

/**@}*/

#if defined(__linux__)
#	include "linux/platform.h"
#elif defined(_WIN32)
#	include "windows/platform.h"
#elif defined(__FreeBSD__)
#	include "freebsd/platform.h"
#else
#	error "Unsupported platform"
#endif

#include <bio/bio.h>
#include <stdio.h>
#include "array.h"

#ifdef __GNUC__
#	define BIO_LIKELY(X) __builtin_expect(!!(X), 1)
#else
#	define BIO_LIKELY(X) (X)
#endif

#define BIO_DEFINE_LIST_LINK(NAME) \
	typedef struct NAME##_s { \
		struct NAME##_s* next; \
		struct NAME##_s* prev; \
	} NAME##_t

#define BIO_LIST_APPEND(list, item) \
	do { \
		(item)->next = (list); \
		(item)->prev = (list)->prev; \
		(list)->prev->next = (item); \
		(list)->prev = (item); \
	} while (0)

#define BIO_LIST_REMOVE(item) \
	do { \
		if ((item)->next != NULL) { \
			(item)->next->prev = (item)->prev; \
			(item)->prev->next = (item)->next; \
			(item)->next = NULL; \
			(item)->prev = NULL; \
		} \
	} while (0)

#define BIO_LIST_IS_EMPTY(list) \
	((list)->next == (list) || (list)->next == NULL)

#define BIO_LIST_INIT(list) \
	do { \
		(list)->next = (list); \
		(list)->prev = (list); \
	} while (0)

#define BIO_LIST_POP(list) \
	BIO_LIST_REMOVE((list)->next)

#define BIO_CONTAINER_OF(ptr, type, member) \
	((type *)((char *)(1 ? (ptr) : &((type *)0)->member) - offsetof(type, member)))

#ifdef _MSC_VER
#	define BIO_ALIGN_TYPE long double
#else
#	define BIO_ALIGN_TYPE max_align_t
#endif

typedef struct {
	const bio_tag_t* tag;
	void* obj;

	int32_t gen;
	int32_t next_handle_slot;
} bio_handle_slot_t;

typedef struct mco_coro mco_coro;

BIO_DEFINE_LIST_LINK(bio_signal_link);
BIO_DEFINE_LIST_LINK(bio_logger_link);
BIO_DEFINE_LIST_LINK(bio_monitor_link);
BIO_DEFINE_LIST_LINK(bio_cls_link);

typedef struct bio_coro_impl_s bio_coro_impl_t;

typedef struct {
	bio_signal_link_t link;

	bio_coro_impl_t* owner;
	bio_handle_t handle;

	int wait_counter;
} bio_signal_impl_t;

typedef struct {
	bio_cls_link_t link;
	const bio_cls_t* spec;

	_Alignas(BIO_ALIGN_TYPE) char data[];
} bio_cls_entry_t;

typedef struct {
	bio_signal_t signal;
	bio_exit_reason_t reason;
} bio_exit_info_t;

struct bio_coro_impl_s {
	mco_coro* impl;
	bio_entrypoint_t entrypoint;
	bool daemon;
	void* userdata;
	void* extra_data;
	const bio_tag_t* extra_data_tag;
	const char* name;
	bio_cls_link_t* cls_buckets;

	bio_handle_t handle;
	bio_coro_state_t state;

	bio_signal_link_t pending_signals;
	bio_monitor_link_t monitors;

	int num_blocking_signals;
	int wait_counter;
};

typedef struct {
	bio_monitor_link_t link;
	bio_handle_t handle;
	bio_signal_t signal;
} bio_monitor_impl_t;

typedef struct bio_worker_thread_s bio_worker_thread_t;

typedef struct {
	bio_time_t due_time_ms;
	bio_signal_t signal;
} bio_timer_entry_t;

typedef struct {
	char* ptr;
	int len;
} bio_fmt_buf_t;

typedef struct {
	bio_options_t options;
	bool is_terminating;
	bio_exit_info_t* exit_handler;

	// Handle table
	bio_handle_slot_t* handle_slots;
	int32_t handle_capacity;
	int32_t next_handle_slot;
	int32_t num_handles;

	// Timer
	bio_timer_entry_t* timer_entries;
	int32_t timer_capacity;
	int32_t num_timers;
	bio_time_t current_time_ms;

	// Scheduler
	BIO_ARRAY(bio_coro_impl_t*) current_ready_coros;
	BIO_ARRAY(bio_coro_impl_t*) next_ready_coros;
	int32_t num_coros;
	int32_t num_daemons;

	// Logging
	bio_logger_link_t loggers;
	int log_prefix_len;

	// Thread pool
	bio_worker_thread_t* thread_pool;
	int32_t num_running_async_jobs;

	// Platform specific
	bio_platform_t platform;
} bio_ctx_t;

typedef enum {
	BIO_PLATFORM_UPDATE_NO_WAIT,
	BIO_PLATFORM_UPDATE_WAIT_INDEFINITELY,
	BIO_PLATFORM_UPDATE_WAIT_NOTIFIABLE,
} bio_platform_update_type_t;

extern bio_ctx_t bio_ctx;

static inline void*
bio_realloc(void* ptr, size_t size) {
	return bio_ctx.options.allocator.realloc(ptr, size, bio_ctx.options.allocator.ctx);
}

static inline void*
bio_malloc(size_t size) {
	return bio_realloc(NULL, size);
}

static inline void
bio_free(void* ptr) {
	bio_realloc(ptr, 0);
}

static inline uint32_t
bio_next_pow2(uint32_t v) {
    uint32_t next = v;
    next--;
    next |= next >> 1;
    next |= next >> 2;
    next |= next >> 4;
    next |= next >> 8;
    next |= next >> 16;
    next++;

    return next;
}

static inline int
bio_vfmt(bio_fmt_buf_t* buf, const char* fmt, va_list args) {
	va_list args_copy;
	va_copy(args_copy, args);
	int num_chars = vsnprintf(buf->ptr, (size_t)buf->len, fmt, args_copy);
	va_end(args_copy);

	if (num_chars < 0) { return num_chars; }  // Format error

	if (num_chars >= (int)buf->len) {
		bio_free(buf->ptr);
		buf->ptr = bio_malloc(num_chars + 1);
		buf->len = num_chars + 1;

		vsnprintf(buf->ptr, (size_t)buf->len, fmt, args);
	}

	buf->ptr[num_chars] = '\0';
	return num_chars;
}

BIO_FORMAT_ATTRIBUTE(2, 3)
static inline int
bio_fmt(bio_fmt_buf_t* buf, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int num_chars = bio_vfmt(buf, fmt, args);
	va_end(args);
	return num_chars;
}

/**
 * @addtogroup internal
 * @{
 */

// Platform

/// Initialize the platform layer
void
bio_platform_init(void);

/// Cleanup the platform layer
void
bio_platform_cleanup(void);

/**
 * Check on I/O completion status
 *
 * The event loop will call this function whenever all coroutines have yielded
 * or suspended.
 * This function should use the platform's polling API to check for completion
 * status and raise the relevant I/O wait signals.
 *
 * @param wait_timeout_ms How much time to wait for I/O completion.
 *   This should be followed as closely as possible since bio relies on it for
 *   @ref bio_raise_signal_after "timing".
 *   If this is 0, the function should return as soon as there is no more pending
 *   I/O event.
 *   If this is -1, the function should wait indefinitely until there is at least
 *   one I/O event.
 * @param notifiable Should this wait be notifiable with @ref bio_platform_notify.
 *   When this is `false`, this function was called at a time when there is no
 *   pending async task.
 *   It may choose to execute in a more efficient code path.
 */
void
bio_platform_update(bio_time_t wait_timeout_ms, bool notifiable);

/**
 * Make @ref bio_platform_update return
 *
 * This function will always be called from the async thread pool.
 * It must signal to @ref bio_platform_update in the main thread to return
 * regardless of I/O completion status.
 */
void
bio_platform_notify(void);

/**
 * Return the current time in milliseconds
 *
 * @see bio_current_time_ms
 */
bio_time_t
bio_platform_current_time_ms(void);

/// Called before the async thread pool is created
void
bio_platform_begin_create_thread_pool(void);

/// Called after the async thread pool is created
void
bio_platform_end_create_thread_pool(void);

/**
 * Set the signal to be raised when the OS requests that the program exits
 *
 * When this is not set, the program should follow the default action without
 * any handling.
 * Typically, this means exiting without cleanup.
 *
 * If this is set a second time, replace the old signal without triggering it.
 */
void
bio_platform_set_exit_signal(bio_signal_t signal);

/**
 * Clear the previously set exit signal
 *
 * The program should revert to its previous signal handling behaviour.
 */
void
bio_platform_clear_exit_signal(void);

// File

/// Initialize the File I/O subsystem
void
bio_fs_init(void);

/// Cleanup the File I/O subsystem
void
bio_fs_cleanup(void);

// Net

/// Initialize the Network I/O subsystem
void
bio_net_init(void);

/// Cleanup the Network I/O subsystem
void
bio_net_cleanup(void);

/**@}*/

// Handle table

void
bio_handle_table_init(void);

void
bio_handle_table_cleanup(void);

// Timer

void
bio_timer_init(void);

void
bio_timer_cleanup(void);

void
bio_timer_update(void);

bio_time_t
bio_time_until_next_timer(void);

// Scheduler

void
bio_scheduler_init(void);

void
bio_scheduler_cleanup(void);

// Thread

void
bio_thread_init(void);

void
bio_thread_cleanup(void);

void
bio_thread_update(void);

int32_t
bio_num_running_async_jobs(void);

// Logging

void
bio_logging_init(void);

void
bio_logging_cleanup(void);

#endif
