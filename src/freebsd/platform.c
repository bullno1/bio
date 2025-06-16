#include "common.h"
#include <sys/event.h>
#include <unistd.h>

void
bio_platform_init(void) {
	bio_ctx.platform.kqueue = kqueuex(KQUEUE_CLOEXEC);
}

void
bio_platform_cleanup(void) {
	close(bio_ctx.platform.kqueue);
}

void
bio_platform_update(bio_time_t wait_timeout_ms, bool notifiable) {
}

void
bio_platform_notify(void);

bio_time_t
bio_platform_current_time_ms(void);

void
bio_platform_begin_create_thread_pool(void);

void
bio_platform_end_create_thread_pool(void);
