#include "../net.h"

#define BIO_NET_PIPE_NAME_PREFIX "\\\\.\\pipe"

static HANDLE
bio_net_pipe_create(const char* name, int sock_type, DWORD extra_open_flags) {
	return CreateNamedPipeA(
		name,
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | extra_open_flags,
		(sock_type == SOCK_STREAM ? PIPE_TYPE_BYTE : PIPE_TYPE_MESSAGE)
		| (sock_type == SOCK_STREAM ? PIPE_READMODE_BYTE : PIPE_READMODE_MESSAGE)
		| PIPE_WAIT
		| PIPE_REJECT_REMOTE_CLIENTS,
		PIPE_UNLIMITED_INSTANCES,
		1024, 1024,
		0,
		NULL
	);
}

static bio_completion_mode_t
bio_net_pipe_init_completion_mode(HANDLE socket) {
	return SetFileCompletionNotificationModes(socket, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)
		? BIO_COMPLETION_MODE_SKIP_ON_SUCCESS
		: BIO_COMPLETION_MODE_ALWAYS;
}

bool
bio_net_pipe_listen(
	bio_socket_type_t socket_type,
	const bio_addr_translation_result_t* addr,
	bio_net_pipe_socket_t* sock,
	bio_error_t* error
) {
	HANDLE handle = bio_net_pipe_create(
		addr->storage.named_pipe.name,
		socket_type,
		FILE_FLAG_FIRST_PIPE_INSTANCE
	);
	if (handle == INVALID_HANDLE_VALUE) {
		bio_set_last_error(error);
		return false;
	}

	*sock = (bio_net_pipe_socket_t){
		.handle = handle,
	};
	return true;
}

bool
bio_net_pipe_accept(
	bio_net_pipe_socket_t* socket,
	bio_net_pipe_socket_t* client,
	bio_error_t* error
) {
	// Init lazily so we can accept a handle through bio_net_wrap_handle
	if (socket->listen_data == NULL) {
		bio_net_pipe_listen_data_t listen_data;
		size_t name_prefix_len = sizeof(BIO_NET_PIPE_NAME_PREFIX) - 1;
		memcpy(listen_data.name, BIO_NET_PIPE_NAME_PREFIX, name_prefix_len);
		char* name_buf = listen_data.name + name_prefix_len;

		_Alignas(FILE_NAME_INFO) char name_info_buf[512];
		if (!GetFileInformationByHandleEx(
			socket->handle,
			FileNameInfo, name_info_buf, sizeof(name_info_buf)
		)) {
			bio_set_last_error(error);
			return false;
		}
		FILE_NAME_INFO* name_info = (FILE_NAME_INFO*)name_info_buf;
		int num_chars = WideCharToMultiByte(
			CP_UTF8, 0,
			name_info->FileName, name_info->FileNameLength / sizeof(wchar_t),
			name_buf, (int)(sizeof(listen_data.name) - name_prefix_len),
			NULL, NULL
		);
		if (num_chars == 0) {
			bio_set_last_error(error);
			return false;
		} else if (num_chars >= sizeof(listen_data.name)) {
			bio_set_error(error, ERROR_NOT_SUPPORTED);
			return false;
		}
		listen_data.name[num_chars] = '\0';

		DWORD flags;
		if (!GetNamedPipeInfo(socket->handle, &flags, NULL, NULL, NULL)) {
			bio_set_last_error(error);
			return false;
		}
		listen_data.sock_type = (flags & PIPE_TYPE_MESSAGE) > 0 ? SOCK_DGRAM : SOCK_STREAM;

		socket->listen_data = bio_malloc(sizeof(bio_net_pipe_listen_data_t));
		*(socket->listen_data) = listen_data;
		BIO_LIST_INIT(&socket->acceptors);
	}

	HANDLE new_instance = bio_net_pipe_create(
		socket->listen_data->name,
		socket->listen_data->sock_type,
		0
	);
	if (new_instance == INVALID_HANDLE_VALUE) {
		bio_set_last_error(error);
		return false;
	}

	HANDLE client_sock = socket->handle;
	socket->handle = new_instance;
	bio_completion_mode_t completion_mode = socket->completion_mode;
	socket->completion_mode = BIO_COMPLETION_MODE_UNKNOWN;

	if (completion_mode == BIO_COMPLETION_MODE_UNKNOWN) {
		completion_mode = bio_net_pipe_init_completion_mode(client_sock);
	}

	// Create an entry in the acceptor list so we can be cancelled
	bool success = false;
	bio_net_pipe_acceptor_t acceptor = { .handle = client_sock };
	if (CreateIoCompletionPort(client_sock, bio_ctx.platform.iocp, 0, 0) == NULL) {
		bio_set_last_error(error);
		goto end;
	}

	BIO_LIST_APPEND(&socket->acceptors, &acceptor.link);
	bio_io_req_t req = bio_prepare_io_req();
	DWORD num_bytes;
	if (!ConnectNamedPipe(client_sock, &req.overlapped)) {
		int error_code = GetLastError();
		if (error_code == ERROR_IO_PENDING) {
			bio_wait_for_io(&req);
			if (GetOverlappedResult(client_sock, &req.overlapped, &num_bytes, FALSE)) {
				error_code = ERROR_SUCCESS;
			} else {
				error_code = GetLastError();
			}
		}

		if (error_code != ERROR_PIPE_CONNECTED && error_code != ERROR_SUCCESS) {
			bio_set_last_error(error);
			goto end;
		}
	} else {
		bio_maybe_wait_after_success(&req, completion_mode);
	}
	BIO_LIST_REMOVE(&acceptor.link);
	if (!bio_has_error(error) && acceptor.cancelled) {
		bio_set_error(error, ERROR_CANCELLED);
		goto end;
	}

	success = true;
end:
	if (success) {
		*client = (bio_net_pipe_socket_t){
			.handle = client_sock,
			.completion_mode = completion_mode,
		};
	} else {
		if (!acceptor.cancelled) {
			CloseHandle(client_sock);
		}
	}

	return success;
}

bool
bio_net_pipe_connect(
	bio_socket_type_t socket_type,
	const bio_addr_translation_result_t* addr,
	bio_net_pipe_socket_t* socket,
	bio_error_t* error
) {
	HANDLE handle = CreateFile(
		addr->storage.named_pipe.name,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED,
		NULL
	);
	if (handle == INVALID_HANDLE_VALUE) {
		bio_set_last_error(error);
		return false;
	}

	bool success = false;
	DWORD mode = (socket_type == SOCK_DGRAM ? PIPE_READMODE_MESSAGE : PIPE_READMODE_BYTE) | PIPE_WAIT;
	if (!SetNamedPipeHandleState(handle, &mode, NULL, NULL)) {
		bio_set_last_error(error);
		goto end;
	}

	if (CreateIoCompletionPort(handle, bio_ctx.platform.iocp, 0, 0) == NULL) {
		bio_set_last_error(error);
		goto end;
	}

	success = true;
end:
	if (success) {
		*socket = (bio_net_pipe_socket_t){ .handle = handle };
	} else {
		CloseHandle(handle);
	}

	return success;
}

bool
bio_net_pipe_close(bio_net_pipe_socket_t* socket, bio_error_t* error) {
	while (!BIO_LIST_IS_EMPTY(&socket->acceptors)) {
		bio_net_pipe_acceptor_link_t* link = socket->acceptors.next;
		bio_net_pipe_acceptor_t* acceptor = BIO_CONTAINER_OF(link, bio_net_pipe_acceptor_t, link);
		BIO_LIST_REMOVE(&acceptor->link);

		acceptor->cancelled = true;
		CancelIo(acceptor->handle);
		DisconnectNamedPipe(acceptor->handle);
		CloseHandle(acceptor->handle);
	}

	CancelIo(socket->handle);
	DisconnectNamedPipe(socket->handle);
	bio_free(socket->listen_data);
	if (CloseHandle(socket->handle)) {
		return true;
	} else {
		bio_set_last_error(error);
		return false;
	}
}

size_t
bio_net_pipe_send(
	bio_net_pipe_socket_t* socket,
	const void* buf,
	size_t size,
	bio_error_t* error
) {
	if (socket->completion_mode == BIO_COMPLETION_MODE_UNKNOWN) {
		socket->completion_mode = bio_net_pipe_init_completion_mode(socket->handle);
	}

	DWORD buf_size = size > MAXDWORD ? MAXDWORD : (DWORD)size;
	DWORD num_bytes_transferred;
	bio_io_req_t req = bio_prepare_io_req();
	if (!WriteFile(
		socket->handle,
		buf, buf_size,
		&num_bytes_transferred, &req.overlapped
	)) {
		int error_code = GetLastError();
		if (error_code != ERROR_IO_PENDING) {
			bio_set_error(error, error_code);
			return 0;
		}
		bio_wait_for_io(&req);
		if (!GetOverlappedResult(socket->handle, &req.overlapped, &num_bytes_transferred, FALSE)) {
			bio_set_last_error(error);
			return 0;
		}
		return num_bytes_transferred;
	} else {
		bio_maybe_wait_after_success(&req, socket->completion_mode);
		return num_bytes_transferred;
	}
}

size_t
bio_net_pipe_recv(
	bio_net_pipe_socket_t* socket,
	void* buf,
	size_t size,
	bio_error_t* error
) {
	if (socket->completion_mode == BIO_COMPLETION_MODE_UNKNOWN) {
		socket->completion_mode = bio_net_pipe_init_completion_mode(socket->handle);
	}

	DWORD buf_size = size > MAXDWORD ? MAXDWORD : (DWORD)size;
	DWORD num_bytes_transferred;
	bio_io_req_t req = bio_prepare_io_req();
	if (!ReadFile(
		socket->handle,
		buf, buf_size,
		&num_bytes_transferred, &req.overlapped
	)) {
		int error_code = GetLastError();
		if (error_code != ERROR_IO_PENDING) {
			bio_set_error(error, error_code);
			return 0;
		}
		bio_wait_for_io(&req);
		if (!GetOverlappedResult(socket->handle, &req.overlapped, &num_bytes_transferred, FALSE)) {
			bio_set_last_error(error);
			return 0;
		}
		return num_bytes_transferred;
	} else {
		bio_maybe_wait_after_success(&req, socket->completion_mode);
		return num_bytes_transferred;
	}
}
