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
bio_fdopen(bio_file_t* file_ptr, uintptr_t fd, bio_error_t* error);

uintptr_t
bio_funwrap(bio_file_t file);

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

static inline size_t
bio_fwrite_exactly(bio_file_t file, const void* buf, size_t size, bio_error_t* error) {
	size_t total_bytes_written = 0;
	while (total_bytes_written < size) {
		size_t bytes_written = bio_fwrite(
			file,
			(char*)buf + total_bytes_written,
			size - total_bytes_written,
			error
		);
		if (bytes_written == 0) { break; }
		total_bytes_written += bytes_written;
	}

	return total_bytes_written;
}

static inline size_t
bio_fread_exactly(bio_file_t file, void* buf, size_t size, bio_error_t* error) {
	size_t total_bytes_read = 0;
	while (total_bytes_read < size) {
		size_t bytes_read = bio_fread(
			file,
			(char*)buf + total_bytes_read,
			size - total_bytes_read,
			error
		);
		if (bytes_read == 0) { break; }
		total_bytes_read += bytes_read;
	}

	return total_bytes_read;
}

#endif
