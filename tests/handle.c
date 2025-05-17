#include "common.h"
#include <bio/file.h>
#include <string.h>

static suite_t handle = {
	.name = "handle",
	.init_per_test = init_bio,
	.cleanup_per_test = cleanup_bio,
};

TEST(handle, make_and_close) {
	bio_tag_t tag1 = BIO_TAG_INIT("tag1");
	bio_tag_t tag2 = BIO_TAG_INIT("tag2");

	int a, b, c, d, e, f;
	void* ptr_a = &a;
	void* ptr_b = &b;
	void* ptr_c = &c;
	void* ptr_d = &d;
	void* ptr_e = &e;
	void* ptr_f = &f;

	bio_handle_t handle_a = bio_make_handle(ptr_a, &tag1);
	bio_handle_t handle_b = bio_make_handle(ptr_b, &tag2);
	CHECK(handle_a.index != handle_b.index, "Handles have the same index");

	CHECK(bio_resolve_handle(handle_a, &tag1) == ptr_a, "Invalid resolution");
	CHECK(bio_resolve_handle(handle_a, &tag2) == NULL, "Invalid resolution");
	CHECK(bio_resolve_handle(handle_b, &tag1) == NULL, "Invalid resolution");
	CHECK(bio_resolve_handle(handle_b, &tag2) == ptr_b, "Invalid resolution");

	bio_handle_t handle_c = bio_make_handle(ptr_c, &tag2);
	bio_handle_t handle_d = bio_make_handle(ptr_d, &tag2);
	CHECK(bio_resolve_handle(handle_c, &tag2) == ptr_c, "Invalid resolution");
	CHECK(bio_resolve_handle(handle_d, &tag2) == ptr_d, "Invalid resolution");

	bio_close_handle(handle_a, &tag1);
	CHECK(bio_resolve_handle(handle_a, &tag1) == NULL, "Invalid resolution");
	bio_close_handle(handle_b, &tag1);  // Invalid tag
	CHECK(bio_resolve_handle(handle_b, &tag2) == ptr_b, "Invalid resolution");
	bio_close_handle(handle_b, &tag2);
	CHECK(bio_resolve_handle(handle_b, &tag2) == NULL, "Invalid resolution");

	bio_handle_t handle_e = bio_make_handle(ptr_e, &tag1);
	CHECK(handle_e.index == handle_b.index, "Slot not reused");
	CHECK(bio_resolve_handle(handle_e, &tag1) == ptr_e, "Invalid resolution");

	bio_handle_t handle_f = bio_make_handle(ptr_f, &tag2);
	CHECK(handle_f.index == handle_a.index, "Slot not reused");
	CHECK(bio_resolve_handle(handle_f, &tag2) == ptr_f, "Invalid resolution");

	// Resize
	handle_a = bio_make_handle(ptr_a, &tag1);
	handle_b = bio_make_handle(ptr_b, &tag2);

	CHECK(bio_resolve_handle(handle_a, &tag1) == ptr_a, "Invalid resolution");
	CHECK(bio_resolve_handle(handle_b, &tag2) == ptr_b, "Invalid resolution");
	CHECK(bio_resolve_handle(handle_c, &tag2) == ptr_c, "Invalid resolution");
	CHECK(bio_resolve_handle(handle_d, &tag2) == ptr_d, "Invalid resolution");
	CHECK(bio_resolve_handle(handle_e, &tag1) == ptr_e, "Invalid resolution");
	CHECK(bio_resolve_handle(handle_f, &tag2) == ptr_f, "Invalid resolution");
}

TEST(handle, zero_is_invalid) {
	bio_handle_t invalid = { 0 };
	CHECK(bio_handle_info(invalid) == NULL, "Zero handle must be invalid");
}

TEST(handle, info) {
	const bio_tag_t* file_tag = bio_handle_info(BIO_STDIN.handle);
	CHECK(strcmp(file_tag->name, "bio.handle.file") == 0, "Invalid tag");
}
