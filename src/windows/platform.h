#ifndef BIO_WINDOW_PLATFORM_H
#define BIO_WINDOW_PLATFORM_H

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

typedef struct {
	HANDLE iocp;
} bio_platform_t;

#endif