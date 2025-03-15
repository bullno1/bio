#ifndef BIO_INTERNAL_H
#define BIO_INTERNAL_H

#ifdef __linux__
#	include "linux/platform.h"
#else
#	error "Unsupported platform"
#endif

#include <bio/bio.h>

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
		(item)->next->prev = (item)->prev; \
		(item)->prev->next = (item)->next; \
	} while (0)

#define BIO_LIST_IS_EMPTY(list) \
	((list)->next == (list))

#define BIO_LIST_INIT(list) \
	do { \
		(list)->next = (list); \
		(list)->prev = (list); \
	} while (0)

#define BIO_LIST_POP(list) \
	BIO_LIST_REMOVE((list)->next)

#define BIO_CONTAINER_OF(ptr, type, member) \
	((type *)((char *)(1 ? (ptr) : &((type *)0)->member) - offsetof(type, member)))

#define BIO_INVALID_HANDLE ((bio_handle_t){ .index = -1 })

typedef struct {
	const bio_tag_t* tag;
	void* obj;

	int32_t gen;
	int32_t next_handle_slot;
} bio_handle_slot_t;

typedef struct mco_coro mco_coro;

BIO_DEFINE_LIST_LINK(bio_signal_link);
BIO_DEFINE_LIST_LINK(bio_coro_link);
BIO_DEFINE_LIST_LINK(bio_logger_link);

typedef struct bio_coro_impl_s bio_coro_impl_t;

typedef struct {
	bio_signal_link_t link;

	bio_coro_impl_t* owner;
	bio_handle_t handle;

	int wait_counter;
} bio_signal_impl_t;

struct bio_coro_impl_s {
	bio_coro_link_t link;

	mco_coro* impl;
	bio_entrypoint_t entrypoint;
	void* userdata;

	bio_handle_t handle;
	bio_coro_state_t state;

	bio_signal_link_t pending_signals;

	int num_blocking_signals;
	int wait_counter;
};

typedef struct bio_worker_thread_s bio_worker_thread_t;

typedef struct {
	bio_options_t options;

	// Handle
	bio_handle_slot_t* handle_slots;
	int32_t handle_capacity;
	int32_t next_handle_slot;
	int32_t num_handles;

	// Coro
	bio_coro_link_t ready_coros_a;
	bio_coro_link_t ready_coros_b;
	bio_coro_link_t* current_ready_coros;
	bio_coro_link_t* next_ready_coros;
	int32_t num_coros;

	// Logging
	bio_logger_link_t loggers;

	// Thread pool
	bio_worker_thread_t* thread_pool;

	// Platform specific
	bio_platform_t platform;
} bio_ctx_t;

typedef enum {
	BIO_PLATFORM_UPDATE_NO_WAIT,
	BIO_PLATFORM_UPDATE_WAIT_INDEFINITELY,
	BIO_PLATFORM_UPDATE_WAIT_NOTIFIABLE,
} bio_platform_update_type_t;

extern bio_ctx_t bio_ctx;

extern const bio_tag_t BIO_PLATFORM_ERROR;

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

void
bio_logging_init(void);

void
bio_logging_cleanup(void);

void
bio_platform_init(void);

void
bio_platform_cleanup(void);

void
bio_platform_update(bio_platform_update_type_t type);

void
bio_platform_notify(void);

void
bio_fs_init(void);

void
bio_fs_cleanup(void);

void
bio_thread_init(void);

void
bio_thread_cleanup(void);

int
bio_thread_update(void);

#endif
