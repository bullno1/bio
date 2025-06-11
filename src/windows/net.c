#include "common.h"
#include <bio/net.h>
#include <WS2tcpip.h>

struct sockaddr_named_pipe {
	struct sockaddr base;
	char name[256];
};

typedef struct {
	union {
		struct sockaddr_in ipv4;
		struct sockaddr_in6 ipv6;
		struct sockaddr_named_pipe named_pipe;
	} storage;

	struct sockaddr* addr;
	int addr_len;
	bool should_bind;
} bio_addr_translation_result_t;

typedef enum {
	BIO_SOCKET_WINSOCK,
	BIO_SOCKET_NAMED_PIPE,
} bio_socket_type_t;

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

				_Static_assert(sizeof(result->storage.ipv4.sin_addr) == sizeof(addr->addr.ipv4), "Address size mismatch");
				memcpy(&result->storage.ipv4.sin_addr, addr->addr.ipv4, sizeof(addr->addr.ipv4));

				result->should_bind = true;
			}

			result->addr = (struct sockaddr*)&result->storage.ipv4;
			result->addr_len = sizeof(result->storage.ipv4);
			return true;
		case BIO_ADDR_IPV6:
			result->storage.ipv6.sin6_family = AF_INET6;
			if (bio_net_address_compare(addr, &BIO_ADDR_IPV6_ANY) != 0 || port != BIO_PORT_ANY) {
				result->storage.ipv6.sin6_port = htons(port);

				_Static_assert(sizeof(result->storage.ipv6.sin6_addr) == sizeof(addr->addr.ipv6), "Address size mismatch");
				memcpy(&result->storage.ipv6.sin6_addr, addr->addr.ipv6, sizeof(addr->addr.ipv6));

				result->should_bind = true;
			}

			result->addr = (struct sockaddr*)&result->storage.ipv6;
			result->addr_len = sizeof(result->storage.ipv6);
			return true;
		case BIO_ADDR_NAMED:
			result->storage.named_pipe.base.sa_family = AF_UNIX;
			if (addr->addr.named.len > 0) {
				if (BIO_LIKELY(addr->addr.named.len < sizeof(result->storage.named_pipe.name))) {
					memcpy(result->storage.named_pipe.name, addr->addr.named.name, addr->addr.named.len);
					result->storage.named_pipe.name[addr->addr.named.len] = '\0';
					result->addr = (struct sockaddr*)&result->storage.named_pipe.base;
					result->should_bind = true;
					return true;
				} else {
					bio_set_error(error, ERROR_INVALID_PARAMETER);
					return false;
				}
			}

			result->addr = (struct sockaddr*)&result->storage.named_pipe.base;
			return true;
	}

	bio_set_errno(error, EINVAL);
	return false;
}

void
bio_net_init(void) {
	int err = WSAStartup(MAKEWORD(2, 2), &bio_ctx.platform.wsadata);
}

void
bio_net_cleanup(void) {
	WSACleanup();
}

static bool
bio_named_pipe_listen(
	bio_socket_type_t socket_type,
	bio_addr_translation_result_t* addr,
	bio_port_t port,
	bio_socket_t* sock,
	bio_error_t* error
) {
}

static bool
bio_winsock_listen(
	bio_socket_type_t socket_type,
	bio_addr_translation_result_t* addr,
	bio_port_t port,
	bio_socket_t* sock,
	bio_error_t* error
) {	
	int type;
	switch (socket_type) {
	case BIO_SOCKET_STREAM:
		type = SOCK_STREAM;
		break;
	case BIO_SOCKET_DATAGRAM:
		type = SOCK_DGRAM;
		break;
	}

	SOCKET sock = socket(addr->addr->sa_family, type, 0);

	if (addr->should_bind) {
		bind(sock, addr->addr, addr->addr_len);
	}

	listen(sock, 5);
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

	if (translation_result.addr->sa_family == AF_UNIX) {
		return bio_named_pipe_listen(socket_type, &translation_result, port, sock, error);
	} else {
		return bio_winsock_listen(socket_type, &translation_result, port, sock, error);
	}
}

bool
bio_net_accept(
	bio_socket_t socket,
	bio_socket_t* client,
	bio_error_t* error
);

bool
bio_net_connect(
	bio_socket_type_t socket_type,
	const bio_addr_t* addr,
	bio_port_t port,
	bio_socket_t* socket,
	bio_error_t* error
);

bool
bio_net_close(bio_socket_t socket, bio_error_t* error);

size_t
bio_net_send(
	bio_socket_t socket,
	const void* buf,
	size_t size,
	bio_error_t* error
);

size_t
bio_net_recv(
	bio_socket_t socket,
	void* buf,
	size_t size,
	bio_error_t* error
);

size_t
bio_net_sendto(
	bio_socket_t socket,
	const bio_addr_t* addr,
	const void* buf,
	size_t size,
	bio_error_t* error
);

size_t
bio_net_recvfrom(
	bio_socket_t socket,
	bio_addr_t* addr,
	void* buf,
	size_t size,
	bio_error_t* error
);
