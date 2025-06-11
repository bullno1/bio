#include "common.h"

static suite_t coro = {
	.name = "coro",
	.init_per_test = init_bio,
	.cleanup_per_test = cleanup_bio,
};

typedef struct {
	bio_coro_t coro_a;
	bio_coro_t coro_b;
	int counter;
} yield_ctx_t;

static void
yield_a_entry(void* userdata) {
	yield_ctx_t* ctx = userdata;
	CHECK(bio_coro_state(ctx->coro_a) == BIO_CORO_RUNNING, "Invalid coro state");
	CHECK(bio_coro_state(ctx->coro_b) == BIO_CORO_READY, "Invalid coro state");
	CHECK(bio_handle_compare(ctx->coro_a.handle, bio_current_coro().handle) == 0, "Invalid current coro");

	CHECK(ctx->counter++ == 0, "Invalid scheduling");
	bio_yield();
	CHECK(ctx->counter++ == 2, "Invalid scheduling");
	bio_yield();

	CHECK(bio_coro_state(ctx->coro_b) == BIO_CORO_DEAD, "Invalid coro state");
}

static void
yield_b_entry(void* userdata) {
	yield_ctx_t* ctx = userdata;
	CHECK(bio_coro_state(ctx->coro_a) == BIO_CORO_READY, "Invalid coro state");
	CHECK(bio_coro_state(ctx->coro_b) == BIO_CORO_RUNNING, "Invalid coro state");
	CHECK(bio_handle_compare(ctx->coro_b.handle, bio_current_coro().handle) == 0, "Invalid current coro");

	CHECK(ctx->counter++ == 1, "Invalid scheduling");
	bio_yield();
	CHECK(ctx->counter++ == 3, "Invalid scheduling");
}

TEST(coro, yield) {
	yield_ctx_t ctx = { 0 };

	ctx.coro_a = bio_spawn(yield_a_entry, &ctx);
	ctx.coro_b = bio_spawn(yield_b_entry, &ctx);

	bio_loop();

	CHECK(bio_coro_state(ctx.coro_a) == BIO_CORO_DEAD, "Invalid coro state");
	CHECK(bio_coro_state(ctx.coro_b) == BIO_CORO_DEAD, "Invalid coro state");
}

typedef struct {
	bio_coro_t main_coro;
	bio_signal_t signals[2];
	int counter;
} signal_ctx_t;

static void
signal_all_a_entry(void* userdata) {
	signal_ctx_t* ctx = userdata;
	bio_coro_t main_coro = ctx->main_coro;

	CHECK(bio_coro_state(main_coro) == BIO_CORO_WAITING, "Invalid coro state");
	CHECK(ctx->counter++ == 0, "Invalid scheduling");
	CHECK(bio_coro_state(main_coro) == BIO_CORO_WAITING, "Invalid coro state");

	bio_raise_signal(ctx->signals[0]);
	CHECK(bio_coro_state(main_coro) == BIO_CORO_WAITING, "Invalid coro state");
	bio_yield();  // Schedule b
	CHECK(bio_coro_state(main_coro) == BIO_CORO_READY, "Invalid coro state");
	bio_yield();  // Schedule main
	CHECK(bio_coro_state(main_coro) == BIO_CORO_DEAD, "Invalid coro state");
}

static void
signal_one_a_entry(void* userdata) {
	signal_ctx_t* ctx = userdata;
	bio_coro_t main_coro = ctx->main_coro;

	CHECK(bio_coro_state(main_coro) == BIO_CORO_WAITING, "Invalid coro state");
	CHECK(ctx->counter++ == 0, "Invalid scheduling");
	CHECK(bio_coro_state(main_coro) == BIO_CORO_WAITING, "Invalid coro state");

	bio_raise_signal(ctx->signals[0]);
	CHECK(bio_coro_state(main_coro) == BIO_CORO_READY, "Invalid coro state");
	bio_yield();
	CHECK(bio_coro_state(main_coro) == BIO_CORO_DEAD, "Invalid coro state");
}

static void
signal_b_entry(void* userdata) {
	signal_ctx_t* ctx = userdata;
	bio_raise_signal(ctx->signals[1]);
}

BIO_TEST(coro, wait_all_signal) {
	signal_ctx_t ctx = {
		.main_coro = bio_current_coro(),
		.signals = {
			bio_make_signal(),
			bio_make_signal(),
		},
		.counter = 0,
	};
	bio_make_signal();  // This should not leak

	bio_spawn(signal_all_a_entry, &ctx);
	bio_spawn(signal_b_entry, &ctx);
	bio_wait_for_signals(ctx.signals, 2, true);

	CHECK(ctx.counter++ == 1, "Invalid scheduling");
}

BIO_TEST(coro, wait_one_signal) {
	signal_ctx_t ctx = {
		.main_coro = bio_current_coro(),
		.signals = {
			bio_make_signal(),
			bio_make_signal(),
		},
		.counter = 0,
	};
	bio_make_signal();  // This should not leak

	bio_spawn(signal_one_a_entry, &ctx);
	bio_wait_for_signals(ctx.signals, 2, false);

	CHECK(ctx.counter++ == 1, "Invalid scheduling");
}

static void
wait_invalid(void* userdata) {
	signal_ctx_t* ctx = userdata;
	ctx->signals[1] = bio_make_signal();
	bio_wait_for_signals(ctx->signals, 2, false);
}

BIO_TEST(coro, wait_invalid_signal) {
	signal_ctx_t ctx = {
		.main_coro = bio_current_coro(),
		.counter = 0,
	};

	bio_coro_t waiter = bio_spawn(wait_invalid, &ctx);
	bio_coro_state_t state;
	while ((state = bio_coro_state(waiter)) == BIO_CORO_READY) {
		bio_yield();
	}
	// It should be blocked by the second signal as the first one does not count
	BTEST_EXPECT_EX(state == BIO_CORO_WAITING, "state = %d", state);
	bio_raise_signal(ctx.signals[1]);
	for (int i = 0; i < 5; ++i) {
		if ((state = bio_coro_state(waiter)) == BIO_CORO_DEAD) {
			break;
		}
		bio_yield();
	}
	BTEST_EXPECT_EX(state == BIO_CORO_DEAD, "state = %d", state);
}

static void
monitor_child(void* userdata) {
	int arg = *(int*)userdata;
	CHECK(arg == 42, "Corrupted arg");
	bio_yield();  // Parent must wait
	*(int*)userdata = 69;
}

BIO_TEST(coro, monitor) {
	int arg = 42;
	bio_coro_t child = bio_spawn(monitor_child, &arg);

	// Monitor to wait until the child stopped.
	// When this function returns the stack is in an undefined state.
	// In release mode, this would not be a problem however, in debug, esp with
	// sanitizer, the old stack seems to be poisoned with garbage.
	// The framework cannot help with this.
	// A wrapper around this function would not be able to preserve its stack.
	// TODO: Add a log/warning/hard crash when a parent exits while some children
	// has not started to enforce this behaviour.
	bio_signal_t child_terminated = bio_make_signal();
	bio_monitor(child, child_terminated);
	CHECK(bio_coro_state(child) != BIO_CORO_DEAD, "Invalid child state");
	bio_wait_for_signals(&child_terminated, 1, true);
	CHECK(arg == 69, "Child is still running");
	CHECK(bio_coro_state(child) == BIO_CORO_DEAD, "Invalid child state");
}

static void
unmonitor_child(void* userdata) {
	(void)userdata;
	bio_yield();
}

BIO_TEST(coro, unmonitor) {
	bio_coro_t child = bio_spawn(unmonitor_child, NULL);

	bio_signal_t child_terminated = bio_make_signal();
	bio_monitor_t monitor = bio_monitor(child, child_terminated);
	CHECK(bio_coro_state(child) != BIO_CORO_DEAD, "Invalid child state");
	bio_unmonitor(monitor);

	// Busy wait until child stop
	while (bio_coro_state(child) != BIO_CORO_DEAD) {
		bio_yield();
	}
	// Signal must not be raised since we unmonitored the child
	CHECK(!bio_check_signal(child_terminated), "Signal was raised");
}

static void
monitor_dead_coro_child(void* userdata) {
	(void)userdata;
}

BIO_TEST(coro, monitor_dead_coro) {
	bio_coro_t child = bio_spawn(monitor_dead_coro_child, NULL);
	CHECK(bio_coro_state(child) != BIO_CORO_DEAD, "Invalid child state");

	// Busy wait until child stop
	while (bio_coro_state(child) != BIO_CORO_DEAD) {
		bio_yield();
	}

	bio_signal_t child_terminated = bio_make_signal();
	CHECK(!bio_check_signal(child_terminated), "Signal was raised");
	bio_monitor(child, child_terminated);
	CHECK(bio_check_signal(child_terminated), "Signal was not raised immediately");
}

static const bio_tag_t TAG1 = BIO_TAG_INIT("bio.test.coro.tag1");
static const bio_tag_t TAG2 = BIO_TAG_INIT("bio.test.coro.tag2");

typedef struct {
	bio_signal_t wait_signal;
	bio_signal_t start_signal;
} data_holder_ctx_t;

static void
data_holder(void* userdata) {
	data_holder_ctx_t* ctx = userdata;
	bio_raise_signal(ctx->start_signal);

	ctx->wait_signal = bio_make_signal();
	bio_set_coro_data(ctx, &TAG1);
	bio_wait_for_one_signal(ctx->wait_signal);
}

BIO_TEST(coro, data) {
	data_holder_ctx_t ctx = {
		.start_signal = bio_make_signal(),
	};
	bio_coro_t data_holder_coro = bio_spawn(data_holder, &ctx);
	BTEST_EXPECT(bio_get_coro_data(data_holder_coro, &TAG1) == NULL);
	bio_wait_for_one_signal(ctx.start_signal);

	BTEST_EXPECT(bio_get_coro_data(data_holder_coro, &TAG1) == &ctx);
	BTEST_EXPECT(bio_get_coro_data(data_holder_coro, &TAG2) == NULL);
	bio_raise_signal(ctx.wait_signal);
	bio_join(data_holder_coro);
	BTEST_EXPECT(bio_get_coro_data(data_holder_coro, &TAG1) == NULL);
}
