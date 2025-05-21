#ifndef BIO_FILE_H
#define BIO_FILE_H

#include "bio.h"

typedef struct {
	bio_handle_t handle;
} bio_file_t;

extern bio_file_t BIO_STDIN;
extern bio_file_t BIO_STDOUT;
extern bio_file_t BIO_STDERR;

typedef struct {
	uint64_t size;
} bio_stat_t;

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

bool
bio_fseek(
	bio_file_t file,
	int64_t offset,
	int origin,
	bio_error_t* error
);

int64_t
bio_ftell(
	bio_file_t file,
	bio_error_t* error
);

bool
bio_fflush(bio_file_t file, bio_error_t* error);

bool
bio_fclose(bio_file_t file, bio_error_t* error);

bool
bio_fstat(bio_file_t file, bio_stat_t* stat, bio_error_t* error);

#endif
