#include "../internal.h"
#include <bio/logging/file.h>
#include <bio/mailbox.h>
#include <string.h>

typedef struct {
	bio_file_logger_options_t options;
	bio_fmt_buf_t format_buf;
} bio_file_logger_data_t;

static const char* const BIO_LOG_LEVEL_LABEL[] = {
    "TRACE",
    "DEBUG",
    "INFO ",
    "WARN ",
    "ERROR",
    "FATAL",
    0
};

// Appropriated from https://github.com/rxi/log.c (MIT licensed)
static const char* BIO_LOG_LEVEL_COLOR[] = {
    "[94m", "[36m", "[32m", "[33m", "[31m", "[35m", NULL
};

#define BIO_LOG_TERM_CODE  0x1B
#define BIO_LOG_TERM_RESET "[0m"

static void
bio_log_to_file(void* userdata, const bio_log_ctx_t* ctx, const char* msg) {
	bio_file_logger_data_t* data = userdata;
	if (BIO_LIKELY(ctx != NULL)) {
		const char* coro_name = bio_get_coro_name(ctx->coro);
		int msg_len;
		if (data->options.with_colors) {
			if (coro_name != NULL) {
				msg_len = bio_fmt(
					&data->format_buf,
					"[%c%s%s%c%s][%s:%d]<%s>: %s\n",

					BIO_LOG_TERM_CODE, BIO_LOG_LEVEL_COLOR[ctx->level],
					BIO_LOG_LEVEL_LABEL[ctx->level],
					BIO_LOG_TERM_CODE, BIO_LOG_TERM_RESET,

					ctx->file, ctx->line,
					coro_name,
					msg
				);
			} else {
				msg_len = bio_fmt(
					&data->format_buf,
					"[%c%s%s%c%s][%s:%d]<%d:%d>: %s\n",

					BIO_LOG_TERM_CODE, BIO_LOG_LEVEL_COLOR[ctx->level],
					BIO_LOG_LEVEL_LABEL[ctx->level],
					BIO_LOG_TERM_CODE, BIO_LOG_TERM_RESET,

					ctx->file, ctx->line,
					ctx->coro.handle.index, ctx->coro.handle.gen,
					msg
				);
			}
		} else {
			if (coro_name != NULL) {
				msg_len = bio_fmt(
					&data->format_buf,
					"[%s][%s:%d]<%s>: %s\n",
					BIO_LOG_LEVEL_LABEL[ctx->level],

					ctx->file, ctx->line,
					coro_name,
					msg
				);
			} else {
				msg_len = bio_fmt(
					&data->format_buf,
					"[%s][%s:%d]<%d:%d>: %s\n",
					BIO_LOG_LEVEL_LABEL[ctx->level],

					ctx->file, ctx->line,
					ctx->coro.handle.index, ctx->coro.handle.gen,
					msg
				);
			}
		}

		bio_fwrite_exactly(data->options.file, data->format_buf.ptr, msg_len, NULL);
	} else {
		bio_free(data->format_buf.ptr);
		bio_free(data);
	}
}

bio_logger_t
bio_add_file_logger(
	bio_log_level_t min_level,
	const bio_file_logger_options_t* options
) {
	bio_file_logger_data_t* logger_data = bio_malloc(sizeof(bio_file_logger_data_t));
	*logger_data = (bio_file_logger_data_t){ .options = *options };
	return bio_add_logger(min_level, bio_log_to_file, logger_data);
}
