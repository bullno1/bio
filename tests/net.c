#include "common.h"
#include <bio/net.h>

static suite_t net = {
	.name = "net",
	.init = init_bio,
	.cleanup = cleanup_bio,
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

TEST(net, listen) {
	bio_spawn(net_test_listen_ipv4, NULL);
	bio_spawn(net_test_listen_ipv6, NULL);

	bio_loop();
}
