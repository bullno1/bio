#ifndef BIO_FILE_LOGGER_H
#define BIO_FILE_LOGGER_H

#include <bio/file.h>

typedef struct {
	bio_file_t file;
	bio_log_level_t min_level;
	bool with_colors;

	// For shortening path in log
	const char* current_filename;
	int current_depth_in_project;
} bio_file_logger_options_t;

bio_logger_t
bio_add_file_logger(const bio_file_logger_options_t* options);

#endif
