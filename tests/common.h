#ifndef BIO_TEST_COMMON_H
#define BIO_TEST_COMMON_H

#include <bio/bio.h>
#include <btest.h>
#include <blog.h>

// Compatibility for old test framework
typedef btest_suite_t suite_t;

#define CHECK(condition, msg) BTEST_ASSERT_EX(condition, "%s", msg)

#define TEST(suite, name) BTEST(suite, name)

#define LOG_BIO_ERROR(error) \
	BLOG_ERROR(BIO_ERROR_FMT, BIO_ERROR_FMT_ARGS(&(error))); \

#define CHECK_NO_ERROR(error) \
	do { \
		if (bio_has_error(&(error))) { \
			LOG_BIO_ERROR(error); \
			btest_fail(true); \
		} \
	} while (0)

#define BIO_TEST(SUITE, NAME) \
	static void SUITE##_##NAME##_##entry(void); \
	BTEST_REGISTER(SUITE, NAME, SUITE##_##NAME##_##wrapper) \
	static void SUITE##_##NAME##_##wrapper(void) { \
		bio_spawn(bio_test_wrapper, &(bio_test_wrapper_arg_t){ \
			.entry = SUITE##_##NAME##_##entry, \
		}); \
		bio_loop(); \
	} \
	static void SUITE##_##NAME##_##entry(void)

typedef struct {
	void (*entry)(void);
} bio_test_wrapper_arg_t;

void
init_bio(void);

void
cleanup_bio(void);

void
bio_test_wrapper(void* userdata);

#endif
