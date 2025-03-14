#ifndef BIO_FILE_H
#define BIO_FILE_H

#include "bio.h"

typedef struct {
	bio_handle_t handle;
} bio_file_t;

extern const bio_file_t BIO_STDIN;
extern const bio_file_t BIO_STDOUT;
extern const bio_file_t BIO_STDERR;

bool
bio_fopen(
	bio_file_t* file_ptr,
	const char* restrict filename,
	const char* restrict mode,
	bio_error_t* error
);

size_t
bio_fwrite(
	bio_file_t file,
	const void* buf,
	size_t size,
	bio_error_t* error
);

size_t
bio_fread(
	bio_file_t file,
	void* buf,
	size_t size,
	bio_error_t* error
);

void
bio_fclose(bio_file_t file);

#endif
