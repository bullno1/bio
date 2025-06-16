#include <bio/bio.h>
#include <bio/net.h>

void writer_entry(void* userdata);

//! [Using handle]
void client_handler(void* userdata) {
	bio_socket_t socket;  // Assume we got this somehow

	// Spawn a writer, giving it the same socket
	bio_coro_t writer = bio_spawn(writer_entry, &socket);

	char buf[1024];
	bio_error_t error = { 0 };
	while (true) {
		// Read from the socket
		size_t bytes_received = bio_net_recv(socket, buf, sizeof(buf), &error);
		if (bio_has_error(&error)) {
			// Break out of the loop if there is an error
			break;
		}

		// Handle message and maybe message writer for a response
	}

	// Potential double free but it is safe
	// This will also cause the writer to terminate
	bio_net_close(socket, NULL);

	// Wait for writer to actually terminate
	bio_join(writer);
}

void writer_entry(void* userdata) {
	bio_socket_t socket = *(bio_socket_t*)userdata;

	bio_error_t error = { 0 };
	while (true) {
		// Get a message from some source
		// Send it
		size_t bytes_sent = bio_net_send(socket, msg, msg_len, &error);
		if (bio_has_error(&error)) {
			// Break out of the loop if there is an error
			break;
		}
	}

	// Potential double free but it is safe
	// This also cause the reader to terminate
	bio_net_close(socket, NULL);
}
//! [Using handle]
