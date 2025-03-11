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
coro_a_entry(void* userdata) {
	yield_ctx_t* ctx = userdata;
	CHECK(bio_coro_state(ctx->coro_a) == BIO_CORO_RUNNING, "Invalid coro state");
	CHECK(bio_coro_state(ctx->coro_b) == BIO_CORO_READY, "Invalid coro state");

	CHECK(ctx->counter++ == 0, "Invalid scheduling");
	bio_yield();
	CHECK(ctx->counter++ == 2, "Invalid scheduling");
	bio_yield();

	CHECK(bio_coro_state(ctx->coro_b) == BIO_CORO_DEAD, "Invalid coro state");
}

static void
coro_b_entry(void* userdata) {
	yield_ctx_t* ctx = userdata;
	CHECK(bio_coro_state(ctx->coro_a) == BIO_CORO_READY, "Invalid coro state");
	CHECK(bio_coro_state(ctx->coro_b) == BIO_CORO_RUNNING, "Invalid coro state");

	CHECK(ctx->counter++ == 1, "Invalid scheduling");
	bio_yield();
	CHECK(ctx->counter++ == 3, "Invalid scheduling");
}

TEST(coro, yield) {
	yield_ctx_t ctx = { 0 };

	ctx.coro_a = bio_spawn(coro_a_entry, &ctx);
	ctx.coro_b = bio_spawn(coro_b_entry, &ctx);

	bio_loop();

	CHECK(bio_coro_state(ctx.coro_a) == BIO_CORO_DEAD, "Invalid coro state");
	CHECK(bio_coro_state(ctx.coro_b) == BIO_CORO_DEAD, "Invalid coro state");
}
