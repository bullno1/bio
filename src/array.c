#include "internal.h"
#include "array.h"

typedef struct {
	size_t capacity;
	size_t len;
	_Alignas(BIO_ALIGN_TYPE) char elems[];
} bio_array_header_t;

static inline bio_array_header_t*
bio_array__header_of(void* array) {
	if (array != NULL) {
		return (bio_array_header_t*)((char*)array - offsetof(bio_array_header_t, elems));
	} else {
		return NULL;
	}
}

size_t
bio_array_len(void* array) {
	bio_array_header_t* header = bio_array__header_of(array);
	return header != NULL ? header->len : 0;
}

size_t
bio_array_capacity(void* array) {
	bio_array_header_t* header = bio_array__header_of(array);
	return header != NULL ? header->capacity : 0;
}

void
bio_array_free(void* array) {
	bio_array_header_t* header = bio_array__header_of(array);
	if (header != NULL) {
		bio_free(header);
	}
}

void
bio_array_clear(void* array) {
	bio_array_header_t* header = bio_array__header_of(array);
	if (header != NULL) {
		header->len = 0;
	}
}

void*
bio_array__prepare_push(void* array, size_t* new_len, size_t elem_size) {
	bio_array_header_t* header = bio_array__header_of(array);
	size_t len = header != NULL ? header->len : 0;
	size_t capacity = header != NULL ? header->capacity : 0;

	if (len < capacity) {
		header->len = *new_len = len + 1;
		return array;
	} else {
		size_t new_capacity = capacity > 0 ? capacity * 2 : 2;
		bio_array_header_t* new_header = bio_realloc(
			header, sizeof(bio_array_header_t) + elem_size * new_capacity
		);
		new_header->capacity = new_capacity;
		new_header->len = *new_len = len + 1;
		return new_header->elems;
	}
}

void*
bio_array__do_reserve(void* array, size_t new_capacity, size_t elem_size) {
	bio_array_header_t* header = bio_array__header_of(array);
	size_t current_capacity = header != NULL ? header->capacity : 0;
	if (new_capacity <= current_capacity) {
		return array;
	}

	bio_array_header_t* new_header = bio_realloc(
		header, sizeof(bio_array_header_t) + elem_size * new_capacity
	);
	new_header->capacity = new_capacity;
	return new_header->elems;
}

void*
bio_array__do_resize(void* array, size_t new_len, size_t elem_size) {
	bio_array_header_t* header = bio_array__header_of(array);
	size_t current_capacity = header != NULL ? header->capacity : 0;

	if (new_len <= current_capacity) {
		header->len = new_len;
		return array;
	} else {
		bio_array_header_t* new_header = bio_realloc(
			header, sizeof(bio_array_header_t) + elem_size * new_len
		);
		new_header->capacity = new_len;
		new_header->len = new_len;
		return new_header->elems;
	}
}

void
bio_array__do_pop(void* array) {
	bio_array_header_t* header = bio_array__header_of(array);
	header->len -= 1;
}
