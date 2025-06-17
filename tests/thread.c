#include "common.h"
#include <bio/bio.h>

static btest_suite_t thread = {
	.name = "thread",
	.init_per_test = init_bio,
	.cleanup_per_test = cleanup_bio,
};

static void
async_task(void* userdata) {
	int* data = userdata;
	*data = 42;
}

BIO_TEST(thread, run_async) {
	int data = 10;
	bio_run_async_and_wait(async_task, &data);
	BTEST_EXPECT(data == 42);
	data = 3;
	bio_run_async_and_wait(async_task, &data);
	BTEST_EXPECT(data == 42);
}
