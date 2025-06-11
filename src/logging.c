#include "internal.h"
#include <string.h>
#include <stdio.h>

static const bio_tag_t BIO_LOGGER_HANDLE = BIO_TAG_INIT("bio.handle.logger");

typedef struct {
	bio_logger_link_t link;
	bio_handle_t handle;

	bio_log_level_t min_level;
	bio_log_fn_t log_fn;
	void* userdata;
} bio_logger_impl_t;

void
bio_logging_init(void) {
	BIO_LIST_INIT(&bio_ctx.loggers);
	bio_log_options_t options = bio_ctx.options.log_options;
	const char* current_filename = options.current_filename;

	int depth = options.current_depth_in_project + 1;
	if (options.current_filename != NULL) {
		int len = (int)strlen(current_filename);
		int i = len - 1;
		for (; i >= 0; --i) {
			char ch = current_filename[i];
			if (ch == '/' || ch == '\\') {
				--depth;
			}

			if (depth == 0) { break; }
		}

		if (current_filename[i] == '/' || current_filename[i] == '\\') {
			i += 1;
		}

		if (depth == 0 && i >= 0) {
			bio_ctx.log_prefix_len = i;
		}
	}
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
bio_finalize_logger(bio_logger_impl_t* logger) {
	logger->log_fn(logger->userdata, NULL, NULL);
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

typedef struct {
	bio_fmt_buf_t log_msg_buf;
} bio_logging_cls_t;

static void
bio_init_logging_cls(void* data) {
	bio_logging_cls_t* cls = data;
	*cls = (bio_logging_cls_t) { 0 };
}

static void
bio_cleanup_logging_cls(void* data) {
	bio_logging_cls_t* cls = data;
	bio_free(cls->log_msg_buf.ptr);
}

static const bio_cls_t bio_logging_cls = {
	.size = sizeof(bio_logging_cls_t),
	.init = bio_init_logging_cls,
	.cleanup = bio_cleanup_logging_cls,
};

void
bio_log(
	bio_log_level_t level,
	const char* filename,
	int line,
	const char* fmt,
	...
) {
	filename = filename != NULL ? filename : "<unknown>";
	int filename_len = (int)strlen(filename);

	// Shorten filename by common prefix
	if (
		bio_ctx.log_prefix_len > 0
		&& filename_len >= bio_ctx.log_prefix_len
		&& memcmp(filename, bio_ctx.options.log_options.current_filename, bio_ctx.log_prefix_len) == 0
	) {
		filename += bio_ctx.log_prefix_len;
	}

	bio_log_ctx_t ctx = {
		.coro = bio_current_coro(),
		.level = level,
		.line = line,
		.file = filename,
	};

	const char* msg = NULL;
	for (
		bio_logger_link_t* itr = bio_ctx.loggers.next;
		itr != &bio_ctx.loggers;
	) {
		bio_logger_link_t* next = itr->next;
		bio_logger_impl_t* logger = BIO_CONTAINER_OF(itr, bio_logger_impl_t, link);

		if (level >= logger->min_level) {
			// Delay formatting until it's actually needed
			if (msg == NULL) {
				bio_logging_cls_t* cls = bio_get_cls(&bio_logging_cls);
				va_list args;
				va_start(args, fmt);
				int msg_len = bio_vfmt(&cls->log_msg_buf, fmt, args);
				va_end(args);
				if (msg_len < 0) { return; }
				msg = cls->log_msg_buf.ptr;
			}

			logger->log_fn(logger->userdata, &ctx, msg);
		}

		itr = next;
	}
}
