#ifndef BIO_NET_H
#define BIO_NET_H

#include "bio.h"

typedef struct {
	bio_handle_t handle;
} bio_socket_t;

typedef enum {
	BIO_ADDR_IPV4,
	BIO_ADDR_IPV6,
	BIO_ADDR_NAMED,
} bio_addr_type_t;

typedef enum {
	BIO_SOCKET_STREAM,
	BIO_SOCKET_DATAGRAM,
} bio_socket_type_t;

typedef struct {
	bio_addr_type_t type;
	union {
		char ipv4[4];
		char ipv6[16];
		struct {
			size_t len;
			char name[256];
		} named;
	};
} bio_addr_t;

typedef uint16_t bio_port_t;

static const bio_port_t BIO_PORT_ANY = 0;

static const bio_addr_t BIO_ADDR_IPV4_ANY = {
	.type = BIO_ADDR_IPV4,
	.ipv4 = { 0 },
};

static const bio_addr_t BIO_ADDR_IPV4_LOOPBACK = {
	.type = BIO_ADDR_IPV4,
	.ipv4 = { 127, 0, 0, 1 },
};

static const bio_addr_t BIO_ADDR_IPV6_ANY = {
	.type = BIO_ADDR_IPV6,
	.ipv6 = { 0 },
};

static const bio_addr_t BIO_ADDR_IPV6_LOOPBACK = {
	.type = BIO_ADDR_IPV6,
	.ipv6 = { [15] = 1 },
};

bool
bio_net_wrap_handle(
	bio_socket_t* sock,
	uintptr_t handle,
	bio_addr_type_t addr_type,
	bio_error_t* error
);

uintptr_t
bio_net_unwrap(bio_socket_t socket);

bool
bio_net_listen(
	bio_socket_type_t socket_type,
	const bio_addr_t* addr,
	bio_port_t port,
	bio_socket_t* sock,
	bio_error_t* error
);

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

int
bio_net_address_compare(const bio_addr_t* lhs, const bio_addr_t* rhs);

static inline size_t
bio_net_send_exactly(bio_socket_t socket, const void* buf, size_t size, bio_error_t* error) {
	size_t total_bytes_sent = 0;
	while (total_bytes_sent < size) {
		size_t bytes_sent = bio_net_send(
			socket,
			(char*)buf + total_bytes_sent,
			size - total_bytes_sent,
			error
		);
		if (bytes_sent == 0) { break; }
		total_bytes_sent += bytes_sent;
	}

	return total_bytes_sent;
}


#endif
