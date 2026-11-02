#include "system.h"
#include "alignment.h"
#include "hatch.h"
#include "relay.h"
#include "axis.h"
#include "main.h"
#include <stdio.h>
#include <stdbool.h>

static bool faulted = false;

void system_fault(const char *msg) {
    alignment_emergency_stop();
    hatch_emergency_stop();
    light_off(); /* a fault is not a state where anything should keep
                  * running unsupervised */
    faulted = true;
    printf("FAULT: %s\r\n", msg);
}

bool system_in_fault(void) {
    return faulted;
}

void system_clear_fault(void) {
    faulted = false;
}

void system_force_safe_outputs(void) {
    /* Motor drivers (all axes): EN is active-LOW, so high = disabled */
    axis_force_safe_outputs();
    /* Hatch H-bridge: all inputs low = outputs off */
    HAL_GPIO_WritePin(GRIP_IN1_GPIO_Port, GRIP_IN1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GRIP_IN2_GPIO_Port, GRIP_IN2_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GRIP_IN3_GPIO_Port, GRIP_IN3_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GRIP_IN4_GPIO_Port, GRIP_IN4_Pin, GPIO_PIN_RESET);
    /* Light relay: active-LOW, so SET = off */
    HAL_GPIO_WritePin(RELAY_LIGHT_GPIO_Port, RELAY_LIGHT_Pin, GPIO_PIN_SET);
}
