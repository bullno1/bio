#ifndef BIO_TIMER_H
#define BIO_TIMER_H

#include "bio.h"

/**
 * @defgroup timer Timer
 *
 * Execute a function at an interval or after a delay
 *
 * @{
 */

/// Handle to a timer
typedef struct {
	bio_handle_t handle;
} bio_timer_t;

/// Types of timer
typedef enum {
	BIO_TIMER_ONESHOT,   /**< Trigger only once */
	BIO_TIMER_INTERVAL,  /**< Trigger repeatedly */
} bio_timer_type_t;

/**
 * Create a new timer
 *
 * @remarks
 *   A timer is implemented as a coroutine waiting on its own (delayed) signal.
 *   When a timer of type @ref BIO_TIMER_INTERVAL is no longer needed, it should
 *   be cancelled with @ref bio_cancel_timer.
 *   Otherwise, @ref bio_loop will never terminate.
 *
 * @param type The type of timer
 * @param timeout_ms How often or the delay for the timer in milliseconds
 * @param fn The function to be called
 * @param userdata Data passed to @p fn
 * @return Handle to a timer
 */
bio_timer_t
bio_create_timer(
	bio_timer_type_t type,
	bio_time_t timeout_ms,
	bio_entrypoint_t fn, void* userdata
);

/**
 * Check whether a timer is pending
 *
 * For an uncancelled timer of type @ref BIO_TIMER_INTERVAL, this will always
 * return `true`.
 */
bool
bio_is_timer_pending(bio_timer_t timer);

/**
 * Reset a timer
 *
 * For a timer of type @ref BIO_TIMER_ONESHOT, this will postpone the execution
 * time as if @ref bio_create_timer was just called at this point.
 *
 * For a timer of type @ref BIO_TIMER_INTERVAL, the current wait interval will
 * be cancelled.
 * The timer will begin a new wait interval with value @p timeout_ms.
 *
 * @param timer The timer to reset
 * @param timeout_ms The new timeout
 */
void
bio_reset_timer(bio_timer_t timer, bio_time_t timeout_ms);

/**
 * Cancel a timer.
 *
 * For a timer of type @ref BIO_TIMER_ONESHOT, it is guaranteed that the
 * associated function will not be invoked if the timer has not fired.
 */
void
bio_cancel_timer(bio_timer_t timer);

/**@}*/

#endif
