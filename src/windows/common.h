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

#endif