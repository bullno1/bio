#include "internal.h"
#include <stdlib.h>
#include <minicoro.h>
#include <string.h>

bio_ctx_t bio_ctx = { 0 };

const bio_tag_t BIO_CORE_ERROR = BIO_TAG_INIT("bio.error.core");

static void*
bio_stdlib_realloc(void* ptr, size_t size, void* ctx) {
	(void)ctx;
	if (size == 0) {
		free(ptr);
		return NULL;
	} else {
		return realloc(ptr, size);
	}
}

static const char*
bio_core_strerror(int code) {
	switch ((bio_core_error_code_t)code) {
		case BIO_NO_ERROR:
			return "No error";
		case BIO_ERROR_INVALID_ARGUMENT:
			return "Invalid argument";
		case BIO_ERROR_NOT_SUPPORTED:
			return "Operation is not supported";
	}

	return "Unknown error";
}

static void
bio_dispatch_exit_signal(bio_exit_reason_t reason) {
	size_t num_handlers = bio_array_len(bio_ctx.exit_handlers);
	for (size_t i = 0; i < num_handlers; ++i) {
		bio_ctx.exit_handlers[i]->reason = reason;
		bio_raise_signal(bio_ctx.exit_handlers[i]->signal);
	}
	bio_array_clear(bio_ctx.exit_handlers);

	if (num_handlers > 0) {
		bio_platform_unblock_exit_signal();
	}
}

void
bio_init(const bio_options_t* options) {
	if (options == NULL) {
		options = &(bio_options_t){ 0 };
	}
	memset(&bio_ctx, 0, sizeof(bio_ctx));

	bio_ctx.options = *options;
	if (bio_ctx.options.allocator.realloc == NULL) {
		bio_ctx.options.allocator.realloc = bio_stdlib_realloc;
	}

	bio_ctx.is_terminating = false;

	bio_platform_init();
	bio_handle_table_init();
	bio_timer_init();
	bio_scheduler_init();
	bio_thread_init();
	bio_fs_init();
	bio_net_init();
	bio_logging_init();
}

void
bio_terminate(void) {
	bio_ctx.is_terminating = true;

	bio_dispatch_exit_signal(BIO_EXIT_TERMINATE);

	// Logging uses daemon coroutines
	bio_logging_cleanup();
	if (bio_ctx.num_daemons > 0) { bio_loop(); }

	bio_net_cleanup();
	bio_fs_cleanup();
	bio_thread_cleanup();
	bio_scheduler_cleanup();
	bio_timer_cleanup();
	bio_handle_table_cleanup();
	bio_platform_cleanup();
	bio_array_free(bio_ctx.exit_handlers);
}

bool
bio_is_terminating(void) {
	return bio_ctx.is_terminating;
}

bio_time_t
bio_current_time_ms(void) {
	return bio_platform_current_time_ms();
}

bio_exit_reason_t
bio_wait_for_exit(void) {
	mco_coro* impl = mco_running();
	if (BIO_LIKELY(impl != NULL && !bio_ctx.is_terminating)) {
		if (bio_array_len(bio_ctx.exit_handlers) == 0) {
			bio_platform_block_exit_signal();
		}

		// Set new handler
		bio_signal_t signal = bio_make_signal();
		bio_exit_info_t exit_info = { .signal = signal };
		bio_array_push(bio_ctx.exit_handlers, &exit_info);

		// Daemonize
		bio_coro_impl_t* coro = impl->user_data;
		if (!coro->daemon) {
			coro->daemon = true;
			++bio_ctx.num_daemons;
		}

		// Wait
		bio_wait_for_one_signal(signal);
		return exit_info.reason;
	} else {
		return BIO_EXIT_TERMINATE;
	}
}

void
bio_handle_exit_signal(void) {
	bio_dispatch_exit_signal(BIO_EXIT_OS_REQUEST);
}

void
(bio_set_core_error)(bio_error_t* error, bio_core_error_code_t code, const char* file, int line) {
	if (error != NULL) {
		error->tag = &BIO_CORE_ERROR;
		error->code = (int)code;
		error->strerror = bio_core_strerror;
		error->file = file;
		error->line = line;
	}
}
