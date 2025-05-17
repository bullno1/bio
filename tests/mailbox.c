#include "common.h"
#include <bio/mailbox.h>

static suite_t mailbox = {
	.name = "mailbox",
	.init_per_test = init_bio,
	.cleanup_per_test = cleanup_bio,
};

typedef BIO_MAILBOX(int) int_mailbox_t;

static void
send_recv_receiver(void* userdata) {
	int_mailbox_t mailbox = *(int_mailbox_t*)userdata;

	int msg;
	CHECK(bio_recv_message(mailbox, &msg), "Could not receive");
	CHECK(msg == 69, "Corrupted message");
	CHECK(!bio_can_recv_message(mailbox), "Invalid state");
}

static void
send_recv_sender(void* userdata) {
	int_mailbox_t mailbox = *(int_mailbox_t*)userdata;

	CHECK(bio_send_message(mailbox, (int){ 69 }), "Could not send");
}

TEST(mailbox, send_recv) {
	int_mailbox_t mailbox;
	bio_open_mailbox(&mailbox, 4);

	bio_spawn(send_recv_receiver, &mailbox);
	bio_spawn(send_recv_sender, &mailbox);

	bio_loop();

	bio_close_mailbox(mailbox);
}

TEST(mailbox, full) {
	int_mailbox_t mailbox;
	bio_open_mailbox(&mailbox, 4);

	CHECK(bio_send_message(mailbox, (int){ 69 }), "Could not send");
	CHECK(bio_send_message(mailbox, (int){ 69 }), "Could not send");
	CHECK(bio_send_message(mailbox, (int){ 69 }), "Could not send");
	CHECK(bio_send_message(mailbox, (int){ 69 }), "Could not send");
	CHECK(!bio_send_message(mailbox, (int){ 69 }), "Overflow");
	CHECK(!bio_can_send_message(mailbox), "Overflow");
}

void
close_receiver(void* userdata) {
	int_mailbox_t mailbox = *(int_mailbox_t*)userdata;

	int msg;
	CHECK(!bio_recv_message(mailbox, &msg), "Invalid state");
	CHECK(!bio_can_recv_message(mailbox), "Invalid state");
}

void
close_sender(void* userdata) {
	int_mailbox_t mailbox = *(int_mailbox_t*)userdata;

	CHECK(bio_can_send_message(mailbox), "Invalid state");
	bio_close_mailbox(mailbox);
	CHECK(!bio_can_recv_message(mailbox), "Invalid state");
	CHECK(!bio_can_send_message(mailbox), "Invalid state");
}

TEST(mailbox, close) {
	int_mailbox_t mailbox;
	bio_open_mailbox(&mailbox, 4);

	bio_spawn(close_receiver, &mailbox);
	bio_spawn(close_sender, &mailbox);

	bio_loop();
}
