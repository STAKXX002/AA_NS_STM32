#include "axis.h"
#include "critical.h"
#include <stddef.h>

/* ---------------------------------------------------------------------
 * Pin table for the standalone STM32F446RET6 (LQFP64) on the *ADA_5 PCB.
 * Grouped strictly by physical connector location on the board edges:
 *
 *   DRIVERS (12 pins) - West & South-West edges facing J1-J4:
 *     A1 (J1, top-west):      STEP PC10 (pin 51), DIR PC11 (pin 52), EN PA15 (pin 50)
 *     A2 (J2, upper-west):    STEP PC0  (pin 8),  DIR PC1  (pin 9),  EN PC2  (pin 10)
 *     A3 (J3, lower-west):    STEP PA4  (pin 20), DIR PA5  (pin 21), EN PA6  (pin 22)
 *     A4 (J4, bottom-west):   STEP PA7  (pin 23), DIR PC4  (pin 24), EN PC5  (pin 25)
 *
 *   ENDSTOPS (8 pins) - East edge facing J5 (8-pin vertical header):
 *     Pins 33-40: PB12 A1 MIN, PB13 A1 MAX, PB14 A2 MIN, PB15 A2 MAX,
 *                 PC6  A3 MIN, PC7  A3 MAX, PC8  A4 MIN, PC9  A4 MAX
 *     (Distinct EXTI lines 12, 13, 14, 15, 6, 7, 8, 9 - zero EXTI collisions)
 *
 *   HATCH / L298N (J7, top edge): PB4, PB5, PB6, PB7
 *   RELAY LIGHT   (J7, top edge): PB8 (active low)
 *   PI UART       (J10, top edge): PC12 (TX, pin 53), PD2 (RX, pin 54)
 *   CONSOLE UART  (auxiliary):    PA2 (TX, pin 16), PA3 (RX, pin 17)
 *   SWD DEBUG     (J11, top edge): PA13 (SWDIO), PA14 (SWCLK), NRST (pin 7)
 *   OSCILLATOR    (crystal Y1):   PH0 (OSC_IN), PH1 (OSC_OUT)
 *
 * Update this table when the PCB is redesigned - it is the single source
 * of truth in firmware. Endstop polarity: see ENDSTOP_ACTIVE_LOW in aa_config.h.
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
    /*          STEP               DIR                EN                 MIN                MAX            */
    /* A1 */ { GPIOC, GPIO_PIN_10, GPIOC, GPIO_PIN_11, GPIOA, GPIO_PIN_15, GPIOB, GPIO_PIN_12, GPIOB, GPIO_PIN_13, false },
    /* A2 */ { GPIOC, GPIO_PIN_0,  GPIOC, GPIO_PIN_1,  GPIOC, GPIO_PIN_2,  GPIOB, GPIO_PIN_14, GPIOB, GPIO_PIN_15, false },
    /* A3 */ { GPIOA, GPIO_PIN_4,  GPIOA, GPIO_PIN_5,  GPIOA, GPIO_PIN_6,  GPIOC, GPIO_PIN_6,  GPIOC, GPIO_PIN_7,  false },
    /* A4 */ { GPIOA, GPIO_PIN_7,  GPIOC, GPIO_PIN_4,  GPIOC, GPIO_PIN_5,  GPIOC, GPIO_PIN_8,  GPIOC, GPIO_PIN_9,  false },
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
