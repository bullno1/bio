#ifndef BIO_WINDOWS_PLATFORM_H
#define BIO_WINDOWS_PLATFORM_H

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <Windows.h>

typedef struct {
	HANDLE iocp;
} bio_platform_t;

#endif