#include "common.h"
#include <bio/net.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>

const bio_tag_t BIO_SOCKET_HANDLE = BIO_TAG_INIT("bio.handle.socket");

typedef struct {
	int fd;
} bio_socket_impl_t;

typedef struct {
	union {
		struct sockaddr_in ipv4;
		struct sockaddr_in6 ipv6;
		struct sockaddr_un unix;
	} storage;

	struct sockaddr* addr;
	socklen_t addr_len;
	bool should_bind;
} bio_addr_translation_result_t;

bool
bio_translate_address(
	const bio_addr_t* addr,
	uint16_t port,
	bio_addr_translation_result_t* result,
	bio_error_t* error
) {
	switch (addr->type) {
		case BIO_ADDR_IPV4:
			result->storage.ipv4.sin_family = AF_INET;
			if (bio_net_address_compare(addr, &BIO_ADDR_IPV4_ANY) != 0 || port != BIO_PORT_ANY) {
				result->storage.ipv4.sin_port = htons(port);

				_Static_assert(sizeof(result->storage.ipv4.sin_addr) == sizeof(addr->ipv4), "Address size mismatch");
				memcpy(&result->storage.ipv4.sin_addr, addr->ipv4, sizeof(addr->ipv4));

				result->should_bind = true;
			}

			result->addr = (struct sockaddr*)&result->storage.ipv4;
			result->addr_len = sizeof(result->storage.ipv4);
			return true;
		case BIO_ADDR_IPV6:
			result->storage.ipv6.sin6_family = AF_INET6;
			if (bio_net_address_compare(addr, &BIO_ADDR_IPV6_ANY) != 0 || port != BIO_PORT_ANY) {
				result->storage.ipv6.sin6_port = htons(port);

				_Static_assert(sizeof(result->storage.ipv6.sin6_addr) == sizeof(addr->ipv6), "Address size mismatch");
				memcpy(&result->storage.ipv6.sin6_addr, addr->ipv6, sizeof(addr->ipv6));

				result->should_bind = true;
			}

			result->addr = (struct sockaddr*)&result->storage.ipv6;
			result->addr_len = sizeof(result->storage.ipv6);
			return true;
		case BIO_ADDR_NAMED:
			result->storage.unix.sun_family = AF_UNIX;
			if (addr->named.len > 0) {
				if (BIO_LIKELY(addr->named.len < sizeof(result->storage.unix.sun_path))) {
					memcpy(result->storage.unix.sun_path, addr->named.name, addr->named.len);
					// Abstract socket
					if (result->storage.unix.sun_path[0] == '@') {
						result->storage.unix.sun_path[0] = '\0';
					}
					result->addr = (struct sockaddr*)&result->storage.unix;
					result->addr_len = offsetof(struct sockaddr_un, sun_path) + addr->named.len;
					result->should_bind = true;
					return true;
				} else {
					bio_set_errno(error, EINVAL);
					return false;
				}
			}

			result->addr = (struct sockaddr*)&result->storage.unix;
			return true;
	}

	bio_set_errno(error, EINVAL);
	return false;
}

static int
bio_make_socket(
	bio_socket_type_t socket_type,
	const bio_addr_t* addr,
	uint16_t port,
	bio_error_t* error
) {
	bio_addr_translation_result_t translation_result = { 0 };
	if (!bio_translate_address(addr, port, &translation_result, error)) {
		return -1;
	}

	int type;
	switch (socket_type) {
		case BIO_SOCKET_STREAM:
			type = SOCK_STREAM;
			break;
		case BIO_SOCKET_DATAGRAM:
			type = SOCK_DGRAM;
			break;
	}

	int result;
	struct io_uring_sqe* sqe;

	int fd;
	sqe = bio_acquire_io_req();
	io_uring_prep_socket(sqe, translation_result.addr->sa_family, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, 0);
	fd = result = bio_submit_io_req(sqe, NULL);
	if (fd < 0) {
		bio_set_errno(error, -result);
		return -1;
	}

	if (translation_result.should_bind) {
		if (bio_ctx.platform.has_op_bind) {
			sqe = bio_acquire_io_req();
			io_uring_prep_bind(sqe, fd, translation_result.addr, translation_result.addr_len);
			result = bio_submit_io_req(sqe, NULL);
			if (result < 0) {
				bio_set_errno(error, -result);
				bio_io_close(fd);
				return -1;
			}
		} else {
			result = bind(fd, translation_result.addr, translation_result.addr_len);
			if (result < 0) {
				bio_set_errno(error, errno);
				bio_io_close(fd);
				return -1;
			}
		}
	}

	return fd;
}

static bio_socket_t
bio_socket_from_fd(int fd) {
	bio_socket_impl_t* sock_impl = bio_malloc(sizeof(bio_socket_impl_t));
	*sock_impl = (bio_socket_impl_t){ .fd = fd };
	return (bio_socket_t){
		.handle = bio_make_handle(sock_impl, &BIO_SOCKET_HANDLE),
	};
}

bool
bio_net_listen(
	bio_socket_type_t socket_type,
	const bio_addr_t* addr,
	uint16_t port,
	bio_socket_t* sock,
	bio_error_t* error
) {
	int fd = bio_make_socket(socket_type, addr, port, error);
	if (fd < 0) { return false; }

	// TODO: make configurable
	int backlog = 5;

	if (bio_ctx.platform.has_op_listen) {
		struct io_uring_sqe* sqe = bio_acquire_io_req();
		io_uring_prep_listen(sqe, fd, backlog);
		int result = bio_submit_io_req(sqe, NULL);
		if (result < 0) {
			bio_set_errno(error, -result);
			bio_io_close(fd);
			return false;
		}
	} else {
		int result = listen(fd, backlog);
		if (result < 0) {
			bio_set_errno(error, errno);
			bio_io_close(fd);
			return false;
		}
	}

	*sock = bio_socket_from_fd(fd);
	return true;
}

bool
bio_net_accept(
	bio_socket_t socket,
	bio_socket_t* client,
	bio_error_t* error
) {
	bio_socket_impl_t* impl = bio_resolve_handle(socket.handle, &BIO_SOCKET_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		struct io_uring_sqe* sqe = bio_acquire_io_req();
		io_uring_prep_accept(sqe, impl->fd, NULL, NULL, 0);
		int result = bio_submit_io_req(sqe, NULL);
		if (result > 0) {
			*client = bio_socket_from_fd(result);
			return true;
		} else {
			bio_set_errno(error, -result);
			return false;
		}
	} else {
		bio_set_errno(error, EINVAL);
		return 0;
	}
}

bool
bio_net_connect(
	bio_socket_type_t socket_type,
	const bio_addr_t* addr,
	bio_port_t port,
	bio_socket_t* sock,
	bio_error_t* error
) {
	bio_addr_translation_result_t translation_result = { 0 };
	if (!bio_translate_address(addr, port, &translation_result, error)) {
		return false;
	}

	// TODO: configure source address
	bio_addr_t src_addr = { 0 };
	src_addr.type = addr->type;
	int fd = bio_make_socket(socket_type, &src_addr, BIO_PORT_ANY, error);
	if (fd < 0) { return false; }

	struct io_uring_sqe* sqe = bio_acquire_io_req();
	io_uring_prep_connect(sqe, fd, translation_result.addr, translation_result.addr_len);
	int result = bio_submit_io_req(sqe, NULL);
	if (result < 0) {
		bio_set_errno(error, -result);
		bio_io_close(fd);
		return false;
	}

	*sock = bio_socket_from_fd(fd);
	return true;
}

bool
bio_net_close(bio_socket_t socket, bio_error_t* error) {
	bio_socket_impl_t* impl = bio_close_handle(socket.handle, &BIO_SOCKET_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		int fd = impl->fd;
		bio_free(impl);

		struct io_uring_sqe* sqe = bio_acquire_io_req();
		io_uring_prep_shutdown(sqe, fd, SHUT_RDWR);
		int result = bio_submit_io_req(sqe, NULL);
		if (result < 0) {
			bio_set_errno(error, -result);
			bio_io_close(fd);
			return false;
		} else {
			bio_io_close(fd);
			return true;
		}
	} else {
		bio_set_errno(error, EINVAL);
		return false;
	}
}

size_t
bio_net_send(
	bio_socket_t socket,
	const void* buf,
	size_t size,
	bio_error_t* error
) {
	bio_socket_impl_t* impl = bio_resolve_handle(socket.handle, &BIO_SOCKET_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		struct io_uring_sqe* sqe = bio_acquire_io_req();
		io_uring_prep_send(sqe, impl->fd, buf, size, 0);
		int result = bio_submit_io_req(sqe, NULL);
		return bio_result_to_size(result, error);
	} else {
		bio_set_errno(error, EINVAL);
		return 0;
	}
}

size_t
bio_net_recv(
	bio_socket_t socket,
	void* buf,
	size_t size,
	bio_error_t* error
) {
	bio_socket_impl_t* impl = bio_resolve_handle(socket.handle, &BIO_SOCKET_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		struct io_uring_sqe* sqe = bio_acquire_io_req();
		io_uring_prep_recv(sqe, impl->fd, buf, size, 0);
		int result = bio_submit_io_req(sqe, NULL);
		return bio_result_to_size(result, error);
	} else {
		bio_set_errno(error, EINVAL);
		return 0;
	}
}
