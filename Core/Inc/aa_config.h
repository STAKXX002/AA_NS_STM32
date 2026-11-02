#ifndef AA_CONFIG_H
#define AA_CONFIG_H

/* Active Alignment (AA) - tunables in ONE place.
 *
 * Items marked VERIFY are carried over from the Swapping-system firmware
 * (AA_NS_STM_PORT) and must be checked against the AA lead screws /
 * drivers before trusting any mm figure. Run `CAL <n>` on the real
 * mechanism: it reports the measured travel so TRAVEL_MM can be set. */

#define NUM_AXES              4

/* ---- mechanics ---- */
#define STEPS_PER_MM          200L   /* VERIFY: depends on screw pitch + microstepping */
#define AXIS_TRAVEL_MM        40L    /* VERIFY: usable travel from zero until CAL measures it */
#define BACKOFF_MM            5L     /* zero sits this far off the MIN switch; also the
                                      * margin kept at the MAX end after CAL */
#define SEARCH_MM             200L   /* max distance searched for an endstop before
                                      * declaring "NOT FOUND" */

#define BACKOFF_STEPS         (BACKOFF_MM * STEPS_PER_MM)
#define SEARCH_STEPS          (SEARCH_MM  * STEPS_PER_MM)

/* ---- step generation (TIM3 tick = 100 us) ---- */
#define TICK_US               100UL
#define STEP_INTERVAL_START   60L    /* slow start: 10 kHz / 60 ~= 167 steps/s */
#define STEP_INTERVAL_CRUISE  10L    /* 10 kHz / 10 = 1 kHz = 5 mm/s at 200 steps/mm */
#define RAMP_TICKS            5000L  /* ramp duration: 5000 * 100 us = 500 ms */

/* ---- endstops (sampled in the tick ISR) ---- */
/* 1: switch pulls the pin LOW when pressed (internal pull-up used).
 * 0: switch drives the pin HIGH when pressed (internal pull-down used).
 * VERIFY against the red endstop boards: their signal polarity depends on
 * the board version and the NC/NO jumper. Wrong polarity = "MIN ACTIVE"
 * fault on the first HOME, which is safe but confusing. */
#define ENDSTOP_ACTIVE_LOW    1
#define ENDSTOP_DEBOUNCE_TICKS 50    /* 50 * 100 us = 5 ms continuous agreement */

/* ---- timeouts (ms) ---- */
#define HOME_TIMEOUT_MS       60000UL
#define CAL_TIMEOUT_MS        90000UL
#define BACKOFF_TIMEOUT_MS    15000UL
#define MOVE_TIMEOUT_MS       60000UL

#endif /* AA_CONFIG_H */
