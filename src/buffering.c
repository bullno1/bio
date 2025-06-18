#include "internal.h"
#include <bio/buffering.h>
#include <string.h>

static const bio_tag_t BIO_BUFFER_HANDLE = BIO_TAG_INIT("bio.handle.buffer");

typedef struct {
	bio_buffer_options_t options;
	size_t offset;
	size_t data_size;
	uint8_t data[];
} bio_io_buffer_impl_t;

bio_io_buffer_t
bio_make_buffer(const bio_buffer_options_t* options) {
	bio_io_buffer_impl_t* impl = bio_malloc(sizeof(bio_io_buffer_impl_t)+ options->size);
	impl->options = *options;
	impl->offset = 0;
	impl->data_size = 0;

	return (bio_io_buffer_t){  bio_make_handle(impl, &BIO_BUFFER_HANDLE) };
}

void
bio_destroy_buffer(bio_io_buffer_t buffer) {
	bio_io_buffer_impl_t* impl = bio_close_handle(buffer.handle, &BIO_BUFFER_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		if (impl->offset > 0) {
			if (impl->options.write_fn != NULL) {
				impl->options.write_fn(impl->options.ctx, impl->data, impl->offset, NULL);
			}
			if (impl->options.flush_fn != NULL) {
				impl->options.flush_fn(impl->options.ctx, NULL);
			}
		}
		bio_free(impl);
	}
}

static size_t
bio_read_file_buffered(
	bio_io_buffer_ctx_t ctx,
	void* buf,
	size_t size,
	bio_error_t* error
) {
	return bio_fread((bio_file_t){ ctx.handle }, buf, size, error);
}

bio_io_buffer_t
bio_make_file_read_buffer(bio_file_t file, size_t size) {
	return bio_make_buffer(&(bio_buffer_options_t){
		.ctx.handle = file.handle,
		.read_fn = bio_read_file_buffered,
		.size = size,
	});
}

static size_t
bio_write_file_buffered(
	bio_io_buffer_ctx_t ctx,
	const void* buf,
	size_t size,
	bio_error_t* error
) {
	return bio_fwrite((bio_file_t){ ctx.handle }, buf, size, error);
}

static bool
bio_flush_file_buffered(bio_io_buffer_ctx_t ctx, bio_error_t* error) {
	return bio_fflush((bio_file_t){ ctx.handle }, error);
}

bio_io_buffer_t
bio_make_file_write_buffer(bio_file_t file, size_t size, bool fflush) {
	return bio_make_buffer(&(bio_buffer_options_t){
		.ctx.handle = file.handle,
		.write_fn = bio_write_file_buffered,
		.flush_fn = fflush ? bio_flush_file_buffered : NULL,
		.size = size,
	});
}

static size_t
bio_read_socket_buffered(
	bio_io_buffer_ctx_t ctx,
	void* buf,
	size_t size,
	bio_error_t* error
) {
	return bio_net_recv((bio_socket_t){ ctx.handle }, buf, size, error);
}

bio_io_buffer_t
bio_make_socket_read_buffer(bio_socket_t socket, size_t size) {
	return bio_make_buffer(&(bio_buffer_options_t){
		.ctx.handle = socket.handle,
		.read_fn = bio_read_socket_buffered,
		.size = size,
	});
}

static size_t
bio_write_socket_buffered(
	bio_io_buffer_ctx_t ctx,
	const void* buf,
	size_t size,
	bio_error_t* error
) {
	return bio_net_send((bio_socket_t){ ctx.handle }, buf, size, error);
}

static bool
bio_flush_internal_buffer(bio_io_buffer_impl_t* impl, bio_error_t* error) {
	size_t bytes_to_write = impl->offset;
	size_t total_bytes_written = 0;
	while (bytes_to_write > 0) {
		size_t bytes_written = impl->options.write_fn(
			impl->options.ctx,
			impl->data + total_bytes_written, bytes_to_write,
			error
		);
		if (bytes_written == 0) { return false; }
		total_bytes_written += bytes_written;
		bytes_to_write -= bytes_written;
	}

	impl->offset = 0;
	return true;
}

bio_io_buffer_t
bio_make_socket_write_buffer(bio_socket_t socket, size_t size) {
	return bio_make_buffer(&(bio_buffer_options_t){
		.ctx.handle = socket.handle,
		.write_fn = bio_write_socket_buffered,
		.size = size,
	});
}

size_t
bio_buffered_read(
	bio_io_buffer_t buffer,
	void* in,
	size_t size,
	bio_error_t* error
) {
	bio_io_buffer_impl_t* impl = bio_resolve_handle(buffer.handle, &BIO_BUFFER_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		if (BIO_LIKELY(impl->options.read_fn != NULL)) {
			if (!(impl->offset < impl->data_size)) {
				// Refill buffer
				size_t bytes_read = impl->options.read_fn(
					impl->options.ctx, impl->data, impl->options.size, error
				);
				if (bytes_read == 0) { return 0; }

				impl->data_size = bytes_read;
				impl->offset = 0;
			}

			size_t data_available = impl->data_size - impl->offset;
			size_t num_bytes_to_read = size < data_available ? size : data_available;
			memcpy(in, impl->data + impl->offset, num_bytes_to_read);
			impl->offset += num_bytes_to_read;
			return num_bytes_to_read;
		} else {
			bio_set_core_error(error, BIO_ERROR_NOT_SUPPORTED);
			return 0;
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
		return 0;
	}
}

size_t
bio_buffered_write(
	bio_io_buffer_t buffer,
	const void* out,
	size_t size,
	bio_error_t* error
) {
	bio_io_buffer_impl_t* impl = bio_resolve_handle(buffer.handle, &BIO_BUFFER_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		if (BIO_LIKELY(impl->options.write_fn != NULL)) {
			if (
				(impl->offset + size > impl->options.size)
				&& !bio_flush_internal_buffer(impl, error)
			) {
				return 0;
			}

			if (impl->offset + size <= impl->options.size) {
				// Data is small enough to fit in the buffer
				memcpy(impl->data + impl->offset, out, size);
				impl->offset += size;
				return size;
			} else {
				// Directly write if it can't fit
				return impl->options.write_fn(impl->options.ctx, out, size, error);
			}
		} else {
			bio_set_core_error(error, BIO_ERROR_NOT_SUPPORTED);
			return 0;
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
		return 0;
	}
}

bool
bio_flush_buffer(bio_io_buffer_t buffer, bio_error_t* error) {
	bio_io_buffer_impl_t* impl = bio_resolve_handle(buffer.handle, &BIO_BUFFER_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		if (BIO_LIKELY(impl->options.write_fn != NULL)) {
			if ((impl->offset > 0) && !bio_flush_internal_buffer(impl, error)) {
				return false;
			}

			if (impl->options.flush_fn != NULL) {
				return impl->options.flush_fn(impl->options.ctx, error);
			} else {
				return true;
			}
		} else {
			bio_set_core_error(error, BIO_ERROR_NOT_SUPPORTED);
			return false;
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
		return false;
	}
}
