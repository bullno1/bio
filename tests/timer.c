#include "common.h"
#include <inttypes.h>

static suite_t timer = {
	.name = "timer",
	.init = init_bio,
	.cleanup = cleanup_bio,
};

typedef struct {
	bio_time_t start_time;
	bio_time_t early_wakeup_time;
} fixture_t;

void
timer_early(void* userdata) {
	fixture_t* fixture = userdata;
	CHECK(fixture->early_wakeup_time == 0, "Corrupted fixture");

	bio_signal_t signal = bio_make_signal();
	bio_raise_signal_after(signal, 100);
	bio_wait_for_signals(&signal, 1, true);
	CHECK(fixture->early_wakeup_time == 0, "Corrupted fixture");

	bio_time_t current_time = fixture->early_wakeup_time = bio_current_time_ms();
	CHECK(current_time - fixture->start_time >= 100, "Bad time scheduling");
}

void
timer_late(void* userdata) {
	fixture_t* fixture = userdata;
	CHECK(fixture->early_wakeup_time == 0, "Corrupted fixture");

	bio_signal_t signal = bio_make_signal();
	bio_raise_signal_after(signal, 200);
	bio_wait_for_signals(&signal, 1, true);
	CHECK(fixture->early_wakeup_time > 0, "Bad time scheduling");

	bio_time_t current_time = bio_current_time_ms();
	CHECK(current_time - fixture->start_time >= 200, "Bad time scheduling");
	CHECK(current_time > fixture->early_wakeup_time, "Bad time scheduling");
}

TEST(timer, scheduling) {
	fixture_t fixture = {
		.start_time = bio_current_time_ms(),
	};
	// Spawn timer_late first but make it wait longer
	bio_spawn(timer_late, &fixture);
	bio_spawn(timer_early, &fixture);

	bio_loop();
}
