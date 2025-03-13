#ifndef BIO_MAIN_H
#define BIO_MAIN_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

#define BIO_TAG_INIT(NAME) \
	{ \
		.name = NAME, \
		.file = __FILE__, \
		.line = __LINE__, \
	}

typedef void (*bio_entrypoint_t)(void* userdata);

typedef struct {
	int32_t index;
	int32_t gen;
} bio_handle_t;

typedef struct bio_coro_ref_s {
	bio_handle_t handle;
} bio_coro_ref_t;

typedef struct bio_signal_ref_s {
	bio_handle_t handle;
} bio_signal_ref_t;

typedef struct {
	const char* name;
	const char* file;
	int line;
} bio_tag_t;

typedef struct {
	void* memctx;
	void* (*realloc)(void* ptr, size_t size, void* memctx);
} bio_options_t;

typedef struct {
	const bio_tag_t* tag;
	const char* (*strerror)(int code);
	int code;
} bio_error_t;

extern const bio_tag_t BIO_OS_ERROR;
extern const bio_tag_t BIO_CORE_ERROR;

typedef enum {
	BIO_CORE_ERROR_INVALID_HANDLE,
} bio_core_error_code_t;

typedef enum {
	BIO_CORO_READY,
	BIO_CORO_RUNNING,
	BIO_CORO_WAITING,
	BIO_CORO_DEAD,
} bio_coro_state_t;

void
bio_init(bio_options_t options);

void
bio_loop(void);

void
bio_terminate(void);

bio_coro_ref_t
bio_spawn(bio_entrypoint_t entrypoint, void* userdata);

bio_coro_state_t
bio_coro_state(bio_coro_ref_t coro);

bio_coro_ref_t
bio_current_coro(void);

void
bio_yield(void);

bio_signal_ref_t
bio_make_signal(void);

void
bio_raise_signal(bio_signal_ref_t signal);

bool
bio_check_signal(bio_signal_ref_t signal);

void
bio_wait_for_signals(
	bio_signal_ref_t* signals,
	int num_signals,
	bool wait_all
);

bio_handle_t
bio_make_handle(void* obj, const bio_tag_t* tag);

void*
bio_resolve_handle(bio_handle_t handle, const bio_tag_t* tag);

void*
bio_close_handle(bio_handle_t handle, const bio_tag_t* tag);

static inline int
bio_handle_compare(bio_handle_t lhs, bio_handle_t rhs) {
	if (lhs.index != rhs.index) {
		return lhs.index - rhs.index;
	} else {
		return lhs.gen - rhs.gen;
	}
}

#endif
