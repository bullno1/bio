#include "common.h"
#include <bio/bio.h>

void
init_bio(void) {
	bio_init(NULL);
}

void
cleanup_bio(void) {
	bio_terminate();
}

void
bio_test_wrapper(void* userdata) {
	bio_test_wrapper_arg_t* arg = userdata;
	arg->entry();
}
