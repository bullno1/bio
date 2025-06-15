#ifndef BIO_FILE_LOGGER_H
#define BIO_FILE_LOGGER_H

#include <bio/file.h>

/**
 * @addtogroup logging
 * @{
 */

/**
 * @defgroup file_logger File logger
 *
 * A logger that writes messages to a file.
 * Typically this is @ref BIO_STDERR.
 *
 * @{
 */

/// Configuration options
typedef struct {
	/// The file to writes to
	bio_file_t file;
	/// Whether log messages should be colored based on log level
	bool with_colors;
} bio_file_logger_options_t;

/**
 * Register the logger
 *
 * @remark
 *   This will spawn a coroutine to handle writing and avoid making the calling
 *   coroutine context switch due to I/O.
 *   The returned `bio_logger_t` should be removed with @ref bio_remove_logger
 *   at the end of a program to ensure that @ref bio_loop will terminate.
 */
bio_logger_t
bio_add_file_logger(bio_log_level_t min_level, const bio_file_logger_options_t* options);

/**@}*/

/**@}*/

#endif
