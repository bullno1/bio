#ifndef BIO_MAIN_H
#define BIO_MAIN_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

#define BIO_LOG(LEVEL, ...) \
	bio_log(LEVEL, __FILE__, __LINE__, __VA_ARGS__)

#define BIO_TRACE(...) BIO_LOG(BIO_LOG_LEVEL_TRACE, __VA_ARGS__)
#define BIO_DEBUG(...) BIO_LOG(BIO_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define BIO_INFO(...)  BIO_LOG(BIO_LOG_LEVEL_INFO , __VA_ARGS__)
#define BIO_WARN(...)  BIO_LOG(BIO_LOG_LEVEL_WARN , __VA_ARGS__)
#define BIO_ERROR(...) BIO_LOG(BIO_LOG_LEVEL_ERROR, __VA_ARGS__)
#define BIO_FATAL(...) BIO_LOG(BIO_LOG_LEVEL_FATAL, __VA_ARGS__)

#define BIO_ERROR_FMT "%s (%s[%d]) (from %s:%d)"
#define BIO_ERROR_FMT_ARGS(error) \
	(bio_has_error((error)) ? bio_strerror((error)) : "No error"), \
	(bio_has_error((error)) ? (error)->tag->name : "bio.error.core"), \
	((error)->code), \
	((error)->file ? (error)->file : "<no source info>"), \
	((error)->line)

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

typedef struct {
	bio_handle_t handle;
} bio_coro_t;

typedef struct {
	bio_handle_t handle;
} bio_signal_t;

typedef struct {
	bio_handle_t handle;
} bio_monitor_t;

typedef struct {
	bio_handle_t handle;
} bio_logger_t;

typedef struct {
	const char* name;
	const char* file;
	int line;
} bio_tag_t;

typedef int64_t bio_time_t;

typedef struct {
	struct {
		unsigned int queue_size;
	} io_uring;
} bio_linux_options_t;

typedef struct {
	void* ctx;
	void* (*realloc)(void* ptr, size_t size, void* ctx);
} bio_allocator_t;

typedef struct {
	bio_allocator_t allocator;

	struct {
		int num_threads;
		int queue_size;
	} thread_pool;

	bio_linux_options_t linux;
} bio_options_t;

typedef struct {
	const bio_tag_t* tag;
	const char* (*strerror)(int code);
	int code;
	const char* file;
	int line;
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

typedef enum {
    BIO_LOG_LEVEL_TRACE,
    BIO_LOG_LEVEL_DEBUG,
    BIO_LOG_LEVEL_INFO,
    BIO_LOG_LEVEL_WARN,
    BIO_LOG_LEVEL_ERROR,
    BIO_LOG_LEVEL_FATAL,
} bio_log_level_t;

typedef struct {
	bio_coro_t coro;
	bio_log_level_t level;
	int line;
	const char* file;
} bio_log_ctx_t;

typedef void (*bio_log_fn_t)(
	void* userdata,
	const bio_log_ctx_t* ctx,
	const char* fmt,
	va_list args
);

void
bio_init(const bio_options_t* options);

void
bio_loop(void);

void
bio_terminate(void);

bio_coro_t
bio_spawn(bio_entrypoint_t entrypoint, void* userdata);

bio_coro_state_t
bio_coro_state(bio_coro_t coro);

bio_coro_t
bio_current_coro(void);

void
bio_yield(void);

bio_signal_t
bio_make_signal(void);

bool
bio_raise_signal(bio_signal_t signal);

void
bio_raise_signal_after(bio_signal_t signal, bio_time_t time_ms);

bio_monitor_t
bio_monitor(bio_coro_t coro, bio_signal_t signal);

void
bio_unmonitor(bio_monitor_t monitor);

bio_time_t
bio_current_time_ms(void);

bool
bio_check_signal(bio_signal_t signal);

void
bio_wait_for_signals(
	bio_signal_t* signals,
	int num_signals,
	bool wait_all
);

static inline void
bio_join(bio_coro_t coro) {
	bio_signal_t term_signal = bio_make_signal();
	bio_monitor(coro, term_signal);
	bio_wait_for_signals(&term_signal, 1, true);
}

bio_handle_t
bio_make_handle(void* obj, const bio_tag_t* tag);

void*
bio_resolve_handle(bio_handle_t handle, const bio_tag_t* tag);

/**
 * This should only be used for debugging
 */
const bio_tag_t*
bio_handle_info(bio_handle_t handle);

void*
bio_close_handle(bio_handle_t handle, const bio_tag_t* tag);

void
bio_run_async(bio_entrypoint_t task, void* userdata, bio_signal_t signal);

static inline void
bio_run_async_and_wait(bio_entrypoint_t task, void* userdata) {
	bio_signal_t signal = bio_make_signal();
	bio_run_async(task, userdata, signal);
	bio_wait_for_signals(&signal, 1, true);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 4, 5)))
#endif
void
bio_log(
	bio_log_level_t level,
	const char* file,
	int line,
	const char* fmt,
	...
);

bio_logger_t
bio_add_logger(bio_log_level_t min_level, bio_log_fn_t log_fn, void* userdata);

void
bio_remove_logger(bio_logger_t logger);

void
bio_set_min_log_level(bio_logger_t logger, bio_log_level_t level);

static inline int
bio_handle_compare(bio_handle_t lhs, bio_handle_t rhs) {
	if (lhs.index != rhs.index) {
		return lhs.index - rhs.index;
	} else {
		return lhs.gen - rhs.gen;
	}
}

static inline bool
bio_has_error(bio_error_t* error) {
	return error != NULL && error->tag != NULL;
}

static inline const char*
bio_strerror(bio_error_t* error) {
	if (error != NULL && error->tag != NULL) {
		return error->strerror(error->code);
	} else {
		return NULL;
	}
}

static inline void
bio_clear_error(bio_error_t* error) {
	if (error != NULL) { error->tag = NULL; }
}

#endif
