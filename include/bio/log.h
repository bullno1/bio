#ifndef BIO_LOG_H
#define BIO_LOG_H

#include "bio.h"
#include <stdarg.h>

#define BIO_LOG(LEVEL, ...) \
	bio_log(LEVEL, bio_current_coro(), __FILE__, __LINE__, __VA_ARGS__)

#define BIO_TRACE(...) BIO_LOG(BIO_LOG_LEVEL_TRACE, __VA_ARGS__)
#define BIO_DEBUG(...) BIO_LOG(BIO_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define BIO_INFO(...)  BIO_LOG(BIO_LOG_LEVEL_INFO , __VA_ARGS__)
#define BIO_WARN(...)  BIO_LOG(BIO_LOG_LEVEL_WARN , __VA_ARGS__)
#define BIO_ERROR(...) BIO_LOG(BIO_LOG_LEVEL_ERROR, __VA_ARGS__)
#define BIO_FATAL(...) BIO_LOG(BIO_LOG_LEVEL_FATAL, __VA_ARGS__)

#define BIO_ERROR_FMT "%s (%s[%d])"
#define BIO_ERROR_FMT_ARGS(error) \
	(bio_has_error((error)) ? bio_strerror((error)) : "No error"), \
	(bio_has_error((error)) ? (error)->tag->name : "bio.error.core"), \
	(error)->code

typedef enum {
    BIO_LOG_LEVEL_TRACE,
    BIO_LOG_LEVEL_DEBUG,
    BIO_LOG_LEVEL_INFO,
    BIO_LOG_LEVEL_WARN,
    BIO_LOG_LEVEL_ERROR,
    BIO_LOG_LEVEL_FATAL,
} bio_log_level_t;

typedef struct {
	bio_handle_t handle;
} bio_logger_t;

typedef struct {
	bio_log_level_t level;
	bio_coro_t coro;
	const char* file;
	int line;
	const char* fmt;
	va_list args;
} bio_log_record_t;

typedef void (*bio_log_fn_t)(const bio_log_record_t* record, void* userdata);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 5, 6)))
#endif
void
bio_log(
	bio_log_level_t level,
	bio_coro_t coro,
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

#endif
