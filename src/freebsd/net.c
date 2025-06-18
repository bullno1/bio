#include "common.h"
#include <bio/net.h>
#include <string.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>

static const bio_tag_t BIO_SOCKET_HANDLE = BIO_TAG_INIT("bio.handle.socket");

typedef struct {
	int fd;

	struct kevent* read;
	struct kevent* write;
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

static bool
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
					// Abstract socket is not supported on FreeBSD
					if (result->storage.unix.sun_path[0] == '@') {
						result->storage.unix.sun_path[0] = '\0';
						bio_set_errno(error, ENOTSUP);
						return false;
					}
					result->addr = (struct sockaddr*)&result->storage.unix;
					result->addr_len = offsetof(struct sockaddr_un, sun_path) + addr->named.len;
					result->should_bind = true;
					return true;
				} else {
					bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
					return false;
				}
			}

			result->addr = (struct sockaddr*)&result->storage.unix;
			return true;
	}

	bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
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

	int fd = socket(translation_result.addr->sa_family, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		bio_set_errno(error, errno);
		return -1;
	}

	if (translation_result.should_bind) {
		result = bind(fd, translation_result.addr, translation_result.addr_len);
		if (result < 0) {
			bio_set_errno(error, errno);
			close(fd);
			return -1;
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

static int
bio_net_wait_for_read(bio_socket_t ref, bio_socket_impl_t* impl) {
	if (BIO_LIKELY(impl->read == NULL)) {
		struct kevent event = {
			.ident = impl->fd,
			.flags = EV_ADD | EV_DISPATCH | EV_ENABLE,
			.filter = EVFILT_READ,
		};
		bio_io_req_t req = bio_prepare_io_req(&event);
		event.udata = &req;

		bio_array_push(bio_ctx.platform.in_events, event);
		impl->read = &event;
		bio_wait_for_io(&req);
		// Resolve again as socket might be closed
		impl = bio_resolve_handle(ref.handle, &BIO_SOCKET_HANDLE);
		if (impl != NULL) { impl->read = NULL; }

		if ((event.flags & EV_ERROR) > 0) {
			return event.data;
		} else {
			return 0;
		}
	} else {
		return EBUSY;
	}
}

static int
bio_net_wait_for_write(bio_socket_t ref, bio_socket_impl_t* impl) {
	if (BIO_LIKELY(impl->write == NULL)) {
		struct kevent event = {
			.ident = impl->fd,
			.flags = EV_ADD | EV_DISPATCH | EV_ENABLE,
			.filter = EVFILT_WRITE,
		};
		bio_io_req_t req = bio_prepare_io_req(&event);
		event.udata = &req;

		bio_array_push(bio_ctx.platform.in_events, event);
		impl->write = &event;
		bio_wait_for_io(&req);
		// Resolve again as socket might be closed
		impl = bio_resolve_handle(ref.handle, &BIO_SOCKET_HANDLE);
		if (impl != NULL) { impl->write = NULL; }

		if ((event.flags & EV_ERROR) > 0) {
			return event.data;
		} else {
			return 0;
		}
	} else {
		return EBUSY;
	}
}

void
bio_net_init(void) {
}

void
bio_net_cleanup(void) {
}

bool
bio_net_wrap(
	bio_socket_t* sock,
	uintptr_t handle,
	bio_addr_type_t addr_type,
	bio_error_t* error
) {
	bio_socket_from_fd((int)handle);
	return true;
}

uintptr_t
bio_net_unwrap(bio_socket_t socket) {
	bio_socket_impl_t* impl = bio_resolve_handle(socket.handle, &BIO_SOCKET_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		return (uintptr_t)(impl->fd);
	} else {
		return (uintptr_t)(-1);
	}
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

	int result = listen(fd, backlog);
	if (result < 0) {
		bio_set_errno(error, errno);
		close(fd);
		return false;
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
		int server_fd = impl->fd;
		int client_fd = accept(server_fd, NULL, NULL);
		if (client_fd == -1) {
			if (errno == EWOULDBLOCK) {
				int read_wait_result = bio_net_wait_for_read(socket, impl);

				if (read_wait_result == 0) {
					client_fd = accept(server_fd, NULL, NULL);
					if (client_fd >= 0) {
						*client = bio_socket_from_fd(client_fd);
						return true;
					} else {
						bio_set_errno(error, errno);
						return false;
					}
				} else {
					bio_set_errno(error, read_wait_result);
					return false;
				}
			} else {
				bio_set_errno(error, errno);
				return false;
			}
		} else {
			*client = bio_socket_from_fd(client_fd);
			return true;
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
		return false;
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

	int result = connect(fd, translation_result.addr, translation_result.addr_len);
	if (result != 0) {
		if (errno == EINPROGRESS) {
			struct kevent event = {
				.ident = fd,
				.flags = EV_ADD | EV_DISPATCH | EV_ENABLE,
				.filter = EVFILT_WRITE,
			};
			bio_io_req_t req = bio_prepare_io_req(NULL);
			event.udata = &req;

			bio_array_push(bio_ctx.platform.in_events, event);
			bio_wait_for_io(&req);

			if ((event.flags & EV_ERROR) > 0) {
				close(fd);
				bio_set_errno(error, event.data);
				return false;
			} else {
				*sock = bio_socket_from_fd(fd);
				return true;
			}
		} else {
			close(fd);
			bio_set_errno(error, errno);
			return false;
		}
	} else {
		*sock = bio_socket_from_fd(fd);
		return true;
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
		int fd = impl->fd;
		ssize_t result = send(fd, buf, size, MSG_DONTWAIT | MSG_NOSIGNAL);
		if (result < 0) {
			if (errno == EWOULDBLOCK) {
				int write_wait_result = bio_net_wait_for_write(socket, impl);

				if (write_wait_result == 0) {
					result = send(fd, buf, size, MSG_DONTWAIT | MSG_NOSIGNAL);
					if (result >= 0) {
						return (size_t)result;
					} else {
						bio_set_errno(error, errno);
						return 0;
					}
				} else {
					bio_set_errno(error, write_wait_result);
					return 0;
				}
			} else {
				bio_set_errno(error, errno);
				return 0;
			}
		} else {
			return (size_t)result;
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
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
		int fd = impl->fd;
		ssize_t result = recv(fd, buf, size, MSG_DONTWAIT);
		if (result < 0) {
			if (errno == EWOULDBLOCK) {
				int read_wait_result = bio_net_wait_for_read(socket, impl);

				if (read_wait_result == 0) {
					result = recv(fd, buf, size, MSG_DONTWAIT);
					if (result >= 0) {
						return (size_t)result;
					} else {
						bio_set_errno(error, errno);
						return 0;
					}
				} else {
					bio_set_errno(error, read_wait_result);
					return 0;
				}
			} else {
				bio_set_errno(error, errno);
				return 0;
			}
		} else {
			return (size_t)result;
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
		return 0;
	}
}

bool
bio_net_close(bio_socket_t socket, bio_error_t* error) {
	bio_socket_impl_t* impl = bio_close_handle(socket.handle, &BIO_SOCKET_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		if (impl->read != NULL) {
			bio_cancel_io(impl->read->udata);
		}

		if (impl->write != NULL) {
			bio_cancel_io(impl->read->udata);
		}

		int fd = impl->fd;
		bio_free(impl);

		int result = close(fd);
		if (result == 0) {
			return true;
		} else {
			bio_set_errno(error, errno);
			return false;
		}
	} else {
		bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
		return false;
	}
}
