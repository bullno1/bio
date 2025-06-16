#ifndef BIO_FILE_H
#define BIO_FILE_H

#include "bio.h"

/**
 * @defgroup file File I/O
 *
 * Read/write to files.
 *
 * @{
 */

/// Handle to a file
typedef struct {
	bio_handle_t handle;
} bio_file_t;

/// Standard input
extern bio_file_t BIO_STDIN;
/// Standard output
extern bio_file_t BIO_STDOUT;
/// Standard error
extern bio_file_t BIO_STDERR;

/// Statistics about a file
typedef struct {
	uint64_t size;  /**< The file size in bytes */
} bio_stat_t;

/**
 * Wraps a handle from the OS into a @ref bio_file_t
 *
 * This can be used for platform-specific special files
 * (e.g: memfd in Linux, mailslot in Windows).
 *
 * @remarks
 *   bio has its own internal file offset handling.
 *   This is required when using asynchronous I/O API in many platforms.
 *   Thus, if a file offset was set before being passed to this function, it
 *   will have no effect.
 *
 * @param file_ptr Pointer to a file handle.
 *   This will only be set if the open was successful.
 * @param fd A platform-specific handle.
 *   For Unix platforms, it is a file descriptor.
 *   For Windows, it is a `HANDLE`.
 * @param error See @ref error.
 * @return Whether this was successful.
 */
bool
bio_fdopen(bio_file_t* file_ptr, uintptr_t fd, bio_error_t* error);

/**
 * Retrieve an OS handle from a @ref bio_file_t
 *
 * This can be used to call platform-specific API such as `ioctl` or `flock`.
 *
 * For Unix platforms, this is will return a file descriptor.
 * A value of -1 is possible if the file was closed with @ref bio_fclose.
 *
 * For Windows, this will return a `HANDLE`.
 * A value of `INVALID_HANDLE_VALUE` may be returned.
 *
 * @remarks
 *   It is important not to interfere with bio when manipulating the OS handle
 *   directly.
 *   Please refer to the implementation details for the given platform.
 */
uintptr_t
bio_funwrap(bio_file_t file);

/**
 * Open a file for reading or writing
 *
 * This is similar to stdlib `fopen_s`.
 *
 * @param file_ptr Pointer to a file handle.
 *   This will only be set if the open was successful.
 * @param filename The file to open.
 * @param mode Mode string
 * @param error See @ref error
 * @return Whether this was successful.
 */
bool
bio_fopen(
	bio_file_t* file_ptr,
	const char* restrict filename,
	const char* restrict mode,
	bio_error_t* error
);

/**
 * Write to a file.
 *
 * This is similar to stdlib `fwrite`.
 * @remarks Short write is possible.
 *
 * @see bio_fwrite_exactly
 */
size_t
bio_fwrite(
	bio_file_t file,
	const void* buf,
	size_t size,
	bio_error_t* error
);

/**
 * Read from a file.
 *
 * This is similar to stdlib `fread`.
 * @remarks Short read is possible.
 *
 * @see bio_fread_exactly
 */
size_t
bio_fread(
	bio_file_t file,
	void* buf,
	size_t size,
	bio_error_t* error
);

/**
 * Modify the file offset
 *
 * This is similar to stdlib `fseek`.
 *
 * @remarks
 *   bio maintains its own file offset which has no relation to the offset
 *   maintained by synchronous file API provided by the OS.
 */
bool
bio_fseek(
	bio_file_t file,
	int64_t offset,
	int origin,
	bio_error_t* error
);

/**
 * Retrieve the file offset
 *
 * This is similar to stdlib `ftell`.
 */
int64_t
bio_ftell(
	bio_file_t file,
	bio_error_t* error
);

/**
 * Flush the file buffer.
 *
 * This is similar to stdlib `flush`.
 *
 * @remarks
 *   Unlike the stdlib, bio does not do any userspace buffering.
 *   This flushes the OS buffer.
 */
bool
bio_fflush(bio_file_t file, bio_error_t* error);

/// Close a file handle
bool
bio_fclose(bio_file_t file, bio_error_t* error);

/// Retrieves statistics about a file
bool
bio_fstat(bio_file_t file, bio_stat_t* stat, bio_error_t* error);

/**
 * Convenient function to write exactly a number of bytes to a file without short write.
 *
 * @remarks
 *   If an error was encountered while writing, it is still possible for this
 *   function to write fewer bytes.
 *   Always check @p error.
 */
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

/**
 * Convenient function to read exactly a number of bytes from a file without short read.
 *
 * @remarks
 *   If an error was encountered while reading, it is still possible for this
 *   function to read fewer bytes.
 *   Always check @p error.
 */
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

/**@}*/

#endif
