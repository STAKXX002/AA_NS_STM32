#include "axis.h"
#include "critical.h"
#include <stddef.h>

/* ---------------------------------------------------------------------
 * Pin table. Draft for the AA breakout: axes 1-2 reuse the Swapping
 * system's Z1/Z2 pins and the freed PB9 (fan relay) / PB11; axes 3-4 sit
 * on GPIOC. Avoid: PA2/PA3 (console), PA13/PA14 (SWD), PB3 (SWO),
 * PC13 (user button), PA5 (LED). Update this table when the PCB is
 * redesigned - it is the only place the pins are defined.
 * Endstop polarity: see ENDSTOP_ACTIVE_LOW in aa_config.h.
 * ------------------------------------------------------------------- */
typedef struct {
    GPIO_TypeDef *step_port; uint16_t step_pin;
    GPIO_TypeDef *dir_port;  uint16_t dir_pin;
    GPIO_TypeDef *en_port;   uint16_t en_pin;   /* active-LOW */
    GPIO_TypeDef *min_port;  uint16_t min_pin;
    GPIO_TypeDef *max_port;  uint16_t max_pin;
    bool dir_invert;                            /* flip if the actuator is mounted mirrored */
} AxisHw;

static const AxisHw hw[NUM_AXES] = {
    /* A1 */ { GPIOB, GPIO_PIN_0,  GPIOB, GPIO_PIN_1,  GPIOB, GPIO_PIN_2,  GPIOB, GPIO_PIN_10, GPIOB, GPIO_PIN_11, false },
    /* A2 */ { GPIOB, GPIO_PIN_4,  GPIOB, GPIO_PIN_5,  GPIOB, GPIO_PIN_6,  GPIOB, GPIO_PIN_8,  GPIOB, GPIO_PIN_9,  false },
    /* A3 */ { GPIOC, GPIO_PIN_0,  GPIOC, GPIO_PIN_1,  GPIOC, GPIO_PIN_2,  GPIOC, GPIO_PIN_3,  GPIOC, GPIO_PIN_4,  false },
    /* A4 */ { GPIOC, GPIO_PIN_5,  GPIOC, GPIO_PIN_6,  GPIOC, GPIO_PIN_8,  GPIOC, GPIO_PIN_9,  GPIOC, GPIO_PIN_10, false },
};

typedef struct {
    volatile long     pos;
    volatile long     target;
    volatile long     acc;               /* ticks since last step */
    volatile uint32_t move_start_tick;
    volatile bool     step_high;         /* STEP is high, clear on next tick */
    volatile bool     enabled;
    volatile AxisStop stop;
    uint8_t  min_cnt, max_cnt;           /* debounce integrators, 0..DEBOUNCE */
    volatile bool min_state, max_state;
} AxisState;

static AxisState ax[NUM_AXES];
static volatile uint32_t isr_ticks = 0;
static TIM_HandleTypeDef *tim = NULL;

/* ---- GPIO helpers (HAL_GPIO_* are plain register accesses - ISR-safe, and
 * the cost is negligible next to the 100 us tick; going through the HAL also
 * lets the host-side logic tests in host_tests/ substitute a fake GPIO) ---- */
static inline void pin_high(GPIO_TypeDef *p, uint16_t pin) { HAL_GPIO_WritePin(p, pin, GPIO_PIN_SET); }
static inline void pin_low (GPIO_TypeDef *p, uint16_t pin) { HAL_GPIO_WritePin(p, pin, GPIO_PIN_RESET); }
static inline bool pin_read(GPIO_TypeDef *p, uint16_t pin) { return HAL_GPIO_ReadPin(p, pin) == GPIO_PIN_SET; }

static void dir_write(int i, bool positive) {
    if (positive ^ hw[i].dir_invert) pin_high(hw[i].dir_port, hw[i].dir_pin);
    else                             pin_low (hw[i].dir_port, hw[i].dir_pin);
}

/* Debounce: integrator saturating in [0, N]. State goes active when it
 * reaches N and inactive when it falls back to 0, so a bouncing switch
 * has to agree for 5 ms either way before the state changes. */
static void filter(uint8_t *cnt, volatile bool *state, bool pressed) {
    if (pressed) { if (*cnt < ENDSTOP_DEBOUNCE_TICKS) (*cnt)++; }
    else         { if (*cnt > 0) (*cnt)--; }
    if (*cnt >= ENDSTOP_DEBOUNCE_TICKS) *state = true;
    else if (*cnt == 0)                 *state = false;
}

void axis_init(TIM_HandleTypeDef *htim) {
    tim = htim;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    for (int i = 0; i < NUM_AXES; i++) {
        /* Safe levels first: STEP/DIR low, driver disabled (EN high). */
        pin_low (hw[i].step_port, hw[i].step_pin);
        pin_low (hw[i].dir_port,  hw[i].dir_pin);
        pin_high(hw[i].en_port,   hw[i].en_pin);

        g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW;
        g.Pin = hw[i].step_pin; HAL_GPIO_Init(hw[i].step_port, &g);
        g.Pin = hw[i].dir_pin;  HAL_GPIO_Init(hw[i].dir_port,  &g);
        g.Pin = hw[i].en_pin;   HAL_GPIO_Init(hw[i].en_port,   &g);

        g.Mode = GPIO_MODE_INPUT; g.Pull = ENDSTOP_ACTIVE_LOW ? GPIO_PULLUP : GPIO_PULLDOWN;
        g.Pin = hw[i].min_pin;  HAL_GPIO_Init(hw[i].min_port,  &g);
        g.Pin = hw[i].max_pin;  HAL_GPIO_Init(hw[i].max_port,  &g);

        ax[i].pos = ax[i].target = ax[i].acc = 0;
        ax[i].step_high = false;
        ax[i].stop = STOP_NONE;
        ax[i].min_cnt = ax[i].max_cnt = 0;
        ax[i].min_state = ax[i].max_state = false;
        ax[i].enabled = false;
    }
    axis_enable_all(true);
}

void axis_enable(int i, bool on) {
    CRITICAL_ENTER();
    ax[i].enabled = on;
    if (!on) ax[i].target = ax[i].pos;
    CRITICAL_EXIT();
    if (on) pin_low (hw[i].en_port, hw[i].en_pin);
    else    pin_high(hw[i].en_port, hw[i].en_pin);
}

void axis_enable_all(bool on) {
    for (int i = 0; i < NUM_AXES; i++) axis_enable(i, on);
}

void axis_move_to(int i, long target) {
    CRITICAL_ENTER();
    ax[i].stop = STOP_NONE;
    ax[i].target = target;
    ax[i].move_start_tick = isr_ticks;
    ax[i].acc = 0;
    if (target != ax[i].pos) dir_write(i, target > ax[i].pos);
    CRITICAL_EXIT();
}

void axis_halt(int i) {
    CRITICAL_ENTER();
    ax[i].target = ax[i].pos;
    CRITICAL_EXIT();
}

void axis_set_zero(int i) {
    CRITICAL_ENTER();
    ax[i].pos = 0;
    ax[i].target = 0;
    ax[i].acc = 0;
    CRITICAL_EXIT();
}

long axis_pos(int i) {
    CRITICAL_ENTER();
    long p = ax[i].pos;
    CRITICAL_EXIT();
    return p;
}

bool axis_done(int i) {
    CRITICAL_ENTER();
    bool d = (ax[i].pos == ax[i].target);
    CRITICAL_EXIT();
    return d;
}

bool axis_min_active(int i) { return ax[i].min_state; }
bool axis_max_active(int i) { return ax[i].max_state; }

AxisStop axis_stop_reason(int i) { return ax[i].stop; }
void     axis_clear_stop(int i)  { ax[i].stop = STOP_NONE; }

void axis_force_safe_outputs(void) {
    for (int i = 0; i < NUM_AXES; i++) pin_high(hw[i].en_port, hw[i].en_pin);
}

/* TIM3 tick (100 us). HAL weak-callback override - this module owns the
 * timer and every axis, so nothing else defines it. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (tim == NULL || htim->Instance != tim->Instance) return;

    isr_ticks++;

    /* Finish pulses started last tick first: guarantees a full ~100 us
     * HIGH time on STEP (shorter pulses caused intermittent missed steps). */
    for (int i = 0; i < NUM_AXES; i++) {
        if (ax[i].step_high) {
            pin_low(hw[i].step_port, hw[i].step_pin);
            ax[i].step_high = false;
        }
    }

    for (int i = 0; i < NUM_AXES; i++) {
        AxisState *a = &ax[i];

        filter(&a->min_cnt, &a->min_state, pin_read(hw[i].min_port, hw[i].min_pin) != ENDSTOP_ACTIVE_LOW);
        filter(&a->max_cnt, &a->max_state, pin_read(hw[i].max_port, hw[i].max_pin) != ENDSTOP_ACTIVE_LOW);

        if (!a->enabled) continue;
        long diff = a->target - a->pos;
        if (diff == 0) continue;
        int dir = (diff > 0) ? 1 : -1;

        /* Never step toward a pressed switch. Latch which one so the main
         * loop can decide whether that was the goal (homing) or a fault. */
        if ((dir < 0 && a->min_state) || (dir > 0 && a->max_state)) {
            a->target = a->pos;
            a->stop = (dir < 0) ? STOP_MIN : STOP_MAX;
            continue;
        }

        uint32_t elapsed = isr_ticks - a->move_start_tick;
        long interval;
        if (elapsed >= (uint32_t)RAMP_TICKS) {
            interval = STEP_INTERVAL_CRUISE;
        } else {
            long delta = STEP_INTERVAL_START - STEP_INTERVAL_CRUISE;
            interval = STEP_INTERVAL_START - ((delta * (long)elapsed) / RAMP_TICKS);
        }

        if (++a->acc >= interval) {
            a->acc = 0;
            pin_high(hw[i].step_port, hw[i].step_pin);
            a->pos += dir;
            a->step_high = true;
        }
    }
}
