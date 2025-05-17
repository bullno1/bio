#include "common.h"
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>

static suite_t timer = {
	.name = "timer",
	.init_per_test = init_bio,
	.cleanup_per_test = cleanup_bio,
};

typedef struct {
	bio_time_t start_time;
	bio_time_t early_wakeup_time;
} simple_fixture_t;

static void
timer_early(void* userdata) {
	simple_fixture_t* fixture = userdata;
	CHECK(fixture->early_wakeup_time == 0, "Corrupted fixture");

	bio_signal_t signal = bio_make_signal();
	bio_raise_signal_after(signal, 100);
	bio_wait_for_signals(&signal, 1, true);
	CHECK(fixture->early_wakeup_time == 0, "Corrupted fixture");

	bio_time_t current_time = fixture->early_wakeup_time = bio_current_time_ms();
	CHECK(current_time - fixture->start_time >= 100, "Bad time scheduling");
}

static void
timer_late(void* userdata) {
	simple_fixture_t* fixture = userdata;
	CHECK(fixture->early_wakeup_time == 0, "Corrupted fixture");

	bio_signal_t signal = bio_make_signal();
	bio_raise_signal_after(signal, 200);
	bio_wait_for_signals(&signal, 1, true);
	CHECK(fixture->early_wakeup_time > 0, "Bad time scheduling");

	bio_time_t current_time = bio_current_time_ms();
	CHECK(current_time - fixture->start_time >= 200, "Bad time scheduling");
	CHECK(current_time > fixture->early_wakeup_time, "Bad time scheduling");
}

TEST(timer, simple) {
	simple_fixture_t fixture = {
		.start_time = bio_current_time_ms(),
	};
	// Spawn timer_late first but make it wait longer
	bio_spawn(timer_late, &fixture);
	bio_spawn(timer_early, &fixture);

	bio_loop();
}

struct {
	int numbers[1000];
	int write_index;
} sort_fixture;

static void
sort_entry(void* userdata) {
	uintptr_t data = (uintptr_t)userdata;
	int delay = data & 0xffffffff;

	bio_signal_t signal = bio_make_signal();
	bio_raise_signal_after(signal, delay);
	bio_wait_for_signals(&signal, 1, true);

	sort_fixture.numbers[sort_fixture.write_index++] = delay;
}

TEST(timer, sort) {
	// Spawn 1000 coros, giving them random numbers which is their delay
	// They must all wake up in order, aka timer sort.

	srand(time(NULL));
	for (int i = 0; i < 1000; ++i) {
		int delay = rand() % 500;
		sort_fixture.numbers[i] = delay;
		uintptr_t userdata = (uintptr_t)delay;
		bio_spawn(sort_entry, (void*)userdata);
	}
	sort_fixture.write_index = 0;

	bio_loop();

	// Verify
	for (int i = 1; i < 1000; ++i) {
		CHECK(sort_fixture.numbers[i] >= sort_fixture.numbers[i - 1], "Bad sorting");
	}
}
