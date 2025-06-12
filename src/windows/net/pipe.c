#include "../net.h"

bool
bio_net_pipe_listen(
	bio_socket_type_t socket_type,
	const bio_addr_translation_result_t* addr,
	bio_net_pipe_socket_t* sock,
	bio_error_t* error
) {
	CreateNamedPipeA(
		addr->storage.named_pipe.name,
		PIPE_ACCESS_DUPLEX
	)
}

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
