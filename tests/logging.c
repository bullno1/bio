#include "common.h"
#include <bio/logging/file.h>

static suite_t logging = {
	.name = "logging",
	.init_per_test = init_bio,
	.cleanup_per_test = cleanup_bio,
};

BIO_TEST(logging, logger_uses_daemon) {
	// This must not block bio_loop
	bio_add_file_logger(BIO_LOG_LEVEL_TRACE, &(bio_file_logger_options_t){
		.file = BIO_STDERR,
		.with_colors = true,
	});
}
