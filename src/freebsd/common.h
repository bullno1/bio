#ifndef BIO_FREEBSD_COMMON_H
#define BIO_FREEBSD_COMMON_H

#include "../internal.h"
#include "platform.h"

void
bio_set_errno(bio_error_t* error, int code, const char* file, int line);

#define bio_set_errno(error, code) bio_set_errno(error, code, __FILE__, __LINE__)

#define BIO_CHECK_VARIABLE(VAR) (sizeof(&VAR)) // This will fail if VAR is a function call

#endif
