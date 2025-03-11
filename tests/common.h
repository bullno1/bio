#ifndef BIO_TEST_COMMON_H
#define BIO_TEST_COMMON_H

#include <bio/bio.h>
#include <autolist.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
	const char* name;
	void (*init)(void);
	void (*cleanup)(void);
} suite_t;

typedef struct {
	suite_t* suite;
	const char* name;
	void (*run)(void);
} test_t;

#define TEST(SUITE, NAME) \
	static void SUITE##_##NAME(void); \
	AUTOLIST_ENTRY(bio_test, test_t, test_##SUITE##_##NAME) = { \
		.suite = &SUITE, \
		.name = #NAME, \
		.run = SUITE##_##NAME, \
	}; \
	static void SUITE##_##NAME(void)

#define CHECK(condition, msg) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, msg, #condition); \
			abort(); \
		} \
	} while (0)

void
init_bio(void);

void
cleanup_bio(void);

#endif
