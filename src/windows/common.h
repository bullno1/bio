#ifndef BIO_WINDOWS_COMMON_H
#define BIO_WINDOWS_COMMON_H

#include "platform.h"
#include "../internal.h"

typedef struct {	
	OVERLAPPED overlapped;
	bio_signal_t signal;
} bio_io_req_t;

bio_io_req_t
bio_prepare_io_req(void);

void
bio_wait_for_io(bio_io_req_t* req);

void
bio_set_error(bio_error_t* error, DWORD last_error, const char* file, int line);

#define bio_set_error(error, code) bio_set_error(error, code, __FILE__, __LINE__)

#define bio_set_last_error(error) bio_set_error(error, GetLastError())

#endif
