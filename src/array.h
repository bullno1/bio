#ifndef BIO_ARRAY_H
#define BIO_ARRAY_H

#include <stddef.h>

#define BIO_ARRAY(T) T*

#define bio_array_push(array, element) \
	do { \
		size_t bio_array__new_len; \
		array = bio_array__prepare_push(array, &bio_array__new_len, sizeof(element)); \
		array[bio_array__new_len - 1] = element; \
	} while (0)

#define bio_array_reserve(array, new_capacity) \
	do { \
		array = bio_array__do_reserve(array, new_capacity, sizeof(*array)); \
	} while (0)

#define bio_array_resize(array, new_len) \
	do { \
		array = bio_array__do_resize(array, new_len, sizeof(*array)); \
	} while (0)

#define bio_array_pop(array) (bio_array__do_pop(array), array[bio_array_len(array)])

size_t
bio_array_len(void* array);

size_t
bio_array_capacity(void* array);

void
bio_array_free(void* array);

void
bio_array_clear(void* array);

// Private

void*
bio_array__prepare_push(void* array, size_t* new_len, size_t elem_size);

void*
bio_array__do_reserve(void* array, size_t new_capacity, size_t elem_size);

void*
bio_array__do_resize(void* array, size_t new_len, size_t elem_size) ;

void
bio_array__do_pop(void* array);

#endif
