#include "../internal.h"
#include <bio/logging/file.h>
#include <bio/mailbox.h>
#include <stdio.h>
#include <string.h>

typedef struct {
	bool terminate;
	bio_fmt_buf_t buf;
} bio_file_log_msg_t;

typedef BIO_MAILBOX(bio_file_log_msg_t) log_mailbox_t;

typedef struct {
	bio_file_t file;
	bool with_colors;
	// Serialize writes through a mailbox
	log_mailbox_t log_mailbox;
	bio_coro_t writer_coro;
	bio_fmt_buf_t msg_buf;
} bio_file_logger_data_t;

typedef struct {
	bio_file_t file;
	log_mailbox_t log_mailbox;
} bio_log_file_writer_data_t;

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
bio_log_file_writer(void* userdata) {
	bio_log_file_writer_data_t* writer_data = userdata;

	while (true) {
		bio_file_log_msg_t msg;
		if (!bio_recv_message(writer_data->log_mailbox, &msg)) { break; }

		if (msg.terminate) { break; }

		// Write in a loop to deal with short writes
		size_t log_size = (size_t)msg.buf.len;
		const char* log_ptr = msg.buf.ptr;
		while (log_size > 0) {
			size_t bytes_written = bio_fwrite(writer_data->file, log_ptr, log_size, NULL);
			if (bytes_written == 0) { break; }

			log_size -= bytes_written;
			log_ptr += bytes_written;
		}

		bio_free(msg.buf.ptr);
	}

	bio_close_mailbox(writer_data->log_mailbox);
	bio_free(writer_data);
}

static void
bio_log_to_file(void* userdata, const bio_log_ctx_t* ctx, const char* msg) {
	bio_file_logger_data_t* data = userdata;
	if (BIO_LIKELY(ctx != NULL)) {
		const char* coro_name = bio_get_coro_name(ctx->coro);
		int msg_len;
		if (data->with_colors) {
			if (coro_name != NULL) {
				msg_len = bio_fmt(
					&data->msg_buf,
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
					&data->msg_buf,
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
					&data->msg_buf,
					"[%s][%s:%d]<%s>: %s\n",
					BIO_LOG_LEVEL_LABEL[ctx->level],

					ctx->file, ctx->line,
					coro_name,
					msg
				);
			} else {
				msg_len = bio_fmt(
					&data->msg_buf,
					"[%s][%s:%d]<%d:%d>: %s\n",
					BIO_LOG_LEVEL_LABEL[ctx->level],

					ctx->file, ctx->line,
					ctx->coro.handle.index, ctx->coro.handle.gen,
					msg
				);
			}
		}

		bio_fmt_buf_t msg_copy = {
			.len = msg_len,
			.ptr = bio_malloc(msg_len),
		};
		memcpy(msg_copy.ptr, data->msg_buf.ptr, msg_len);
		bio_file_log_msg_t msg_to_writer = { .buf = msg_copy };
		while (!bio_send_message(data->log_mailbox, msg_to_writer)) {
			bio_yield();
		}
	} else {
		bio_file_log_msg_t terminate = { .terminate = true };
		while (!bio_send_message(data->log_mailbox, terminate)) {
			bio_yield();
		}
		bio_join(data->writer_coro);

		bio_free(data->msg_buf.ptr);
		bio_free(data);
	}
}

bio_logger_t
bio_add_file_logger(
	bio_log_level_t min_level,
	const bio_file_logger_options_t* options
) {
	bio_file_logger_data_t* logger_data = bio_malloc(sizeof(bio_file_logger_data_t));

	*logger_data = (bio_file_logger_data_t){
		.file = options->file,
		.with_colors = options->with_colors,
	};

	// TODO: make configurable
	bio_open_mailbox(&logger_data->log_mailbox, 128);

	bio_log_file_writer_data_t* writer_data = bio_malloc(sizeof(bio_log_file_writer_data_t));
	*writer_data = (bio_log_file_writer_data_t){
		.file = options->file,
		.log_mailbox = logger_data->log_mailbox,
	};
	logger_data->writer_coro = bio_spawn(bio_log_file_writer, writer_data);

	return bio_add_logger(min_level, bio_log_to_file, logger_data);
}
