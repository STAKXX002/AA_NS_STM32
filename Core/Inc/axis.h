#ifndef AXIS_H
#define AXIS_H

#include "main.h"
#include "aa_config.h"
#include <stdint.h>
#include <stdbool.h>

/* Low-level stepper axis engine. Owns TIM3 and the NUM_AXES axes: step
 * pulse generation, ramping, and endstop sampling all happen in the 100 us
 * tick ISR, so an endstop halts its axis within ~100 us regardless of how
 * busy the main loop is. Nothing outside this module touches the pins.
 *
 * Direction convention: positive = toward MAX, negative = toward MIN. */

typedef enum { STOP_NONE = 0, STOP_MIN, STOP_MAX } AxisStop;

/* Configures the axis GPIO (so the CubeMX .ioc is not the source of truth
 * for these pins - see axis.c pin table), stores the timer handle and
 * enables all drivers. Call after MX_GPIO_Init()/MX_TIM3_Init(), before
 * starting the timer interrupt. */
void axis_init(TIM_HandleTypeDef *htim);

void axis_enable(int i, bool on);
void axis_enable_all(bool on);

/* Start a ramped move to an absolute step position. Clears any latched stop. */
void axis_move_to(int i, long target);
/* Stop stepping immediately (target = current position). */
void axis_halt(int i);
/* Declare the current position to be zero. */
void axis_set_zero(int i);

long axis_pos(int i);
bool axis_done(int i);             /* pos == target */

/* Debounced endstop state (true = switch pressed). */
bool axis_min_active(int i);
bool axis_max_active(int i);

/* Which endstop (if any) halted the last move from inside the ISR.
 * axis_stop_reason() peeks, axis_clear_stop() resets. */
AxisStop axis_stop_reason(int i);
void     axis_clear_stop(int i);

/* Raw-GPIO "drivers off", no module state needed. Safe from Error_Handler(). */
void axis_force_safe_outputs(void);

#endif /* AXIS_H */
