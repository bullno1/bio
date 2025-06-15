#ifndef BIO_TIMER_H
#define BIO_TIMER_H

#include "bio.h"

/**
 * @defgroup timer Timer
 *
 * Execute tasks at an interval or after a delay
 *
 * @{
 */

typedef struct {
	bio_handle_t handle;
} bio_timer_t;

typedef enum {
	BIO_TIMER_ONESHOT,
	BIO_TIMER_INTERVAL,
} bio_timer_type_t;

bio_timer_t
bio_create_timer(
	bio_timer_type_t type, bio_time_t timeout_ms,
	bio_entrypoint_t fn, void* userdata
);

bool
bio_is_timer_pending(bio_timer_t timer);

void
bio_reset_timer(bio_timer_t timer, bio_time_t timeout_ms);

void
bio_cancel_timer(bio_timer_t timer);

/**@}*/

#endif
