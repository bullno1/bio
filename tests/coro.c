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

static void
signal_wait_all(void* userdata) {
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

static void
signal_wait_one(void* userdata) {
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

TEST(coro, wait_all_signal) {
	bio_spawn(signal_wait_all, NULL);

	bio_loop();
}

TEST(coro, wait_one_signal) {
	bio_spawn(signal_wait_one, NULL);

	bio_loop();
}

static void
monitor_child(void* userdata) {
	int arg = *(int*)userdata;
	CHECK(arg == 42, "Corrupted arg");
	bio_yield();  // Parent must wait
	*(int*)userdata = 69;
}

static void
monitor_parent(void* userdata) {
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

TEST(coro, monitor) {
	bio_spawn(monitor_parent, NULL);

	bio_loop();
}

static void
unmonitor_child(void* userdata) {
	bio_yield();
}

static void
unmonitor_parent(void* userdata) {
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

TEST(coro, unmonitor) {
	bio_spawn(unmonitor_parent, NULL);

	bio_loop();
}

static void
monitor_dead_coro_child(void* userdata) {
}

static void
monitor_dead_coro_parent(void* userdata) {
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

TEST(coro, monitor_dead_coro) {
	bio_spawn(monitor_dead_coro_parent, NULL);

	bio_loop();
}
