#include "../net.h"

#define bio_set_last_wsa_error(error) bio_set_error(error, WSAGetLastError())

static bool
bio_net_maybe_wait_for_io(
	SOCKET socket,
	bio_io_req_t* req,
	bio_error_t* error,
	bool overlapped_op_succeeded
) {
	DWORD num_bytes, flags;
	if (!overlapped_op_succeeded) {
		int error_code = WSAGetLastError();
		if (error_code != WSA_IO_PENDING) {
			bio_set_last_wsa_error(error);
			return false;
		}
		bio_wait_for_io(req);

		if (!WSAGetOverlappedResult(socket, &req->overlapped, &num_bytes, FALSE, &flags)) {
			bio_set_last_wsa_error(error);
			return false;
		}

		return true;
	} else {
		bio_raise_signal(req->signal);
		return true;
	}
}

bool
bio_net_ws_listen(
	bio_socket_type_t socket_type,
	const bio_addr_translation_result_t* addr,
	bio_net_ws_socket_t* sock,
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
		default:
			bio_set_error(error, ERROR_INVALID_PARAMETER);
			return false;
	}

	SOCKET handle = WSASocket(addr->addr->sa_family, type, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (handle == INVALID_SOCKET) {
		bio_set_last_wsa_error(error);
		return false;
	}

	bool success = false;
	if (addr->should_bind) {
		if (bind(handle, addr->addr, addr->addr_len) != 0) {
			bio_set_last_wsa_error(error);
			goto end;
		}
	}

	if (listen(handle, 5) != 0) {
		bio_set_last_wsa_error(error);
		goto end;
	}

	if (CreateIoCompletionPort((HANDLE)handle, bio_ctx.platform.iocp, 0, 0) == NULL) {
		bio_set_last_error(error);
		goto end;
	}

	success = true;
end:
	if (BIO_LIKELY(success)) {
		*sock = (bio_net_ws_socket_t){ .socket = handle };
	} else {
		closesocket(handle);
	}

	return success;
}

bool
bio_net_ws_accept(
	bio_net_ws_socket_t* socket,
	bio_net_ws_socket_t* client,
	bio_error_t* error
) {
	DWORD num_bytes;

	// Initialize lazily so we can support sockets passed through bio_net_wrap_handle
	if (socket->accept_fn == NULL) {
		GUID accept_ex_guid = WSAID_ACCEPTEX;
		bio_io_req_t req = bio_prepare_io_req();
		if (!bio_net_maybe_wait_for_io(
			socket->socket, &req, error,
			WSAIoctl(
				socket->socket,
				SIO_GET_EXTENSION_FUNCTION_POINTER,
				&accept_ex_guid, sizeof(accept_ex_guid),
				&socket->accept_fn, sizeof(socket->accept_fn),
				&num_bytes,
				&req.overlapped, NULL
			)
		)) {
			return false;
		}

		if (getsockname(
			socket->socket,
			(struct sockaddr*)&socket->sockaddr, &socket->sockaddr_len
		)) {
			bio_set_last_wsa_error(error);
			return false;
		}

		int sock_type_len = (int)sizeof(socket->sock_type);
		if (getsockopt(
			socket->socket,
			SOL_SOCKET, SO_TYPE,
			(char*)&socket->sock_type, &sock_type_len
		)) {
			bio_set_last_wsa_error(error);
			return false;
		}
	}

	SOCKET client_socket = WSASocketA(
		socket->sockaddr.ss_family,
		socket->sock_type,
		0,
		NULL,
		0,
		WSA_FLAG_OVERLAPPED
	);
	if (client_socket == INVALID_SOCKET) {
		bio_set_last_wsa_error(error);
		return false;
	}

	bool success = false;
	char output_buf[(sizeof(struct sockaddr_in6) + 16) * 2];
	bio_io_req_t req = bio_prepare_io_req();
	if (!bio_net_maybe_wait_for_io(
		socket->socket, &req, error,
		socket->accept_fn(
			socket->socket,
			client_socket,
			output_buf, 0,
			sizeof(struct sockaddr_in6) + 16,
			sizeof(struct sockaddr_in6) + 16,
			&num_bytes,
			&req.overlapped
		)
	)) {
		goto end;
	}

	if (setsockopt(
		client_socket,
		SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
		(char*)&socket->socket, sizeof(socket->socket)
	)) {
		bio_set_last_wsa_error(error);
		goto end;
	}

	if (CreateIoCompletionPort((HANDLE)client_socket, bio_ctx.platform.iocp, 0, 0) == NULL) {
		bio_set_last_error(error);
		goto end;
	}

	success = true;
end:
	if (success) {
		*client = (bio_net_ws_socket_t){ .socket = client_socket };
	} else {
		closesocket(client_socket);
	}

	return success;
}

bool
bio_net_ws_connect(
	bio_socket_type_t socket_type,
	const bio_addr_translation_result_t* addr,
	bio_net_ws_socket_t* socket,
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
		default:
			bio_set_error(error, ERROR_INVALID_PARAMETER);
			return false;
	}

	SOCKET handle = WSASocket(addr->addr->sa_family, type, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (handle == INVALID_SOCKET) {
		bio_set_last_wsa_error(error);
		return false;
	}

	bool success = false;
	LPFN_CONNECTEX connect_ex;
	GUID connect_ex_guid = WSAID_CONNECTEX;
	DWORD num_bytes;
	bio_io_req_t req = bio_prepare_io_req();
	if (!bio_net_maybe_wait_for_io(
		handle, &req, error,
		WSAIoctl(
			handle,
			SIO_GET_EXTENSION_FUNCTION_POINTER,
			&connect_ex_guid, sizeof(connect_ex_guid),
			&connect_ex, sizeof(connect_ex),
			&num_bytes,
			&req.overlapped, NULL
		)
	)) {
		goto end;
	}

	req = bio_prepare_io_req();
	if (!bio_net_maybe_wait_for_io(
		handle, &req, error,
		connect_ex(
			handle,
			addr->addr, addr->addr_len,
			NULL, 0,
			&num_bytes,
			&req.overlapped
		)
	)) {
		goto end;
	}

	if (CreateIoCompletionPort((HANDLE)handle, bio_ctx.platform.iocp, 0, 0) == NULL) {
		bio_set_last_error(error);
		goto end;
	}

	success = true;
end:
	if (BIO_LIKELY(success)) {
		*socket = (bio_net_ws_socket_t){ .socket = handle };
	} else {
		closesocket(handle);
	}

	return success;
}

bool
bio_net_ws_close(bio_net_ws_socket_t* socket, bio_error_t* error) {
	CancelIo((HANDLE)socket->socket);
	shutdown(socket->socket, SD_BOTH);
	if (closesocket(socket->socket) == 0) {
		return true;
	} else {
		bio_set_last_wsa_error(error);
		return false;
	}
}

size_t
bio_net_ws_send(
	bio_net_ws_socket_t* socket,
	const void* buf,
	size_t size,
	bio_error_t* error
) {
	WSABUF wsabuf = {
		.buf = (void*)buf,
		.len = size > ULONG_MAX ? ULONG_MAX : (ULONG)size,
	};
	DWORD num_bytes_transferred, flags;
	bio_io_req_t req = bio_prepare_io_req();
	if (WSASend(
		socket->socket,
		&wsabuf, 1,
		&num_bytes_transferred,
		0,
		&req.overlapped, NULL
	)) {
		int error_code = WSAGetLastError();
		if (error_code != WSA_IO_PENDING) {
			bio_set_last_wsa_error(error);
			return 0;
		}
		bio_wait_for_io(&req);

		if (!WSAGetOverlappedResult(
			socket->socket,
			&req.overlapped,
			&num_bytes_transferred,
			FALSE,
			&flags
		)) {
			bio_set_last_wsa_error(error);
			return 0;
		}

		return num_bytes_transferred;
	} else {
		bio_raise_signal(req.signal);
		return num_bytes_transferred;
	}
}

size_t
bio_net_ws_recv(
	bio_net_ws_socket_t* socket,
	void* buf,
	size_t size,
	bio_error_t* error
) {
	WSABUF wsabuf = {
		.buf = (void*)buf,
		.len = size > ULONG_MAX ? ULONG_MAX : (ULONG)size,
	};
	DWORD num_bytes_transferred, flags;
	bio_io_req_t req = bio_prepare_io_req();
	if (WSARecv(
		socket->socket,
		&wsabuf, 1,
		&num_bytes_transferred,
		0,
		&req.overlapped, NULL
	)) {
		int error_code = WSAGetLastError();
		if (error_code != WSA_IO_PENDING) {
			bio_set_last_wsa_error(error);
			return 0;
		}
		bio_wait_for_io(&req);

		if (!WSAGetOverlappedResult(
			socket->socket,
			&req.overlapped,
			&num_bytes_transferred,
			FALSE,
			&flags
		)) {
			bio_set_last_wsa_error(error);
			return 0;
		}

		return num_bytes_transferred;
	} else {
		bio_raise_signal(req.signal);
		return num_bytes_transferred;
	}
}
