#include "common.h"
#include <bio/net.h>
#include <string.h>

#ifdef __FreeBSD__
#include <unistd.h>
#endif

static suite_t net = {
	.name = "net",
	.init_per_test = init_bio,
	.cleanup_per_test = cleanup_bio,
};

static void
net_test_listen_ipv4(void* userdata) {
	bio_socket_t socket;
	bio_error_t error = { 0 };
	bio_net_listen(
		BIO_SOCKET_STREAM,
		&BIO_ADDR_IPV4_ANY,
		BIO_PORT_ANY,
		&socket,
		&error
	);
	CHECK_NO_ERROR(error);

	bio_net_close(socket, &error);
}

static void
net_test_listen_ipv6(void* userdata) {
	bio_socket_t socket;
	bio_error_t error = { 0 };
	bio_net_listen(
		BIO_SOCKET_STREAM,
		&BIO_ADDR_IPV6_ANY,
		BIO_PORT_ANY,
		&socket,
		&error
	);
	CHECK_NO_ERROR(error);

	bio_net_close(socket, &error);
}

#ifdef __FreeBSD__
#	define SOCKET_PATH "/tmp/bio_test"
#else
#	define SOCKET_PATH "@bio/test"
#endif

static void
net_test_listen_named(void* userdata) {
#ifdef __FreeBSD__
	unlink(SOCKET_PATH);
#endif
	bio_addr_t address = {
		.type = BIO_ADDR_NAMED,
		.named = {
			.len = sizeof(SOCKET_PATH) - 1,
			.name = SOCKET_PATH,
		},
	};

	bio_socket_t socket;
	bio_error_t error = { 0 };
	bio_net_listen(
		BIO_SOCKET_STREAM,
		&address,
		BIO_PORT_ANY,
		&socket,
		&error
	);
	CHECK_NO_ERROR(error);

	bio_net_close(socket, &error);
}

TEST(net, listen) {
	bio_spawn(net_test_listen_ipv4, NULL);
	bio_spawn(net_test_listen_ipv6, NULL);
	bio_spawn(net_test_listen_named, NULL);

	bio_loop();
}

typedef struct {
	bool shutdown_initiated;
	bio_socket_t server_socket;
} echo_fixture_t;

typedef struct {
	bio_signal_t started;
	bio_socket_t socket;
} echo_handler_args_t;

#ifdef __FreeBSD__
#define ECHO_SOCKET_PATH "/tmp/bio-test-echo"
#else
#define ECHO_SOCKET_PATH "@bio/test/echo"
#endif

static void
net_test_echo_client(void* userdata) {
	echo_fixture_t* fixture = userdata;

	bio_addr_t address = {
		.type = BIO_ADDR_NAMED,
		.named = {
			.len = sizeof(ECHO_SOCKET_PATH) - 1,
			.name = ECHO_SOCKET_PATH,
		},
	};

	bio_socket_t socket;
	bio_error_t error = { 0 };
	bio_net_connect(
		BIO_SOCKET_STREAM,
		&address,
		BIO_PORT_ANY,
		&socket,
		&error
	);
	CHECK_NO_ERROR(error);

	const char* message = "Hello world";
	bio_net_send_exactly(socket, message, strlen(message), &error);
	CHECK_NO_ERROR(error);

	char buf[1024];
	size_t bytes_received = bio_net_recv(socket, buf, sizeof(buf), &error);
	CHECK_NO_ERROR(error);

	CHECK(bytes_received == strlen(message), "Invalid echo");
	CHECK(memcmp(message, buf, bytes_received) == 0, "Invalid echo");

	// Terminate handler
	bio_net_close(socket, NULL);

	// Terminate server
	fixture->shutdown_initiated = true;
	bio_net_close(fixture->server_socket, &error);
	CHECK_NO_ERROR(error);
}

static void
net_test_echo_handler(void* userdata) {
	// Copy args to our own stack
	echo_handler_args_t args = *(echo_handler_args_t*)userdata;
	// Tell the parent that we started
	bio_raise_signal(args.started);

	char buf[1024];
	while (true) {
		bio_error_t error = { 0 };
		size_t received = bio_net_recv(args.socket, buf, sizeof(buf), &error);
		if (bio_has_error(&error)) {
			LOG_BIO_ERROR(error);
			break;
		}
		if (received == 0) {
			break;
		}

		// TODO: handle short write
		bio_net_send(args.socket, buf, received, &error);
		if (bio_has_error(&error)) {
			LOG_BIO_ERROR(error);
			break;
		}
	}

	bio_net_close(args.socket, NULL);
}

static void
net_test_echo_server(void* userdata) {
#ifdef __FreeBSD__
	unlink(ECHO_SOCKET_PATH);
#endif
	echo_fixture_t* fixture = userdata;

	bio_addr_t address = {
		.type = BIO_ADDR_NAMED,
		.named = {
			.len = sizeof(ECHO_SOCKET_PATH) - 1,
			.name = ECHO_SOCKET_PATH,
		},
	};

	bio_socket_t server_socket;
	bio_error_t error = { 0 };
	bio_net_listen(
		BIO_SOCKET_STREAM,
		&address,
		BIO_PORT_ANY,
		&server_socket,
		&error
	);
	CHECK_NO_ERROR(error);

	fixture->server_socket = server_socket;
	bio_spawn(net_test_echo_client, fixture);

	// Accept until killed
	while (true) {
		bio_socket_t client;

		if (!bio_net_accept(server_socket, &client, &error)) {
			if (!fixture->shutdown_initiated) {
				LOG_BIO_ERROR(error);
			}
			break;
		}

		echo_handler_args_t args = {
			.socket = client,
			.started = bio_make_signal(),
		};
		bio_spawn(net_test_echo_handler, &args);
		// Wait for child to start before continuing
		// Alternatively, args can be allocated on the heap
		bio_wait_for_signals(&args.started, 1, true);
	}

	bio_net_close(server_socket, NULL);
}

TEST(net, echo) {
	echo_fixture_t fixture = { 0 };
	bio_spawn(net_test_echo_server, &fixture);

	bio_loop();
}

static void
net_test_echo_tcp_client(void* userdata) {
	echo_fixture_t* fixture = userdata;

	bio_addr_t address = BIO_ADDR_IPV4_LOOPBACK;

	bio_socket_t socket;
	bio_error_t error = { 0 };
	bio_net_connect(
		BIO_SOCKET_STREAM,
		&address,
		8088,
		&socket,
		&error
	);
	CHECK_NO_ERROR(error);

	// TODO: handle short write
	const char* message = "Hello world";
	bio_net_send(socket, message, strlen(message), &error);
	CHECK_NO_ERROR(error);

	// TODO: handle short read
	char buf[1024];
	size_t bytes_received = bio_net_recv(socket, buf, sizeof(buf), &error);
	CHECK_NO_ERROR(error);

	CHECK(bytes_received == strlen(message), "Invalid echo");
	CHECK(memcmp(message, buf, bytes_received) == 0, "Invalid echo");

	// Terminate handler
	bio_net_close(socket, NULL);

	// Terminate server
	fixture->shutdown_initiated = true;
	bio_net_close(fixture->server_socket, &error);
	CHECK_NO_ERROR(error);
}

BIO_TEST(net, tcp_echo) {
	bio_addr_t address = BIO_ADDR_IPV4_ANY;

	bio_socket_t server_socket;
	bio_error_t error = { 0 };
	bio_net_listen(
		BIO_SOCKET_STREAM,
		&address,
		8088,
		&server_socket,
		&error
	);
	CHECK_NO_ERROR(error);
	echo_fixture_t fixture = {
		.server_socket = server_socket,
	};
	bio_spawn(net_test_echo_tcp_client, &fixture);

	// Accept until killed
	while (true) {
		bio_socket_t client;

		if (!bio_net_accept(server_socket, &client, &error)) {
			if (!fixture.shutdown_initiated) {
				LOG_BIO_ERROR(error);
			}
			break;
		}

		echo_handler_args_t args = {
			.socket = client,
			.started = bio_make_signal(),
		};
		bio_spawn(net_test_echo_handler, &args);
		// Wait for child to start before continuing
		// Alternatively, args can be allocated on the heap
		bio_wait_for_signals(&args.started, 1, true);
	}

	bio_net_close(server_socket, NULL);
}
