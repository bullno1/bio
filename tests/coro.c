#include "common.h"

static suite_t coro = {
	.name = "coro",
	.init = init_bio,
	.cleanup = cleanup_bio,
};

typedef struct {
	bio_coro_ref_t coro_a;
	bio_coro_ref_t coro_b;
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
	bio_coro_ref_t main_coro;
	bio_signal_ref_t signals[2];
	int counter;
} signal_ctx_t;

static void
signal_all_a_entry(void* userdata) {
	signal_ctx_t* ctx = userdata;
	bio_coro_ref_t main_coro = ctx->main_coro;

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
	bio_coro_ref_t main_coro = ctx->main_coro;

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
delayed_destruction_child(void* userdata) {
	int arg = *(int*)userdata;
	CHECK(arg == 42, "Corrupted arg");
}

static void
delayed_destruction_parent(void* userdata) {
	int arg = 42;
	bio_spawn(delayed_destruction_child, &arg);

	// Yield to give the child a chance to start
	bio_yield();
}

TEST(coro, delayed_destruction) {
	bio_spawn(delayed_destruction_parent, NULL);

	bio_loop();
}
