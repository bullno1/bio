#ifndef BIO_MAIN_H
#define BIO_MAIN_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

#ifndef DOXYGEN

#if defined(__GNUC__) || defined(__clang__)
#	define BIO_FORMAT_ATTRIBUTE(FMT, VA) __attribute__((format(printf, FMT, VA)))
#	define BIO_FORMAT_CHECK(...) (void)(sizeof(0))
#else
#	include <stdio.h>
#	define BIO_FORMAT_ATTRIBUTE(FMT, VA)
#	define BIO_FORMAT_CHECK(...) (void)(sizeof(printf(__VA_ARGS__)))
#endif

#endif

/**
 * @addtogroup logging
 *
 * @{
 */

/**
 * Log a message at a given log level.
 *
 * @code
 * BIO_LOG(BIO_LOG_LEVEL_INFO, "Hello %s", "world");
 * @endcode
 *
 * @param LEVEL a @ref bio_log_level_t
 * @param ... The log message. This can use a printf-like format string
 */
#define BIO_LOG(LEVEL, ...) \
	(BIO_FORMAT_CHECK(__VA_ARGS__), bio_log(LEVEL, __FILE__, __LINE__, __VA_ARGS__))

/// Log a message at @ref BIO_LOG_LEVEL_TRACE level
#define BIO_TRACE(...) BIO_LOG(BIO_LOG_LEVEL_TRACE, __VA_ARGS__)

/// Log a message at @ref BIO_LOG_LEVEL_DEBUG level
#define BIO_DEBUG(...) BIO_LOG(BIO_LOG_LEVEL_DEBUG, __VA_ARGS__)

/// Log a message at @ref BIO_LOG_LEVEL_DEBUG level
#define BIO_INFO(...)  BIO_LOG(BIO_LOG_LEVEL_INFO , __VA_ARGS__)

/// Log a message at @ref BIO_LOG_LEVEL_WARN level
#define BIO_WARN(...)  BIO_LOG(BIO_LOG_LEVEL_WARN , __VA_ARGS__)

/// Log a message at @ref BIO_LOG_LEVEL_ERROR level
#define BIO_ERROR(...) BIO_LOG(BIO_LOG_LEVEL_ERROR, __VA_ARGS__)

/// Log a message at @ref BIO_LOG_LEVEL_FATAL level
#define BIO_FATAL(...) BIO_LOG(BIO_LOG_LEVEL_FATAL, __VA_ARGS__)

/**
 * Helper for logging a @ref bio_error_t
 *
 * Example:
 *
 * @code
 * bio_error_t error = { 0 };
 * if (!bio_fstat(file, &stat, &error)) {
 *     BIO_ERROR("Error while stat-ing: " BIO_ERROR_FMT, BIO_ERROR_FMT_ARGS(&error));
 * }
 * @endcode
 *
 * @see BIO_ERROR_FMT_ARGS
 */
#define BIO_ERROR_FMT "%s (%s[%d]) (from %s:%d)"

/**
 * Helper for logging a @ref bio_error_t
 *
 * @see BIO_ERROR_FMT
 */
#define BIO_ERROR_FMT_ARGS(error) \
	(bio_has_error((error)) ? bio_strerror((error)) : "No error"), \
	(bio_has_error((error)) ? (error)->tag->name : "bio.error.core"), \
	((error)->code), \
	((error)->file ? (error)->file : "<no source info>"), \
	((error)->line)

/**@}*/

/**
 * Utility macro for initializing a tag.
 *
 * @ingroup misc
 * @see bio_tag_t
 */
#define BIO_TAG_INIT(NAME) \
	{ \
		.name = NAME, \
		.file = __FILE__, \
		.line = __LINE__, \
	}

/**
 * An invalid handle.
 *
 * Resolving this will always return `NULL`.
 * This macro is just a short hand for setting all fields in a @ref bio_handle_t to 0.
 * Thus, a handle can be zero-initialized, allowing "Zero Is Initialization" (ZII).
 *
 * @ingroup handle
 * @see bio_resolve_handle
 */
#define BIO_INVALID_HANDLE ((bio_handle_t){ .index = 0 })

/**
 * Entrypoint for a coroutine.
 *
 * @ingroup coro
 * @see bio_spawn
 */
typedef void (*bio_entrypoint_t)(void* userdata);

/**
 * @see handle
 * @ingroup handle
 */
typedef struct {
	int32_t index;  /**< For internal use */
	int32_t gen;  /**< For internal use */
} bio_handle_t;

/**
 * Handle to a coroutine.
 *
 * @ingroup coro
 * @see bio_spawn
 */
typedef struct {
	bio_handle_t handle;
} bio_coro_t;

/**
 * Handle to a signal.
 *
 * @ingroup signal
 * @see bio_make_signal
 */
typedef struct {
	bio_handle_t handle;
} bio_signal_t;

/**
 * Handle to a monitor.
 *
 * @ingroup coro
 * @see bio_monitor
 */
typedef struct {
	bio_handle_t handle;
} bio_monitor_t;

/**
 * Handle to a logger.
 *
 * @ingroup logging
 * @see bio_add_logger
 */
typedef struct {
	bio_handle_t handle;
} bio_logger_t;

/**
 * A unique tag
 *
 * @code{.c}
 * static const bio_tag_t UTILITY_TAG = BIO_TAG_INIT("my_project.utility");
 * @endcode
 *
 * bio uses tags for various purposes.
 * The tag is always declared as a global static variable and passed around
 * as a pointer.
 * This makes it a globally unique identifier with attached metadata.
 *
 * @ingroup misc
 * @see BIO_TAG_INIT
 * @see bio_error_t
 * @see bio_set_coro_data
 * @see bio_make_handle
 */
typedef struct {
	/**
	 * A human-readable name for the tag.
	 *
	 * A dot-separated package name style is recommended but not required.
	 * For example: `my_project.engine.core`
	 */
	const char* name;

	/// The file where the tag was defined
	const char* file;

	/// The line where the tag was defined
	int line;
} bio_tag_t;

/**
 * A timestamp
 *
 * @ingroup misc
 *
 * @see bio_raise_signal_after
 * @see bio_current_time_ms
 */
typedef int64_t bio_time_t;

/**
 * Initialization options for Linux
 *
 * @ingroup init
 * @see bio_init
 */
typedef struct {
	/// Options for [io_uring](https://man7.org/linux/man-pages/man7/io_uring.7.html)
	struct {
		/**
		 * The size of the submission and completion queue.
		 *
		 * Defaults to @ref BIO_LINUX_DEFAULT_QUEUE_SIZE if not set.
		 *
		 * Will be rounded to the nearest power of 2.
		 * A bigger value will consume more memory upfront but it will allow the
		 * program to have more in-flight I/O requests.
		 */
		unsigned int queue_size;
	} io_uring;
} bio_linux_options_t;

/**
 * Initialization options for Windows
 *
 * @ingroup init
 * @see bio_init
 */
typedef struct {
	/// Options for [IOCP](https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports)
	struct {
		/**
		 * The size of the buffer that will be passed to [GetQueuedCompletionStatusEx](https://learn.microsoft.com/en-us/windows/win32/fileio/getqueuedcompletionstatusex-func)
		 *
		 * Defaults to @ref BIO_WINDOWS_DEFAULT_BATCH_SIZE if not set.
		 *
		 * A bigger value will consume more memory upfront but more events will
		 * be dequeued in a single call.
		 * Take note that the library will always try to drain all pending
		 * events if possible.
		 */
		unsigned int batch_size;
	} iocp;
} bio_windows_options_t;

/**
 * Custom memory allocator
 *
 * @ingroup init
 */
typedef struct {
	/// Allocator context
	void* ctx;

	/**
	 * Allocation function.
	 *
	 * This should behave like stdlib `realloc`.
	 *
	 * This will only be called from the main thread so synchronization is not
	 * necessary.
	 *
	 * @param ptr The pointer to reallocate or free.
	 *   This can be `NULL`.
	 * @param size The size to (re)allocate.
	 *   When this is 0, the memory pointed to by @p ptr should be freed.
	 *   Freeing a `NULL` pointer is not an error.
	 * @param ctx Arbitrary context.
	 * @return The (re)allocated memory or `NULL` when @p size is 0.
	 */
	void* (*realloc)(void* ptr, size_t size, void* ctx);
} bio_allocator_t;

/**
 * Coroutine-local storage (CLS) specification.
 *
 * @ingroup coro
 * @see bio_get_cls
 */
typedef struct {
	size_t size;   /**< Size of the data */
	void (*init)(void* data);  /**< Optional initializer */
	void (*cleanup)(void* data);  /**< Optional cleanup function */
} bio_cls_t;

/**
 * Logging options
 *
 * @ingroup logging
 * Typically, it would be used like this:
 *
 * @code{.c}
 * bio_init(&(bio_options_t){
 *     .log_options = {
 *         .current_filename = __FILE__,
 *         .current_depth_in_project = 1,  // If the file is in src/main.c
 *     },
 * });
 * @endcode
 *
 * This helps the logging system to shorten the paths in log messages.
 * The @ref BIO_LOG "logging macros" use the preprocessor variables `__FILE__`
 * and `__LINE__` to give more context to each message.
 * However, `__FILE__` would typically expands into an absolute path
 * (e.g: `/home/username/projects/project_name/src/file.c`).
 * This gives the log messages an extremely long and superflous prefix:
 *
 * @code{.txt}
 * [INFO ][/home/username/projects/project_name/src/file.c:41]: Log message
 * @endcode
 *
 * By passing these info to the log system, the above message will be just:
 *
 * @code{.txt}
 * [INFO ][src/file.c:41]: Log message
 * @endcode
 *
 * The @ref bio_log_ctx_t::file passed to each logger will be the shortened version.
 *
 * When this option is not provided, filename shortening will not be performed.
 *
 * Take note that if you have source files out of the project tree (e.g: `/home/username/projects/another_project_name`),
 * filename shortening will not be performed either and the full path will be logged.
 * It is recommended to keep dependencies in the source tree (e.g: `/home/username/projects/project_name/deps`).
 * In that case, any logging calls from dependencies will show up as `deps/dependency_name`.
 */
typedef struct {
	/**
	 * Full path to the file that is calling @ref bio_init
	 */
	const char* current_filename;

	/**
	 * The depth of the calling file in your project tree.
	 *
	 * If the file that is calling bio_init is `src/main.c` for example,
	 * this should be 1.
	 */
	int current_depth_in_project;
} bio_log_options_t;

/**
 * Initialization options
 *
 * All fields are optional and have default values.
 * It is possible to call @ref bio_init like this:
 *
 * @code{.c}
 * bio_init(&(bio_options_t){ 0 });
 * @endcode
 *
 * Or with `NULL`:
 *
 * @code{.c}
 * bio_init(NULL);
 * @endcode
 *
 * You can also change just selected options:
 *
 * @code{.c}
 * bio_init(&(bio_options_t){
 *     .thread_pool = {
 *	       .num_threads = 8,   // Use more threads
 *     }
 * });
 * @endcode
 *
 * @ingroup init
 * @see bio_init
 */
typedef struct {
	/// Custom allocator. Defaults to stdlib if not provided.
	bio_allocator_t allocator;

	/**
	 * Number of coroutine-local storage (CLS) hash buckets.
	 *
	 * Defaults to @ref BIO_DEFAULT_NUM_CLS_BUCKETS if not provided.
	 *
	 * Internally, a hash table is used to keep track of the CLS in each coroutine.
	 * The buckets are only allocated when a coroutine calls @ref bio_get_cls.
	 * A larger value will consume more memory but CLS lookup would be faster.
	 *
	 * @see bio_get_cls
	 */
	int num_cls_buckets;

	/**
	 * Asynchronous thread pool options.
	 *
	 * @see bio_run_async
	 */
	struct {
		/**
		 * Number of threads in the thread pool.
		 *
		 * Defaults to @ref BIO_DEFAULT_THREAD_POOL_SIZE if not set.
		 */
		int num_threads;

		/**
		 * The length of the message queue for each thread in the async thread poo.
		 *
		 * Defaults to @ref BIO_DEFAULT_THREAD_POOL_QUEUE_SIZE if not set.
		 *
		 * A larger value would consume more memory upfront but it will allow
		 * the main thread to submit more async jobs before being stalled.
		 */
		int queue_size;
	} thread_pool;

	/// Logging options
	bio_log_options_t log_options;

	/// Linux-specific options, ignored on other platforms.
	bio_linux_options_t linux;

	/// Windows-specific options, ignored on other platforms.
	bio_windows_options_t windows;
} bio_options_t;

/**
 * An error returned from a function.
 *
 * @see BIO_ERROR_FMT
 */
typedef struct {
	/**
	 * The unique tag representing the domain that error comes from.
	 */
	const bio_tag_t* tag;

	/**
	 * The error formatting function.
	 *
	 * @see bio_strerror
	 */
	const char* (*strerror)(int code);

	/**
	 * The error code.
	 *
	 * The meaning of the value depends on the @ref bio_error_t::tag.
	 *
	 * For example: The error code 5 could have one meaning with the tag
	 * `bio.error.platform.windows` but a different meaning with
	 * `bio.error.platform.linux`.
	 */
	int code;

	/// The filename where the error originated from.
	const char* file;

	/// The line where the error originated from.
	int line;
} bio_error_t;

/**
 * State of a coroutine
 *
 * @ingroup coro
 */
typedef enum {
	/// The coroutine is ready to run but not running yet
	BIO_CORO_READY,
	/// The coroutine is the currently running coroutine that called @ref bio_coro_state
	BIO_CORO_RUNNING,
	/// The coroutine is waiting for a @ref bio_make_signal "signal"
	BIO_CORO_WAITING,
	/// The coroutine has terminated.
	BIO_CORO_DEAD,
} bio_coro_state_t;

/**
 * Log levels
 * @ingroup logging
 */
typedef enum {
    BIO_LOG_LEVEL_TRACE,  /**< Tracing message, for debugging */
    BIO_LOG_LEVEL_DEBUG,  /**< Debug message */
    BIO_LOG_LEVEL_INFO,   /**< Info message */
    BIO_LOG_LEVEL_WARN,   /**< Warning */
    BIO_LOG_LEVEL_ERROR,  /**< Error but usually recoverable */
    BIO_LOG_LEVEL_FATAL,  /**< Fatal, the program usually terminates after this */
} bio_log_level_t;

/**
 * Context for a log function
 *
 * @ingroup logging
 * @see bio_add_logger
 */
typedef struct {
	/// The coroutine calling the log function
	bio_coro_t coro;
	/// The log level
	bio_log_level_t level;
	/// The source line where the logging call originated from
	int line;
	/// The source file where the logging call originated from
	const char* file;
} bio_log_ctx_t;

/**
 * A log callback
 *
 * This will be called for every message passed to @ref bio_log.
 *
 * @remarks
 *   When @ref bio_remove_logger is called, this will be invoked with @p ctx and
 *   @p msg set to `NULL` instead.
 *   The callback should perform cleanup for "flushing" buffered messages.
 *
 * @param userdata Arbitrary user data
 * @param ctx Log context
 * @param msg Log message
 *
 * @ingroup logging
 * @see bio_add_logger
 */
typedef void (*bio_log_fn_t)(void* userdata, const bio_log_ctx_t* ctx, const char* msg);

/**
 * @defgroup init Initialization
 *
 * Initialization and cleanup
 *
 * A typical program looks like this:
 *
 * @code{.c}
 * int main(int argc, const char* argv[]) {
 *     // Initialize the library
 *     bio_init(&(bio_options_t){
 *         .log_options = {
 *             .current_filename = __FILE__,
 *             .current_depth_in_project = 1, // If the file is in src/main.c
 *         },
 *     });
 *
 *     // Spawn initial coroutines
 *     bio_spawn(main_coro, data);
 *
 *     // Loop until there is no running coroutines
 *     bio_loop();
 *
 *     // Cleanup
 *     bio_terminate();
 *
 *     return 0;
 * }
 * @endcode
 *
 * @{
 */

/// Initialize the framework
void
bio_init(const bio_options_t* options);

/// Enter the event loop until there is no running coroutines
void
bio_loop(void);

/// Cleanup
void
bio_terminate(void);

/**@}*/

/**
 * @defgroup coro Coroutine
 *
 * Cooperative scheduling.
 *
 * At the heart of bio is a cooperative scheduling system.
 * Coroutines are generally cheaper to spawn and context switch compared to threads.
 *
 * Instead of using callbacks, all I/O calls in bio will block the calling coroutine.
 * A different coroutine will be scheduled to run and the original coroutine will
 * only resume once the original call return.
 *
 * Coroutines can interact with one another through:
 *
 * * @ref signal
 * * @ref monitor
 * * @ref mailbox
 * * @ref bio_set_coro_data "Associated data"
 *
 * @{
 */

/**
 * Spawn a new coroutine.
 *
 * @param entrypoint The entrypoint for the coroutine.
 * @param userdata Data to pass to the entrypoint.
 * @return A new coroutine handle.
 */
bio_coro_t
bio_spawn(bio_entrypoint_t entrypoint, void* userdata);

/// Check the state of a coroutine
bio_coro_state_t
bio_coro_state(bio_coro_t coro);

/// Get the handle of the currently running coroutine
bio_coro_t
bio_current_coro(void);

/**
 * Let a different coroutine run
 *
 * Since cooperative scheduling is used in the main thread, if a coroutine is
 * doing something intensive, it could block other coroutines from running.
 * Calling this function will let other coroutines run.
 * However, it is probably a better idea to use the @ref bio_run_async "async thread pool" instead.
 *
 * Another use for this would be to implement a busy wait.
 */
void
bio_yield(void);

/**
 * @defgroup signal Signal
 *
 * Waiting for an event to complete.
 *
 * Signal is the primary synchronization primitive in bio.
 * When a coroutine needs to wait for something to happen, it would:
 *
 * 1. Create a new signal with @ref bio_make_signal.
 * 2. Pass the signal to a target through a @ref bio_monitor "function call" or
 *   a @ref mailbox "mailbox".
 * 3. @ref bio_wait_for_signals "Wait" for the signal to be raised.
 *
 * It is by design that a signal has no associated state like "future" or "promise"
 * in other languages.
 * This keeps the implementation simple as signal is only meant to be a notification
 * mechanism.
 * To transfer data, use a regular function call with a signal as one of the argument.
 * In addition, there is also @ref mailbox "mailbox".
 *
 * Internally, all I/O functions use signals to wait for completion.
 *
 * @{
 */

/**
 * Create a new signal
 *
 * A signal requires some dynamically allocated memory to track its state.
 * When a signal is created but later deemed unnecessary, it should be discarded
 * with @ref bio_raise_signal.
 * Otherwise, it will continue to take some memory.
 * Signals are bound to the coroutine that created it.
 * When a coroutine terminates, all signals bound to it are freed and no manual
 * cleanup is needed.
 *
 * @see bio_wait_for_signals
 * @see bio_raise_signal
 */
bio_signal_t
bio_make_signal(void);

/**
 * Raise a signal
 *
 * A coroutine waiting on the signal will be resumed.
 * A signal can only be raised once.
 * Raising an already raised signal is not an error.
 * Similarly, it is safe to raise a signal from an already terminated coroutine.
 *
 * @see bio_make_signal
 */
bool
bio_raise_signal(bio_signal_t signal);

/**
 * Raise a signal after a delay
 *
 * This is the primary way to create "timers" in bio.
 *
 * @param signal The signal to raise
 * @param time_ms The delay in milliseconds to raise this signal
 *
 * @see timer
 *
 * @see bio_raise_signal
 */
void
bio_raise_signal_after(bio_signal_t signal, bio_time_t time_ms);

/**
 * Check whether a signal has been raised
 *
 * @remark if the signal comes from an already terminated coroutine, this will
 * always return `true`.
 */
bool
bio_check_signal(bio_signal_t signal);

/**
 * Wait for signals to be raised
 *
 * The calling coroutine will be suspended until all or one of the provided
 * signals is raised (depending on @p wait_all).
 *
 * Signals are bound to the coroutines that created it.
 * Only the coroutine that created a signal can wait on that signal.
 * However, any coroutine can raise any signal.
 *
 * It is not an error to pass a signal created by a different coroutine to
 * this function.
 * However, they will be ignored.
 *
 * Passing an already raised signal to this function is also safe.
 * When @p wait_all is `false`, the calling coroutine would immediately continue
 * execution since at least one signal in the array has been raised.
 *
 * If all provided signals are already raised or ignored, this function will
 * immediately return.
 * The calling coroutine will continue to execute without switching to another
 * coroutine.
 *
 * @param signals An array of signals
 * @param num_signals Number of signals to wait for
 * @param wait_all Whether to wait for all or one of the signals.
 *   When this is `true`, the coroutine only resumes if all of the signals have been raised.
 *   When this is `false`, the corutine resumes as soon as one of the provided signals is raised.
 *
 * @see bio_make_signal
 * @see bio_raise_signal
 * @see bio_wait_for_one_signal
 */
void
bio_wait_for_signals(
	bio_signal_t* signals,
	int num_signals,
	bool wait_all
);

/**
 * Convenient function to wait for a single signal
 *
 * @see bio_wait_for_signals
 */
static inline void
bio_wait_for_one_signal(bio_signal_t signal) {
	bio_wait_for_signals(&signal, 1, true);
}

/**@}*/

/**
 * @defgroup monitor Monitor
 *
 * Monitor a coroutine for termination
 *
 * @{
 */

/**
 * Monitor a coroutine for termination
 *
 * The provided @p signal would be raised when the given @p coro terminates.
 *
 * A coroutine can be monitored several times and each would result in separate
 * signal delivery.
 *
 * It is possible to pass the same signal to different calls to to `bio_monitor`
 * since raising an already raised signal is safe.
 * This would allow the monitoring coroutine to wait for at least one of the
 * monitored coroutine to terminate.
 *
 * Dynamic memory is allocated to track the state of a monitor.
 * When a monitoring coroutine is no longer interested in the monitored coroutine,
 * the monitor should be discarded with @ref bio_unmonitor.
 * Otherwise, it will continue to take up memory.
 * However, when a coroutine terminates, all monitors attached to it will be
 * automatically freed.
 *
 * @param coro The coroutine to monitor
 * @param signal The signal that would be raised when the coroutine terminates
 * @return A handle for this monitor
 *
 * @see bio_make_signal
 * @see bio_unmonitor
 */
bio_monitor_t
bio_monitor(bio_coro_t coro, bio_signal_t signal);

/**
 * Discard a monitor
 *
 * If the monitored coroutine has already terminated, the associated signal
 * might already be raised.
 *
 * As with all resources in bio, calling this multiple times on the same monitor
 * is safe.
 *
 * @see bio_monitor
 */
void
bio_unmonitor(bio_monitor_t monitor);

/**
 * Convenient function to wait for a coroutine to terminate
 *
 * This makes use of @ref bio_monitor to wait for the targeted coroutine.
 *
 * @see bio_monitor
 */
static inline void
bio_join(bio_coro_t coro) {
	bio_signal_t term_signal = bio_make_signal();
	bio_monitor(coro, term_signal);
	bio_wait_for_one_signal(term_signal);
}

/**@}*/

/**
 * Associate the calling coroutine with arbitrary data and a tag
 *
 * This data can later be retrieved by any coroutine, provided that they pass
 * the same tag to @ref bio_get_coro_data.
 * This allows a coroutine to quickly expose its states to others.
 *
 * "Module-private" data can be implemented by declaring the tag as a static
 * global variable in a .c/.cpp file.
 * Hence, only functions in the same file can call @ref bio_set_coro_data and
 * @ref bio_get_coro_data.
 *
 * This is provided as an additional runtime safety check.
 * Since all bio_coro_t handles are interchangable and the user data is just an
 * untyped `void*` pointer, it is possible to incorrectly pass the wrong
 * coroutine to the wrong function that does not know how to cast the pointer
 * back to the correct concrete type, resulting in undefined behaviour.
 * The tag acts as a type check and access check on the data.
 *
 * @param data An arbitrary pointer
 * @param tag A unique tag
 *
 * @see bio_get_coro_data
 * @see bio_tag_t
 */
void
bio_set_coro_data(void* data, const bio_tag_t* tag);

/**
 * Retrieve the data associated with a coroutine
 *
 * This returns the data that was set with @ref bio_set_coro_data.
 *
 * If @ref bio_set_coro_data was never called by the target coroutine, this will return `NULL`.
 *
 * If the @p tag is not the same as what was previously passed to @ref bio_set_coro_data, this will return `NULL`.
 *
 * If the targeted @p coro has terminated, this will also return `NULL`.
 *
 * Typically, the associated data is stack-allocated by the owning coroutine.
 * Since it is safe to call this on a dead coroutine, it is unnecessary and even
 * slightly less efficient to check a coroutine's liveness before
 * calling this function.
 *
 * @param coro The coroutine to retrieve data from
 * @param tag A unique tag
 * @return The associated data or NULL
 *
 * @see bio_set_coro_data
 */
void*
bio_get_coro_data(bio_coro_t coro, const bio_tag_t* tag);

/**
 * Give a human-readable name to the calling coroutine
 *
 * Using the default logger, this name will be displayed instead of the coroutine
 * handle's numeric representation (e.g: `main` instead of `1:0`).
 *
 * It is by design that a coroutine can only name itself, hence the lack of a
 * `coro` argument in this function.
 *
 * @see bio_get_coro_name
 */
void
bio_set_coro_name(const char* name);

/**
 * Get a human-readable name for the coroutine if it has one.
 *
 * This may return NULL if a name was not given.
 *
 * @see bio_set_coro_name
 */
const char*
bio_get_coro_name(bio_coro_t coro);

/**
 * Get a coroutine-local storage (CLS) object.
 *
 * This is used when something needs to be (lazily) created once in each coroutine.
 * The CLS spec should be a global variable in a .c/.cpp file and passed as a pointer:
 *
 * @code
 * typedef struct {
 *    int foo;
 * } my_cls_t;
 *
 * static void
 * init_my_cls(void* ptr) {
 *     my_cls_t* cls = ptr;
 *     *cls = (my_cls_t){ .foo = 42 };
 * }
 *
 * static const bio_cls_t MY_CLS = {
 *     .size = sizeof(my_cls_t),
 *     .init = &init_my_cls,
 * };
 *
 * static void entrypoint(void* userdata) {
 *     my_cls_t* cls = bio_get_cls(&MY_CLS);
 *     assert(cls.foo == 42);  // Initialized
 *
 *     my_cls_t* cls2 = bio_get_cls(&MY_CLS);
 *     assert(cls == cls2);  // The same object
 * }
 * @endcode
 *
 * The memory for the CLS object will only be allocated the first time a coroutine
 * calls this function.
 * The optional @ref bio_cls_t::init callback will be invoked to initialize
 * this memory block.
 *
 * Subsequent calls to `bio_get_cls` from the same coroutine with the same
 * @p cls pointer will return the same object.
 *
 * When a coroutine terminates, the optional @ref bio_cls_t::cleanup callback
 * will be invoked.
 * The associated memory will be automatically freed.
 *
 * Internally, the default logger use CLS to allocate a private format buffer
 * for each coroutine.
 * This avoids the problem of corrupting log content when a coroutine inevitably
 * gets context-switched since logging would involve some sort of I/O.
 *
 * @param cls The specification for the CLS.
 *   Using the same pointer will return the same CLS object.
 * @return The CLS object
 */
void*
bio_get_cls(const bio_cls_t* cls);

/**@}*/

/**
 * @defgroup handle Handle
 *
 * [Handles are the better pointers](https://floooh.github.io/2018/06/17/handles-vs-pointers.html).
 *
 * The mechanism to manage handles are exposed so that user programs can also
 * make use of it.
 *
 * bio uses handles for all of its resources.
 * This alleviates the problems of stale references and double free.
 * In a concurrent environment, the lifetime of resources are often tricky to determine.
 * Handles are safe to pass around and act on.
 * Any operations on a stale handle will be noop.
 *
 * Handle enables the following pattern:
 *
 * @code{.c}
 * void client_handler(void* userdata) {
 *     bio_socket_t socket;  // Assume we got this somehow
 *
 *     // Spawn a writer, giving it the same socket
 *     bio_writer_t writer = bio_spawn(writer_entry, &socket);
 *
 *     char buf[1024];
 *     bio_error_t error = { 0 };
 *     while (true) {
 *         // Read from the socket
 *         size_t bytes_received = bio_net_recv(socket, buf, sizeof(buf), &error);
 *         if (bio_has_error(&error)) {
 *             // Break out of the loop if there is an error
 *             break;
 *         }
 *
 *         // Handle message and maybe message writer for a response
 *     }
 *
 *     // Potential double free but it is safe
 *     // This will also cause the writer to terminate
 *     bio_net_close(socket, NULL);
 *
 *     // Wait for writer to actually terminate
 *     bio_join(writer);
 * }
 *
 * void writer_entry(void* userdata) {
 *     bio_socket_t socket = *(bio_socket_t*)userdata;
 *
 *     bio_error_t error = { 0 };
 *     while (true) {
 *         // Get a message from some source
 *         // Send it
 *         size_t bytes_sent = bio_net_send(socket, msg, msg_len, &error);
 *         if (bio_has_error(&error)) {
 *             // Break out of the loop if there is an error
 *             break;
 *         }
 *     }
 *
 *     // Potential double free but it is safe
 *     // This also cause the reader to terminate
 *     bio_net_close(socket, NULL);
 * }
 * @endcode
 *
 * In the above example, as soon as either the reader or the writer coroutine
 * encounters an I/O error, it will break out of the loop and close the socket.
 * This in turn cause the other coroutine to also break out.
 * @ref bio_net_close will be called twice on the same socket but it is safe
 * and the associated resources will only be released once.
 *
 * By making double free safe to execute, user code can be greatly simplified
 * since there is no need to track when a resource was freed.
 * In fact, freeing a resource that another coroutine is waiting on is the idiomatic
 * way to signal to that coroutine to terminate.
 * For example, in a web server, a route handler can close the listening socket
 * to tell the acceptor coroutine to stop accepting new connections.
 * This can be used to implement a "remote shutdown".
 *
 * @{
 */

/**
 * Create a new handle
 *
 * A handle is associated with a @ref bio_tag_t "tag".
 * To retrieve the associated pointer, the same tag must be provided.
 * This is a form of runtime type checking.
 *
 * For compile time, it is recommended to wrap the returned `bio_handle_t` value
 * in a struct:
 *
 * @code
 * typedef {
 *     bio_handle_t handle;
 * } my_handle_type_t;
 *
 * void function_that_uses_my_handle(my_handle_type_t handle);
 * @endcode
 *
 * In fact, this is how bio exposes all of its handles.
 *
 * @param obj The object associated with the handle
 * @param tag The "type" tag for the handle
 * @return A handle
 *
 * @see bio_resolve_handle
 * @see bio_close_handle
 */
bio_handle_t
bio_make_handle(void* obj, const bio_tag_t* tag);

/**
 * Resolve a handle, returning the associated pointer
 *
 * If the provided @p tag is not the same as what was previously passed to
 * @ref bio_make_handle, this will return `NULL`.
 *
 * If @ref bio_close_handle was called on the handle, this will return `NULL`.
 *
 * A function accepting handle should always check for `NULL` and become noop
 * on a `NULL` pointer.
 * This is how all bio functions behave.
 *
 * Example:
 *
 * @code
 * void
 * bio_raise_signal(bio_signal_t ref) {
 *     bio_signal_impl_t* signal = bio_resolve_handle(ref.handle, &BIO_SIGNAL_HANDLE);
 *     if (signal != NULL) {
 *         // Actual code to raise signal
 *     }
 * }
 * @endcode
 */
void*
bio_resolve_handle(bio_handle_t handle, const bio_tag_t* tag);

/**
 * Close a handle and invalidate it
 *
 * This is similar to @ref bio_resolve_handle but it also invalidates the handle.
 *
 * Take note that the associated pointer is also returned so that the calling
 * code can perform cleanup of the associated data.
 *
 * Just like with @ref bio_resolve_handle, always check for a `NULL` return
 * and make it noop.
 */
void*
bio_close_handle(bio_handle_t handle, const bio_tag_t* tag);

/**
 * Retrieve the associated tag of a handle.
 *
 * This should only be used for debugging.
 */
const bio_tag_t*
bio_handle_info(bio_handle_t handle);

/// Compare two handles
static inline int
bio_handle_compare(bio_handle_t lhs, bio_handle_t rhs) {
	if (lhs.index != rhs.index) {
		return lhs.index - rhs.index;
	} else {
		return lhs.gen - rhs.gen;
	}
}

/**@}*/

/**
 * @defgroup logging Logging
 *
 * @brief Write text to console or file
 *
 * There are several @ref BIO_LOG "logging macros" that should be used instead of the raw
 * @ref bio_log function.
 *
 * @remarks
 *   By default, there is no logger configured.
 *   See @ref file_logger.
 *
 * @see bio_options_t::log_options
 *
 * @{
 */

/**
 * Raw logging function.
 *
 * Use the @ref BIO_LOG "logging macros" instead.
 *
 * @ingroup logging
 */
BIO_FORMAT_ATTRIBUTE(4, 5)
void
bio_log(
	bio_log_level_t level,
	const char* file,
	int line,
	const char* fmt,
	...
);

/**
 * Add a new logger
 *
 * @param min_level Minimum log level for this logger
 * @param log_fn The log function
 * @param userdata Arbitrary data passed to the log function for every message
 * @return A logger handle
 */
bio_logger_t
bio_add_logger(bio_log_level_t min_level, bio_log_fn_t log_fn, void* userdata);

/**
 * Remove a previously configured logger
 *
 * The log function will be called with `NULL` arguments, except for @p userdata.
 * This allows it to have a final chance to "flush" all buffered messages.
 */
void
bio_remove_logger(bio_logger_t logger);

/// Change the minimum log level for a given logger
void
bio_set_min_log_level(bio_logger_t logger, bio_log_level_t level);

/**@}*/

/**
 * @defgroup error Error handling
 * @{
 */

static inline bool
bio_has_error(bio_error_t* error) {
	return error != NULL && error->tag != NULL;
}

static inline const char*
bio_strerror(bio_error_t* error) {
	if (error != NULL && error->tag != NULL) {
		return error->strerror(error->code);
	} else {
		return NULL;
	}
}

static inline void
bio_clear_error(bio_error_t* error) {
	if (error != NULL) { error->tag = NULL; }
}

/**@}*/

/**
 * @defgroup misc Miscellaneous functions
 * @{
 */

/**
 * Run a function in the async thread pool
 *
 * When lengthy or potentially blocking work needs to be performed, this should
 * be used to avoid blocking the main thread.
 *
 * The number of async thread is configured through @ref bio_options_t::num_threads.
 * More threads will allow more tasks to be executed in parallel.
 *
 * Each thread has a queue whose size is configured through @ref bio_options_t::queue_size.
 * A larger queue size will allow more tasks to be pending.
 *
 * bio will always try to load balance the thread pool and send a new task to the least loaded thread.
 * However, when all queues are full, the calling coroutine will enter a busy wait loop.
 * This is done using @ref bio_yield so other coroutines still get to run.
 *
 * Internally, bio also uses the async thread pool to convert a potentially
 * blocking syscall to an asynchronous one when there is no async equivalence.
 *
 * @param task Entrypoint of the function
 * @param userdata Arbitrary userdata passed to the function.
 *   This data must remain valid until the function finishes.
 *   Since this function does not return a value, any result should be written to this data instead.
 * @param signal The signal that will be raised when @p task finishes
 *   execution
 */
void
bio_run_async(bio_entrypoint_t task, void* userdata, bio_signal_t signal);

/// Convenient function to start an async task and wait for it to complete.
static inline void
bio_run_async_and_wait(bio_entrypoint_t task, void* userdata) {
	bio_signal_t signal = bio_make_signal();
	bio_run_async(task, userdata, signal);
	bio_wait_for_one_signal(signal);
}

/**
 * Return the current time in milliseconds
 *
 * Take note that this is taken from an undetermined platform-dependent point.
 * That is to say, it is only suitable for measuring intervals and has no
 * relation to UNIX time or boot time.
 */
bio_time_t
bio_current_time_ms(void);

/**@}*/

#endif
