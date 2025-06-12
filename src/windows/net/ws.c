#include "../net.h"
#include "mstcpip.h"

#define bio_set_last_wsa_error(error) bio_set_error(error, WSAGetLastError())

static bio_completion_mode_t
bio_net_ws_completion_mode(SOCKET socket) {
	WSAPROTOCOL_INFO protocol_info;
	int proto_info_len = (int)sizeof(protocol_info);
	if (getsockopt(socket, SOL_SOCKET, SO_PROTOCOL_INFO, (char*)&protocol_info, &proto_info_len) == 0) {
		return (protocol_info.dwServiceFlags1 & XP1_IFS_HANDLES) > 0
			? BIO_COMPLETION_MODE_SKIP_ON_SUCCESS
			: BIO_COMPLETION_MODE_ALWAYS;
	} else {
		return BIO_COMPLETION_MODE_UNKNOWN;
	}
}

static bio_completion_mode_t
bio_net_ws_init_completion_mode(SOCKET socket) {
	bio_completion_mode_t completion_mode = bio_net_ws_completion_mode(socket);
	if (completion_mode == BIO_COMPLETION_MODE_SKIP_ON_SUCCESS) {
		return SetFileCompletionNotificationModes((HANDLE)socket, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)
			? BIO_COMPLETION_MODE_SKIP_ON_SUCCESS
			: BIO_COMPLETION_MODE_ALWAYS;
	} else {
		return BIO_COMPLETION_MODE_ALWAYS;
	}
}

static bool
bio_net_maybe_wait_for_io(
	SOCKET socket, bio_completion_mode_t completion_mode,
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
		bio_maybe_wait_after_success(req, completion_mode);
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
	// Always bind in Windows
	if (bind(handle, addr->addr, addr->addr_len) != 0) {
		bio_set_last_wsa_error(error);
		goto end;
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
		*sock = (bio_net_ws_socket_t){ .handle = handle };
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
	if (socket->completion_mode == BIO_COMPLETION_MODE_UNKNOWN) {
		socket->completion_mode = bio_net_ws_init_completion_mode(socket->handle);
	}

	if (socket->listen_data == NULL) {
		GUID accept_ex_guid = WSAID_ACCEPTEX;
		bio_net_ws_listen_data_t listen_data;
		bio_io_req_t req = bio_prepare_io_req();
		if (!bio_net_maybe_wait_for_io(
			socket->handle, socket->completion_mode,
			&req, error,
			WSAIoctl(
				socket->handle,
				SIO_GET_EXTENSION_FUNCTION_POINTER,
				&accept_ex_guid, sizeof(accept_ex_guid),
				&listen_data.accept_fn, sizeof(listen_data.accept_fn),
				&num_bytes,
				&req.overlapped, NULL
			) == 0
		)) {
			return false;
		}

		listen_data.sockaddr_len = (int)sizeof(listen_data.sockaddr);
		if (getsockname(
			socket->handle,
			(struct sockaddr*)&listen_data.sockaddr, &listen_data.sockaddr_len
		)) {
			bio_set_last_wsa_error(error);
			return false;
		}

		int sock_type_len = (int)sizeof(listen_data.sock_type);
		if (getsockopt(
			socket->handle,
			SOL_SOCKET, SO_TYPE,
			(char*)&listen_data.sock_type, &sock_type_len
		)) {
			bio_set_last_wsa_error(error);
			return false;
		}

		socket->listen_data = bio_malloc(sizeof(bio_net_ws_listen_data_t));
		*(socket->listen_data) = listen_data;
	}

	SOCKET client_socket = WSASocketA(
		socket->listen_data->sockaddr.ss_family,
		socket->listen_data->sock_type,
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
		socket->handle, socket->completion_mode,
		&req, error,
		socket->listen_data->accept_fn(
			socket->handle,
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
		(char*)&socket->handle, sizeof(socket->handle)
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
		*client = (bio_net_ws_socket_t){ .handle = client_socket };
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
	bio_completion_mode_t completion_mode = BIO_COMPLETION_MODE_UNKNOWN;

	SOCKADDR_STORAGE bind_addr;
	bind_addr.ss_family = addr->addr->sa_family;
	INETADDR_SETANY((struct sockaddr*)&bind_addr);
	if (bind(handle, (struct sockaddr*)&bind_addr, (int)sizeof(bind_addr)) != 0) {
		bio_set_last_wsa_error(error);
		goto end;
	}

	completion_mode = bio_net_ws_init_completion_mode(handle);

	if (CreateIoCompletionPort((HANDLE)handle, bio_ctx.platform.iocp, 0, 0) == NULL) {
		bio_set_last_error(error);
		goto end;
	}

	LPFN_CONNECTEX connect_ex;
	GUID connect_ex_guid = WSAID_CONNECTEX;
	DWORD num_bytes;
	bio_io_req_t req = bio_prepare_io_req();
	if (!bio_net_maybe_wait_for_io(
		handle, completion_mode,
		&req, error,
		WSAIoctl(
			handle,
			SIO_GET_EXTENSION_FUNCTION_POINTER,
			&connect_ex_guid, sizeof(connect_ex_guid),
			&connect_ex, sizeof(connect_ex),
			&num_bytes,
			&req.overlapped, NULL
		) == 0
	)) {
		goto end;
	}

	req = bio_prepare_io_req();
	if (!bio_net_maybe_wait_for_io(
		handle, completion_mode,
		&req, error,
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

	success = true;
end:
	if (BIO_LIKELY(success)) {
		*socket = (bio_net_ws_socket_t){
			.handle = handle,
			.completion_mode = completion_mode,
		};
	} else {
		closesocket(handle);
	}

	return success;
}

bool
bio_net_ws_close(bio_net_ws_socket_t* socket, bio_error_t* error) {
	SOCKET handle = socket->handle;
	CancelIo((HANDLE)handle);
	shutdown(handle, SD_BOTH);
	bio_free(socket->listen_data);
	if (closesocket(handle) == 0) {
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
	if (socket->completion_mode == BIO_COMPLETION_MODE_UNKNOWN) {
		socket->completion_mode = bio_net_ws_init_completion_mode(socket->handle);
	}

	WSABUF wsabuf = {
		.buf = (void*)buf,
		.len = size > ULONG_MAX ? ULONG_MAX : (ULONG)size,
	};
	DWORD num_bytes_transferred, flags;
	bio_io_req_t req = bio_prepare_io_req();
	if (WSASend(
		socket->handle,
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
			socket->handle,
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
		bio_maybe_wait_after_success(&req, socket->completion_mode);
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
	if (socket->completion_mode == BIO_COMPLETION_MODE_UNKNOWN) {
		socket->completion_mode = bio_net_ws_init_completion_mode(socket->handle);
	}

	WSABUF wsabuf = {
		.buf = (void*)buf,
		.len = size > ULONG_MAX ? ULONG_MAX : (ULONG)size,
	};
	DWORD num_bytes_transferred;
	DWORD flags = 0;
	bio_io_req_t req = bio_prepare_io_req();
	if (WSARecv(
		socket->handle,
		&wsabuf, 1,
		&num_bytes_transferred,
		&flags,
		&req.overlapped, NULL
	)) {
		int error_code = WSAGetLastError();
		if (error_code != WSA_IO_PENDING) {
			bio_set_last_wsa_error(error);
			return 0;
		}
		bio_wait_for_io(&req);

		if (!WSAGetOverlappedResult(
			socket->handle,
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
		bio_maybe_wait_after_success(&req, socket->completion_mode);
		return num_bytes_transferred;
	}
}
