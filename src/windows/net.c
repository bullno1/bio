#include "net.h"

#define BIO_PIPE_PREFIX "\\\\.\\pipe\\"

static const bio_tag_t BIO_SOCKET_HANDLE = BIO_TAG_INIT("bio.handle.socket");

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
			result->storage.named_pipe.base.sa_family = AF_UNIX;
			if (addr->named.len > 0) {
				if (BIO_LIKELY(
					addr->named.len + sizeof(BIO_PIPE_PREFIX) <= sizeof(result->storage.named_pipe.name)
				)) {
					size_t i = 0;
					// Named pipe in Windows is equivalent to abstract socket
					if (addr->named.name[0] == '@' || addr->named.name[0] == '\0') {
						++i;
					}

					memcpy(result->storage.named_pipe.name, BIO_PIPE_PREFIX, sizeof(BIO_PIPE_PREFIX) - 1);
					int addr_len = (int)sizeof(BIO_PIPE_PREFIX) - 1;
					for (; i < addr->named.len; ++i) {
						char ch = addr->named.name[i];
						if (ch == '/') { ch = '\\'; }
						result->storage.named_pipe.name[addr_len] = ch;
						++addr_len;
					}
					result->storage.named_pipe.name[addr_len] = '\0';

					result->addr = &result->storage.named_pipe.base;
					result->addr_len = addr_len;
					result->should_bind = true;
					return true;
				} else {
					bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
					return false;
				}
			}

			result->addr = &result->storage.named_pipe.base;
			return true;
	}

	bio_set_core_error(error, BIO_ERROR_INVALID_ARGUMENT);
	return false;
}

void
bio_net_init(void) {
	WSAStartup(MAKEWORD(2, 2), &bio_ctx.platform.wsadata);
}

void
bio_net_cleanup(void) {
	WSACleanup();
}

static bio_socket_t
bio_net_make_socket(const bio_socket_impl_t* proto) {
	bio_socket_impl_t* instance = bio_malloc(sizeof(bio_socket_impl_t));
	*instance = *proto;
	return (bio_socket_t){
		.handle = bio_make_handle(instance, &BIO_SOCKET_HANDLE),
	};
}

bool
bio_net_wrap(
	bio_socket_t* sock,
	uintptr_t handle,
	bio_addr_type_t addr_type,
	bio_error_t* error
) {
	if (!CreateIoCompletionPort((HANDLE)handle, bio_ctx.platform.iocp, 0, 0)) {
		bio_set_last_error(error);
		return false;
	}

	bio_socket_impl_t proto;
	if (addr_type == BIO_ADDR_NAMED) {
		proto = (bio_socket_impl_t){
			.type = BIO_SOCKET_PIPE,
			.pipe = {
				.handle = (HANDLE)handle,
			},
		};
	} else {
		proto = (bio_socket_impl_t){
			.type = BIO_SOCKET_WS,
			.ws = {
				.handle = (SOCKET)handle,
			}
		};
	}

	*sock = bio_net_make_socket(&proto);
	return true;
}

uintptr_t
bio_net_unwrap(bio_socket_t socket) {
	bio_socket_impl_t* impl = bio_resolve_handle(socket.handle, &BIO_SOCKET_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		if (impl->type == BIO_SOCKET_WS) {
			return (uintptr_t)impl->ws.handle;
		} else {
			return (uintptr_t)impl->pipe.handle;
		}
	} else {
		return (uintptr_t)(INVALID_HANDLE_VALUE);
	}
}

bool
bio_net_listen(
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

	bio_socket_impl_t proto;
	if (translation_result.addr->sa_family == AF_UNIX) {
		proto.type = BIO_SOCKET_PIPE;
		if (!bio_net_pipe_listen(socket_type, &translation_result, &proto.pipe, error)) {
			return false;
		}
	} else {
		proto.type = BIO_SOCKET_WS;
		if (!bio_net_ws_listen(socket_type, &translation_result, &proto.ws, error)) {
			return false;
		}
	}

	*sock = bio_net_make_socket(&proto);
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
		bio_socket_impl_t proto;
		if (impl->type == BIO_SOCKET_WS) {
			proto.type = BIO_SOCKET_WS;
			if (!bio_net_ws_accept(&impl->ws, &proto.ws, error)) {
				return false;
			}
		} else {
			proto.type = BIO_SOCKET_PIPE;
			if (!bio_net_pipe_accept(&impl->pipe, &proto.pipe, error)) {
				return false;
			}
		}

		*client = bio_net_make_socket(&proto);
		return true;
	} else {
		bio_set_error(error, ERROR_INVALID_HANDLE);
		return false;
	}
}

bool
bio_net_connect(
	bio_socket_type_t socket_type,
	const bio_addr_t* addr,
	bio_port_t port,
	bio_socket_t* socket,
	bio_error_t* error
) {
	bio_addr_translation_result_t translation_result = { 0 };
	if (!bio_translate_address(addr, port, &translation_result, error)) {
		return false;
	}

	bio_socket_impl_t proto;
	if (translation_result.addr->sa_family == AF_UNIX) {
		proto.type = BIO_SOCKET_PIPE;
		if (!bio_net_pipe_connect(socket_type, &translation_result, &proto.pipe, error)) {
			return false;
		}
	} else {
		proto.type = BIO_SOCKET_WS;
		if (!bio_net_ws_connect(socket_type, &translation_result, &proto.ws, error)) {
			return false;
		}
	}

	*socket = bio_net_make_socket(&proto);
	return true;
}

bool
bio_net_close(bio_socket_t socket, bio_error_t* error) {
	bio_socket_impl_t* impl = bio_close_handle(socket.handle, &BIO_SOCKET_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		bool success;
		if (impl->type == BIO_SOCKET_WS) {
			success = bio_net_ws_close(&impl->ws, error);
		} else {
			success = bio_net_pipe_close(&impl->pipe, error);
		}
		bio_free(impl);
		return true;
	} else {
		bio_set_error(error, ERROR_INVALID_HANDLE);
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
		size_t bytes_transferred;
		if (impl->type == BIO_SOCKET_WS) {
			bytes_transferred = bio_net_ws_send(&impl->ws, buf, size, error);
		} else {
			bytes_transferred = bio_net_pipe_send(&impl->pipe, buf, size, error);
		}
		return bytes_transferred;
	} else {
		bio_set_error(error, ERROR_INVALID_HANDLE);
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
		size_t bytes_transferred;
		if (impl->type == BIO_SOCKET_WS) {
			bytes_transferred = bio_net_ws_recv(&impl->ws, buf, size, error);
		} else {
			bytes_transferred = bio_net_pipe_recv(&impl->pipe, buf, size, error);
		}
		return bytes_transferred;
	} else {
		bio_set_error(error, ERROR_INVALID_HANDLE);
		return 0;
	}
}
