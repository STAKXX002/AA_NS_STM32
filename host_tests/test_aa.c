/* Host-side logic tests for the AA firmware: the REAL axis.c, alignment.c,
 * commands.c, hatch.c, relay.c and system.c compiled against a fake GPIO,
 * driven through the UART command path, with a simple physical model of
 * each actuator (screw position + MIN/MAX switches). Runs in about a second;
 * needs no Renode and no hardware. It validates logic and timing-in-ticks,
 * not electrical behaviour. */
#define _GNU_SOURCE
#include "main.h"
#include "aa_config.h"
#include "alignment.h"
#include "commands.h"
#include "hatch.h"
#include "relay.h"
#include "system.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ fake HAL */
GPIO_TypeDef fakeGPIOA, fakeGPIOB, fakeGPIOC;
static uint32_t tick_count;            /* 100 us ticks */
static uint8_t *uart_rx_ptr;
static UART_HandleTypeDef huart_fake = { (void*)1 };
static TIM_HandleTypeDef  htim_fake  = { (void*)2 };

typedef struct { GPIO_TypeDef *p; uint16_t pin; } PinRef;
static const PinRef STEP_PIN[NUM_AXES] = { {GPIOB,GPIO_PIN_0}, {GPIOB,GPIO_PIN_4}, {GPIOC,GPIO_PIN_0}, {GPIOC,GPIO_PIN_5} };
static const PinRef DIR_PIN [NUM_AXES] = { {GPIOB,GPIO_PIN_1}, {GPIOB,GPIO_PIN_5}, {GPIOC,GPIO_PIN_1}, {GPIOC,GPIO_PIN_6} };
static const PinRef EN_PIN  [NUM_AXES] = { {GPIOB,GPIO_PIN_2}, {GPIOB,GPIO_PIN_6}, {GPIOC,GPIO_PIN_2}, {GPIOC,GPIO_PIN_8} };
static const PinRef MIN_PIN [NUM_AXES] = { {GPIOB,GPIO_PIN_10},{GPIOB,GPIO_PIN_8}, {GPIOC,GPIO_PIN_3}, {GPIOC,GPIO_PIN_9} };
static const PinRef MAX_PIN [NUM_AXES] = { {GPIOB,GPIO_PIN_11},{GPIOB,GPIO_PIN_9}, {GPIOC,GPIO_PIN_4}, {GPIOC,GPIO_PIN_10} };

/* physical model */
static long phys[NUM_AXES];            /* real screw position, steps (can differ from firmware pos) */
static long min_trig[NUM_AXES];        /* MIN pressed when phys <= this */
static long max_trig[NUM_AXES];        /* MAX pressed when phys >= this */
static int  glitch_ticks[NUM_AXES];    /* force MIN pressed for N ticks (bounce/noise) */
static long total_steps[NUM_AXES];     /* every STEP rising edge, either direction */

uint32_t HAL_GetTick(void) { return tick_count / 10; }
void HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *g) { (void)p; (void)g; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p, uint16_t pin) { return (p->IDR & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET; }
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState s) {
    uint32_t before = p->ODR;
    if (s) p->ODR |= pin; else p->ODR &= ~(uint32_t)pin;
    for (int i = 0; i < NUM_AXES; i++) {
        if (p == STEP_PIN[i].p && pin == STEP_PIN[i].pin && s && !(before & pin)) {
            bool pos_dir = (DIR_PIN[i].p->ODR & DIR_PIN[i].pin) != 0;
            phys[i] += pos_dir ? 1 : -1;
            total_steps[i]++;
        }
    }
}
int HAL_UART_Receive_IT(UART_HandleTypeDef *h, uint8_t *buf, uint16_t n) { (void)h; (void)n; uart_rx_ptr = buf; return 0; }

static bool en_disabled(int i) { return (EN_PIN[i].p->ODR & EN_PIN[i].pin) != 0; }

/* ------------------------------------------------------------ harness */
static char  *outbuf; static size_t outlen; static size_t mark;
static int failures, checks;

static void set_switch(PinRef r, bool pressed) {      /* active-low */
    if (pressed) r.p->IDR &= ~(uint32_t)r.pin; else r.p->IDR |= r.pin;
}

static void step_ms(void) {
    for (int t = 0; t < 10; t++) {
        for (int i = 0; i < NUM_AXES; i++) {
            bool minp = phys[i] <= min_trig[i] || glitch_ticks[i] > 0;
            if (glitch_ticks[i] > 0) glitch_ticks[i]--;
            set_switch(MIN_PIN[i], minp);
            set_switch(MAX_PIN[i], phys[i] >= max_trig[i]);
        }
        HAL_TIM_PeriodElapsedCallback(&htim_fake);
        tick_count++;
    }
    uint32_t now = HAL_GetTick();
    alignment_update(now);
    hatch_update(now);
    commands_update(now);
}
static void run_ms(uint32_t ms) { while (ms--) step_ms(); }

static const char *out_since_mark(void) { fflush(stdout); outbuf[outlen] = 0; return outbuf + mark; }
static bool seen(const char *s) { return strstr(out_since_mark(), s) != NULL; }
static void new_mark(void) { fflush(stdout); mark = outlen; }

/* Runs until `s` appears in output (since last mark) or timeout; true if seen. */
static bool run_until(const char *s, uint32_t max_ms) {
    for (uint32_t i = 0; i < max_ms; i++) { if (seen(s)) return true; step_ms(); }
    return seen(s);
}
static void send(const char *line) {
    new_mark();
    for (const char *c = line; *c; c++) { *uart_rx_ptr = (uint8_t)*c; HAL_UART_RxCpltCallback(&huart_fake); }
    *uart_rx_ptr = '\n'; HAL_UART_RxCpltCallback(&huart_fake);
    step_ms();
}

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "    FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)
#define SEEN(s)  CHECK(seen(s), "expected \"%s\" in output, got:\n%s", s, out_since_mark())
#define NOT_SEEN(s) CHECK(!seen(s), "unexpected \"%s\" in output:\n%s", s, out_since_mark())

#define MM(x) ((long)((x) * STEPS_PER_MM))

/* Fresh firmware: switch positions are in physical steps; the MIN switch
 * sits at phys=0 and MAX at `travel_mm` further, start `start_mm` from MIN. */
static void fresh(double travel_mm, double start_mm) {
    memset(&fakeGPIOA, 0, sizeof fakeGPIOA); memset(&fakeGPIOB, 0, sizeof fakeGPIOB); memset(&fakeGPIOC, 0, sizeof fakeGPIOC);
    memset(total_steps, 0, sizeof total_steps);
    tick_count = 0;
    for (int i = 0; i < NUM_AXES; i++) {
        min_trig[i] = 0; max_trig[i] = MM(travel_mm); phys[i] = MM(start_mm); glitch_ticks[i] = 0;
    }
    alignment_init(&htim_fake);
    commands_init(&huart_fake);
    relay_init();
    system_clear_fault();
    alignment_reset();
    new_mark();
}

static void home_and_wait(int axis) {
    char cmd[16]; snprintf(cmd, sizeof cmd, "HOME %d", axis);
    send(cmd);
    char want[16]; snprintf(want, sizeof want, "A%d HOMED", axis);
    CHECK(run_until(want, 40000), "A%d did not home:\n%s", axis, out_since_mark());
}

/* ------------------------------------------------------------ tests */
static void t_boot_idle(void) {
    fresh(100, 30);
    run_ms(100);
    for (int i = 0; i < NUM_AXES; i++) CHECK(total_steps[i] == 0, "A%d stepped while idle", i + 1);
    send("POS");
    SEEN("A1 UNHOMED"); SEEN("A4 UNHOMED");
    send("MOVE 1 5");   SEEN("NO HOME");
}

static void t_home_single(void) {
    fresh(100, 30);
    home_and_wait(1);
    SEEN("A1 MIN HIT");
    /* zero sits BACKOFF_MM off the switch, within a few steps of overshoot
     * (5 ms debounce at 5 mm/s = ~5 steps) */
    CHECK(labs(phys[0] - BACKOFF_STEPS) <= 10, "phys=%ld, want ~%ld", phys[0], (long)BACKOFF_STEPS);
    for (int i = 1; i < NUM_AXES; i++) CHECK(total_steps[i] == 0, "A%d moved but was never commanded", i + 1);
    send("POS"); SEEN("A1 READY POS 0.000");
}

static void t_move_and_range(void) {
    fresh(100, 30);
    home_and_wait(1);
    long z = phys[0];
    send("MOVE 1 12.5");  SEEN("A1 MOVE 12.500");
    CHECK(run_until("A1 DONE 12.500", 20000), "move did not finish:\n%s", out_since_mark());
    CHECK(phys[0] - z == MM(12.5), "moved %ld steps, want %ld", phys[0] - z, MM(12.5));
    send("MOVE 1 12.5");  SEEN("A1 DONE 12.500");            /* no-op move */
    send("MOVE 1 -1");    SEEN("RANGE");
    send("MOVE 1 41");    SEEN("RANGE");                       /* default travel is 40 mm */
    send("MOVE 5 1");     SEEN("INVALID");
    send("MOVE 1 abc");   SEEN("INVALID");
    send("MOVE 1 1.2345");SEEN("INVALID");
    send("MOVE 1");       SEEN("INVALID");
    send("MOVE 1 0");     CHECK(run_until("A1 DONE 0.000", 20000), "return to zero failed");
    CHECK(phys[0] == z, "did not return to the same physical spot: %ld vs %ld", phys[0], z);
}

static void t_cal_measures_travel(void) {
    fresh(60, 20);                                  /* MIN->MAX switch distance 60 mm */
    send("CAL 2");
    CHECK(run_until("A2 CAL OK", 90000), "cal did not finish:\n%s", out_since_mark());
    SEEN("A2 MIN HIT"); SEEN("A2 MAX HIT");
    /* zero is 5 mm above MIN; usable travel = 60 - 5 (zero offset) - 5 (max margin) = 50 */
    CHECK(labs(alignment_travel_steps(1) - MM(50)) <= 12, "travel=%ld steps, want ~%ld", alignment_travel_steps(1), MM(50));
    send("MOVE 2 49"); SEEN("A2 MOVE 49.000");
    CHECK(run_until("A2 DONE 49.000", 30000), "move after cal failed");
    send("MOVE 2 51"); SEEN("RANGE");
}

static void t_independent_axes(void) {
    fresh(100, 10);
    phys[1] = MM(25); phys[2] = MM(5); phys[3] = MM(60);
    send("HOME ALL");
    CHECK(run_until("A4 HOMED", 60000), "HOME ALL did not complete:\n%s", out_since_mark());
    for (int i = 1; i <= 4; i++) { char s[16]; snprintf(s, sizeof s, "A%d HOMED", i); SEEN(s); }
    long z[NUM_AXES]; for (int i = 0; i < NUM_AXES; i++) z[i] = phys[i];
    send("MOVE 1 10"); send("MOVE 3 20"); send("MOVE 4 5");
    run_ms(2000);
    SEEN("A4 DONE 5.000");                                    /* shortest move already finished on its own */
    send("HALT 3");    SEEN("A3 HALT");                       /* mid-move halt of ONE axis */
    CHECK(run_until("A1 DONE 10.000", 20000), "A1 should finish independently");
    CHECK(phys[0] - z[0] == MM(10), "A1 moved %ld", phys[0] - z[0]);
    CHECK(phys[3] - z[3] == MM(5),  "A4 moved %ld", phys[3] - z[3]);
    long h = phys[2] - z[2];
    CHECK(h > 0 && h < MM(20), "A3 halted at %ld steps, want partway", h);
    send("POS"); SEEN("A3 READY");                            /* position kept after HALT */
    NOT_SEEN("FAULT");
}

static void t_min_not_found(void) {
    fresh(100, 30);
    min_trig[0] = -MM(100000);                              /* broken/missing MIN switch */
    send("HOME 1");
    CHECK(run_until("FAULT", 90000), "expected a fault, got:\n%s", out_since_mark());
    SEEN("A1 MIN NOT FOUND");
    CHECK(labs(phys[0] - (MM(30) - SEARCH_STEPS)) <= 2, "should stop after exactly SEARCH_MM of travel");
    for (int i = 0; i < NUM_AXES; i++) CHECK(en_disabled(i), "A%d driver still enabled after fault", i + 1);
    send("HOME 2");   SEEN("FAULT - RST REQUIRED");
    send("MOVE 1 1"); SEEN("FAULT - RST REQUIRED");
    send("OPEN");     SEEN("FAULT - RST REQUIRED");
    send("RST");      SEEN("HOME REQUIRED");
    for (int i = 0; i < NUM_AXES; i++) CHECK(!en_disabled(i), "A%d driver not re-enabled by RST", i + 1);
    min_trig[0] = 0;
    phys[0] = MM(30);                                       /* axis put back on the bench */
    home_and_wait(1);
}

static void t_start_on_switch(void) {
    fresh(100, 0);                                         /* axis sitting ON its MIN switch */
    run_ms(20);
    send("HOME 1");
    SEEN("A1 MIN ACTIVE"); SEEN("FAULT");
}

static void t_fault_stops_all(void) {
    fresh(100, 10);
    home_and_wait(1); home_and_wait(2);
    send("MOVE 1 30"); send("MOVE 2 30");
    run_ms(1500);
    long a2_before = total_steps[1];
    max_trig[0] = phys[0] + MM(1);                         /* lost steps: MAX switch appears 1 mm ahead of A1 */
    CHECK(run_until("A1 MAX HIT", 20000), "no fault on unexpected MAX:\n%s", out_since_mark());
    SEEN("FAULT");
    run_ms(500);
    long a2_after = total_steps[1];
    run_ms(500);
    CHECK(total_steps[1] == a2_after, "A2 kept stepping after another axis faulted");
    CHECK(a2_after - a2_before < 400, "A2 ran on for %ld steps after the fault", a2_after - a2_before);
    for (int i = 0; i < NUM_AXES; i++) CHECK(en_disabled(i), "A%d driver enabled after fault", i + 1);
}

static void t_debounce(void) {
    fresh(100, 10);
    home_and_wait(1);
    send("MOVE 1 20");
    run_ms(1500);
    glitch_ticks[0] = 20;                                  /* 2 ms noise burst on MIN while moving AWAY from it */
    run_ms(100);
    NOT_SEEN("FAULT");
    CHECK(run_until("A1 DONE 20.000", 20000), "move disturbed by glitch");
    /* a glitch on the switch we are driving TOWARD must not trip either */
    send("MOVE 1 0");
    run_ms(1500);
    glitch_ticks[0] = 20;                                  /* 2 ms < 5 ms debounce */
    CHECK(run_until("A1 DONE 0.000", 30000), "2 ms glitch should be ignored:\n%s", out_since_mark());
    NOT_SEEN("FAULT");
}

static void t_hatch_interlock(void) {
    fresh(100, 10);
    home_and_wait(1);
    send("MOVE 1 20");
    run_ms(500);
    send("OPEN");  SEEN("BUSY");                           /* an axis is moving */
    CHECK(run_until("A1 DONE 20.000", 30000), "move did not finish");
    send("OPEN");  NOT_SEEN("BUSY"); NOT_SEEN("INVALID");
}

static void t_stop_and_rst(void) {
    fresh(100, 10);
    home_and_wait(1);
    send("MOVE 1 30");
    run_ms(1000);
    send("STOP");
    long s = total_steps[0];
    run_ms(500);
    CHECK(total_steps[0] == s, "stepping continued after STOP");
    for (int i = 0; i < NUM_AXES; i++) CHECK(en_disabled(i), "driver %d enabled after STOP", i + 1);
    send("POS"); SEEN("A1 UNHOMED");
    send("MOVE 1 5"); SEEN("NO HOME");
    send("RST");
    home_and_wait(1);                                       /* usable again after RST */
}

static void t_commands(void) {
    fresh(100, 10);
    send("FOO");        SEEN("UNKNOWN");
    send("HOME");       SEEN("INVALID");
    send("HOME 9");     SEEN("INVALID");
    send("HOME 1 2 3 4"); SEEN("UNKNOWN");
    send("HOME 1");     SEEN("A1 HOME");
    send("HOME 1");     SEEN("BUSY");
    send("HOME ALL");   SEEN("BUSY");                       /* all-or-nothing */
    CHECK(total_steps[1] == 0, "HOME ALL must not partially start when one axis is busy");
}

int main(void) {
    FILE *m = open_memstream(&outbuf, &outlen);
    FILE *real_stdout = stdout;
    stdout = m;

    struct { const char *name; void (*fn)(void); } tests[] = {
        {"boot_idle", t_boot_idle}, {"home_single", t_home_single}, {"move_and_range", t_move_and_range},
        {"cal_measures_travel", t_cal_measures_travel}, {"independent_axes", t_independent_axes},
        {"min_not_found", t_min_not_found}, {"start_on_switch", t_start_on_switch},
        {"fault_stops_all", t_fault_stops_all}, {"debounce", t_debounce},
        {"hatch_interlock", t_hatch_interlock}, {"stop_and_rst", t_stop_and_rst}, {"commands", t_commands},
    };
    int n = sizeof tests / sizeof tests[0], failed_tests = 0;
    for (int i = 0; i < n; i++) {
        int before = failures;
        tests[i].fn();
        bool ok = failures == before;
        if (!ok) failed_tests++;
        fprintf(stderr, "%s %s\n", ok ? "PASS" : "FAIL", tests[i].name);
    }
    fprintf(stderr, "\n%d/%d tests passed (%d checks)\n", n - failed_tests, n, checks);
    (void)real_stdout;
    return failed_tests ? 1 : 0;
}
