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
		char ipv4[8];
		char ipv6[16];
		struct {
			size_t len;
			char content[256];
		} named;
	} addr;

	uint16_t port;
} bio_addr_t;

bool
bio_net_listen(
	bio_addr_t* addr,
	bio_socket_type_t socket_type,
	bio_socket_t* socket,
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
	bio_addr_t* addr,
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

#endif
