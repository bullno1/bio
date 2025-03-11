#include "common.h"
#include <stdio.h>

AUTOLIST_DECLARE(bio_test)

int
main(int argc, const char* argv[]) {
	AUTOLIST_FOREACH(itr, bio_test) {
		const autolist_entry_t* entry = *itr;

		const test_t* test = entry->value_addr;
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
