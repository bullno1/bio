#include "common.h"
#include <stdio.h>
#include <string.h>

AUTOLIST_DECLARE(bio_test)

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

	AUTOLIST_FOREACH(itr, bio_test) {
		const autolist_entry_t* entry = *itr;
		const test_t* test = entry->value_addr;

		if (suite_filter && strcmp(suite_filter, test->suite->name) != 0) {
			continue;
		}

		if (test_filter && strcmp(test_filter, test->name) != 0) {
			continue;
		}

		printf("--- %s/%s ---\n", test->suite->name, test->name);
		if (test->suite->init != NULL) {
			test->suite->init();
		}
		test->run();
		if (test->suite->cleanup != NULL) {
			test->suite->cleanup();
		}
	}

	return 0;
}
