#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <bio/bio.h>
#include <bio/net.h>
#include <bio/log.h>

static void*
stdlib_realloc(void* ptr, size_t size, void* ctx) {
	if (size == 0) {
		free(ptr);
		return NULL;
	} else {
		return realloc(ptr, size);
	}
}

static void
echo_handler(void* userdata) {
	printf("Started handler\n");

	uintptr_t data = (uintptr_t)userdata;
	bio_socket_t socket;
	memcpy(&socket, &data, sizeof(socket));

	bio_error_t error = { 0 };
	while (true) {
		char buf[1024];
		size_t bytes_received = bio_net_recv(socket, buf, sizeof(buf), &error);
		if (bio_has_error(&error)) {
			fprintf(stderr, BIO_ERROR_FMT "\n", BIO_ERROR_FMT_ARGS(&error));
			break;
		}
		if (bytes_received == 0) {
			printf("Connection closed\n");
			break;
		}

		// Loop to deal with short write
		char* send_buf = buf;
		while (bytes_received > 0) {
			size_t bytes_sent = bio_net_send(socket, send_buf, bytes_received, &error);
			if (bio_has_error(&error)) {
				fprintf(stderr, BIO_ERROR_FMT "\n", BIO_ERROR_FMT_ARGS(&error));
				break;
			}
			bytes_received -= bytes_sent;
			send_buf += bytes_sent;
		}
	}

	bio_clear_error(&error);
	if (!bio_net_close(socket, &error)) {
		fprintf(stderr, BIO_ERROR_FMT "\n", BIO_ERROR_FMT_ARGS(&error));
	}

	printf("Stopped handler\n");
}

static void
echo_server(void* userdata) {
	uint16_t port = *(uint16_t*)userdata;

	bio_socket_t server_socket;
	bio_error_t error = { 0 };
	if (!bio_net_listen(
		BIO_SOCKET_STREAM,
		&BIO_ADDR_IPV4_ANY,
		port,
		&server_socket,
		&error
	)) {
		fprintf(stderr, BIO_ERROR_FMT "\n", BIO_ERROR_FMT_ARGS(&error));
		return;
	}
	printf("Started server\n");

	while (true) {
		bio_error_t error = { 0 };
		bio_socket_t client;

		if (!bio_net_accept(server_socket, &client, &error)) {
			fprintf(stderr, BIO_ERROR_FMT "\n", BIO_ERROR_FMT_ARGS(&error));
			break;
		}

		uintptr_t ptr;
		_Static_assert(sizeof(ptr) >= sizeof(client), "Handle is bigger than pointer");
		memcpy(&ptr, &client, sizeof(client));
		bio_spawn(echo_handler, (void*)ptr);
	}

	bio_clear_error(&error);
	if (!bio_net_close(server_socket, &error)) {
		fprintf(stderr, BIO_ERROR_FMT "\n", BIO_ERROR_FMT_ARGS(&error));
	}
}

int
main(int argc, const char* argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: echo <port>\n");
		return 1;
	}

	char* end = NULL;
	errno = 0;
	long port_number = strtol(argv[1], &end, 10);
	if (errno != 0 || port_number < 0 || port_number > UINT16_MAX || *end != '\0') {
		fprintf(stderr, "Invalid port value: %s\n", argv[1]);
		return 1;
	}
	uint16_t port = (uint16_t)port_number;

	bio_init((bio_options_t){
		.realloc = stdlib_realloc,
	});

	bio_spawn(echo_server, &port);

	bio_loop();

	bio_terminate();
}
