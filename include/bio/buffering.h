#ifndef BIO_BUFFERING_H
#define BIO_BUFFERING_H

#include "bio.h"
#include "file.h"
#include "net.h"

/**
 * @defgroup buffering Buffering
 *
 * Avoid making a lot of syscalls.
 *
 * This is useful for delimitor-based protocols such as HTTP.
 * Program code can just read character by character from the buffer until a
 * delimitor is encountered.
 * Data will get pulled as a block into the buffer as needed instead of single
 * bytes.
 *
 * @{
 */

/// Context for a buffer function
typedef union {
	bio_handle_t handle;  /**< A @ref handle "Handle" */
	void* ptr;  /**< A pointer */
} bio_io_buffer_ctx_t;

/// Read callback
typedef size_t (*bio_read_fn_t)(
	bio_io_buffer_ctx_t ctx,
	void* buf,
	size_t size,
	bio_error_t* error
);

/// Write callback
typedef size_t (*bio_write_fn_t)(
	bio_io_buffer_ctx_t ctx,
	const void* buf,
	size_t size,
	bio_error_t* error
);

/// Flush callback
typedef bool (*bio_flush_fn_t)(
	bio_io_buffer_ctx_t ctx,
	bio_error_t* error
);

/// Options for a buffer
typedef struct {
	/// Context for functions
	bio_io_buffer_ctx_t ctx;
	/// Read callback
	bio_read_fn_t read_fn;
	/// Write callback
	bio_write_fn_t write_fn;
	/// Flush callback
	bio_flush_fn_t flush_fn;
	/// Buffer size in bytes
	size_t size;
} bio_buffer_options_t;

/// Handle to a buffer
typedef struct {
	bio_handle_t handle;
} bio_io_buffer_t;

/**
 * Create a new I/O buffer
 *
 * @param options Options for this buffer
 * @return A new buffer
 *
 * @remarks
 *   A single buffer should only be used for either read or write, not both
 */
bio_io_buffer_t
bio_make_buffer(const bio_buffer_options_t* options);

/**
 * Destroy a buffer
 *
 * Existing data in a write buffer will be flushed.
 */
void
bio_destroy_buffer(bio_io_buffer_t buffer);

/// Create a read buffer for a file
bio_io_buffer_t
bio_make_file_read_buffer(bio_file_t file, size_t size);

/**
 * Create a write buffer for a file
 *
 * @param file A file
 * @param size Buffer size
 * @param fflush Whether to call fflush on flush
 * @return A new io buffer
 */
bio_io_buffer_t
bio_make_file_write_buffer(bio_file_t file, size_t size, bool fflush);

/// Create a read buffer for a socket
bio_io_buffer_t
bio_make_socket_read_buffer(bio_socket_t socket, size_t size);

/// Create a write buffer for a socket
bio_io_buffer_t
bio_make_socket_write_buffer(bio_socket_t socket, size_t size);

/// Read from a buffer
size_t
bio_buffered_read(
	bio_io_buffer_t buffer,
	void* in,
	size_t size,
	bio_error_t* error
);

/// Write to a buffer
size_t
bio_buffered_write(
	bio_io_buffer_t buffer,
	const void* out,
	size_t size,
	bio_error_t* error
);

/// Flush all bufferred content
bool
bio_flush_buffer(bio_io_buffer_t buffer, bio_error_t* error);

/// Helper to deal with short read
static inline size_t
bio_buffered_read_exactly(
	bio_io_buffer_t buffer,
	void* in,
	size_t size,
	bio_error_t* error
) {
	size_t total_bytes_read = 0;
	while (total_bytes_read < size) {
		size_t bytes_read = bio_buffered_read(
			buffer,
			(char*)in + total_bytes_read,
			size - total_bytes_read,
			error
		);
		if (bytes_read == 0) { break; }
		total_bytes_read += bytes_read;
	}

	return total_bytes_read;
}

/// Helper to deal with short write
static inline size_t
bio_buffered_write_exactly(
	bio_io_buffer_t buffer,
	const void* out,
	size_t size,
	bio_error_t* error
) {
	size_t total_bytes_written = 0;
	while (total_bytes_written < size) {
		size_t bytes_written = bio_buffered_write(
			buffer,
			(const char*)out + total_bytes_written,
			size - total_bytes_written,
			error
		);
		if (bytes_written == 0) { break; }
		total_bytes_written += bytes_written;
	}

	return total_bytes_written;
}

/**@}*/

#endif
