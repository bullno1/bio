#include "internal.h"

static const bio_tag_t BIO_LOGGER_HANDLE = BIO_TAG_INIT("bio.handle.logger");

typedef struct {
	bio_logger_link_t link;
	bio_handle_t handle;

	bio_log_level_t min_level;
	bio_log_fn_t log_fn;
	void* userdata;
} bio_logger_impl_t;

typedef struct {
	FILE* file;
	char* buf;
	int buf_len;
} bio_file_logger_data_t;

void
bio_logging_init(void) {
	BIO_LIST_INIT(&bio_ctx.loggers);
}

void
bio_logging_cleanup(void) {
	while (!BIO_LIST_IS_EMPTY(&bio_ctx.loggers)) {
		bio_logger_impl_t* logger = BIO_CONTAINER_OF(bio_ctx.loggers.next, bio_logger_impl_t, link);
		bio_remove_logger((bio_logger_t){ logger->handle });
	}
}

bio_logger_t
bio_add_logger(bio_log_level_t min_level, bio_log_fn_t log_fn, void* userdata) {
	bio_logger_impl_t* impl = bio_malloc(sizeof(bio_logger_impl_t));
	*impl = (bio_logger_impl_t){
		.min_level = min_level,
		.log_fn = log_fn,
		.userdata = userdata,
	};

	impl->handle = bio_make_handle(impl, &BIO_LOGGER_HANDLE);
	BIO_LIST_APPEND(&bio_ctx.loggers, &impl->link);

	return (bio_logger_t) { .handle = impl->handle };
}

static void
bio_finalize_logger(bio_logger_impl_t* logger, ...) {
	va_list args;
	va_start(args, logger);
	logger->log_fn(logger->userdata, NULL, NULL, args);
	va_end(args);
}

void
bio_remove_logger(bio_logger_t logger) {
	bio_logger_impl_t* impl = bio_close_handle(logger.handle, &BIO_LOGGER_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		bio_finalize_logger(impl);
		BIO_LIST_REMOVE(&impl->link);
		bio_free(impl);
	}
}

void
bio_set_min_log_level(bio_logger_t logger, bio_log_level_t level) {
	bio_logger_impl_t* impl = bio_resolve_handle(logger.handle, &BIO_LOGGER_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		impl->min_level = level;
	}
}

void
bio_log(
	bio_log_level_t level,
	const char* file,
	int line,
	const char* fmt,
	...
) {
	bio_log_ctx_t ctx = {
		.coro = bio_current_coro(),
		.level = level,
		.line = line,
		.file = file,
	};

	va_list args;
	va_start(args, fmt);
	for (
		bio_logger_link_t* itr = bio_ctx.loggers.next;
		itr != &bio_ctx.loggers;
	) {
		bio_logger_link_t* next = itr->next;
		bio_logger_impl_t* logger = BIO_CONTAINER_OF(itr, bio_logger_impl_t, link);

		if (level >= logger->min_level) {
			va_list args_copy;
			va_copy(args_copy, args);
			logger->log_fn(logger->userdata, &ctx, fmt, args);
			va_end(args_copy);
		}

		itr = next;
	}
	va_end(args);
}

void
bio_log_to_file(
	void* userdata,
	const bio_log_ctx_t* ctx,
	const char* fmt,
	va_list args
) {
	bio_file_logger_data_t* data = userdata;
	if (ctx == NULL) {
		bio_free(data->buf);
		bio_free(data);
		return;
	}

	va_list args_copy;
	va_copy(args_copy, args);
	int num_chars = vsnprintf(data->buf, (size_t)data->buf_len, fmt, args_copy);
	va_end(args_copy);
	if (num_chars >= (int)data->buf_len) {
		data->buf = bio_realloc(data->buf, num_chars + 1);
		data->buf_len = num_chars + 1;

		vsnprintf(data->buf, (size_t)data->buf_len, fmt, args);
	}

	fprintf(
		data->file,
		"[%s:%d](%d:%d): %.*s\n",
		ctx->file, ctx->line,
		ctx->coro.handle.index, ctx->coro.handle.gen,
		num_chars, data->buf
	);
}

bio_logger_t
bio_add_file_logger(FILE* file, bio_log_level_t level, bool with_colors) {
	bio_file_logger_data_t* data = bio_malloc(sizeof(bio_logger_impl_t));
	*data = (bio_file_logger_data_t){
		.file = file,
	};
	return bio_add_logger(level, bio_log_to_file, data);
}
