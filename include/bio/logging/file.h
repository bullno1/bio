#ifndef BIO_FILE_LOGGER_H
#define BIO_FILE_LOGGER_H

#include <bio/file.h>

typedef struct {
	bio_file_t file;
	bool with_colors;
} bio_file_logger_options_t;

bio_logger_t
bio_add_file_logger(bio_log_level_t min_level, const bio_file_logger_options_t* options);

#endif
