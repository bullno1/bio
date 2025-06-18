#include <bio/bio.h>
#include <stdio.h>

static bio_signal_t final_exit = { 0 };

static void
waiter(void* userdata) {
	final_exit = bio_make_signal();
	bio_wait_for_one_signal(final_exit);
}


static void
signal_handler(void* userdata) {
	for (int i = 4; i > 0; --i) {
		printf("Press Ctrl+C %d time(s)\n", i);
		bio_wait_for_exit();
	}

	bio_raise_signal(final_exit);
}

int
main(int argc, const char* argv[]) {
	bio_init(&(bio_options_t){
		.log_options = {
			.current_filename = __FILE__,
			.current_depth_in_project = 1,
		},
	});

	bio_spawn(waiter, NULL);
	bio_spawn(signal_handler, NULL);

	bio_loop();
	bio_terminate();
	return 0;
}
