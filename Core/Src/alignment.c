#include "alignment.h"
#include "axis.h"
#include "system.h"
#include <stdio.h>
#include <stdlib.h>

typedef enum {
    AX_UNHOMED,       /* zero not valid                                  */
    AX_HOMING,        /* driving toward MIN                              */
    AX_HOME_BACKOFF,  /* off the MIN switch, will set zero               */
    AX_CAL_MAX,       /* (CAL only) driving toward MAX to measure travel */
    AX_CAL_BACKOFF,   /* (CAL only) off the MAX switch                   */
    AX_READY,         /* homed and stationary                            */
    AX_MOVING         /* homed, executing MOVE                           */
} AxState;

static AxState  st[NUM_AXES];
static uint32_t stateStart[NUM_AXES];
static bool     measuring[NUM_AXES];     /* this homing is part of a CAL */
static long     travel[NUM_AXES];        /* usable travel from zero, steps */
static long     calMaxPos[NUM_AXES];

static void init_axis_state(int i) {
    st[i] = AX_UNHOMED;
    measuring[i] = false;
    travel[i] = AXIS_TRAVEL_MM * STEPS_PER_MM;
}

/* ---- small helpers ---- */

/* steps -> "[-]W.mmm" without float printf */
static void fmt_mm(char *out, size_t n, long steps) {
    long a = labs(steps);
    long milli = (a * 1000L + STEPS_PER_MM / 2) / STEPS_PER_MM;
    snprintf(out, n, "%s%ld.%03ld", steps < 0 ? "-" : "", milli / 1000L, milli % 1000L);
}

static void fault_axis(int i, const char *msg) {
    char buf[40];
    snprintf(buf, sizeof(buf), "A%d %s", i + 1, msg);
    system_fault(buf);
}

static const char *state_name(AxState s) {
    switch (s) {
    case AX_UNHOMED:      return "UNHOMED";
    case AX_HOMING:       return "HOMING";
    case AX_HOME_BACKOFF: return "HOMING";
    case AX_CAL_MAX:      return "CAL";
    case AX_CAL_BACKOFF:  return "CAL";
    case AX_READY:        return "READY";
    case AX_MOVING:       return "MOVING";
    }
    return "?";
}

/* ---- public API ---- */

void alignment_init(TIM_HandleTypeDef *htim) {
    axis_init(htim);
    for (int i = 0; i < NUM_AXES; i++) init_axis_state(i);
}

void alignment_home(int i, bool measure_travel) {
    /* A switch already pressed before we move is ambiguous (stuck switch,
     * broken wire, or axis really sitting on it) - refuse rather than guess. */
    if (axis_min_active(i)) { fault_axis(i, "MIN ACTIVE"); return; }
    if (axis_max_active(i)) { fault_axis(i, "MAX ACTIVE"); return; }

    axis_enable(i, true);
    axis_set_zero(i);
    axis_move_to(i, -SEARCH_STEPS);
    measuring[i] = measure_travel;
    st[i] = AX_HOMING;
    stateStart[i] = HAL_GetTick();
    printf("A%d %s\r\n", i + 1, measure_travel ? "CAL" : "HOME");
}

void alignment_move_steps(int i, long steps) {
    char b[16];
    if (steps == axis_pos(i)) {
        fmt_mm(b, sizeof(b), steps);
        printf("A%d DONE %s\r\n", i + 1, b);
        return;
    }
    axis_move_to(i, steps);
    st[i] = AX_MOVING;
    stateStart[i] = HAL_GetTick();
    fmt_mm(b, sizeof(b), steps);
    printf("A%d MOVE %s\r\n", i + 1, b);
}

void alignment_halt(int i) {
    axis_halt(i);
    /* A halted move keeps its position (stepping stopped between steps);
     * a halted home/cal has no valid zero. */
    st[i] = (st[i] == AX_MOVING) ? AX_READY : AX_UNHOMED;
    measuring[i] = false;
    printf("A%d HALT\r\n", i + 1);
}

void alignment_reset(void) {
    axis_enable_all(true);
    for (int i = 0; i < NUM_AXES; i++) {
        axis_halt(i);
        axis_set_zero(i);
        axis_clear_stop(i);
        init_axis_state(i);
    }
}

void alignment_emergency_stop(void) {
    for (int i = 0; i < NUM_AXES; i++) {
        axis_halt(i);
        init_axis_state(i);
    }
    axis_enable_all(false);
}

bool alignment_axis_busy(int i)  { return st[i] != AX_UNHOMED && st[i] != AX_READY; }
bool alignment_axis_homed(int i) { return st[i] == AX_READY || st[i] == AX_MOVING; }
long alignment_travel_steps(int i) { return travel[i]; }

bool alignment_any_busy(void) {
    for (int i = 0; i < NUM_AXES; i++) if (alignment_axis_busy(i)) return true;
    return false;
}

void alignment_print_status(void) {
    for (int i = 0; i < NUM_AXES; i++) {
        char p[16], t[16];
        fmt_mm(p, sizeof(p), axis_pos(i));
        fmt_mm(t, sizeof(t), travel[i]);
        printf("A%d %s POS %s TRAVEL %s MIN %d MAX %d\r\n", i + 1, state_name(st[i]),
               p, t, axis_min_active(i) ? 1 : 0, axis_max_active(i) ? 1 : 0);
    }
}

/* ---- per-axis state machine ---- */

static void update_axis(int i, uint32_t now) {
    AxisStop stop = axis_stop_reason(i);
    uint32_t elapsed = now - stateStart[i];
    char b[16];

    switch (st[i]) {

    case AX_HOMING:
        if (stop == STOP_MIN) {
            axis_clear_stop(i);
            printf("A%d MIN HIT\r\n", i + 1);
            axis_move_to(i, axis_pos(i) + BACKOFF_STEPS);
            st[i] = AX_HOME_BACKOFF;
            stateStart[i] = now;
        } else if (elapsed > HOME_TIMEOUT_MS) {
            fault_axis(i, "HOME TIMEOUT");
        } else if (axis_done(i)) {
            fault_axis(i, "MIN NOT FOUND");   /* travelled SEARCH_MM, no switch */
        }
        break;

    case AX_HOME_BACKOFF:
        if (elapsed > BACKOFF_TIMEOUT_MS) {
            fault_axis(i, "BACKOFF TIMEOUT");
        } else if (axis_done(i)) {
            if (axis_min_active(i)) { fault_axis(i, "MIN STUCK"); break; }
            axis_set_zero(i);
            axis_clear_stop(i);
            if (measuring[i]) {
                axis_move_to(i, SEARCH_STEPS);
                st[i] = AX_CAL_MAX;
                stateStart[i] = now;
            } else {
                st[i] = AX_READY;
                printf("A%d HOMED\r\n", i + 1);
            }
        }
        break;

    case AX_CAL_MAX:
        if (stop == STOP_MAX) {
            axis_clear_stop(i);
            calMaxPos[i] = axis_pos(i);
            printf("A%d MAX HIT\r\n", i + 1);
            axis_move_to(i, calMaxPos[i] - BACKOFF_STEPS);
            st[i] = AX_CAL_BACKOFF;
            stateStart[i] = now;
        } else if (elapsed > CAL_TIMEOUT_MS) {
            fault_axis(i, "CAL TIMEOUT");
        } else if (axis_done(i)) {
            fault_axis(i, "MAX NOT FOUND");
        }
        break;

    case AX_CAL_BACKOFF:
        if (elapsed > BACKOFF_TIMEOUT_MS) {
            fault_axis(i, "BACKOFF TIMEOUT");
        } else if (axis_done(i)) {
            if (axis_max_active(i)) { fault_axis(i, "MAX STUCK"); break; }
            travel[i] = axis_pos(i);          /* usable travel keeps BACKOFF_MM margin at MAX */
            measuring[i] = false;
            st[i] = AX_READY;
            fmt_mm(b, sizeof(b), travel[i]);
            printf("A%d CAL OK TRAVEL %s\r\n", i + 1, b);
        }
        break;

    case AX_MOVING:
        /* A switch during a normal move means the position is wrong (lost
         * steps, wrong travel setting, mechanical slip) - never "success". */
        if (stop == STOP_MIN)      { fault_axis(i, "MIN HIT"); }
        else if (stop == STOP_MAX) { fault_axis(i, "MAX HIT"); }
        else if (elapsed > MOVE_TIMEOUT_MS) { fault_axis(i, "MOVE TIMEOUT"); }
        else if (axis_done(i)) {
            st[i] = AX_READY;
            fmt_mm(b, sizeof(b), axis_pos(i));
            printf("A%d DONE %s\r\n", i + 1, b);
        }
        break;

    case AX_UNHOMED:
    case AX_READY:
    default:
        break;
    }
}

void alignment_update(uint32_t now) {
    if (system_in_fault()) return;
    for (int i = 0; i < NUM_AXES; i++) {
        update_axis(i, now);
        if (system_in_fault()) return;   /* a fault stops everything */
    }
}
