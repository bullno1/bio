#include "internal.h"
#include <stdlib.h>

bio_ctx_t bio_ctx = { 0 };

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

void
bio_init(const bio_options_t* options) {
	if (options == NULL) {
		options = &(bio_options_t){ 0 };
	}

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
}

bool
bio_is_terminating(void) {
	return bio_ctx.is_terminating;
}

bio_time_t
bio_current_time_ms(void) {
	return bio_platform_current_time_ms();
}
