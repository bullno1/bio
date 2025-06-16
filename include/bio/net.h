#ifndef BIO_NET_H
#define BIO_NET_H

#include "bio.h"

/**
 * @defgroup net Network I/O
 *
 * Read/write over the network.
 *
 * @{
 */

/// Handle to a socket
typedef struct {
	bio_handle_t handle;
} bio_socket_t;

/// Address type
typedef enum {
	/// IPv4
	BIO_ADDR_IPV4,
	/// IPv6
	BIO_ADDR_IPV6,
	/**
	 * On Unix platforms, this will be a Unix domain socket.
	 * On Windows, this will be a named pipe.
	 */
	BIO_ADDR_NAMED,
} bio_addr_type_t;

/// Socket type
typedef enum {
	BIO_SOCKET_STREAM,    /**< Stream socket */
	BIO_SOCKET_DATAGRAM,  /**< Datagram socket */
} bio_socket_type_t;

/// Net address
typedef struct {
	bio_addr_type_t type;  /**< Address type */
	union {
		/// Raw IPv4 address
		char ipv4[4];
		/// Raw IPv6 address
		char ipv6[16];
		/**
		 * "Named" socket address.
		 *
		 * This should always use forward slash (`/`) regardless of platform.
		 *
		 * On Linux, this will be mapped to a unix domain socket.
		 * If the path starts with a `@` or `NULL` (`\0`) character, it will
		 * be treated as an abstract socket.
		 *
		 * On Windows, this will always be implemented as a named pipe which is
		 * similar to an abstract socket in Linux.
		 * The `@` prefix will be ignored.
		 * The prefix `\\.\pipe\` will be prepended.
		 * Forward slashes will be translated into backslashes (`\`)
		 *
		 * Therefore, the most cross-platform and consistent name is to use the
		 * syntax: `@path/name`.
		 */
		struct {
			size_t len;
			char name[256];
		} named;
	};
} bio_addr_t;

/// Port number in host byte order
typedef uint16_t bio_port_t;

/**
 * Any port
 *
 * @see bio_net_listen
 */
static const bio_port_t BIO_PORT_ANY = 0;

/**
 * Any IPv4 address
 *
 * @see bio_net_listen
 */
static const bio_addr_t BIO_ADDR_IPV4_ANY = {
	.type = BIO_ADDR_IPV4,
	.ipv4 = { 0 },
};

/// Local loopback IPv4 address
static const bio_addr_t BIO_ADDR_IPV4_LOOPBACK = {
	.type = BIO_ADDR_IPV4,
	.ipv4 = { 127, 0, 0, 1 },
};

/**
 * Any IPv6 address
 *
 * @see bio_net_listen
 */
static const bio_addr_t BIO_ADDR_IPV6_ANY = {
	.type = BIO_ADDR_IPV6,
	.ipv6 = { 0 },
};

/// Local loopback IPv6 address
static const bio_addr_t BIO_ADDR_IPV6_LOOPBACK = {
	.type = BIO_ADDR_IPV6,
	.ipv6 = { [15] = 1 },
};

/**
 * Wrap a socket handle from the OS into a @ref bio_socket_t
 *
 * This can be used to handle platform-specific special sockets such as netlink
 * socket on Linux.
 *
 * @param sock Pointer to a socket handle.
 *   This will only be assigned when the operation is successful.
 * @param handle The OS socket handle.
 *   On Unix platforms, this is a file descriptor.
 *   On Windows, this is a `SOCKET`/`HANDLE`.
 * @param addr_type The address type of the socket.
 *   This only matters on Windows as it uses two separate sets of APIs:
 *   one for named pipe and one for socket.
 * @param error See @ref error.
 * @return Whether this operation is successful.
 */
bool
bio_net_wrap(
	bio_socket_t* sock,
	uintptr_t handle,
	bio_addr_type_t addr_type,
	bio_error_t* error
);

/**
 * Retrieve an OS handle from a @ref bio_socket_t
 *
 * This can be used to call platform-specific API such as `getsockopt` or `WSAIoctl`
 *
 * For Unix platforms, this is will return a file descriptor.
 * A value of -1 is possible if the socket was closed with @ref bio_net_close.
 *
 * For Windows, this will return a `SOCKET`/`HANDLE`.
 * A value of `INVALID_HANDLE_VALUE`/`INVALID_SOCKET` may be returned.
 *
 * @remarks
 *   It is important not to interfere with bio when manipulating the OS handle
 *   directly.
 *   Please refer to the implementation details for the given platform.
 */
uintptr_t
bio_net_unwrap(bio_socket_t socket);

/**
 * Create a listening socket
 *
 * @param socket_type The socket type
 * @param addr The socket address to listen from
 * @param addr The port number for IP socket.
 *   Ignored for "named" socket.
 * @param sock Pointer to a socket handle.
 *   This is only assigned if the operation is successful.
 * @param error Seee @ref error
 * @return Whether the operation was successful.
 *
 * @see bio_net_accept
 */
bool
bio_net_listen(
	bio_socket_type_t socket_type,
	const bio_addr_t* addr,
	bio_port_t port,
	bio_socket_t* sock,
	bio_error_t* error
);

/**
 * Accept a new connection
 *
 * @param socket A socket handle
 * @param client Pointer to a socket handle to receive the new connection
 * @param error Seee @ref error
 * @return Whether the operation was successful.
 */
bool
bio_net_accept(
	bio_socket_t socket,
	bio_socket_t* client,
	bio_error_t* error
);

/**
 * Make an outgoing connection
 *
 * @param socket_type The socket type
 * @param addr The remote address
 * @param addr The port number for IP socket.
 *   Ignored for "named" socket.
 * @param sock Pointer to a socket handle.
 *   This is only assigned if the operation is successful.
 * @param error Seee @ref error
 * @return Whether the operation was successful.
 */
bool
bio_net_connect(
	bio_socket_type_t socket_type,
	const bio_addr_t* addr,
	bio_port_t port,
	bio_socket_t* socket,
	bio_error_t* error
);

/// Close a socket
bool
bio_net_close(bio_socket_t socket, bio_error_t* error);

/**
 * Send to a socket
 *
 * @remarks Short write is possible
 *
 * @see bio_net_send_exactly
 */
size_t
bio_net_send(
	bio_socket_t socket,
	const void* buf,
	size_t size,
	bio_error_t* error
);

/**
 * Receive from a socket
 *
 * @remarks Short read is possible
 *
 * @see bio_net_recv_exactly
 */
size_t
bio_net_recv(
	bio_socket_t socket,
	void* buf,
	size_t size,
	bio_error_t* error
);

/// Not implemented
size_t
bio_net_sendto(
	bio_socket_t socket,
	const bio_addr_t* addr,
	const void* buf,
	size_t size,
	bio_error_t* error
);

/// Not implemented
size_t
bio_net_recvfrom(
	bio_socket_t socket,
	bio_addr_t* addr,
	void* buf,
	size_t size,
	bio_error_t* error
);

/// Compare two addresses
int
bio_net_address_compare(const bio_addr_t* lhs, const bio_addr_t* rhs);

/// Helper to deal with short write
static inline size_t
bio_net_send_exactly(bio_socket_t socket, const void* buf, size_t size, bio_error_t* error) {
	size_t total_bytes_sent = 0;
	while (total_bytes_sent < size) {
		size_t bytes_sent = bio_net_send(
			socket,
			(char*)buf + total_bytes_sent,
			size - total_bytes_sent,
			error
		);
		if (bytes_sent == 0) { break; }
		total_bytes_sent += bytes_sent;
	}

	return total_bytes_sent;
}

/// Helper to deal with short read
static inline size_t
bio_net_recv_exactly(bio_socket_t socket, void* buf, size_t size, bio_error_t* error) {
	size_t total_bytes_received = 0;
	while (total_bytes_received < size) {
		size_t bytes_received = bio_net_recv(
			socket,
			(char*)buf + total_bytes_received,
			size - total_bytes_received,
			error
		);
		if (bytes_received == 0) { break; }
		total_bytes_received += bytes_received;
	}

	return total_bytes_received;
}

/**@}*/

#endif
