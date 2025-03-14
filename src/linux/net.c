#include "common.h"
#include <bio/net.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>

const bio_tag_t BIO_SOCKET_HANDLE = BIO_TAG_INIT("bio.handle.socket");

typedef struct {
	int fd;
} bio_socket_impl_t;

bool
bio_net_listen(
	bio_socket_type_t socket_type,
	const bio_addr_t* addr,
	uint16_t port,
	bio_socket_t* sock,
	bio_error_t* error
) {
	int family;
	struct sockaddr* bind_addr = NULL;
	socklen_t addr_len;

	struct sockaddr_in ipv4_addr;
	_Static_assert(sizeof(ipv4_addr.sin_addr) == sizeof(addr->addr.ipv4), "Address size mismatch");

	struct sockaddr_in6 ipv6_addr;
	_Static_assert(sizeof(ipv6_addr.sin6_addr) == sizeof(addr->addr.ipv6), "Address size mismatch");

	struct sockaddr_un named_addr;

	switch (addr->type) {
		case BIO_ADDR_IPV4:
			family = ipv4_addr.sin_family =  AF_INET;
			if (bio_net_address_compare(addr, &BIO_ADDR_IPV4_ANY) != 0 || port != BIO_PORT_ANY) {
				memset(&ipv4_addr, 0, sizeof(ipv4_addr));
				ipv4_addr.sin_port = htons(port);
				memcpy(&ipv4_addr.sin_addr, addr->addr.ipv4, sizeof(ipv4_addr.sin_addr));
				bind_addr = (struct sockaddr*)&ipv4_addr;
				addr_len = sizeof(ipv4_addr);
			}
			break;
		case BIO_ADDR_IPV6:
			family = ipv6_addr.sin6_family = AF_INET6;
			if (bio_net_address_compare(addr, &BIO_ADDR_IPV6_ANY) != 0 || port != BIO_PORT_ANY) {
				memset(&ipv6_addr, 0, sizeof(ipv6_addr));
				ipv6_addr.sin6_port = htons(port);
				memcpy(&ipv6_addr.sin6_addr, addr->addr.ipv6, sizeof(ipv6_addr.sin6_addr));
				bind_addr = (struct sockaddr*)&ipv6_addr;
				addr_len = sizeof(ipv6_addr);
			}
			break;
		case BIO_ADDR_NAMED:
			named_addr.sun_family = family = AF_UNIX;
			if (BIO_LIKELY(0 < addr->addr.named.len && addr->addr.named.len < sizeof(named_addr.sun_path))) {
				memset(&named_addr.sun_path, 0, sizeof(named_addr.sun_path));
				memcpy(named_addr.sun_path, addr->addr.named.name, addr->addr.named.len);
				if (named_addr.sun_path[0] == '@') {
					named_addr.sun_path[0] = '\0';
				}
				bind_addr = (struct sockaddr*)&named_addr;
				addr_len = sizeof(named_addr);
			} else {
				bio_set_errno(error, EINVAL);
				return false;
			}
			break;
	}

	int type;
	switch (socket_type) {
		case BIO_SOCKET_STREAM:
			type = SOCK_STREAM;
			break;
		case BIO_SOCKET_DATAGRAM:
			type = SOCK_DGRAM;
			break;
	}

	uint32_t flags;
	int result;
	struct io_uring_sqe* sqe;

	int fd;
	sqe = bio_acquire_io_req();
	io_uring_prep_socket(sqe, family, type | SOCK_NONBLOCK, 0, 0);
	fd = result = bio_submit_io_req(sqe, &flags);
	if (fd < 0) {
		bio_set_errno(error, -result);
		return false;
	}

	if (bind_addr != NULL) {
		sqe = bio_acquire_io_req();
		io_uring_prep_bind(sqe, fd, bind_addr, addr_len);
		result = bio_submit_io_req(sqe, &flags);
		if (result < 0) {
			bio_set_errno(error, -result);
			bio_io_close(fd);
		}
		return false;
	}

	// TODO: make configurable
	sqe = bio_acquire_io_req();
	io_uring_prep_listen(sqe, fd, 5);
	result = bio_submit_io_req(sqe, &flags);
	if (result < 0) {
		bio_set_errno(error, -result);
		bio_io_close(fd);
		return false;
	}

	bio_socket_impl_t* sock_impl = bio_malloc(sizeof(bio_socket_impl_t));
	*sock_impl = (bio_socket_impl_t){
		.fd = fd,
	};
	*sock = (bio_socket_t){
		.handle = bio_make_handle(sock_impl, &BIO_SOCKET_HANDLE),
	};
	error->tag = NULL;
	return true;
}

bool
bio_net_close(bio_socket_t socket, bio_error_t* error) {
	bio_socket_impl_t* impl = bio_close_handle(socket.handle, &BIO_SOCKET_HANDLE);
	if (BIO_LIKELY(impl != NULL)) {
		int fd = impl->fd;
		bio_free(impl);
		return bio_io_close(fd) > 0;
	} else {
		bio_set_errno(error, EINVAL);
		return false;
	}
}
