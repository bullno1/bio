#ifndef BIO_WINDOWS_NET_H
#define BIO_WINDOWS_NET_H

#include "common.h"
#include <bio/net.h>
#include <Winsock2.h>
#include <mswsock.h>

struct sockaddr_named_pipe {
	struct sockaddr base;
	char name[257];
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
	BIO_SOCKET_WS,
	BIO_SOCKET_PIPE,
} bio_win_socket_type_t;

typedef struct {
	SOCKET socket;
	LPFN_ACCEPTEX accept_fn;
	SOCKADDR_STORAGE sockaddr;
	int sockaddr_len;
	int sock_type;
} bio_net_ws_socket_t;

typedef struct {
	HANDLE handle;
} bio_net_pipe_socket_t;

typedef struct {
	bio_win_socket_type_t type;
	union {
		bio_net_ws_socket_t ws;
		bio_net_pipe_socket_t pipe;
	};
} bio_socket_impl_t;

// Winsock

bool
bio_net_ws_listen(
	bio_socket_type_t socket_type,
	const bio_addr_translation_result_t* addr,
	bio_net_ws_socket_t* sock,
	bio_error_t* error
);

bool
bio_net_ws_accept(
	bio_net_ws_socket_t* socket,
	bio_net_ws_socket_t* client,
	bio_error_t* error
);

bool
bio_net_ws_connect(
	bio_socket_type_t socket_type,
	const bio_addr_translation_result_t* addr,
	bio_net_ws_socket_t* socket,
	bio_error_t* error
);

bool
bio_net_ws_close(bio_net_ws_socket_t* socket, bio_error_t* error);

size_t
bio_net_ws_send(
	bio_net_ws_socket_t* socket,
	const void* buf,
	size_t size,
	bio_error_t* error
);

size_t
bio_net_ws_recv(
	bio_net_ws_socket_t* socket,
	void* buf,
	size_t size,
	bio_error_t* error
);

// Named pipe

bool
bio_net_pipe_listen(
	bio_socket_type_t socket_type,
	const bio_addr_translation_result_t* addr,
	bio_net_pipe_socket_t* sock,
	bio_error_t* error
);

bool
bio_net_pipe_accept(
	bio_net_pipe_socket_t* socket,
	bio_net_pipe_socket_t* client,
	bio_error_t* error
);

bool
bio_net_pipe_connect(
	bio_socket_type_t socket_type,
	const bio_addr_translation_result_t* addr,
	bio_net_pipe_socket_t* socket,
	bio_error_t* error
);

bool
bio_net_pipe_close(bio_net_pipe_socket_t* socket, bio_error_t* error);

size_t
bio_net_pipe_send(
	bio_net_pipe_socket_t* socket,
	const void* buf,
	size_t size,
	bio_error_t* error
);

size_t
bio_net_pipe_recv(
	bio_net_pipe_socket_t* socket,
	void* buf,
	size_t size,
	bio_error_t* error
);

#endif
