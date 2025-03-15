#ifndef BIO_FILE_LOGGER_H
#define BIO_FILE_LOGGER_H

#include <bio/file.h>

bio_logger_t
bio_add_file_logger(bio_file_t file, bio_log_level_t level, bool with_colors);

#endif
