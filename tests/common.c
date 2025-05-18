#include "common.h"
#include <bio/bio.h>
#include <stdlib.h>

static void*
stdlib_realloc(void* ptr, size_t size, void* ctx) {
	if (size == 0) {
		free(ptr);
		return NULL;
	} else {
		return realloc(ptr, size);
	}
}

void
init_bio(void) {
	bio_init(&(bio_options_t){
		.allocator.realloc = stdlib_realloc,
	});
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
