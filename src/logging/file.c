#include "../internal.h"
#include <bio/logging/file.h>
#include <bio/mailbox.h>
#include <stdio.h>
#include <string.h>

typedef struct {
	char* ptr;
	int len;
} bio_fmt_buf_t;

typedef struct {
	bool terminate;
	bio_fmt_buf_t buf;
} bio_file_log_msg_t;

typedef BIO_MAILBOX(bio_file_log_msg_t) log_mailbox_t;

typedef struct {
	bio_file_t file;
	bool with_colors;

	// Serialize log writes through a queue
	log_mailbox_t log_mailbox;
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
bio_vfmt(bio_fmt_buf_t* buf, const char* fmt, va_list args) {
	va_list args_copy;
	va_copy(args_copy, args);
	int num_chars = vsnprintf(buf->ptr, (size_t)buf->len, fmt, args_copy);
	va_end(args_copy);

	if (num_chars >= (int)buf->len) {
		buf->ptr = bio_realloc(buf->ptr, num_chars + 1);
		buf->len = num_chars + 1;

		vsnprintf(buf->ptr, (size_t)buf->len, fmt, args);
	}
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void
bio_fmt(bio_fmt_buf_t* buf, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	bio_vfmt(buf, fmt, args);
	va_end(args);
}

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
bio_log_to_file(
	void* userdata,
	const bio_log_ctx_t* ctx,
	const char* fmt,
	va_list args
) {
	bio_file_logger_data_t* data = userdata;
	if (ctx == NULL) {
		bio_file_log_msg_t terminate = { .terminate = true };
		while (!bio_send_message(data->log_mailbox, terminate)) {
			bio_yield();
		}

		bio_free(data);
		return;
	}

	bio_fmt_buf_t msg_buf = { 0 };
	bio_fmt_buf_t log_buf = { 0 };
	bio_vfmt(&msg_buf, fmt, args);

	if (data->with_colors) {
		bio_fmt(
			&log_buf,
			"[%c%s%s%c%s][%s:%d]<%d:%d>: %.*s\n",

			BIO_LOG_TERM_CODE, BIO_LOG_LEVEL_COLOR[ctx->level],
			BIO_LOG_LEVEL_LABEL[ctx->level],
			BIO_LOG_TERM_CODE, BIO_LOG_TERM_RESET,

			ctx->file, ctx->line,
			ctx->coro.handle.index, ctx->coro.handle.gen,
			msg_buf.len, msg_buf.ptr
		);
	} else {
		bio_fmt(
			&log_buf,
			"[%s][%s:%d]<%d:%d>: %.*s\n",
			BIO_LOG_LEVEL_LABEL[ctx->level],
			ctx->file, ctx->line,
			ctx->coro.handle.index, ctx->coro.handle.gen,
			msg_buf.len, msg_buf.ptr
		);
	}

	bio_free(msg_buf.ptr);

	// TODO: Give mailbox a waiter queue
	bio_file_log_msg_t msg = { .buf = log_buf };
	while (!bio_send_message(data->log_mailbox, msg)) {
		bio_yield();
	}
}

bio_logger_t
bio_add_file_logger(bio_file_t file, bio_log_level_t level, bool with_colors) {
	bio_file_logger_data_t* data = bio_malloc(sizeof(bio_file_logger_data_t));
	*data = (bio_file_logger_data_t){
		.file = file,
		.with_colors = with_colors,
	};

	// TODO: make configurable
	bio_open_mailbox(&data->log_mailbox, 128);

	bio_log_file_writer_data_t* writer_data = bio_malloc(sizeof(bio_log_file_writer_data_t));
	*writer_data = (bio_log_file_writer_data_t){
		.file = file,
		.log_mailbox = data->log_mailbox,
	};
	bio_spawn(bio_log_file_writer, writer_data);

	return bio_add_logger(level, bio_log_to_file, data);
}
