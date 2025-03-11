#ifndef BIO_IO_H
#define BIO_IO_H

#include "bio.h"

size_t
bio_write(
	bio_handle_t socket,
	const void* buf,
	size_t size,
	bio_error_t* error
);

size_t
bio_read(
	bio_handle_t socket,
	void* buf,
	size_t size,
	bio_error_t* error
);

#endif
