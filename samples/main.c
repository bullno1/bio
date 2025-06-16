#include <bio/bio.h>

//! [Entrypoint]
int main(int argc, const char* argv[]) {
	// Initialize the library
	bio_init(&(bio_options_t){
		.log_options = {
			.current_filename = __FILE__,
			.current_depth_in_project = 1, // If the file is in src/main.c
		},
	});

	// Spawn initial coroutines
	bio_spawn(main_coro, data);

	// Loop until there is no running coroutines
	bio_loop();

	// Cleanup
	bio_terminate();

	return 0;
}
//! [Entrypoint]
