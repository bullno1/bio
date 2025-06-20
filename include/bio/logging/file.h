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
 *
 * @code{.c}
 * bio_logger_t logger = bio_add_file_logger(&(bio_file_logger_options_t){
 *     .file = BIO_STDERR,
 *     .with_colors = true,
 * });
 * BIO_INFO("Started");
 *
 * // Main loop
 * while (running) {
 *     // ...
 * }
 * @endcode
 *
 * @{
 */

/// Configuration options
typedef struct {
	/**
	 * The file to writes to
	 *
	 * Usually, this is @ref BIO_STDERR
	 */
	bio_file_t file;
	/// Whether log messages should be colored based on log level
	bool with_colors;
} bio_file_logger_options_t;

/**
 * Register the logger
 */
bio_logger_t
bio_add_file_logger(bio_log_level_t min_level, const bio_file_logger_options_t* options);

/**@}*/

/**@}*/

#endif
