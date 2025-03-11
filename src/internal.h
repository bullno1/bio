#ifndef BIO_INTERNAL_H
#define BIO_INTERNAL_H

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

typedef struct {
	bio_tag_t* tag;
	void* obj;

	int32_t gen;
	int32_t next_handle_slot;
} bio_handle_slot_t;

typedef struct mco_coro mco_coro;

BIO_DEFINE_LIST_LINK(bio_signal_link);
BIO_DEFINE_LIST_LINK(bio_coro_link);

typedef struct bio_coro_s bio_coro_t;

typedef struct {
	bio_signal_link_t link;

	bio_coro_t* owner;
	int wait_counter;
	bool raised;
} bio_signal_t;

struct bio_coro_s {
	bio_coro_link_t link;

	mco_coro* impl;

	bio_signal_link_t pending_signals;
	bio_signal_link_t raised_signals;

	bio_signal_ref_t* waited_signals;
	int num_waited_signals;
	int num_blocking_signals;
	int wait_counter;
};

typedef struct {
	bio_options_t options;

	// Handle
	bio_handle_slot_t* handle_slots;
	int32_t handle_capacity;
	int32_t next_handle_slot;
	int32_t num_handles;

	// Coro
	bio_coro_link_t ready_coros;
	bio_coro_link_t waiting_coros;
} bio_ctx_t;

extern bio_ctx_t bio_ctx;

extern const bio_tag_t BIO_CORO_HANDLE;
extern const bio_tag_t BIO_SIGNALHANDLE;

static inline void*
bio_realloc(void* ptr, size_t size) {
	return bio_ctx.options.realloc(ptr, size, bio_ctx.options.memctx);
}

static inline void*
bio_malloc(size_t size) {
	return bio_realloc(NULL, size);
}

static inline void
bio_free(void* ptr) {
	bio_realloc(ptr, 0);
}

#endif
