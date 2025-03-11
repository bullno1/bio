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
	bio_coro_ref_t coro_a;
	bio_signal_ref_t signal;
	int counter;
} signal_ctx_t;

static void
signal_b_entry(void* userdata);

static void
signal_a_entry(void* userdata) {
	signal_ctx_t ctx = {
		.coro_a = bio_current_coro(),
		.signal = bio_make_signal(),
		.counter = 0,
	};
	bio_make_signal();  // This should not leak

	bio_spawn(signal_b_entry, &ctx);
	bio_wait_for_signals(&ctx.signal, 1, true);

	CHECK(ctx.counter++ == 1, "Invalid scheduling");
}

static void
signal_b_entry(void* userdata) {
	// Make copy since the other coro could terminate
	signal_ctx_t* ctx = userdata;
	bio_coro_ref_t coro_a = ctx->coro_a;

	CHECK(bio_coro_state(coro_a) == BIO_CORO_WAITING, "Invalid coro state");
	bio_yield();  // Immediately come back to us
	CHECK(ctx->counter++ == 0, "Invalid scheduling");
	CHECK(bio_coro_state(coro_a) == BIO_CORO_WAITING, "Invalid coro state");

	bio_raise_signal(ctx->signal);
	bio_yield();  // Immediately come back to us
	CHECK(bio_coro_state(coro_a) == BIO_CORO_DEAD, "Invalid coro state");
}

TEST(coro, signal) {
	bio_spawn(signal_a_entry, NULL);

	bio_loop();
}
