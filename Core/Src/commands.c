#include "commands.h"
#include "alignment.h"
#include "hatch.h"
#include "relay.h"
#include "system.h"
#include "critical.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

static UART_HandleTypeDef *uart = NULL;

static volatile char rx_buffer[48];
static uint8_t        rx_char;
static uint8_t        rx_idx = 0;
static volatile bool  cmd_ready = false;

/* ---- argument helpers ---- */

#define MAX_ARGS 3

/* "1".."NUM_AXES" -> 0-based index, else -1 */
static int parse_axis(const char *s) {
    if (s == NULL || s[0] < '1' || s[0] > '0' + NUM_AXES || s[1] != '\0') return -1;
    return s[0] - '1';
}

/* "[-]W[.fff]" millimetres -> steps, rounded to nearest step.
 * Integer-only (no strtof): at most 3 fractional digits. Returns false on
 * anything malformed. */
static bool parse_mm(const char *s, long *steps) {
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    else if (*s == '+') { s++; }
    if (*s < '0' || *s > '9') return false;

    long whole = 0;
    while (*s >= '0' && *s <= '9') {
        whole = whole * 10 + (*s++ - '0');
        if (whole > 100000L) return false;
    }
    long milli = whole * 1000L;
    if (*s == '.') {
        s++;
        long scale = 100L;
        if (*s < '0' || *s > '9') return false;
        while (*s >= '0' && *s <= '9') {
            if (scale == 0) return false;      /* more than 3 decimals */
            milli += (*s++ - '0') * scale;
            scale /= 10;
        }
    }
    if (*s != '\0') return false;

    long st = (milli * STEPS_PER_MM + 500L) / 1000L;
    *steps = neg ? -st : st;
    return true;
}

static bool reject_if_faulted(void) {
    if (system_in_fault()) { printf("FAULT - RST REQUIRED\r\n"); return true; }
    return false;
}

/* HOME / CAL share everything except the flag. Argument is an axis number
 * or ALL; ALL starts every axis at once (they are independent). */
static void start_homing(int argc, char **argv, bool measure) {
    if (reject_if_faulted()) return;
    if (argc < 2) { printf("INVALID\r\n"); return; }

    int lo, hi;
    if (strcmp(argv[1], "ALL") == 0) { lo = 0; hi = NUM_AXES - 1; }
    else {
        lo = hi = parse_axis(argv[1]);
        if (lo < 0) { printf("INVALID\r\n"); return; }
    }
    for (int i = lo; i <= hi; i++) {
        if (alignment_axis_busy(i)) { printf("BUSY\r\n"); return; }   /* all-or-nothing */
    }
    for (int i = lo; i <= hi; i++) {
        alignment_home(i, measure);
        if (system_in_fault()) return;
    }
}

/* ---- individual command handlers ---- */

static void cmd_home(uint32_t now, int argc, char **argv) { (void)now; start_homing(argc, argv, false); }
static void cmd_cal (uint32_t now, int argc, char **argv) { (void)now; start_homing(argc, argv, true);  }

static void cmd_move(uint32_t now, int argc, char **argv) {
    (void)now;
    if (reject_if_faulted()) return;
    int i; long steps;
    if (argc < 3 || (i = parse_axis(argv[1])) < 0 || !parse_mm(argv[2], &steps)) {
        printf("INVALID\r\n");
        return;
    }
    if (!alignment_axis_homed(i))      { printf("NO HOME\r\n"); return; }
    if (alignment_axis_busy(i))        { printf("BUSY\r\n");    return; }
    if (steps < 0 || steps > alignment_travel_steps(i)) { printf("RANGE\r\n"); return; }
    alignment_move_steps(i, steps);
}

static void cmd_halt(uint32_t now, int argc, char **argv) {
    (void)now;
    int i;
    if (argc < 2 || (i = parse_axis(argv[1])) < 0) { printf("INVALID\r\n"); return; }
    alignment_halt(i);
}

static void cmd_pos(uint32_t now, int argc, char **argv) {
    (void)now; (void)argc; (void)argv;
    alignment_print_status();
}

static void cmd_rst(uint32_t now, int argc, char **argv) {
    (void)now; (void)argc; (void)argv;
    hatch_emergency_stop(); /* RST must not leave the actuator driving */
    alignment_reset();
    system_clear_fault();
    printf("RST\r\nHOME REQUIRED\r\n");
}

/* Hatch moves only while no axis is moving (and never while faulted). */
static void cmd_open(uint32_t now, int argc, char **argv) {
    (void)argc; (void)argv;
    if (reject_if_faulted()) return;
    if (hatch_is_busy() || alignment_any_busy()) printf("BUSY\r\n");
    else hatch_open(now);
}

static void cmd_close(uint32_t now, int argc, char **argv) {
    (void)argc; (void)argv;
    if (reject_if_faulted()) return;
    if (hatch_is_busy() || alignment_any_busy()) printf("BUSY\r\n");
    else hatch_close(now);
}

static void cmd_stop(uint32_t now, int argc, char **argv) {
    (void)now; (void)argc; (void)argv;
    /* STOP is the user-facing normal stop: halt the hatch AND every axis.
     * Axes are left un-homed with drivers disabled; a fresh HOME is
     * required. It does not latch a fault. (HALT <n> stops one axis and
     * keeps its position.) */
    alignment_emergency_stop();
    hatch_stop_cmd();
}

static void cmd_on(uint32_t now, int argc, char **argv) {
    (void)now; (void)argc; (void)argv;
    /* Fault policy: while faulted only RST, OFF and STOP are accepted.
     * system_fault() latches everything off - ON must not undo that. */
    if (reject_if_faulted()) return;
    light_on();
    printf("LIGHT ON\r\n");
}

static void cmd_off(uint32_t now, int argc, char **argv) {
    (void)now; (void)argc; (void)argv;
    light_off();
    printf("LIGHT OFF\r\n");
}

typedef void (*CommandFn)(uint32_t now, int argc, char **argv);
typedef struct { const char *name; CommandFn fn; } Command;

static const Command commandTable[] = {
    { "HOME",  cmd_home  },
    { "CAL",   cmd_cal   },
    { "MOVE",  cmd_move  },
    { "HALT",  cmd_halt  },
    { "POS",   cmd_pos   },
    { "RST",   cmd_rst   },
    { "OPEN",  cmd_open  },
    { "CLOSE", cmd_close },
    { "STOP",  cmd_stop  },
    { "ON",    cmd_on    },
    { "OFF",   cmd_off   },
};
#define NUM_COMMANDS (sizeof(commandTable) / sizeof(commandTable[0]))

/* ---- public API ---- */

void commands_init(UART_HandleTypeDef *huart) {
    uart = huart;
    HAL_UART_Receive_IT(uart, &rx_char, 1);
}

void commands_update(uint32_t now) {
    if (!cmd_ready) return;

    /* Snapshot under a critical section before clearing cmd_ready - once
     * cmd_ready goes false, the RX ISR is free to start overwriting
     * rx_buffer for the next line, and without this copy that could
     * happen mid-parse against the buffer we're still reading. */
    char local_buf[sizeof(rx_buffer)];
    CRITICAL_ENTER();
    memcpy(local_buf, (const void*)rx_buffer, sizeof(local_buf));
    cmd_ready = false;
    CRITICAL_EXIT();

    /* Split on spaces in place: "MOVE 2 12.5" -> {"MOVE","2","12.5"} */
    char *argv[MAX_ARGS];
    int argc = 0;
    char *p = local_buf;
    while (*p != '\0') {
        while (*p == ' ') *p++ = '\0';
        if (*p == '\0') break;
        if (argc == MAX_ARGS) { printf("UNKNOWN\r\n"); return; }
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ') p++;
    }
    if (argc == 0) return;

    for (unsigned i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(argv[0], commandTable[i].name) == 0) {
            commandTable[i].fn(now, argc, argv);
            return;
        }
    }
    printf("UNKNOWN\r\n");
}

/* HAL weak-callback override - without this, a real-world framing/noise/
 * overrun error on the wire aborts HAL's UART receive state and the port
 * goes permanently silent, since nothing re-arms HAL_UART_Receive_IT().
 * Renode's virtual UART never raises these, so this only shows up on
 * real wiring - easy to miss without deliberately looking for it. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (uart != NULL && huart->Instance == uart->Instance) {
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        __HAL_UART_CLEAR_PEFLAG(huart);
        rx_idx = 0; /* discard whatever partial line was in progress */
        HAL_UART_Receive_IT(uart, &rx_char, 1);
    }
}

/* HAL weak-callback override - this is the only file that touches the
 * UART RX line-buffer, so rx_buffer/rx_idx/cmd_ready above can stay
 * file-scope statics instead of project-wide globals. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (uart != NULL && huart->Instance == uart->Instance) {
        if (rx_char == '\r') {
            /* ignore CR so \r\n acts as a single line terminator */
        } else if (rx_char == '\n') {
            if (rx_idx > 0 && !cmd_ready) {
                rx_buffer[rx_idx] = '\0';
                cmd_ready = true;
                rx_idx = 0;
            }
        } else {
            if (rx_idx < sizeof(rx_buffer) - 1 && !cmd_ready) {
                rx_buffer[rx_idx++] = rx_char;
            }
        }
        HAL_UART_Receive_IT(uart, &rx_char, 1);
    }
}
