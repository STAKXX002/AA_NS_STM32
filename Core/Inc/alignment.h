#ifndef ALIGNMENT_H
#define ALIGNMENT_H

#include "main.h"
#include "aa_config.h"
#include <stdint.h>
#include <stdbool.h>

/* Active Alignment: NUM_AXES independent actuators, each with its own
 * state machine (UNHOMED -> HOMING -> READY <-> MOVING, plus an optional
 * CAL run that measures travel between the MIN and MAX endstops).
 *
 * Axis numbers in this API are 0-based; the UART protocol uses 1-based
 * (A1..A4) and commands.c converts. */

/* Call once at startup, after MX_TIM3_Init(). */
void alignment_init(TIM_HandleTypeDef *htim);

/* Call once per main-loop iteration. Runs every axis's state machine. */
void alignment_update(uint32_t now);

/* Commands - the caller (commands.c) checks fault/busy/homed with the
 * queries below first. */
void alignment_home(int axis, bool measure_travel); /* measure_travel => CAL */
void alignment_move_steps(int axis, long steps);    /* absolute, from zero   */
void alignment_halt(int axis);                      /* stop ONE axis         */
void alignment_reset(void);                         /* RST: stop all, zero, un-home */
void alignment_print_status(void);                  /* POS */

/* Query functions - the only window into per-axis state. */
bool alignment_axis_busy(int axis);     /* homing / cal / moving */
bool alignment_axis_homed(int axis);    /* READY or MOVING: zero is valid */
bool alignment_any_busy(void);
long alignment_travel_steps(int axis);  /* usable travel from zero */

/* Called by system_fault(): stop all axes and disable the drivers
 * without printing (system.c owns the FAULT message). */
void alignment_emergency_stop(void);

#endif /* ALIGNMENT_H */
