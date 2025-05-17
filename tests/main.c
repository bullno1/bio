#include <blog.h>
#include <btest.h>
#include <string.h>

int
main(int argc, const char* argv[]) {
	const char* suite_filter = NULL;
	const char* test_filter = NULL;
	if (argc > 1) {
		suite_filter = argv[1];
		if (argc > 2) {
			test_filter = argv[2];
		}
	}

	blog_init(&(blog_options_t){
		.current_filename = __FILE__,
		.current_depth_in_project = 1,
	});
	blog_add_file_logger(BLOG_LEVEL_DEBUG, &(blog_file_logger_options_t){
		.file = stderr,
		.with_colors = true,
	});

	int num_tests = 0;
	int num_failed = 0;

	BTEST_FOREACH(test) {
		if (suite_filter && strcmp(suite_filter, test->suite->name) != 0) {
			continue;
		}

		if (test_filter && strcmp(test_filter, test->name) != 0) {
			continue;
		}

		++num_tests;

		BLOG_INFO("---- %s/%s: Running ----", test->suite->name, test->name);
		if (btest_run(test)) {
			BLOG_INFO("---- %s/%s: Passed  ----", test->suite->name, test->name);
		} else {
			BLOG_ERROR("---- %s/%s: Failed  ----", test->suite->name, test->name);
			++num_failed;
		}
	}

	BLOG_INFO("%d/%d tests passed", num_tests - num_failed, num_tests);
	return num_failed;
}

#define BLIB_IMPLEMENTATION
#include <blog.h>
#include <btest.h>
